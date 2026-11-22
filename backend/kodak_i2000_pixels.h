#ifndef KODAK_I2000_PIXELS_H
#define KODAK_I2000_PIXELS_H

#include <stddef.h>
#include <stdint.h>

static inline unsigned
pixel_luma(const uint8_t *pixel, size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1U)
    return pixel[0];
  /* The vendor's PixelRGB2Gray multiplies its default 0.299/0.587/0.114
   * coefficients by 2^15, truncates each coefficient, and shifts the sum
   * back by 15.  Keep edge detection and the actual gray conversion on the
   * same integer path. */
  return ((UINT32_C(9797) * pixel[0] + UINT32_C(19234) * pixel[1] +
           UINT32_C(3735) * pixel[2]) >>
          15);
}

#endif
