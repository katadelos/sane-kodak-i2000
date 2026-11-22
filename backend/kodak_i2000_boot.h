#ifndef KODAK_I2000_BOOT_H
#define KODAK_I2000_BOOT_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
SANE_Status prepare_scanner_session(kodak_i2000_handle *scanner,
                                    const char *firmware_source);
#pragma GCC visibility pop

#endif
