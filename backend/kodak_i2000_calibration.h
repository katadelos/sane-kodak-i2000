#ifndef KODAK_I2000_CALIBRATION_H
#define KODAK_I2000_CALIBRATION_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
SANE_Status drain_selected_baffle_captures(kodak_i2000_handle *scanner);
SANE_Status ensure_cold_session_ready(kodak_i2000_handle *scanner);
#pragma GCC visibility pop

#endif
