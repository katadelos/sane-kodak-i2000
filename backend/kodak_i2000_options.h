#ifndef KODAK_I2000_OPTIONS_H
#define KODAK_I2000_OPTIONS_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
SANE_Bool resolution_supported(SANE_Int resolution);
SANE_Status init_options(kodak_i2000_handle *scanner);
void init_device_options(kodak_i2000_handle *scanner);
SANE_Status get_device_option(kodak_i2000_handle *scanner, SANE_Int option,
                              void *value);
SANE_Status copy_string_value(SANE_String value, SANE_Int size, const char *source);
#pragma GCC visibility pop

#endif
