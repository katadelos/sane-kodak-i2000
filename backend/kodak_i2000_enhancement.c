/* Deskew, tone and sharpening controls, rotation, and blank-page detection. */
#include <limits.h>
#include "kodak_i2000_image.h"
#include "kodak_i2000_pixels.h"
#include <stdlib.h>
#include <string.h>

/* Host deskew uses a bounded vertical shear.  A slope unit is 1/512 pixel per
 * horizontal pixel; 44 units is approximately tan(4.9 degrees).  Projection
 * scoring is deliberately downsampled so a 600-dpi A4 page stays bounded. */
#define KODAK_I2000_DESKEW_SLOPE_SCALE 512
#define KODAK_I2000_DESKEW_MAX_SLOPE 44
#define KODAK_I2000_DESKEW_SLOPE_STEP 2
#define KODAK_I2000_DESKEW_SAMPLE_WIDTH 256U
#define KODAK_I2000_DESKEW_SAMPLE_HEIGHT 512U
#define KODAK_I2000_DESKEW_PAPER_CUTOFF 245U
#define KODAK_I2000_DESKEW_MIN_DARK_SAMPLES 64U
#define KODAK_I2000_DESKEW_CONFIDENCE_DIVISOR 50U

static uint8_t
clamp_byte(int value)
{
  if (value < 0)
    return 0U;
  if (value > 255)
    return 255U;
  return (uint8_t)value;
}

static int
estimate_deskew_slope(const uint8_t *image, size_t width, size_t height,
                      size_t channels, int *slope)
{
  size_t step_x;
  size_t step_y;
  size_t sampled_rows;
  size_t maximum_shift;
  size_t margin;
  size_t projection_length;
  uint64_t *projection;
  uint64_t best_score = 0U;
  uint64_t zero_score = 0U;
  size_t dark_samples = 0U;
  int best_slope = 0;
  int candidate;

  if (image == NULL || slope == NULL)
    return 0;
  *slope = 0;
  if (width < 32U || height < 32U)
    return 1;
  step_x =
      (width + KODAK_I2000_DESKEW_SAMPLE_WIDTH - 1U) / KODAK_I2000_DESKEW_SAMPLE_WIDTH;
  step_y = (height + KODAK_I2000_DESKEW_SAMPLE_HEIGHT - 1U) /
           KODAK_I2000_DESKEW_SAMPLE_HEIGHT;
  sampled_rows = (height + step_y - 1U) / step_y;
  maximum_shift = ((size_t)KODAK_I2000_DESKEW_MAX_SLOPE * (width / 2U) +
                   KODAK_I2000_DESKEW_SLOPE_SCALE - 1U) /
                      KODAK_I2000_DESKEW_SLOPE_SCALE +
                  1U;
  margin = (maximum_shift + step_y - 1U) / step_y + 1U;
  if (margin > (SIZE_MAX - sampled_rows) / 2U)
    return 0;
  projection_length = sampled_rows + margin * 2U;
  projection = (uint64_t *)calloc(projection_length, sizeof(*projection));
  if (projection == NULL)
    return 0;

  for (candidate = -KODAK_I2000_DESKEW_MAX_SLOPE;
       candidate <= KODAK_I2000_DESKEW_MAX_SLOPE;
       candidate += KODAK_I2000_DESKEW_SLOPE_STEP) {
    uint64_t score = 0U;
    size_t y;

    memset(projection, 0, projection_length * sizeof(*projection));
    for (y = 0U; y < height; y += step_y) {
      size_t x;
      for (x = 0U; x < width; x += step_x) {
        unsigned luma = pixel_luma(image + (y * width + x) * channels, channels);
        unsigned darkness;
        int64_t shifted;
        size_t bin;

        if (luma >= KODAK_I2000_DESKEW_PAPER_CUTOFF)
          continue;
        darkness = KODAK_I2000_DESKEW_PAPER_CUTOFF - luma;
        if (candidate == 0)
          ++dark_samples;
        shifted =
            ((int64_t)y + (int64_t)maximum_shift) * KODAK_I2000_DESKEW_SLOPE_SCALE +
            (int64_t)candidate * ((int64_t)x - (int64_t)(width / 2U));
        if (shifted < 0)
          continue;
        bin = (size_t)(shifted /
                       ((int64_t)KODAK_I2000_DESKEW_SLOPE_SCALE * (int64_t)step_y));
        if (bin < projection_length)
          projection[bin] += darkness;
      }
    }
    for (y = 0U; y < projection_length; ++y) {
      uint64_t value = projection[y];
      score += value * value;
    }
    if (candidate == 0)
      zero_score = score;
    if (score > best_score) {
      best_score = score;
      best_slope = candidate;
    }
  }
  free(projection);

  /* A blank/photo-like page should not be transformed on a weak projection
   * preference.  Boundary maxima also indicate a page outside the supported
   * angle rather than a trustworthy correction. */
  if (dark_samples < KODAK_I2000_DESKEW_MIN_DARK_SAMPLES || best_slope == 0 ||
      best_slope == -KODAK_I2000_DESKEW_MAX_SLOPE ||
      best_slope == KODAK_I2000_DESKEW_MAX_SLOPE ||
      best_score <= zero_score + zero_score / KODAK_I2000_DESKEW_CONFIDENCE_DIVISOR)
    return 1;
  *slope = best_slope;
  return 1;
}

static int
apply_deskew(uint8_t **image, size_t image_size, size_t width, size_t height,
             size_t channels, int enabled)
{
  uint8_t *source;
  uint8_t *output;
  int slope = 0;
  size_t x;

  if (!enabled)
    return 1;
  if (!estimate_deskew_slope(*image, width, height, channels, &slope))
    return 0;
  if (slope == 0)
    return 1;

  source = *image;
  output = (uint8_t *)malloc(image_size);
  if (output == NULL)
    return 0;
  memset(output, 0xff, image_size);
  for (x = 0U; x < width; ++x) {
    int64_t correction = (int64_t)slope * ((int64_t)x - (int64_t)(width / 2U)) *
                         (INT64_C(65536) / KODAK_I2000_DESKEW_SLOPE_SCALE);
    size_t y;
    for (y = 0U; y < height; ++y) {
      int64_t source_y_fixed = (int64_t)y * INT64_C(65536) - correction;
      size_t source_y;
      unsigned fraction;
      size_t channel;

      if (source_y_fixed < 0 || source_y_fixed >= (int64_t)height * INT64_C(65536))
        continue;
      source_y = (size_t)(source_y_fixed / INT64_C(65536));
      fraction = (unsigned)(source_y_fixed & INT64_C(0xffff));
      for (channel = 0U; channel < channels; ++channel) {
        unsigned first = source[(source_y * width + x) * channels + channel];
        unsigned second =
            source_y + 1U < height
                ? source[((source_y + 1U) * width + x) * channels + channel]
                : 255U;
        output[(y * width + x) * channels + channel] =
            (uint8_t)((first * (65536U - fraction) + second * fraction + 32768U) >> 16);
      }
    }
  }
  free(source);
  *image = output;
  return 1;
}

static void
apply_tone(uint8_t *image, size_t image_size, int brightness, int contrast)
{
  int factor = 256 + contrast * 2;
  int offset = brightness * 255 / 100;
  size_t index;

  for (index = 0U; index < image_size; ++index) {
    int centered = (int)image[index] - 128;
    int value = 128 + centered * factor / 256 + offset;
    image[index] = clamp_byte(value);
  }
}

/* Kodak's document mask is separable. Round to 8 bits after each pass,
 * then add the gain-scaled difference to the original pixel. */
static uint8_t
blur_five(const uint8_t *first, size_t step)
{
  unsigned sum = 71U * (first[0] + first[4U * step]) +
                 97U * (first[step] + first[3U * step]) + 176U * first[2U * step];
  return (uint8_t)((sum + 256U) >> 9);
}

static int
apply_sharpen(uint8_t **image, size_t image_size, size_t width, size_t height,
              size_t channels, unsigned level)
{
  const size_t stride = width * channels;
  const int gain = level == 1U ? 407204 : (channels == 3U ? 25450 : 19449);
  const unsigned shift = level == 1U ? 20U : (level == 2U ? 14U : 12U);
  uint8_t *source = *image;
  uint8_t *output;
  uint8_t *blurred_row;

  if (level == 0U || width < 5U || height < 5U)
    return 1;
  output = malloc(image_size);
  blurred_row = malloc(stride);
  if (output == NULL || blurred_row == NULL) {
    free(blurred_row);
    free(output);
    return 0;
  }
  memcpy(output, source, image_size);
  for (size_t y = 2U; y + 2U < height; ++y) {
    const uint8_t *first = source + (y - 2U) * stride;
    const uint8_t *row = source + y * stride;
    uint8_t *dest = output + y * stride;

    for (size_t i = 0U; i < stride; ++i)
      blurred_row[i] = blur_five(first + i, stride);
    for (size_t i = 2U * channels; i < stride - 2U * channels; ++i) {
      int blurred = blur_five(blurred_row + i - 2U * channels, channels);
      int detail = (row[i] - blurred) * gain;
      dest[i] = clamp_byte(row[i] + ((detail + (1 << (shift - 1U))) >> shift));
    }
  }
  free(blurred_row);
  free(source);
  *image = output;
  return 1;
}

static int
apply_rotation(uint8_t **image, size_t image_size, size_t *width, size_t *height,
               size_t channels, unsigned rotation)
{
  uint8_t *source;
  uint8_t *output;
  size_t source_width = *width;
  size_t source_height = *height;
  size_t output_width =
      rotation == 90U || rotation == 270U ? source_height : source_width;
  size_t output_height =
      rotation == 90U || rotation == 270U ? source_width : source_height;
  size_t y;

  if (rotation == 0U)
    return 1;
  source = *image;
  output = (uint8_t *)malloc(image_size);
  if (output == NULL)
    return 0;
  for (y = 0U; y < source_height; ++y) {
    size_t x;
    for (x = 0U; x < source_width; ++x) {
      size_t output_x;
      size_t output_y;
      const uint8_t *source_pixel = source + (y * source_width + x) * channels;

      if (rotation == 90U) {
        output_x = source_height - 1U - y;
        output_y = x;
      } else if (rotation == 180U) {
        output_x = source_width - 1U - x;
        output_y = source_height - 1U - y;
      } else {
        output_x = y;
        output_y = source_width - 1U - x;
      }
      memcpy(output + (output_y * output_width + output_x) * channels, source_pixel,
             channels);
    }
  }
  free(source);
  *image = output;
  *width = output_width;
  *height = output_height;
  return 1;
}

static int
detect_blank_page(const uint8_t *image, size_t width, size_t height, size_t channels,
                  unsigned threshold)
{
  uint64_t dark_pixels = 0U;
  uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
  size_t pixel;

  for (pixel = 0U; pixel < (size_t)pixel_count; ++pixel)
    if (pixel_luma(image + pixel * channels, channels) < 245U)
      ++dark_pixels;
  return dark_pixels * UINT64_C(100) <= pixel_count * threshold;
}

int
kodak_i2000_image_process(uint8_t **image, size_t *image_size, size_t *width,
                          size_t *height, size_t channels,
                          const kodak_i2000_image_options *options, int *blank_page)
{
  size_t pixel_count;

  if (blank_page != NULL)
    *blank_page = 0;
  if (image == NULL || *image == NULL || image_size == NULL || width == NULL ||
      height == NULL || options == NULL || blank_page == NULL ||
      (*width == 0U || *height == 0U) || (channels != 1U && channels != 3U) ||
      options->brightness < -100 || options->brightness > 100 ||
      options->contrast < -100 || options->contrast > 100 || options->sharpen > 3U ||
      options->blank_threshold > 100U ||
      (options->rotation != 0U && options->rotation != 90U &&
       options->rotation != 180U && options->rotation != 270U) ||
      *width > SIZE_MAX / *height)
    return 0;
  pixel_count = *width * *height;
  if (pixel_count > SIZE_MAX / channels || pixel_count * channels != *image_size)
    return 0;

  if (!apply_deskew(image, *image_size, *width, *height, channels, options->deskew))
    return 0;
  if (options->brightness != 0 || options->contrast != 0)
    apply_tone(*image, *image_size, options->brightness, options->contrast);
  if (!apply_sharpen(image, *image_size, *width, *height, channels, options->sharpen) ||
      !apply_rotation(image, *image_size, width, height, channels, options->rotation))
    return 0;
  if (options->detect_blank)
    *blank_page =
        detect_blank_page(*image, *width, *height, channels, options->blank_threshold);
  return 1;
}
