#ifndef KODAK_I2000_COLOR_H
#define KODAK_I2000_COLOR_H

#include <stdint.h>
#include <stddef.h>
#pragma GCC visibility push(hidden)
uint8_t *sharpen_sensor_rgb(const uint8_t *source, size_t width, size_t height,
                            unsigned sensor_dpi);
int apply_color_document_correction(uint8_t *image, size_t width, size_t height,
                                    unsigned rear);
#pragma GCC visibility pop

#endif
