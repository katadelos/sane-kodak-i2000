#ifndef KODAK_I2000_TRANSPORT_H
#define KODAK_I2000_TRANSPORT_H

#include "kodak_i2000_device.h"

#pragma GCC visibility push(hidden)
SANE_Bool acquisition_cancelled(const kodak_i2000_handle *scanner);
SANE_Status acquisition_status(const kodak_i2000_handle *scanner, SANE_Status status);
SANE_Status read_status(kodak_i2000_handle *scanner);
SANE_Status poll_button_events(kodak_i2000_handle *scanner);
SANE_Status read_device_get(kodak_i2000_handle *scanner, const char *name,
                            uint8_t request, uint16_t value, uint16_t index,
                            SANE_Int length, SANE_Byte *data);
void log_raw_block(const char *name, const SANE_Byte *data, size_t length);
SANE_Status read_session_info(kodak_i2000_handle *scanner);
SANE_Status
write_session_info_checked(kodak_i2000_handle *scanner, const char *name,
                           const SANE_Byte session[KODAK_I2000_SESSION_LENGTH]);
SANE_Status read_fpga_byte(kodak_i2000_handle *scanner, uint16_t value,
                           SANE_Byte *result);
SANE_Status read_fpga_doc_id(kodak_i2000_handle *scanner, uint16_t value,
                             uint32_t *result);
SANE_Status write_device_set(kodak_i2000_handle *scanner, const char *name,
                             uint8_t request, uint16_t value, uint16_t index,
                             SANE_Int length, SANE_Byte *data);
SANE_Status set_interrupt_events(kodak_i2000_handle *scanner, SANE_Bool enabled);
SANE_Status write_fpga_register(kodak_i2000_handle *scanner, uint16_t value,
                                SANE_Int length, SANE_Byte *data);
SANE_Status write_fpga_byte(kodak_i2000_handle *scanner, uint16_t value,
                            SANE_Byte data);
SANE_Status read_optional_device_state(kodak_i2000_handle *scanner);
#pragma GCC visibility pop

#endif
