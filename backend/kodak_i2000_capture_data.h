#ifndef KODAK_I2000_CAPTURE_DATA_H
#define KODAK_I2000_CAPTURE_DATA_H

#include <stddef.h>
#include <stdint.h>

/* These tables are the fixed i2400/A4/200-dpi setup observed in the
 * paper-backed vendor capture.  They are kept in a separate translation unit
 * so the protocol implementation remains readable. */
typedef struct {
  const uint8_t *e5[8];
  const uint8_t *e4[4];
  uint8_t correction;
  uint32_t correction_crc;
} kodak_i2000_color_profile;

extern const kodak_i2000_color_profile kodak_i2000_piranha_profile;
extern const kodak_i2000_color_profile kodak_i2000_dt_profile;
extern const uint16_t kodak_i2000_calibration_e4_values[4];
extern const uint8_t *const kodak_i2000_camera_blocks[2][6];
extern const uint8_t *const kodak_i2000_background_camera_blocks[2][6];
extern const uint8_t *const kodak_i2000_camera_color_300_blocks[2][6];
/* The i2400 high-resolution branch uses the 600-dpi color sensor tables for
 * both the 400- and 600-dpi camera selections. */
extern const uint8_t *const kodak_i2000_camera_600_blocks[2][6];

extern const uint8_t *const kodak_i2000_camera_color_300_dt_blocks[2][6];

extern const uint8_t *const kodak_i2000_camera_600_dt_blocks[2][6];

/* Build the capture-backed 40/37 payload from a live 64-byte session read.
 * Returns non-zero only for a recognized session record. */
int kodak_i2000_build_session_setup(const kodak_i2000_color_profile *profile,
                                    unsigned side, const uint8_t *session_info,
                                    size_t session_info_size, uint8_t *setup,
                                    size_t setup_size);

#endif /* KODAK_I2000_CAPTURE_DATA_H */
