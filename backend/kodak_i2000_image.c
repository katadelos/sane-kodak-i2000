/*
 * Raw-image framing and rendering for Kodak i2000 scanners.
 *
 * This is deliberately a small, transport-independent decoder.  The footer
 * layout is recovered from the device-manager image parser and validated
 * against the captured i2400 page stream before the raster is exposed to
 * SANE.
 */
#include "sane/config.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "kodak_i2000_image.h"
#include "kodak_i2000_pixels.h"
#include "kodak_i2000_color.h"

#define KODAK_I2000_FOOTER_SEARCH_LIMIT ((size_t)0x200000U)
#define KODAK_I2000_RASTER_PREFIX_LENGTH 16U
#define KODAK_I2000_EDGE_THRESHOLD 64U
#define KODAK_I2000_EDGE_RUN 16U
/* Require illuminated paper when masking the antialiased bed boundary. */
#define KODAK_I2000_COLOR_PAPER_EDGE_MIN 202U
#define KODAK_I2000_IMAGE_MAX_BYTES ((size_t)256U * 1024U * 1024U)

/* Values recovered from hippo.so::BkgrdColorSmooth with radare2. */
#define KODAK_I2000_BCS_SAMPLE_GRID 11U
#define KODAK_I2000_BCS_PIXEL_DISTANCE 25U
#define KODAK_I2000_BCS_BACKGROUND_SPREAD 65U
#define KODAK_I2000_BCS_PEAK_COUNT_DIVISOR 50U

/* The image-data selector in SDmScanConfig is reflected by the FE footer
 * value: one is the scanner's 8-bit gray stream and zero is packed RGB24.
 * These are not ripl::ImageInfo::FORMAT values (those are bit flags in the
 * vendor processing library). */
#define KODAK_I2000_IMAGE_FORMAT_RGB24 0U
#define KODAK_I2000_IMAGE_FORMAT_GRAY8 1U

static unsigned
read_be16(const uint8_t *data)
{
  return ((unsigned)data[0] << 8) | (unsigned)data[1];
}

static unsigned
resolution_from_enum(unsigned value)
{
  switch (value) {
  case 0U:
    return 600U;
  case 1U:
    return 400U;
  case 2U:
    return 300U;
  case 3U:
    return 200U;
  case 4U:
    return 150U;
  default:
    return 0U;
  }
}

static int
footer_candidate(const uint8_t *raw, size_t raw_size, size_t footer_tag,
                 kodak_i2000_image_info *info)
{
  static const uint8_t raster_prefix[KODAK_I2000_RASTER_PREFIX_LENGTH] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  size_t body_size;
  size_t raster_offset;
  size_t raster_size;
  size_t width;
  size_t height;
  size_t bytes_per_pixel;
  size_t row_bytes;
  unsigned resolution_x;
  unsigned resolution_y;
  unsigned format;

  /* footer_tag points at fc's two-byte value; the following separator and
   * tag bytes are checked by the caller. */
  if (footer_tag < 23U || footer_tag > raw_size - 4U)
    return 0;
  width = (size_t)read_be16(raw + footer_tag) * 2U;
  if (width < 64U)
    return 0;
  body_size = footer_tag - 20U;
  if (body_size == 0U || body_size > raw_size - 32U)
    return 0;
  format = read_be16(raw + body_size + 28U);
  if (format == KODAK_I2000_IMAGE_FORMAT_GRAY8)
    bytes_per_pixel = 1U;
  else if (format == KODAK_I2000_IMAGE_FORMAT_RGB24)
    bytes_per_pixel = 3U;
  else
    return 0;
  if (width > SIZE_MAX / bytes_per_pixel)
    return 0;
  row_bytes = width * bytes_per_pixel;
  if (body_size % row_bytes == 0U)
    raster_offset = 0U;
  else if (body_size >= KODAK_I2000_RASTER_PREFIX_LENGTH &&
           (body_size - KODAK_I2000_RASTER_PREFIX_LENGTH) % row_bytes == 0U &&
           memcmp(raw, raster_prefix, sizeof(raster_prefix)) == 0)
    raster_offset = KODAK_I2000_RASTER_PREFIX_LENGTH;
  else
    return 0;
  raster_size = body_size - raster_offset;
  height = raster_size / row_bytes;
  if (height < 64U)
    return 0;

  /* The footer tags are four bytes apart: separator, value, separator, tag. */
  if (raw[body_size + 1U] != 0x01U || raw[body_size + 7U] != 0xf5U ||
      raw[body_size + 11U] != 0xf6U || raw[body_size + 15U] != 0xf9U ||
      raw[body_size + 19U] != 0xfaU || raw[body_size + 23U] != 0xfcU ||
      raw[body_size + 27U] != 0xfdu || raw[body_size + 31U] != 0xfeU)
    return 0;

  /* The structured fields end at FE, but the captured vendor page carries
   * additional footer bytes after that tag.  Those bytes are not yet decoded
   * and the complete transfer does not end in FF, so do not treat FF as a
   * stream terminator.  The fixed tag pattern above, raster-size divisibility,
   * and the bounded reverse search still constrain candidate footers. */
  resolution_x = resolution_from_enum(read_be16(raw + body_size + 12U));
  resolution_y = resolution_from_enum(read_be16(raw + body_size + 16U));
  if (resolution_x == 0U || resolution_y == 0U)
    return 0;

  memset(info, 0, sizeof(*info));
  info->raw_size = raw_size;
  info->raster_offset = raster_offset;
  info->footer_offset = body_size;
  info->footer_length = raw_size - body_size;
  info->raw_width = width;
  info->raw_height = height;
  info->resolution_x = resolution_x;
  info->resolution_y = resolution_y;
  info->format = format;
  info->crop_width = width;
  info->crop_height = height;
  return 1;
}

int
kodak_i2000_image_parse(const uint8_t *raw, size_t raw_size,
                        kodak_i2000_image_info *info)
{
  size_t first;
  size_t tag;

  if (raw == NULL || info == NULL || raw_size < 64U)
    return 0;
  first = raw_size > KODAK_I2000_FOOTER_SEARCH_LIMIT
              ? raw_size - KODAK_I2000_FOOTER_SEARCH_LIMIT
              : 0U;
  tag = raw_size - 4U;
  for (;;) {
    /* A footer's fc tag is preceded by a two-byte value and separator. */
    if (raw[tag + 2U] == 0x01U && raw[tag + 3U] == 0xfcu &&
        footer_candidate(raw, raw_size, tag, info))
      return 1;
    if (tag == first)
      break;
    --tag;
  }
  return 0;
}

static uint8_t *
apply_background_smoothing(const uint8_t *raster, size_t width, size_t height,
                           unsigned resolution)
{
  size_t block_size;
  size_t tile_size;
  size_t tile_width;
  size_t tile_height;
  size_t raster_size;
  uint8_t *output;
  unsigned global_histogram[256];
  unsigned background_tiles = 0U;
  unsigned primary = 0U;
  unsigned secondary = 0U;
  unsigned tertiary = 0U;
  unsigned primary_count = 0U;
  unsigned secondary_count = 0U;
  unsigned tertiary_count = 0U;
  unsigned selected[3];
  size_t selected_count = 0U;
  size_t tile_y;
  size_t tile_x;
  size_t index;

  if (raster == NULL || width == 0U || height == 0U || resolution == 0U ||
      width > SIZE_MAX / height)
    return NULL;
  raster_size = width * height;
  if (raster_size > KODAK_I2000_IMAGE_MAX_BYTES)
    return NULL;

  /* The original divides the image resolution by 50 before making an 11 by
   * 11 lattice.  Its loop starts at the second lattice row and leaves two
   * lattice cells in each dimension, hence floor(size / tile) - 2. */
  block_size = (size_t)resolution / 50U;
  if (block_size == 0U || block_size > SIZE_MAX / KODAK_I2000_BCS_SAMPLE_GRID)
    return NULL;
  tile_size = block_size * KODAK_I2000_BCS_SAMPLE_GRID;
  tile_width = width / tile_size;
  tile_height = height / tile_size;
  if (tile_width <= 2U || tile_height <= 2U)
    return NULL;
  tile_width -= 2U;
  tile_height -= 2U;
  if (tile_width > SIZE_MAX / tile_height)
    return NULL;
  output = (uint8_t *)malloc(raster_size);
  if (output == NULL) {
    free(output);
    return NULL;
  }
  memset(global_histogram, 0, sizeof(global_histogram));

  for (tile_y = 0U; tile_y < tile_height; ++tile_y) {
    size_t top = (tile_y + 1U) * tile_size;
    for (tile_x = 0U; tile_x < tile_width; ++tile_x) {
      size_t left = tile_x * tile_size;
      unsigned histogram[256];
      unsigned mode = 0U;
      unsigned mode_count = 0U;
      int low = -1;
      int high = -1;
      unsigned sample_y;
      unsigned sample_x;

      memset(histogram, 0, sizeof(histogram));
      for (sample_y = 0U; sample_y < KODAK_I2000_BCS_SAMPLE_GRID; ++sample_y) {
        size_t row = top + (size_t)sample_y * block_size;
        for (sample_x = 0U; sample_x < KODAK_I2000_BCS_SAMPLE_GRID; ++sample_x) {
          size_t column = left + (size_t)sample_x * block_size;
          ++histogram[raster[row * width + column]];
        }
      }

      /* The vendor's content test uses the lowest and highest bins that
       * occur more than once, and accepts a tile as background when their
       * spread is at most 65. */
      for (index = 0U; index < 256U; ++index) {
        if (histogram[index] > 1U) {
          low = (int)index;
          break;
        }
      }
      for (index = 256U; index-- > 0U;) {
        if (histogram[index] > 1U) {
          high = (int)index;
          break;
        }
      }

      /* The mode scan is a strict maximum from bin zero upward, so equal
       * counts resolve to the lower gray code. */
      for (index = 0U; index < 256U; ++index) {
        if (histogram[index] > mode_count) {
          mode = (unsigned)index;
          mode_count = histogram[index];
        }
      }
      if (low >= 0 && high >= 0 &&
          (unsigned)(high - low) <= KODAK_I2000_BCS_BACKGROUND_SPREAD) {
        ++background_tiles;
        /* The vendor indexes its mode array by the count of qualifying
         * tiles, then builds the global histogram from that prefix. */
        ++global_histogram[mode];
      }
    }
  }

  /* Select the strongest mode, then the strongest modes at least 26 codes
   * away from all earlier modes.  This mirrors the three histogram passes
   * in BkgrdColorSmooth. */
  for (index = 0U; index < 256U; ++index) {
    if (global_histogram[index] > primary_count) {
      primary = (unsigned)index;
      primary_count = global_histogram[index];
    }
  }
  for (index = 0U; index < 256U; ++index) {
    size_t distance = index > primary ? index - primary : primary - index;
    if (distance > KODAK_I2000_BCS_PIXEL_DISTANCE &&
        global_histogram[index] > secondary_count) {
      secondary = (unsigned)index;
      secondary_count = global_histogram[index];
    }
  }
  for (index = 0U; index < 256U; ++index) {
    size_t distance_primary = index > primary ? index - primary : primary - index;
    size_t distance_secondary =
        index > secondary ? index - secondary : secondary - index;
    if (distance_primary > KODAK_I2000_BCS_PIXEL_DISTANCE &&
        distance_secondary > KODAK_I2000_BCS_PIXEL_DISTANCE &&
        global_histogram[index] > tertiary_count) {
      tertiary = (unsigned)index;
      tertiary_count = global_histogram[index];
    }
  }

  /* The original compares each peak count with background_tiles / 50. */
  if (primary_count > background_tiles / KODAK_I2000_BCS_PEAK_COUNT_DIVISOR)
    selected[selected_count++] = primary;
  if (secondary_count > background_tiles / KODAK_I2000_BCS_PEAK_COUNT_DIVISOR)
    selected[selected_count++] = secondary;
  if (tertiary_count > background_tiles / KODAK_I2000_BCS_PEAK_COUNT_DIVISOR)
    selected[selected_count++] = tertiary;

  if (selected_count == 0U) {
    free(output);
    return NULL;
  }

  memcpy(output, raster, raster_size);
  for (index = 0U; index < raster_size; ++index) {
    unsigned pixel = raster[index];
    size_t selected_index;
    for (selected_index = 0U; selected_index < selected_count; ++selected_index) {
      unsigned peak = selected[selected_index];
      unsigned distance = pixel > peak ? pixel - peak : peak - pixel;
      if (distance <= KODAK_I2000_BCS_PIXEL_DISTANCE) {
        output[index] = (uint8_t)peak;
        break;
      }
    }
  }
  return output;
}

static unsigned
sample_row_mean(const uint8_t *raster, size_t width, size_t row, size_t bytes_per_pixel)
{
  size_t x;
  size_t step = width > 2048U ? 8U : 4U;
  size_t count = 0U;
  size_t sum = 0U;

  for (x = 0U; x < width; x += step) {
    sum += pixel_luma(raster + (row * width + x) * bytes_per_pixel, bytes_per_pixel);
    ++count;
  }
  return count == 0U ? 0U : (unsigned)(sum / count);
}

static unsigned
sample_column_mean(const uint8_t *raster, size_t width, size_t height, size_t column,
                   size_t bytes_per_pixel)
{
  size_t y;
  size_t step = height > 2048U ? 8U : 4U;
  size_t count = 0U;
  size_t sum = 0U;

  for (y = 0U; y < height; y += step) {
    sum += pixel_luma(raster + (y * width + column) * bytes_per_pixel, bytes_per_pixel);
    ++count;
  }
  return count == 0U ? 0U : (unsigned)(sum / count);
}

static int
row_is_sheet(const uint8_t *raster, size_t width, size_t row, size_t bytes_per_pixel)
{
  return sample_row_mean(raster, width, row, bytes_per_pixel) >=
         KODAK_I2000_EDGE_THRESHOLD;
}

static int
column_is_sheet(const uint8_t *raster, size_t width, size_t height, size_t column,
                size_t bytes_per_pixel)
{
  return sample_column_mean(raster, width, height, column, bytes_per_pixel) >=
         KODAK_I2000_EDGE_THRESHOLD;
}

static int
find_sheet_crop(const uint8_t *raster, size_t width, size_t height, size_t *left,
                size_t *top, size_t *crop_width, size_t *crop_height,
                size_t bytes_per_pixel)
{
  size_t index;
  size_t run;
  size_t first_row = 0U;
  size_t last_row = 0U;
  size_t first_column = 0U;
  size_t last_column = 0U;

  if (width < KODAK_I2000_EDGE_RUN || height < KODAK_I2000_EDGE_RUN)
    return 0;

  for (index = 0U; index + KODAK_I2000_EDGE_RUN <= height; ++index) {
    for (run = 0U; run < KODAK_I2000_EDGE_RUN; ++run)
      if (!row_is_sheet(raster, width, index + run, bytes_per_pixel))
        break;
    if (run == KODAK_I2000_EDGE_RUN) {
      first_row = index;
      break;
    }
  }
  for (index = height; index >= KODAK_I2000_EDGE_RUN; --index) {
    size_t start = index - KODAK_I2000_EDGE_RUN;
    for (run = 0U; run < KODAK_I2000_EDGE_RUN; ++run)
      if (!row_is_sheet(raster, width, start + run, bytes_per_pixel))
        break;
    if (run == KODAK_I2000_EDGE_RUN) {
      last_row = start + KODAK_I2000_EDGE_RUN;
      break;
    }
  }
  for (index = 0U; index + KODAK_I2000_EDGE_RUN <= width; ++index) {
    for (run = 0U; run < KODAK_I2000_EDGE_RUN; ++run)
      if (!column_is_sheet(raster, width, height, index + run, bytes_per_pixel))
        break;
    if (run == KODAK_I2000_EDGE_RUN) {
      first_column = index;
      break;
    }
  }
  for (index = width; index >= KODAK_I2000_EDGE_RUN; --index) {
    size_t start = index - KODAK_I2000_EDGE_RUN;
    for (run = 0U; run < KODAK_I2000_EDGE_RUN; ++run)
      if (!column_is_sheet(raster, width, height, start + run, bytes_per_pixel))
        break;
    if (run == KODAK_I2000_EDGE_RUN) {
      last_column = start + KODAK_I2000_EDGE_RUN;
      break;
    }
  }

  if (last_row <= first_row || last_column <= first_column ||
      last_row - first_row < height / 2U || last_column - first_column < width / 2U)
    return 0;
  *left = first_column;
  *top = first_row;
  *crop_width = last_column - first_column;
  *crop_height = last_row - first_row;
  return 1;
}

static void
expand_sheet_crop(size_t width, size_t height, unsigned resolution_x,
                  unsigned resolution_y, size_t *left, size_t *top, size_t *crop_width,
                  size_t *crop_height)
{
  size_t margin_x;
  size_t margin_y;
  size_t right;
  size_t bottom;

  if (left == NULL || top == NULL || crop_width == NULL || crop_height == NULL ||
      *crop_width == 0U || *crop_height == 0U)
    return;

  /* FindImageEdges/DeskewCrop keeps a narrow amount of paper around the
   * detected RulerROI edge.  The captured i2400 pair measured about 0.09in
   * horizontally and 0.07in vertically; scale the guard with the source
   * resolution so this remains valid for the other sensor modes. */
  margin_x = ((size_t)(resolution_x != 0U ? resolution_x : 300U) * 9U) / 100U;
  margin_y = ((size_t)(resolution_y != 0U ? resolution_y : 300U) * 7U) / 100U;
  if (margin_x == 0U)
    margin_x = 1U;
  if (margin_y == 0U)
    margin_y = 1U;

  right = width - (*left + *crop_width);
  bottom = height - (*top + *crop_height);
  if (margin_x > *left)
    margin_x = *left;
  if (margin_x > right)
    margin_x = right;
  if (margin_y > *top)
    margin_y = *top;
  if (margin_y > bottom)
    margin_y = bottom;

  *left -= margin_x;
  *top -= margin_y;
  *crop_width += margin_x;
  *crop_width += margin_x <= right ? margin_x : right;
  *crop_height += margin_y;
  *crop_height += margin_y <= bottom ? margin_y : bottom;
}

static uint8_t
sample_bilinear_channel(const uint8_t *raster, size_t raster_width,
                        size_t raster_height, size_t bytes_per_pixel, size_t channel,
                        uint64_t x_fixed, uint64_t y_fixed)
{
  size_t x0 = (size_t)(x_fixed >> 16);
  size_t y0 = (size_t)(y_fixed >> 16);
  size_t x1;
  size_t y1;
  uint32_t wx0;
  uint32_t wx1;
  uint32_t wy0;
  uint32_t wy1;
  uint64_t value;

  if (x0 >= raster_width)
    x0 = raster_width - 1U;
  if (y0 >= raster_height)
    y0 = raster_height - 1U;
  x1 = x0 + 1U < raster_width ? x0 + 1U : x0;
  y1 = y0 + 1U < raster_height ? y0 + 1U : y0;
  /* IPP's linear resize path uses the complete fractional coordinate.  The
   * older implementation quantized this to the 64-phase table used by the
   * vendor's deskew kernel, which is a materially different operation and
   * softens text when changing DPI. */
  wx1 = (uint32_t)(x_fixed & UINT64_C(0xffff));
  wx0 = UINT32_C(0x10000) - wx1;
  wy1 = (uint32_t)(y_fixed & UINT64_C(0xffff));
  wy0 = UINT32_C(0x10000) - wy1;
  value = (uint64_t)raster[(y0 * raster_width + x0) * bytes_per_pixel + channel] * wx0 *
          wy0;
  value += (uint64_t)raster[(y0 * raster_width + x1) * bytes_per_pixel + channel] *
           wx1 * wy0;
  value += (uint64_t)raster[(y1 * raster_width + x0) * bytes_per_pixel + channel] *
           wx0 * wy1;
  value += (uint64_t)raster[(y1 * raster_width + x1) * bytes_per_pixel + channel] *
           wx1 * wy1;
  value = (value + UINT64_C(0x80000000)) >> 32;
  return value > 255U ? 255U : (uint8_t)value;
}

static int
bright_run_horizontal(const uint8_t *image, size_t width, size_t row, size_t start,
                      size_t run)
{
  size_t offset;

  if (start > width || run > width - start)
    return 0;
  for (offset = 0U; offset < run; ++offset)
    if (pixel_luma(image + (row * width + start + offset) * 3U, 3U) <
        KODAK_I2000_COLOR_PAPER_EDGE_MIN)
      return 0;
  return 1;
}

static int
bright_run_vertical(const uint8_t *image, size_t width, size_t height, size_t column,
                    size_t start, size_t run)
{
  size_t offset;

  if (start > height || run > height - start)
    return 0;
  for (offset = 0U; offset < run; ++offset)
    if (pixel_luma(image + ((start + offset) * width + column) * 3U, 3U) <
        KODAK_I2000_COLOR_PAPER_EDGE_MIN)
      return 0;
  return 1;
}

static void
set_white(uint8_t *image, size_t pixel)
{
  image[pixel * 3U] = 255U;
  image[pixel * 3U + 1U] = 255U;
  image[pixel * 3U + 2U] = 255U;
}

static void
fill_color_exterior(uint8_t *image, size_t width, size_t height, unsigned resolution)
{
  size_t run;
  size_t horizontal_limit;
  size_t vertical_limit;
  size_t y;
  size_t x;

  if (image == NULL || width == 0U || height == 0U)
    return;

  /* An axis-aligned crop around a slightly skewed page necessarily contains
   * narrow triangles of the black scan bed.  Search no farther than a quarter
   * inch from each output edge, and only accept a sustained paper-bright run.
   * This avoids treating arbitrary dark content deeper in the page as bed. */
  run = (size_t)(resolution != 0U ? resolution : 300U) / 50U;
  if (run < 4U)
    run = 4U;
  if (run > width)
    run = width;
  if (run > height)
    run = height;
  horizontal_limit = (size_t)(resolution != 0U ? resolution : 300U) / 4U;
  vertical_limit = horizontal_limit;
  if (horizontal_limit > width / 2U)
    horizontal_limit = width / 2U;
  if (vertical_limit > height / 2U)
    vertical_limit = height / 2U;

  /* Fill the side wedges first.  Those white pixels then let the vertical
   * pass close the otherwise all-black corner rows and columns. */
  for (y = 0U; y < height; ++y) {
    size_t boundary = 0U;
    int found = 0;

    for (x = 0U; x <= horizontal_limit && x + run <= width; ++x) {
      if (bright_run_horizontal(image, width, y, x, run)) {
        boundary = x + run / 4U;
        found = 1;
        break;
      }
    }
    if (found)
      for (x = 0U; x < boundary; ++x)
        set_white(image, y * width + x);

    found = 0;
    for (x = 0U; x <= horizontal_limit && x + run <= width; ++x) {
      size_t start = width - run - x;
      if (bright_run_horizontal(image, width, y, start, run)) {
        boundary = start + run - run / 4U;
        found = 1;
        break;
      }
    }
    if (found)
      for (x = boundary; x < width; ++x)
        set_white(image, y * width + x);
  }

  for (x = 0U; x < width; ++x) {
    size_t boundary = 0U;
    int found = 0;

    for (y = 0U; y <= vertical_limit && y + run <= height; ++y) {
      if (bright_run_vertical(image, width, height, x, y, run)) {
        boundary = y + run / 4U;
        found = 1;
        break;
      }
    }
    if (found)
      for (y = 0U; y < boundary; ++y)
        set_white(image, y * width + x);

    found = 0;
    for (y = 0U; y <= vertical_limit && y + run <= height; ++y) {
      size_t start = height - run - y;
      if (bright_run_vertical(image, width, height, x, start, run)) {
        boundary = start + run - run / 4U;
        found = 1;
        break;
      }
    }
    if (found)
      for (y = boundary; y < height; ++y)
        set_white(image, y * width + x);
  }
}

static int
render_raster(const uint8_t *raw, kodak_i2000_image_info *info, unsigned requested_dpi,
              int auto_crop, size_t bytes_per_pixel, uint8_t **image,
              size_t *image_size, size_t *width, size_t *height)
{
  size_t left;
  size_t top;
  size_t crop_width;
  size_t crop_height;
  size_t output_width;
  size_t output_height;
  size_t output_size;
  unsigned source_xdpi;
  unsigned source_ydpi;
  size_t raster_bytes_per_pixel;
  size_t raster_bytes;
  size_t raster_size;
  const uint8_t *raster;
  uint8_t *converted_raster = NULL;
  uint8_t *processed_raster = NULL;
  uint8_t *output;
  int crop_found = 0;
  size_t y;

  if (raw == NULL || info == NULL || image == NULL || image_size == NULL ||
      width == NULL || height == NULL || requested_dpi == 0U || bytes_per_pixel == 0U ||
      (info->format != KODAK_I2000_IMAGE_FORMAT_GRAY8 &&
       info->format != KODAK_I2000_IMAGE_FORMAT_RGB24) ||
      (bytes_per_pixel != 1U && bytes_per_pixel != 3U) ||
      (bytes_per_pixel == 3U && info->format != KODAK_I2000_IMAGE_FORMAT_RGB24) ||
      info->raster_offset > info->footer_offset ||
      info->footer_offset > info->raw_size || info->raw_width == 0U ||
      info->raw_height == 0U ||
      info->raw_width >
          SIZE_MAX / (info->format == KODAK_I2000_IMAGE_FORMAT_RGB24 ? 3U : 1U))
    return 0;

  source_xdpi = info->resolution_x != 0U ? info->resolution_x : requested_dpi;
  source_ydpi = info->resolution_y != 0U ? info->resolution_y : source_xdpi;
  raster_bytes_per_pixel = info->format == KODAK_I2000_IMAGE_FORMAT_RGB24 ? 3U : 1U;
  if (info->raw_width > SIZE_MAX / info->raw_height ||
      info->raw_width * info->raw_height >
          KODAK_I2000_IMAGE_MAX_BYTES / raster_bytes_per_pixel)
    return 0;
  raster_size = info->raw_width * info->raw_height;
  raster_bytes = raster_size * raster_bytes_per_pixel;
  if (raster_bytes > info->footer_offset - info->raster_offset)
    return 0;
  raster = raw + info->raster_offset;

  /* The vendor accepts an RGB source in its grayscale image chain and uses
   * PixelRGB2Gray before BCS.  Supporting that combination here also keeps
   * the renderer correct when a firmware revision ignores the gray image
   * selector in the scan configuration. */
  if (bytes_per_pixel == 1U && raster_bytes_per_pixel == 3U) {
    size_t index;
    converted_raster = (uint8_t *)malloc(raster_size);
    if (converted_raster == NULL)
      return 0;
    for (index = 0U; index < raster_size; ++index)
      converted_raster[index] = pixel_luma(raster + index * 3U, 3U);
    raster = converted_raster;
    raster_bytes_per_pixel = 1U;
  }
  if (bytes_per_pixel == 1U && raster_bytes_per_pixel == 1U) {
    processed_raster = apply_background_smoothing(raster, info->raw_width,
                                                  info->raw_height, source_ydpi);
    if (processed_raster != NULL)
      raster = processed_raster;
  }

  if (bytes_per_pixel == 3U && raster_bytes_per_pixel == 3U && info->raw_height >= 5U &&
      (source_ydpi == 600U || source_ydpi == 300U)) {
    processed_raster =
        sharpen_sensor_rgb(raster, info->raw_width, info->raw_height, source_ydpi);
    if (processed_raster == NULL)
      return 0;
    raster = processed_raster;
  }

  left = 0U;
  top = 0U;
  crop_width = info->raw_width;
  crop_height = info->raw_height;
  if (auto_crop &&
      find_sheet_crop(raster, info->raw_width, info->raw_height, &left, &top,
                      &crop_width, &crop_height, raster_bytes_per_pixel)) {
    crop_found = 1;
    /* Keep the result bounded to the parsed raster even if the detector is
     * changed later to use a margin. */
    if (left + crop_width > info->raw_width || top + crop_height > info->raw_height) {
      left = 0U;
      top = 0U;
      crop_width = info->raw_width;
      crop_height = info->raw_height;
    }
  }
  if (crop_found)
    expand_sheet_crop(info->raw_width, info->raw_height, source_xdpi, source_ydpi,
                      &left, &top, &crop_width, &crop_height);
  info->crop_left = left;
  info->crop_top = top;
  info->crop_width = crop_width;
  info->crop_height = crop_height;

  if (crop_width > SIZE_MAX / requested_dpi || crop_height > SIZE_MAX / requested_dpi) {
    free(processed_raster);
    free(converted_raster);
    return 0;
  }
  /* mlao::ResampleImageToDPI uses cvttsd2si for these dimensions: truncation,
   * rather than round-to-nearest, is observable in the vendor PNM headers. */
  output_width = (crop_width * requested_dpi) / source_xdpi;
  output_height = (crop_height * requested_dpi) / source_ydpi;
  if (output_width == 0U || output_height == 0U ||
      output_width > SIZE_MAX / bytes_per_pixel ||
      output_width * bytes_per_pixel > SIZE_MAX / output_height) {
    free(processed_raster);
    free(converted_raster);
    return 0;
  }
  output_size = output_width * bytes_per_pixel * output_height;
  if (output_size > KODAK_I2000_IMAGE_MAX_BYTES) {
    free(processed_raster);
    free(converted_raster);
    return 0;
  }
  output = (uint8_t *)malloc(output_size);
  if (output == NULL) {
    free(processed_raster);
    free(converted_raster);
    return 0;
  }

  for (y = 0U; y < output_height; ++y) {
    uint64_t source_y =
        ((uint64_t)top << 16) + (((uint64_t)y * crop_height) << 16) / output_height;
    size_t x;
    for (x = 0U; x < output_width; ++x) {
      uint64_t source_x =
          ((uint64_t)left << 16) + (((uint64_t)x * crop_width) << 16) / output_width;
      size_t channel;
      size_t output_index = (y * output_width + x) * bytes_per_pixel;
      for (channel = 0U; channel < bytes_per_pixel; ++channel)
        output[output_index + channel] = sample_bilinear_channel(
            raster, info->raw_width, info->raw_height, raster_bytes_per_pixel, channel,
            source_x, source_y);
    }
  }
  if (bytes_per_pixel == 3U) {
    if (crop_found)
      fill_color_exterior(output, output_width, output_height, requested_dpi);
    (void)apply_color_document_correction(output, output_width, output_height,
                                          info->rear != 0U);
  }
  free(processed_raster);
  free(converted_raster);
  *image = output;
  *image_size = output_size;
  *width = output_width;
  *height = output_height;
  return 1;
}

int
kodak_i2000_image_render_gray(const uint8_t *raw, kodak_i2000_image_info *info,
                              unsigned requested_dpi, int auto_crop, uint8_t **image,
                              size_t *image_size, size_t *width, size_t *height)
{
  if (info == NULL || (info->format != KODAK_I2000_IMAGE_FORMAT_GRAY8 &&
                       info->format != KODAK_I2000_IMAGE_FORMAT_RGB24))
    return 0;
  return render_raster(raw, info, requested_dpi, auto_crop, 1U, image, image_size,
                       width, height);
}

int
kodak_i2000_image_render_color(const uint8_t *raw, kodak_i2000_image_info *info,
                               unsigned requested_dpi, int auto_crop, uint8_t **image,
                               size_t *image_size, size_t *width, size_t *height)
{
  if (info == NULL || info->format != KODAK_I2000_IMAGE_FORMAT_RGB24)
    return 0;
  return render_raster(raw, info, requested_dpi, auto_crop, 3U, image, image_size,
                       width, height);
}
