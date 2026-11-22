#ifndef KODAK_I2000_CAMERA_H
#define KODAK_I2000_CAMERA_H

#include "kodak_i2000_device.h"
#include "kodak_i2000_capture_data.h"

#pragma GCC visibility push(hidden)
SANE_Status write_fpga_fifo(kodak_i2000_handle *scanner);
SANE_Status write_document_ids(kodak_i2000_handle *scanner);
uint16_t load_uint16_le(const SANE_Byte *value);
SANE_Status
read_image_calibration_record(kodak_i2000_handle *scanner,
                              SANE_Byte record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH]);
SANE_Status setup_background_cameras(kodak_i2000_handle *scanner);
SANE_Status restore_camera_after_capture(kodak_i2000_handle *scanner, uint16_t camera);
SANE_Status restore_capture_hardware(kodak_i2000_handle *scanner);
SANE_Status write_camera_profile(kodak_i2000_handle *scanner, unsigned side,
                                 const kodak_i2000_color_profile *profile);
SANE_Status write_fpga_word(kodak_i2000_handle *scanner, uint16_t high_register,
                            uint16_t value);
SANE_Status setup_capture_fpga(kodak_i2000_handle *scanner, SANE_Bool dt_cameras);
#pragma GCC visibility pop

#endif
