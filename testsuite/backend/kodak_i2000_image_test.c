/* sane - Scanner Access Now Easy.
   Copyright (C) 2026 SANE Project

   This file is part of the SANE package and is distributed under the
   terms of the GNU General Public License version 3 or later.

   Unit tests for the Kodak i2000 image path which do not require a scanner. */

#include "backend/kodak_i2000_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH 64U
#define TEST_HEIGHT 64U
#define TEST_VENDOR_PREFIX_BYTES 16U
#define TEST_VENDOR_TRAILING_BYTES 56U
#define TEST_FORMAT_RGB24 0U
#define TEST_FORMAT_GRAY8 1U

static int failures;

static void
expect_true(const char *name, int condition)
{
  if (!condition) {
    fprintf(stderr, "%s: expectation failed\n", name);
    ++failures;
  }
}

static uint32_t
mutation_next(uint32_t *state)
{
  *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
  return *state;
}

static uint8_t *
make_captured_image(size_t width, size_t height, unsigned format, size_t prefix_bytes,
                    size_t trailing_bytes, size_t *raw_size)
{
  static const uint8_t footer_prefix[32] = {
      0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x02, 0xf5, 0x00, 0x00, 0x01,
      0xf6, 0x00, 0x02, 0x01, 0xf9, 0x00, 0x02, 0x01, 0xfa, 0x00, 0x00,
      0x01, 0xfc, 0x00, 0x00, 0x01, 0xfd, 0x00, 0x01, 0x01, 0xfe};
  size_t bytes_per_pixel = format == TEST_FORMAT_RGB24 ? 3U : 1U;
  size_t body_size;
  size_t footer_offset;
  size_t size;
  uint8_t *raw;
  size_t index;

  if (raw_size == NULL || width == 0U || height == 0U || width > SIZE_MAX / height)
    return NULL;
  body_size = width * height;
  if (body_size > SIZE_MAX / bytes_per_pixel)
    return NULL;
  body_size *= bytes_per_pixel;
  if (prefix_bytes > SIZE_MAX - body_size)
    return NULL;
  footer_offset = prefix_bytes + body_size;
  if (footer_offset > SIZE_MAX - sizeof(footer_prefix) ||
      footer_offset + sizeof(footer_prefix) > SIZE_MAX - trailing_bytes)
    return NULL;
  size = footer_offset + sizeof(footer_prefix) + trailing_bytes;
  if (size == 0U)
    return NULL;
  raw = (uint8_t *)calloc(1U, size);
  if (raw == NULL)
    return NULL;
  if (prefix_bytes == TEST_VENDOR_PREFIX_BYTES)
    raw[9] = 0x02U;
  memset(raw + prefix_bytes, 90, body_size);
  memcpy(raw + footer_offset, footer_prefix, sizeof(footer_prefix));
  raw[footer_offset + 20U] = (uint8_t)((width / 2U) >> 8);
  raw[footer_offset + 21U] = (uint8_t)(width / 2U);
  raw[footer_offset + 28U] = (uint8_t)(format >> 8);
  raw[footer_offset + 29U] = (uint8_t)format;
  for (index = 0U; index < trailing_bytes; ++index) {
    raw[footer_offset + sizeof(footer_prefix) + index] =
        (uint8_t)((index * 7U + 3U) % 19U);
    if (index + 1U == trailing_bytes)
      raw[footer_offset + sizeof(footer_prefix) + index] = 0x05U;
  }
  *raw_size = size;
  return raw;
}

static void
test_capture_backed_footer_shape(void)
{
  const size_t body_size = TEST_WIDTH * TEST_HEIGHT;
  const size_t footer_offset = TEST_VENDOR_PREFIX_BYTES + body_size;
  size_t raw_size = 0U;
  uint8_t *raw = make_captured_image(TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT_GRAY8,
                                     TEST_VENDOR_PREFIX_BYTES,
                                     TEST_VENDOR_TRAILING_BYTES, &raw_size);
  kodak_i2000_image_info info;
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t width = 0U;
  size_t height = 0U;
  size_t index;

  expect_true("allocate gray fixture", raw != NULL);
  if (raw == NULL)
    return;
  expect_true("vendor-style footer does not end in ff", raw[raw_size - 1U] == 0x05U);
  expect_true("parse vendor-style trailing footer",
              kodak_i2000_image_parse(raw, raw_size, &info));
  expect_true("parsed gray width", info.raw_width == TEST_WIDTH);
  expect_true("parsed gray height", info.raw_height == TEST_HEIGHT);
  expect_true("parsed raster prefix", info.raster_offset == TEST_VENDOR_PREFIX_BYTES);
  expect_true("parsed footer offset", info.footer_offset == footer_offset);
  expect_true("parsed footer length",
              info.footer_length == 32U + TEST_VENDOR_TRAILING_BYTES);
  expect_true("parsed x resolution", info.resolution_x == 300U);
  expect_true("parsed y resolution", info.resolution_y == 300U);
  expect_true("parsed gray format", info.format == TEST_FORMAT_GRAY8);

  expect_true("render parsed gray image",
              kodak_i2000_image_render_gray(raw, &info, 150U, 0, &rendered,
                                            &rendered_size, &width, &height));
  expect_true("gray output width truncates", width == 32U);
  expect_true("gray output height truncates", height == 32U);
  expect_true("gray output size", rendered_size == width * height);
  for (index = 0U; index < rendered_size; ++index) {
    if (rendered[index] != 90U) {
      expect_true("constant gray raster remains constant", 0);
      break;
    }
  }
  free(rendered);

  raw[footer_offset + 11U] = 0U;
  expect_true("reject broken footer tag",
              !kodak_i2000_image_parse(raw, raw_size, &info));
  raw[footer_offset + 11U] = 0xf6U;

  raw[footer_offset + 13U] = 5U;
  expect_true("reject unknown resolution enum",
              !kodak_i2000_image_parse(raw, raw_size, &info));
  raw[footer_offset + 13U] = 2U;

  raw[footer_offset + 29U] = 2U;
  expect_true("reject unknown image format",
              !kodak_i2000_image_parse(raw, raw_size, &info));
  raw[footer_offset + 29U] = TEST_FORMAT_GRAY8;

  raw[footer_offset + 21U] = 33U;
  expect_true("reject width inconsistent with raster length",
              !kodak_i2000_image_parse(raw, raw_size, &info));
  free(raw);

  raw = make_captured_image(TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT_GRAY8, 0U, 0U,
                            &raw_size);
  expect_true("allocate prefix-free fixture", raw != NULL);
  if (raw != NULL) {
    expect_true("parse prefix-free footer",
                kodak_i2000_image_parse(raw, raw_size, &info));
    expect_true("prefix-free raster offset", info.raster_offset == 0U);
    free(raw);
  }
}

static void
test_rgb_rendering(void)
{
  size_t raw_size = 0U;
  uint8_t *raw = make_captured_image(TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT_RGB24,
                                     TEST_VENDOR_PREFIX_BYTES,
                                     TEST_VENDOR_TRAILING_BYTES, &raw_size);
  const size_t body_size = TEST_WIDTH * TEST_HEIGHT * 3U;
  const uint8_t expected_gray =
      (uint8_t)((UINT32_C(9797) * 10U + UINT32_C(19234) * 20U + UINT32_C(3735) * 30U) >>
                15);
  kodak_i2000_image_info info;
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t width = 0U;
  size_t height = 0U;
  size_t index;

  expect_true("allocate rgb fixture", raw != NULL);
  if (raw == NULL)
    return;
  for (index = 0U; index < TEST_WIDTH * TEST_HEIGHT; ++index) {
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U] = 10U;
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U + 1U] = 20U;
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U + 2U] = 30U;
  }
  expect_true("parse rgb footer", kodak_i2000_image_parse(raw, raw_size, &info));
  expect_true("parsed rgb format", info.format == TEST_FORMAT_RGB24);
  expect_true("parsed rgb raster prefix",
              info.raster_offset == TEST_VENDOR_PREFIX_BYTES);
  expect_true("parsed rgb raster size",
              info.footer_offset == TEST_VENDOR_PREFIX_BYTES + body_size);

  expect_true("render packed rgb",
              kodak_i2000_image_render_color(raw, &info, 300U, 0, &rendered,
                                             &rendered_size, &width, &height));
  expect_true("rgb output dimensions", width == TEST_WIDTH && height == TEST_HEIGHT);
  expect_true("rgb output size", rendered_size == body_size);
  expect_true("rgb output bytes preserved",
              rendered_size == body_size &&
                  memcmp(rendered, raw + TEST_VENDOR_PREFIX_BYTES, body_size) == 0);
  free(rendered);
  rendered = NULL;

  expect_true("render rgb through gray path",
              kodak_i2000_image_render_gray(raw, &info, 300U, 0, &rendered,
                                            &rendered_size, &width, &height));
  expect_true("rgb-to-gray output size", rendered_size == TEST_WIDTH * TEST_HEIGHT);
  for (index = 0U; index < rendered_size; ++index) {
    if (rendered[index] != expected_gray) {
      expect_true("rgb-to-gray coefficients", 0);
      break;
    }
  }
  free(rendered);
  free(raw);
}

static void
test_high_resolution_color_rendering(void)
{
  size_t raw_size = 0U;
  uint8_t *raw = make_captured_image(TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT_RGB24,
                                     TEST_VENDOR_PREFIX_BYTES,
                                     TEST_VENDOR_TRAILING_BYTES, &raw_size);
  const size_t footer_offset = TEST_VENDOR_PREFIX_BYTES + TEST_WIDTH * TEST_HEIGHT * 3U;
  kodak_i2000_image_info info;
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t width = 0U;
  size_t height = 0U;
  size_t index;

  expect_true("allocate 600 dpi rgb fixture", raw != NULL);
  if (raw == NULL)
    return;

  /* Footer resolution enum zero is the scanner's 600-dpi sensor stream.
   * A requested 400-dpi page is acquired through that stream and resampled
   * by the host renderer. */
  raw[footer_offset + 12U] = 0U;
  raw[footer_offset + 13U] = 0U;
  raw[footer_offset + 16U] = 0U;
  raw[footer_offset + 17U] = 0U;
  for (index = 0U; index < TEST_WIDTH * TEST_HEIGHT; ++index) {
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U] = 17U;
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U + 1U] = 83U;
    raw[TEST_VENDOR_PREFIX_BYTES + index * 3U + 2U] = 149U;
  }

  expect_true("parse 600 dpi rgb footer",
              kodak_i2000_image_parse(raw, raw_size, &info));
  expect_true("parsed 600 dpi x resolution", info.resolution_x == 600U);
  expect_true("parsed 600 dpi y resolution", info.resolution_y == 600U);
  expect_true("render 600 dpi rgb stream at 400 dpi",
              kodak_i2000_image_render_color(raw, &info, 400U, 0, &rendered,
                                             &rendered_size, &width, &height));
  expect_true("400 dpi rgb dimensions truncate", width == 42U && height == 42U);
  expect_true("400 dpi rgb output size", rendered_size == width * height * 3U);
  for (index = 0U; index < rendered_size; index += 3U) {
    if (rendered[index] != 17U || rendered[index + 1U] != 83U ||
        rendered[index + 2U] != 149U) {
      expect_true("constant 600-to-400 rgb resample remains constant", 0);
      break;
    }
  }
  free(rendered);
  rendered = NULL;

  expect_true("render 600 dpi rgb stream at native resolution",
              kodak_i2000_image_render_color(raw, &info, 600U, 0, &rendered,
                                             &rendered_size, &width, &height));
  expect_true("600 dpi rgb dimensions preserved",
              width == TEST_WIDTH && height == TEST_HEIGHT);
  expect_true("600 dpi rgb output size",
              rendered_size == TEST_WIDTH * TEST_HEIGHT * 3U);
  free(rendered);
  free(raw);
}

static void
test_color_document_correction(void)
{
  const size_t width = 320U;
  const size_t height = 320U;
  const size_t raw_size = width * height * 3U;
  uint8_t *raw = (uint8_t *)malloc(raw_size);
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t rendered_width = 0U;
  size_t rendered_height = 0U;
  kodak_i2000_image_info info;
  size_t x;
  size_t y;

  expect_true("allocate color-correction fixture", raw != NULL);
  if (raw == NULL)
    return;

  /* A neutral paper field with repeatable column shading and two narrow
   * channel defects.  The colored and black patches occupy too few rows to
   * influence the per-column upper-background percentile. */
  for (y = 0U; y < height; ++y) {
    for (x = 0U; x < width; ++x) {
      uint8_t *pixel = raw + (y * width + x) * 3U;
      unsigned ripple = (unsigned)((x / 9U + y / 13U) % 7U);

      pixel[0] = (uint8_t)(238U + ripple);
      pixel[1] = (uint8_t)(241U + ripple);
      pixel[2] = (uint8_t)(244U + ripple);
      if (x >= 72U && x < 76U) {
        pixel[0] = 242U;
        pixel[1] = 207U;
        pixel[2] = 251U;
      } else if (x >= 211U && x < 216U) {
        pixel[0] = 209U;
        pixel[1] = 249U;
        pixel[2] = 225U;
      }
      if (y >= 112U && y < 144U && x >= 32U && x < 96U) {
        pixel[0] = 160U;
        pixel[1] = 78U;
        pixel[2] = 58U;
      }
      if (y >= 176U && y < 192U && x >= 120U && x < 240U) {
        pixel[0] = 24U;
        pixel[1] = 26U;
        pixel[2] = 23U;
      }
    }
  }
  memset(&info, 0, sizeof(info));
  info.raw_size = raw_size;
  info.raster_offset = 0U;
  info.footer_offset = raw_size;
  info.raw_width = width;
  info.raw_height = height;
  info.resolution_x = 300U;
  info.resolution_y = 300U;
  info.format = TEST_FORMAT_RGB24;

  expect_true("render color-correction fixture",
              kodak_i2000_image_render_color(raw, &info, 300U, 0, &rendered,
                                             &rendered_size, &rendered_width,
                                             &rendered_height));
  expect_true("color-correction dimensions",
              rendered_width == width && rendered_height == height);
  expect_true("color-correction output size", rendered_size == raw_size);
  if (rendered != NULL) {
    const uint8_t *plain = rendered + (40U * width + 40U) * 3U;
    const uint8_t *magenta_streak = rendered + (40U * width + 73U) * 3U;
    const uint8_t *green_streak = rendered + (40U * width + 213U) * 3U;
    const uint8_t *red = rendered + (120U * width + 40U) * 3U;
    const uint8_t *black = rendered + (180U * width + 160U) * 3U;

    expect_true("plain paper becomes neutral white",
                plain[0] >= 250U && plain[1] >= 250U && plain[2] >= 250U &&
                    plain[0] + 2U >= plain[1] && plain[1] + 2U >= plain[0] &&
                    plain[1] + 2U >= plain[2] && plain[2] + 2U >= plain[1]);
    expect_true("magenta column defect is neutralized", magenta_streak[0] >= 248U &&
                                                            magenta_streak[1] >= 248U &&
                                                            magenta_streak[2] >= 248U);
    expect_true("green column defect is neutralized", green_streak[0] >= 248U &&
                                                          green_streak[1] >= 248U &&
                                                          green_streak[2] >= 248U);
    expect_true("red patch remains strongly red",
                red[0] >= 220U && red[0] > red[1] + 120U && red[0] > red[2] + 150U);
    expect_true("dark content remains dark",
                black[0] < 20U && black[1] < 20U && black[2] < 20U);
  }
  free(rendered);
  free(raw);
}

static void
test_color_crop_exterior_fill(void)
{
  const size_t width = 320U;
  const size_t height = 320U;
  const size_t raw_size = width * height * 3U;
  uint8_t *raw = (uint8_t *)calloc(1U, raw_size);
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t rendered_width = 0U;
  size_t rendered_height = 0U;
  kodak_i2000_image_info info;
  size_t x;
  size_t y;

  expect_true("allocate color crop fixture", raw != NULL);
  if (raw == NULL)
    return;
  for (y = 30U; y < 290U; ++y) {
    size_t left = 38U + (y - 30U) / 65U;
    size_t right = 282U + (y - 30U) / 65U;
    for (x = left; x < right; ++x) {
      uint8_t *pixel = raw + (y * width + x) * 3U;
      pixel[0] = 239U;
      pixel[1] = 242U;
      pixel[2] = 245U;
    }
  }
  for (y = 145U; y < 175U; ++y)
    for (x = 130U; x < 190U; ++x) {
      uint8_t *pixel = raw + (y * width + x) * 3U;
      pixel[0] = 20U;
      pixel[1] = 21U;
      pixel[2] = 19U;
    }
  memset(&info, 0, sizeof(info));
  info.raw_size = raw_size;
  info.raster_offset = 0U;
  info.footer_offset = raw_size;
  info.raw_width = width;
  info.raw_height = height;
  info.resolution_x = 300U;
  info.resolution_y = 300U;
  info.format = TEST_FORMAT_RGB24;

  expect_true("render auto-cropped color fixture",
              kodak_i2000_image_render_color(raw, &info, 300U, 1, &rendered,
                                             &rendered_size, &rendered_width,
                                             &rendered_height));
  expect_true("color fixture was cropped",
              rendered_width < width && rendered_height < height);
  if (rendered != NULL && rendered_width != 0U && rendered_height != 0U) {
    const uint8_t *top_left = rendered;
    const uint8_t *bottom_right =
        rendered + (rendered_width * rendered_height - 1U) * 3U;
    size_t center =
        ((rendered_height / 2U) * rendered_width + rendered_width / 2U) * 3U;

    expect_true("top-left scan-bed wedge is white-filled",
                top_left[0] == 255U && top_left[1] == 255U && top_left[2] == 255U);
    expect_true("bottom-right scan-bed wedge is white-filled",
                bottom_right[0] == 255U && bottom_right[1] == 255U &&
                    bottom_right[2] == 255U);
    expect_true("interior black content survives exterior fill",
                rendered[center] < 20U && rendered[center + 1U] < 20U &&
                    rendered[center + 2U] < 20U);
  }
  free(rendered);
  free(raw);
}

static void
test_background_smoothing(void)
{
  const size_t width = 264U;
  const size_t height = 264U;
  const size_t raw_size = width * height;
  uint8_t *raw = (uint8_t *)malloc(raw_size);
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t rendered_width = 0U;
  size_t rendered_height = 0U;
  kodak_i2000_image_info info;

  expect_true("allocate smoothing fixture", raw != NULL);
  if (raw == NULL)
    return;
  memset(raw, 100, raw_size);
  raw[0] = 110U;
  raw[1] = 126U;
  memset(&info, 0, sizeof(info));
  info.raw_size = raw_size;
  info.raster_offset = 0U;
  info.footer_offset = raw_size;
  info.raw_width = width;
  info.raw_height = height;
  info.resolution_x = 300U;
  info.resolution_y = 300U;
  info.format = TEST_FORMAT_GRAY8;

  expect_true("render smoothing fixture",
              kodak_i2000_image_render_gray(raw, &info, 300U, 0, &rendered,
                                            &rendered_size, &rendered_width,
                                            &rendered_height));
  expect_true("smoothing dimensions",
              rendered_width == width && rendered_height == height);
  expect_true("smoothing output size", rendered_size == raw_size);
  expect_true("near-background pixel maps to mode",
              rendered != NULL && rendered[0] == 100U);
  expect_true("pixel outside smoothing radius is preserved",
              rendered != NULL && rendered[1] == 126U);
  free(rendered);
  free(raw);
}

static size_t
maximum_dark_pixels_in_row(const uint8_t *image, size_t width, size_t height)
{
  size_t maximum = 0U;
  size_t y;

  for (y = 0U; y < height; ++y) {
    size_t count = 0U;
    size_t x;
    for (x = 0U; x < width; ++x)
      if (image[y * width + x] < 128U)
        ++count;
    if (count > maximum)
      maximum = count;
  }
  return maximum;
}

static void
test_deskew_processing(void)
{
  const size_t width = 160U;
  const size_t height = 100U;
  size_t image_size = width * height;
  uint8_t *image = (uint8_t *)malloc(image_size);
  kodak_i2000_image_options options;
  size_t output_width = width;
  size_t output_height = height;
  size_t before;
  size_t after;
  unsigned line;
  int blank = 0;

  expect_true("allocate deskew fixture", image != NULL);
  if (image == NULL)
    return;
  memset(image, 255, image_size);
  for (line = 0U; line < 4U; ++line) {
    size_t x;
    int base_y = 20 + (int)line * 20;
    for (x = 8U; x + 8U < width; ++x) {
      int numerator = 24 * ((int)x - (int)(width / 2U));
      int y = base_y + numerator / 512;
      if (y >= 0 && (size_t)y < height)
        image[(size_t)y * width + x] = 0U;
    }
  }
  before = maximum_dark_pixels_in_row(image, width, height);
  memset(&options, 0, sizeof(options));
  options.deskew = 1;
  expect_true("deskew slanted gray lines",
              kodak_i2000_image_process(&image, &image_size, &output_width,
                                        &output_height, 1U, &options, &blank));
  after = maximum_dark_pixels_in_row(image, output_width, output_height);
  expect_true("deskew preserves frame dimensions", output_width == width &&
                                                       output_height == height &&
                                                       image_size == width * height);
  expect_true("deskew sharpens horizontal projection",
              after > before + before / 2U && after >= 70U);
  free(image);
}

static size_t
maximum_dark_red_pixels_in_row(const uint8_t *image, size_t width, size_t height)
{
  size_t maximum = 0U;
  size_t y;

  for (y = 0U; y < height; ++y) {
    size_t count = 0U;
    size_t x;
    for (x = 0U; x < width; ++x)
      if (image[(y * width + x) * 3U] < 128U)
        ++count;
    if (count > maximum)
      maximum = count;
  }
  return maximum;
}

static void
test_rgb_deskew_processing(void)
{
  const size_t width = 160U;
  const size_t height = 100U;
  size_t image_size = width * height * 3U;
  uint8_t *image = (uint8_t *)malloc(image_size);
  kodak_i2000_image_options options;
  size_t output_width = width;
  size_t output_height = height;
  size_t ordered_pixels = 0U;
  size_t before;
  size_t after;
  unsigned line;
  int blank = 0;

  expect_true("allocate RGB deskew fixture", image != NULL);
  if (image == NULL)
    return;
  memset(image, 255, image_size);
  for (line = 0U; line < 4U; ++line) {
    size_t x;
    int base_y = 20 + (int)line * 20;
    for (x = 8U; x + 8U < width; ++x) {
      int numerator = -24 * ((int)x - (int)(width / 2U));
      int y = base_y + numerator / 512;
      if (y >= 0 && (size_t)y < height) {
        uint8_t *pixel = image + ((size_t)y * width + x) * 3U;
        pixel[0] = 0U;
        pixel[1] = 64U;
        pixel[2] = 128U;
      }
    }
  }
  before = maximum_dark_red_pixels_in_row(image, width, height);
  memset(&options, 0, sizeof(options));
  options.deskew = 1;
  expect_true("deskew slanted RGB lines",
              kodak_i2000_image_process(&image, &image_size, &output_width,
                                        &output_height, 3U, &options, &blank));
  after = maximum_dark_red_pixels_in_row(image, output_width, output_height);
  expect_true("RGB deskew preserves frame dimensions",
              output_width == width && output_height == height &&
                  image_size == width * height * 3U);
  expect_true("RGB deskew sharpens horizontal projection",
              after > before + before / 2U && after >= 70U);
  if (image != NULL) {
    size_t index;
    for (index = 0U; index < output_width * output_height; ++index) {
      const uint8_t *pixel = image + index * 3U;
      if (pixel[0] < 240U && pixel[0] < pixel[1] && pixel[1] < pixel[2])
        ++ordered_pixels;
    }
  }
  expect_true("RGB deskew preserves channel order", ordered_pixels >= 256U);
  free(image);
}

static void
test_perfect_page_processing(void)
{
  static const uint8_t rotation_source[6] = {10U, 20U, 30U, 40U, 50U, 60U};
  static const uint8_t rotation_expected[6] = {40U, 10U, 50U, 20U, 60U, 30U};
  kodak_i2000_image_options options;
  uint8_t *image = (uint8_t *)malloc(sizeof(rotation_source));
  size_t image_size = sizeof(rotation_source);
  size_t width = 3U;
  size_t height = 2U;
  int blank = 0;

  expect_true("allocate rotation fixture", image != NULL);
  if (image == NULL)
    return;
  memcpy(image, rotation_source, sizeof(rotation_source));
  memset(&options, 0, sizeof(options));
  options.rotation = 90U;
  expect_true("rotate gray image clockwise",
              kodak_i2000_image_process(&image, &image_size, &width, &height, 1U,
                                        &options, &blank));
  expect_true("rotation swaps dimensions", width == 2U && height == 3U);
  expect_true("rotation preserves pixel order",
              memcmp(image, rotation_expected, sizeof(rotation_expected)) == 0);
  free(image);

  image_size = 3U;
  width = 3U;
  height = 1U;
  image = (uint8_t *)malloc(image_size);
  expect_true("allocate tone fixture", image != NULL);
  if (image != NULL) {
    image[0] = 64U;
    image[1] = 128U;
    image[2] = 192U;
    memset(&options, 0, sizeof(options));
    options.brightness = 10;
    options.contrast = 50;
    expect_true("apply brightness and contrast",
                kodak_i2000_image_process(&image, &image_size, &width, &height, 1U,
                                          &options, &blank));
    expect_true("tone midpoint brightens", image[1] == 153U);
    expect_true("tone contrast expands values", image[0] <= 64U && image[2] > 192U);
    free(image);
  }

  image_size = 25U;
  width = 5U;
  height = 5U;
  image = (uint8_t *)malloc(image_size);
  expect_true("allocate sharpen fixture", image != NULL);
  if (image != NULL) {
    memset(image, 100, image_size);
    image[12] = 50U;
    memset(&options, 0, sizeof(options));
    options.sharpen = 1U;
    expect_true("apply sharpening",
                kodak_i2000_image_process(&image, &image_size, &width, &height, 1U,
                                          &options, &blank));
    expect_true("normal Kodak unsharp mask on dark detail", image[12] == 33U);
    free(image);
  }

  image_size = 16U;
  width = 4U;
  height = 4U;
  image = (uint8_t *)malloc(image_size);
  expect_true("allocate blank-page fixture", image != NULL);
  if (image != NULL) {
    memset(image, 255, image_size);
    memset(&options, 0, sizeof(options));
    options.detect_blank = 1;
    options.blank_threshold = 1U;
    expect_true("detect white blank page",
                kodak_i2000_image_process(&image, &image_size, &width, &height, 1U,
                                          &options, &blank));
    expect_true("white page is blank", blank == 1);
    image[0] = 0U;
    expect_true("measure page with content",
                kodak_i2000_image_process(&image, &image_size, &width, &height, 1U,
                                          &options, &blank));
    expect_true("content page is not blank", blank == 0);
    free(image);
  }
}

static void
exercise_parsed_image(const uint8_t *raw, kodak_i2000_image_info *info)
{
  uint8_t *rendered = NULL;
  size_t rendered_size = 0U;
  size_t width = 0U;
  size_t height = 0U;
  size_t bytes_per_pixel = info->format == TEST_FORMAT_RGB24 ? 3U : 1U;
  int rendered_ok;

  if (info->format == TEST_FORMAT_RGB24)
    rendered_ok = kodak_i2000_image_render_color(
        raw, info, info->resolution_x, 0, &rendered, &rendered_size, &width, &height);
  else
    rendered_ok = kodak_i2000_image_render_gray(
        raw, info, info->resolution_x, 0, &rendered, &rendered_size, &width, &height);
  expect_true("render mutation accepted by parser", rendered_ok);
  if (rendered_ok) {
    int dimensions_ok =
        width != 0U && height != 0U && width <= SIZE_MAX / bytes_per_pixel;
    size_t row_bytes = dimensions_ok ? width * bytes_per_pixel : 0U;

    if (dimensions_ok)
      dimensions_ok =
          row_bytes <= SIZE_MAX / height && rendered_size == row_bytes * height;
    expect_true("mutated render has bounded dimensions", dimensions_ok);
    expect_true("mutated render has allocated output", rendered != NULL);
  }
  free(rendered);
}

static void
test_malformed_mutation_sweep(void)
{
  const size_t footer_offset = TEST_VENDOR_PREFIX_BYTES + TEST_WIDTH * TEST_HEIGHT;
  size_t raw_size = 0U;
  uint8_t *raw = make_captured_image(TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT_GRAY8,
                                     TEST_VENDOR_PREFIX_BYTES,
                                     TEST_VENDOR_TRAILING_BYTES, &raw_size);
  uint8_t *mutated;
  uint8_t *random_bytes;
  uint32_t state = UINT32_C(0x49323030);
  unsigned accepted = 0U;
  unsigned rejected = 0U;
  unsigned iteration;
  size_t supplied_size;

  expect_true("allocate mutation fixture", raw != NULL);
  if (raw == NULL)
    return;
  mutated = (uint8_t *)malloc(raw_size);
  random_bytes = (uint8_t *)malloc(8192U);
  expect_true("allocate mutation working buffers",
              mutated != NULL && random_bytes != NULL);
  if (mutated == NULL || random_bytes == NULL) {
    free(random_bytes);
    free(mutated);
    free(raw);
    return;
  }

  /* Every prefix shorter than the complete structured footer must be
   * rejected.  Running every boundary also gives ASan/UBSan an exhaustive
   * short-buffer sweep over the reverse footer search. */
  for (supplied_size = 0U; supplied_size < footer_offset + 32U; ++supplied_size) {
    kodak_i2000_image_info info;
    expect_true("reject truncated structured footer",
                !kodak_i2000_image_parse(raw, supplied_size, &info));
  }

  for (iteration = 0U; iteration < 2048U; ++iteration) {
    kodak_i2000_image_info info;
    unsigned flips = 1U + mutation_next(&state) % 4U;
    unsigned flip;
    int parsed;

    memcpy(mutated, raw, raw_size);
    for (flip = 0U; flip < flips; ++flip) {
      size_t offset = mutation_next(&state) % raw_size;
      mutated[offset] ^= (uint8_t)(1U << (mutation_next(&state) & UINT32_C(7)));
    }
    supplied_size = raw_size;
    if ((iteration & 3U) == 0U)
      supplied_size = mutation_next(&state) % (raw_size + 1U);
    parsed = kodak_i2000_image_parse(mutated, supplied_size, &info);
    if (parsed) {
      size_t bytes_per_pixel = info.format == TEST_FORMAT_RGB24 ? 3U : 1U;
      int bounds_ok = supplied_size >= 32U &&
                      info.raster_offset <= info.footer_offset &&
                      info.footer_offset <= supplied_size - 32U;

      if (bounds_ok) {
        size_t raster_size = info.footer_offset - info.raster_offset;
        bounds_ok =
            info.raw_width != 0U && info.raw_width <= raster_size / bytes_per_pixel;
        if (bounds_ok) {
          size_t row_bytes = info.raw_width * bytes_per_pixel;
          bounds_ok = row_bytes != 0U && raster_size % row_bytes == 0U &&
                      info.raw_height == raster_size / row_bytes;
        }
      }
      expect_true("mutated footer remains within supplied data", bounds_ok);
      if (bounds_ok && iteration % 67U == 1U)
        exercise_parsed_image(mutated, &info);
      ++accepted;
    } else {
      ++rejected;
    }
  }

  /* Also sweep unstructured data and variable short lengths.  A chance-valid
   * record is checked and rendered instead of being assumed impossible. */
  for (iteration = 0U; iteration < 1024U; ++iteration) {
    kodak_i2000_image_info info;
    size_t index;

    supplied_size = mutation_next(&state) % 8193U;
    for (index = 0U; index < supplied_size; ++index)
      random_bytes[index] = (uint8_t)(mutation_next(&state) >> 24);
    if (kodak_i2000_image_parse(random_bytes, supplied_size, &info))
      exercise_parsed_image(random_bytes, &info);
  }
  expect_true("image mutation sweep exercises accepted frames", accepted != 0U);
  expect_true("image mutation sweep exercises rejected frames", rejected != 0U);
  free(random_bytes);
  free(mutated);
  free(raw);
}

static void
test_invalid_arguments(void)
{
  kodak_i2000_image_info info;

  memset(&info, 0, sizeof(info));
  expect_true("reject null raw image", !kodak_i2000_image_parse(NULL, 64U, &info));
  expect_true("reject null image info",
              !kodak_i2000_image_parse((const uint8_t *)&info, sizeof(info), NULL));
  expect_true("reject undersized image",
              !kodak_i2000_image_parse((const uint8_t *)&info, sizeof(info), &info));
}

int
main(void)
{
  test_capture_backed_footer_shape();
  test_rgb_rendering();
  test_high_resolution_color_rendering();
  test_color_document_correction();
  test_color_crop_exterior_fill();
  test_background_smoothing();
  test_deskew_processing();
  test_rgb_deskew_processing();
  test_perfect_page_processing();
  test_malformed_mutation_sweep();
  test_invalid_arguments();
  if (failures != 0) {
    fprintf(stderr, "%d Kodak i2000 image test(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  puts("Kodak i2000 image tests passed");
  return EXIT_SUCCESS;
}
