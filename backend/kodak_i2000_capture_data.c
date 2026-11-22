#include <stdint.h>
#include <string.h>

#include "kodak_i2000_capture_data.h"
#include "kodak_i2000_protocol.h"

/* LoadColorTableData() sends the blocks in this value order.  The same value
 * selects the corresponding CRC slot in SDmSessionInfo. */
const uint16_t kodak_i2000_calibration_e4_values[4] = {2U, 3U, 1U, 0U};

static uint32_t
capture_crc32(const uint8_t *data, size_t size)
{
  uint32_t crc = UINT32_MAX;
  size_t index;
  unsigned bit;

  for (index = 0U; index < size; ++index) {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1) ^ ((crc & UINT32_C(1)) != 0U ? UINT32_C(0xedb88320) : 0U);
  }
  return crc ^ UINT32_MAX;
}

static void
store_u32_le(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

int
kodak_i2000_build_session_setup(const kodak_i2000_color_profile *profile, unsigned side,
                                const uint8_t *session_info, size_t session_info_size,
                                uint8_t *setup, size_t setup_size)
{
  size_t index;

  if (profile == NULL || side > 1U || session_info == NULL || setup == NULL ||
      session_info_size != KODAK_I2000_SESSION_LENGTH ||
      setup_size != KODAK_I2000_SESSION_DATA_LENGTH ||
      session_info[KODAK_I2000_SESSION_VERSION_OFFSET] != KODAK_I2000_SESSION_VERSION ||
      session_info[KODAK_I2000_SESSION_MARKER_OFFSET] != KODAK_I2000_SESSION_MARKER ||
      session_info[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
          KODAK_I2000_SESSION_BACKGROUND_UNKNOWN ||
      session_info[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
          KODAK_I2000_SESSION_BACKGROUND_UNKNOWN)
    return 0;

  memcpy(setup, session_info, KODAK_I2000_SESSION_LENGTH);
  setup[KODAK_I2000_SESSION_CORRECTION_SELECTOR_OFFSET + side] =
      KODAK_I2000_SESSION_LUT_SELECTOR;
  setup[KODAK_I2000_SESSION_LINEARIZATION_SELECTOR_OFFSET + side] =
      KODAK_I2000_SESSION_LUT_SELECTOR;

  store_u32_le(setup + 6U + side * 4U, profile->correction_crc);
  for (index = 0U; index < sizeof(kodak_i2000_calibration_e4_values) /
                               sizeof(kodak_i2000_calibration_e4_values[0]);
       ++index) {
    size_t crc_offset =
        KODAK_I2000_SESSION_LUT_CRC_BASE_OFFSET + side * 4U +
        kodak_i2000_calibration_e4_values[index] * KODAK_I2000_SESSION_LUT_CRC_STRIDE;
    uint32_t crc = capture_crc32(profile->e4[index], KODAK_I2000_CALIBRATION_E4_LENGTH);

    if (crc_offset > setup_size - sizeof(crc))
      return 0;
    store_u32_le(setup + crc_offset, crc);
  }
  return 1;
}
