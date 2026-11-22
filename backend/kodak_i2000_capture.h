#ifndef KODAK_I2000_CAPTURE_H
#define KODAK_I2000_CAPTURE_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
void release_image_buffer(kodak_i2000_handle *scanner);
void release_all_image_buffers(kodak_i2000_handle *scanner);
SANE_Status activate_pending_image(kodak_i2000_handle *scanner);
SANE_Status prepare_captured_images(kodak_i2000_handle *scanner,
                                    unsigned empty_idle_retries);
#pragma GCC visibility pop

#endif
