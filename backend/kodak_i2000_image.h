/*
 * Raw-image framing and rendering for Kodak i2000 scanners.
 *
 * The scanner transfers an 8-bit sensor raster followed by a compact image
 * footer.  Keep this code independent of SANE so the framing can be tested
 * against captured USB data without opening the device.
 */
#ifndef KODAK_I2000_IMAGE_H
#define KODAK_I2000_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct kodak_i2000_image_info {
  size_t raw_size;
  /* Offset of the raster after any capture-backed transport prefix. */
  size_t raster_offset;
  size_t footer_offset;
  size_t footer_length;
  size_t raw_width;
  size_t raw_height;
  unsigned resolution_x;
  unsigned resolution_y;
  unsigned format;
  size_t crop_left;
  size_t crop_top;
  size_t crop_width;
  size_t crop_height;
  unsigned rear;
} kodak_i2000_image_info;

/* Conservative host-side Perfect Page controls.  Deskew estimates and
 * corrects a page angle of roughly +/-5 degrees.  Brightness and contrast are
 * percentages in [-100, 100], sharpening is [0, 3], rotation is one of
 * 0/90/180/270 clockwise, and blank_threshold is the maximum percentage of
 * pixels darker than the paper cutoff for a page to be reported as blank. */
typedef struct kodak_i2000_image_options {
  int deskew;
  int brightness;
  int contrast;
  unsigned sharpen;
  unsigned rotation;
  int detect_blank;
  unsigned blank_threshold;
} kodak_i2000_image_options;

/* Return non-zero when raw contains a complete i2000 image and footer. */
int kodak_i2000_image_parse(const uint8_t *raw, size_t raw_size,
                            kodak_i2000_image_info *info);

/*
 * Render the parsed 8-bit grayscale raster at requested_dpi.  auto_crop uses
 * the broad white sheet boundary in the sensor raster; if no reliable
 * boundary can be found, the complete raster is rendered instead.
 */
int kodak_i2000_image_render_gray(const uint8_t *raw, kodak_i2000_image_info *info,
                                  unsigned requested_dpi, int auto_crop,
                                  uint8_t **image, size_t *image_size, size_t *width,
                                  size_t *height);

/* Render the scanner's packed 8-bit RGB24 raster at requested_dpi.  When the
 * page supplies enough bright neutral paper evidence, the color path also
 * removes bounded column shading, fills detected scan-bed wedges, and applies
 * conservative document white/black/color normalization. */
int kodak_i2000_image_render_color(const uint8_t *raw, kodak_i2000_image_info *info,
                                   unsigned requested_dpi, int auto_crop,
                                   uint8_t **image, size_t *image_size, size_t *width,
                                   size_t *height);

/* Apply the host-side enhancement controls to an owned gray or RGB image.
 * Rotation may replace *image; on success the caller remains responsible for
 * freeing it. */
int kodak_i2000_image_process(uint8_t **image, size_t *image_size, size_t *width,
                              size_t *height, size_t channels,
                              const kodak_i2000_image_options *options,
                              int *blank_page);

#endif
