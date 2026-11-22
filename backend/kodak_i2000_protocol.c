#include "kodak_i2000_protocol.h"

#include <limits.h>
#include <string.h>

static const kodak_i2000_model model_table[] = {
    {0x040a, 0x601c, "i2400"}, {0x29cc, 0x100a, "i2420"}, {0x040a, 0x601d, "i2600"},
    {0x29cc, 0x100b, "i2620"}, {0x040a, 0x601e, "i2800"}, {0x29cc, 0x100c, "i2820"}};

/* The second 0x30 write in the paper-backed vendor trace is the normal
 * front/gray/200-dpi image path.  SetupScanner mutates only the documented
 * option-dependent fields below before transmitting the structure. */
static const uint8_t normal_scan_configuration[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x5d, 0x0e, 0x00, 0x00, 0x5e, 0x16, 0x00, 0x77,
    0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x36, 0x0c,
    0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x01, 0x0f,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Optional baffle pre-scan captured immediately before the normal record. */
static const uint8_t baffle_scan_configuration[KODAK_I2000_CONFIG_LENGTH] = {
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5d, 0x0e, 0x00, 0x00, 0x5e, 0x16, 0x00,
    0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xc5, 0x00,
    0x00, 0x00, 0x11, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* SetupManualImage(scan type 7, 300 dpi, gray, duplex) emitted this record
 * during the vendor's one-time CheckBackgroundColor() path. */
static const uint8_t cold_background_scan_configuration[KODAK_I2000_CONFIG_LENGTH] = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x71, 0x11, 0x00, 0x00, 0x5e, 0x16, 0x00,
    0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xc5, 0x00,
    0xc5, 0x00, 0x00, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static int
normal_resolution_supported(unsigned resolution)
{
  switch (resolution) {
  case 100U:
  case 150U:
  case 200U:
  case 240U:
  case 300U:
  case 400U:
  case 600U:
    return 1;
  default:
    return 0;
  }
}

static void
put_uint16_le(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)(value & 0xffU);
  destination[1] = (uint8_t)(value >> 8);
}

int
kodak_i2000_session_info_is_uninitialized(const uint8_t *session_info,
                                          size_t session_info_size)
{
  size_t index;

  if (session_info == NULL || session_info_size != KODAK_I2000_SESSION_LENGTH)
    return 0;
  for (index = 0U; index < session_info_size; ++index) {
    if (session_info[index] != 0U)
      return 0;
  }
  return 1;
}

int
kodak_i2000_build_initial_session_info(uint8_t *session_info, size_t session_info_size)
{
  if (session_info == NULL || session_info_size != KODAK_I2000_SESSION_DATA_LENGTH)
    return 0;

  /* DeviceSettings::InitializeSessionInfo() zeroes SDmSessionInfo, sets the
   * record version, then writes ff at offsets 2-5 and 14-15.  Request 0x37 is
   * volatile scanner-session state, not flash or NVRAM. */
  memset(session_info, 0, session_info_size);
  session_info[0] = KODAK_I2000_SESSION_VERSION;
  session_info[2] = 0xffU;
  session_info[3] = 0xffU;
  session_info[4] = 0xffU;
  session_info[5] = 0xffU;
  session_info[14] = 0xffU;
  session_info[15] = 0xffU;
  return 1;
}

int
kodak_i2000_session_info_needs_calibration(const uint8_t *session_info,
                                           size_t session_info_size)
{
  return session_info != NULL && session_info_size == KODAK_I2000_SESSION_LENGTH &&
         session_info[KODAK_I2000_SESSION_VERSION_OFFSET] ==
             KODAK_I2000_SESSION_VERSION &&
         session_info[KODAK_I2000_SESSION_MARKER_OFFSET] ==
             KODAK_I2000_SESSION_INITIAL_MARKER &&
         session_info[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
             KODAK_I2000_SESSION_BACKGROUND_UNKNOWN &&
         session_info[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
             KODAK_I2000_SESSION_BACKGROUND_UNKNOWN;
}

int
kodak_i2000_session_info_needs_background_check(const uint8_t *session_info,
                                                size_t session_info_size)
{
  return session_info != NULL && session_info_size == KODAK_I2000_SESSION_LENGTH &&
         session_info[KODAK_I2000_SESSION_VERSION_OFFSET] ==
             KODAK_I2000_SESSION_VERSION &&
         session_info[KODAK_I2000_SESSION_MARKER_OFFSET] ==
             KODAK_I2000_SESSION_MARKER &&
         (session_info[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
              KODAK_I2000_SESSION_BACKGROUND_UNKNOWN ||
          session_info[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
              KODAK_I2000_SESSION_BACKGROUND_UNKNOWN);
}

int
kodak_i2000_build_calibration_ready_session_info(const uint8_t *session_info,
                                                 size_t session_info_size,
                                                 uint8_t *ready_session,
                                                 size_t ready_session_size)
{
  if (ready_session == NULL || ready_session_size != KODAK_I2000_SESSION_DATA_LENGTH ||
      !kodak_i2000_session_info_needs_calibration(session_info, session_info_size))
    return 0;
  memcpy(ready_session, session_info, ready_session_size);
  ready_session[KODAK_I2000_SESSION_MARKER_OFFSET] = KODAK_I2000_SESSION_MARKER;
  return 1;
}

int
kodak_i2000_build_background_ready_session_info(const uint8_t *session_info,
                                                size_t session_info_size,
                                                uint8_t *ready_session,
                                                size_t ready_session_size)
{
  if (ready_session == NULL || ready_session_size != KODAK_I2000_SESSION_DATA_LENGTH ||
      !kodak_i2000_session_info_needs_background_check(session_info, session_info_size))
    return 0;
  memcpy(ready_session, session_info, ready_session_size);
  ready_session[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] =
      KODAK_I2000_SESSION_BACKGROUND_BLACK;
  ready_session[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] =
      KODAK_I2000_SESSION_BACKGROUND_BLACK;
  return 1;
}

int
kodak_i2000_calibration_sdram_matches(const uint8_t *flash_record,
                                      size_t flash_record_size,
                                      const uint8_t *sdram_record,
                                      size_t sdram_record_size)
{
  size_t offset;

  if (flash_record == NULL || sdram_record == NULL ||
      flash_record_size != KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH ||
      sdram_record_size != KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH)
    return 0;

  /* VerifySramCalData() checks 29 complete 32-bit words with byte order
   * reversed by the SDRAM window, then the first two bytes of word 30. */
  for (offset = 0U; offset < 29U * 4U; offset += 4U) {
    if (flash_record[offset] != sdram_record[offset + 3U] ||
        flash_record[offset + 1U] != sdram_record[offset + 2U] ||
        flash_record[offset + 2U] != sdram_record[offset + 1U] ||
        flash_record[offset + 3U] != sdram_record[offset])
      return 0;
  }
  return flash_record[offset] == sdram_record[offset + 3U] &&
         flash_record[offset + 1U] == sdram_record[offset + 2U];
}

int
kodak_i2000_status_is_idle_and_empty(const uint8_t *status, size_t status_size)
{
  if (status == NULL || status_size != KODAK_I2000_STATUS_LENGTH)
    return 0;
  return status[KODAK_I2000_STATUS_POWER_STATE_OFFSET] ==
             KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL &&
         status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET] == 0U &&
         status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET] !=
             KODAK_I2000_STATUS_TRAY_PAPER_PRESENT &&
         status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] == 0U;
}

unsigned
kodak_i2000_sensor_resolution(unsigned resolution)
{
  return resolution == 400U || resolution == 600U ? 600U : 300U;
}

uint16_t
kodak_i2000_scale_sensor_value(uint16_t value, unsigned sensor_resolution)
{
  uint32_t scaled = value;

  if (sensor_resolution > 300U)
    scaled *= sensor_resolution / 300U;
  return scaled > UINT16_MAX ? UINT16_MAX : (uint16_t)scaled;
}

uint8_t
kodak_i2000_ip_control(unsigned mode, unsigned sensor_resolution)
{
  if (mode == KODAK_I2000_MODE_COLOR)
    return 0x10U;
  return sensor_resolution > 300U ? 0x30U : 0x00U;
}

uint8_t
kodak_i2000_image_footer_only(unsigned mode)
{
  return mode == KODAK_I2000_MODE_COLOR ? 0x00U : 0x01U;
}

uint8_t
kodak_i2000_resolution_footer(unsigned sensor_resolution)
{
  return sensor_resolution > 300U ? 0x00U : 0x22U;
}

uint16_t
kodak_i2000_capture_value(unsigned source)
{
  switch (source) {
  case KODAK_I2000_SOURCE_FRONT:
    return KODAK_I2000_FRONT_CAPTURE_VALUE;
  case KODAK_I2000_SOURCE_REAR:
    return KODAK_I2000_REAR_CAPTURE_VALUE;
  case KODAK_I2000_SOURCE_DUPLEX:
    return KODAK_I2000_DUPLEX_CAPTURE_VALUE;
  default:
    return 0U;
  }
}

int
kodak_i2000_build_normal_scan_configuration_for_pages(unsigned source, unsigned mode,
                                                      unsigned resolution,
                                                      uint16_t page_count,
                                                      uint8_t *configuration,
                                                      size_t configuration_size)
{
  int front_enabled;
  int high_resolution;
  int rear_enabled;

  if (configuration == NULL || configuration_size < KODAK_I2000_CONFIG_LENGTH ||
      source > KODAK_I2000_SOURCE_DUPLEX || mode > KODAK_I2000_MODE_COLOR ||
      !normal_resolution_supported(resolution) ||
      sizeof(normal_scan_configuration) != KODAK_I2000_CONFIG_LENGTH)
    return 0;

  memcpy(configuration, normal_scan_configuration, sizeof(normal_scan_configuration));
  put_uint16_le(configuration + KODAK_I2000_CONFIG_PAGE_COUNT_OFFSET, page_count);
  front_enabled = source != KODAK_I2000_SOURCE_REAR;
  rear_enabled = source != KODAK_I2000_SOURCE_FRONT;
  high_resolution = kodak_i2000_sensor_resolution(resolution) > 300U;

  /* The vendor 400-dpi trace selects the 600-dpi camera/transport group and
   * resamples on the host.  Both 400 and 600 therefore use speed ID two;
   * lower requested resolutions use the 300-dpi group and speed ID four. */
  configuration[KODAK_I2000_CONFIG_SPEED_ID_OFFSET] = high_resolution ? 0x02U : 0x04U;
  configuration[KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET] =
      mode == KODAK_I2000_MODE_COLOR ? 0x00U : 0x01U;
  put_uint16_le(configuration + KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET,
                high_resolution ? UINT16_C(0x0277) : UINT16_C(0x165e));
  /* Each low bit selects the corresponding image FIFO: front=1, rear=2.
   * The duplex vendor capture uses 0x03 here. */
  configuration[KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET] =
      (uint8_t)kodak_i2000_capture_value(source);

  /* Capture style is zero for ordinary pages.  The 0x00c5 overscan length
   * is in feeder encoder units and therefore does not scale with DPI. */
  configuration[KODAK_I2000_CONFIG_CAPTURE_STYLE_OFFSET] = 0x00U;
  put_uint16_le(configuration + KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET,
                front_enabled ? UINT16_C(0x00c5) : 0U);
  put_uint16_le(configuration + KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET,
                rear_enabled ? UINT16_C(0x00c5) : 0U);
  return 1;
}

int
kodak_i2000_build_normal_scan_configuration(unsigned source, unsigned mode,
                                            unsigned resolution, uint8_t *configuration,
                                            size_t configuration_size)
{
  return kodak_i2000_build_normal_scan_configuration_for_pages(
      source, mode, resolution, 0U, configuration, configuration_size);
}

int
kodak_i2000_build_baffle_scan_configuration(unsigned source, unsigned mode,
                                            unsigned resolution, uint8_t *configuration,
                                            size_t configuration_size)
{
  int high_resolution;

  if (configuration == NULL || configuration_size < KODAK_I2000_CONFIG_LENGTH ||
      source > KODAK_I2000_SOURCE_DUPLEX || mode > KODAK_I2000_MODE_COLOR ||
      !normal_resolution_supported(resolution) ||
      sizeof(baffle_scan_configuration) != KODAK_I2000_CONFIG_LENGTH)
    return 0;

  memcpy(configuration, baffle_scan_configuration, sizeof(baffle_scan_configuration));
  high_resolution = kodak_i2000_sensor_resolution(resolution) > 300U;
  put_uint16_le(configuration + KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET,
                high_resolution ? UINT16_C(0x0277) : UINT16_C(0x165e));
  configuration[KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET] =
      mode == KODAK_I2000_MODE_COLOR ? 0x00U : 0x01U;
  /* The baffle form repeats the side mask in both nibbles: front=0x11,
   * rear=0x22, duplex=0x33. */
  configuration[KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET] =
      (uint8_t)(kodak_i2000_capture_value(source) * 0x11U);
  put_uint16_le(configuration + KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET,
                source == KODAK_I2000_SOURCE_REAR ? 0U : UINT16_C(0x00c5));
  put_uint16_le(configuration + KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET,
                source == KODAK_I2000_SOURCE_FRONT ? 0U : UINT16_C(0x00c5));
  return 1;
}

int
kodak_i2000_build_cold_background_scan_configuration(uint8_t *configuration,
                                                     size_t configuration_size)
{
  if (configuration == NULL || configuration_size != KODAK_I2000_CONFIG_LENGTH ||
      sizeof(cold_background_scan_configuration) != KODAK_I2000_CONFIG_LENGTH)
    return 0;
  memcpy(configuration, cold_background_scan_configuration,
         sizeof(cold_background_scan_configuration));
  return 1;
}

const kodak_i2000_model *
kodak_i2000_models(size_t *count)
{
  if (count != NULL)
    *count = sizeof(model_table) / sizeof(model_table[0]);
  return model_table;
}

const kodak_i2000_model *
kodak_i2000_model_lookup(uint16_t vendor_id, uint16_t product_id)
{
  size_t i;
  size_t count = sizeof(model_table) / sizeof(model_table[0]);

  for (i = 0; i < count; ++i) {
    if (model_table[i].vendor_id == vendor_id &&
        model_table[i].product_id == product_id)
      return &model_table[i];
  }
  return NULL;
}

int
kodak_i2000_decode_button_event(const uint8_t *event, size_t event_size,
                                uint16_t *button_number)
{
  if (event == NULL || button_number == NULL ||
      event_size < KODAK_I2000_INTERRUPT_EVENT_LENGTH ||
      event[KODAK_I2000_INTERRUPT_EVENT_TYPE_OFFSET] !=
          KODAK_I2000_INTERRUPT_EVENT_BUTTON_PRESS)
    return 0;
  *button_number =
      (uint16_t)event[KODAK_I2000_INTERRUPT_EVENT_DATA_OFFSET] |
      (uint16_t)((uint16_t)event[KODAK_I2000_INTERRUPT_EVENT_DATA_OFFSET + 1U] << 8);
  return 1;
}
