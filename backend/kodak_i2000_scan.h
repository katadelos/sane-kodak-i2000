#ifndef KODAK_I2000_SCAN_H
#define KODAK_I2000_SCAN_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
SANE_Status write_setup_operation(kodak_i2000_handle *scanner, uint16_t value);
SANE_Status write_completion_marker(kodak_i2000_handle *scanner);
SANE_Status write_cold_background_scan_configuration(kodak_i2000_handle *scanner);
SANE_Status write_lamp_off(kodak_i2000_handle *scanner);
SANE_Status finish_page_capture(kodak_i2000_handle *scanner,
                                SANE_Bool normal_completion);
SANE_Status finish_continuous_job(kodak_i2000_handle *scanner,
                                  SANE_Bool normal_completion);
SANE_Status write_start_capture(kodak_i2000_handle *scanner, uint16_t value);
SANE_Status finish_continuous_capture_error(kodak_i2000_handle *scanner,
                                            SANE_Status capture_status);
SANE_Bool capture_parameters_supported(const kodak_i2000_handle *scanner);
SANE_Status setup_scan(kodak_i2000_handle *scanner);
#pragma GCC visibility pop

#endif
