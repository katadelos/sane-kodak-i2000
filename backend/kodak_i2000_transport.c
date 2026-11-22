/* USB control transfers, scanner status, and device registers. */
#include "sane/config.h"
#include <string.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_transport.h"
#include "kodak_i2000_calibration.h"

#define KODAK_I2000_INTERRUPT_READ_TIMEOUT_MS 250

SANE_Bool
acquisition_cancelled(const kodak_i2000_handle *scanner)
{
  return scanner != NULL && scanner->cancel_requested != 0;
}

SANE_Status
acquisition_status(const kodak_i2000_handle *scanner, SANE_Status status)
{
  return acquisition_cancelled(scanner) ? SANE_STATUS_CANCELLED : status;
}

SANE_Status
read_status(kodak_i2000_handle *scanner)
{
  SANE_Status status;
  size_t index;

  if (scanner == NULL || scanner->usb < 0)
    return SANE_STATUS_INVAL;
  memset(scanner->status, 0, sizeof(scanner->status));
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_STATUS_REQUEST_TYPE,
                                 KODAK_I2000_STATUS_REQUEST, KODAK_I2000_STATUS_VALUE,
                                 KODAK_I2000_STATUS_INDEX, KODAK_I2000_STATUS_LENGTH,
                                 scanner->status);
  scanner->status_valid = status == SANE_STATUS_GOOD;
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: status request failed: %s\n", __func__, sane_strstatus(status));
  else {
    DBG(7, "%s: status bytes:", __func__);
    for (index = 0U; index < sizeof(scanner->status); ++index)
      DBG(7, " %02x", scanner->status[index]);
    DBG(7, "\n");
    DBG(6,
        "%s: firmware id=0x%02x version=0x%08x power=0x%02x "
        "buffer=0x%02x transport=0x%02x tray=0x%02x udds=0x%02x "
        "lamp=0x%04x interlock=0x%02x error=0x%02x\n",
        __func__, scanner->status[0],
        (unsigned)scanner->status[1] | ((unsigned)scanner->status[2] << 8) |
            ((unsigned)scanner->status[3] << 16) | ((unsigned)scanner->status[4] << 24),
        scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET],
        scanner->status[KODAK_I2000_STATUS_BUFFER_STATE_OFFSET],
        scanner->status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET],
        scanner->status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET],
        scanner->status[KODAK_I2000_STATUS_UDDS_STATE_OFFSET],
        (unsigned)scanner->status[KODAK_I2000_STATUS_LAMP_STATE_OFFSET] |
            ((unsigned)scanner->status[KODAK_I2000_STATUS_LAMP_STATE_OFFSET + 1U] << 8),
        scanner->status[KODAK_I2000_STATUS_INTERLOCK_STATE_OFFSET],
        scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET]);
    if (scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] != 0U)
      DBG(1, "%s: device reports error code 0x%02x\n", __func__,
          scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET]);
  }
  return status;
}

SANE_Status
poll_button_events(kodak_i2000_handle *scanner)
{
  SANE_Byte event[KODAK_I2000_INTERRUPT_EVENT_LENGTH];
  SANE_Status result = SANE_STATUS_GOOD;
  unsigned attempt;

  if (scanner == NULL || scanner->usb < 0)
    return SANE_STATUS_INVAL;
  if (scanner->acquiring != 0 || scanner->continuous_job_active)
    return SANE_STATUS_DEVICE_BUSY;

  result = set_interrupt_events(scanner, SANE_TRUE);
  if (result != SANE_STATUS_GOOD)
    return result;

  sanei_usb_set_endpoint(scanner->usb, USB_DIR_IN | USB_ENDPOINT_TYPE_INTERRUPT,
                         KODAK_I2000_INTERRUPT_ENDPOINT);
  sanei_usb_set_timeout(KODAK_I2000_INTERRUPT_READ_TIMEOUT_MS);
  for (attempt = 0U; attempt < 8U; ++attempt) {
    size_t event_size = sizeof(event);
    uint16_t button_number;
    SANE_Status status;

    if (attempt != 0U)
      sanei_usb_set_timeout(1);
    status = sanei_usb_read_int(scanner->usb, event, &event_size);
    if (status != SANE_STATUS_GOOD) {
      /* A timeout is reported as IO_ERROR by sanei_usb.  No event during a
       * sensor poll is normal, so only structural API errors are surfaced. */
      if (status != SANE_STATUS_IO_ERROR && status != SANE_STATUS_EOF)
        result = status;
      break;
    }
    if (kodak_i2000_decode_button_event(event, event_size, &button_number)) {
      scanner->ocp_button_number = (SANE_Word)button_number;
      scanner->scan_button_pending = SANE_TRUE;
      DBG(3, "%s: OCP scan event, job/sequence=%u\n", __func__,
          (unsigned)button_number);
    } else if (event_size != 0U) {
      DBG(6, "%s: ignored interrupt event type=0x%02x length=%lu\n", __func__,
          (unsigned)event[0], (unsigned long)event_size);
    }
  }
  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  return result;
}

SANE_Status
read_device_get(kodak_i2000_handle *scanner, const char *name, uint8_t request,
                uint16_t value, uint16_t index, SANE_Int length, SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || data == NULL || length <= 0)
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_STATUS_REQUEST_TYPE, request,
                                 value, index, length, data);
  if (status != SANE_STATUS_GOOD)
    DBG(3,
        "%s: %s request=0x%02x value=0x%04x index=0x%04x length=%d "
        "failed: %s\n",
        __func__, name != NULL ? name : "Get", request, (unsigned)value,
        (unsigned)index, (int)length, sane_strstatus(status));
  return status;
}

void
log_raw_block(const char *name, const SANE_Byte *data, size_t length)
{
  size_t index;

  if (name == NULL || data == NULL)
    return;
  DBG(6, "%s: %s block (%u bytes):", __func__, name, (unsigned)length);
  for (index = 0U; index < length; ++index)
    DBG(7, " %02x", data[index]);
  DBG(7, "\n");
}

static SANE_Status
read_firmware_versions(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0)
    return SANE_STATUS_INVAL;
  memset(scanner->firmware_versions, 0, sizeof(scanner->firmware_versions));
  status = read_device_get(scanner, "GetFirmwareVersions", KODAK_I2000_FIRMWARE_REQUEST,
                           KODAK_I2000_FIRMWARE_VALUE, KODAK_I2000_FIRMWARE_INDEX,
                           KODAK_I2000_FIRMWARE_LENGTH, scanner->firmware_versions);
  scanner->firmware_versions_valid = status == SANE_STATUS_GOOD;
  if (status != SANE_STATUS_GOOD)
    return status;

  /* The vendor logger labels words at offsets 0, 4, 8, and 12, but the
   * remaining encoding is not established.  Preserve the complete block. */
  log_raw_block("firmware versions", scanner->firmware_versions,
                sizeof(scanner->firmware_versions));
  return status;
}

SANE_Status
read_session_info(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  memset(scanner->session_info, 0, sizeof(scanner->session_info));
  status = read_device_get(scanner, "GetSessionInfo", KODAK_I2000_SESSION_REQUEST,
                           KODAK_I2000_SESSION_VALUE, KODAK_I2000_SESSION_INDEX,
                           KODAK_I2000_SESSION_LENGTH, scanner->session_info);
  scanner->session_info_valid = status == SANE_STATUS_GOOD;
  if (scanner->session_info_valid)
    log_raw_block("session info", scanner->session_info, sizeof(scanner->session_info));
  return status;
}

SANE_Status
write_session_info_checked(kodak_i2000_handle *scanner, const char *name,
                           const SANE_Byte session[KODAK_I2000_SESSION_LENGTH])
{
  SANE_Status status;

  if (scanner == NULL || session == NULL)
    return SANE_STATUS_INVAL;
  status =
      write_device_set(scanner, name, KODAK_I2000_SESSION_DATA_REQUEST,
                       KODAK_I2000_SESSION_DATA_VALUE, KODAK_I2000_SESSION_DATA_INDEX,
                       KODAK_I2000_SESSION_DATA_LENGTH, (SANE_Byte *)session);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_session_info(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (memcmp(scanner->session_info, session, KODAK_I2000_SESSION_LENGTH) != 0) {
    DBG(1, "%s: %s record was not accepted\n", __func__,
        name != NULL ? name : "session");
    return SANE_STATUS_IO_ERROR;
  }
  return SANE_STATUS_GOOD;
}

static SANE_Status
initialize_session_info(kodak_i2000_handle *scanner)
{
  SANE_Byte initial_session[KODAK_I2000_SESSION_DATA_LENGTH];
  SANE_Status status;

  if (scanner == NULL ||
      !kodak_i2000_build_initial_session_info(initial_session, sizeof(initial_session)))
    return SANE_STATUS_INVAL;

  DBG(2, "%s: initializing volatile session data after firmware start\n", __func__);
  status =
      write_session_info_checked(scanner, "InitializeSessionInfo", initial_session);
  return status;
}

static SANE_Status
read_energy_star(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  scanner->energy_star = 0;
  status = read_device_get(scanner, "GetEnergyStar", KODAK_I2000_ENERGY_STAR_REQUEST,
                           KODAK_I2000_ENERGY_STAR_VALUE, KODAK_I2000_ENERGY_STAR_INDEX,
                           KODAK_I2000_ENERGY_STAR_LENGTH, &scanner->energy_star);
  scanner->energy_star_valid = status == SANE_STATUS_GOOD;
  if (scanner->energy_star_valid)
    DBG(6, "%s: energy-star=0x%02x\n", __func__, (unsigned)scanner->energy_star);
  return status;
}

static SANE_Status
read_consumable_meters(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  memset(scanner->consumable_meters, 0, sizeof(scanner->consumable_meters));
  status = read_device_get(
      scanner, "GetConsumableMetersData", KODAK_I2000_CONSUMABLE_METERS_REQUEST,
      KODAK_I2000_CONSUMABLE_METERS_VALUE, KODAK_I2000_CONSUMABLE_METERS_INDEX,
      KODAK_I2000_CONSUMABLE_METERS_LENGTH, scanner->consumable_meters);
  scanner->consumable_meters_valid = status == SANE_STATUS_GOOD;
  scanner->consumable_meters_version_supported =
      scanner->consumable_meters_valid &&
      scanner->consumable_meters[KODAK_I2000_CONSUMABLE_METERS_VERSION_OFFSET] ==
          KODAK_I2000_CONSUMABLE_METERS_SUPPORTED_VERSION;
  if (scanner->consumable_meters_valid)
    log_raw_block("consumable meters", scanner->consumable_meters,
                  sizeof(scanner->consumable_meters));
  if (scanner->consumable_meters_valid)
    DBG(6, "%s: consumable-meter-version=0x%02x supported=%s\n", __func__,
        (unsigned)
            scanner->consumable_meters[KODAK_I2000_CONSUMABLE_METERS_VERSION_OFFSET],
        scanner->consumable_meters_version_supported ? "yes" : "no");
  return status;
}

static SANE_Status
read_meters(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  memset(scanner->meters, 0, sizeof(scanner->meters));
  status = read_device_get(scanner, "GetMeters", KODAK_I2000_METERS_REQUEST,
                           KODAK_I2000_METERS_VALUE, KODAK_I2000_METERS_INDEX,
                           KODAK_I2000_METERS_LENGTH, scanner->meters);
  scanner->meters_valid = status == SANE_STATUS_GOOD;
  if (scanner->meters_valid)
    log_raw_block("meters", scanner->meters, sizeof(scanner->meters));
  return status;
}

static SANE_Status
read_diagnostic_results(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  memset(scanner->diagnostic_results, 0, sizeof(scanner->diagnostic_results));
  status =
      read_device_get(scanner, "GetDiagnosticResults", KODAK_I2000_DIAGNOSTIC_REQUEST,
                      KODAK_I2000_DIAGNOSTIC_VALUE, KODAK_I2000_DIAGNOSTIC_INDEX,
                      KODAK_I2000_DIAGNOSTIC_LENGTH, scanner->diagnostic_results);
  scanner->diagnostic_results_valid = status == SANE_STATUS_GOOD;
  if (scanner->diagnostic_results_valid)
    log_raw_block("diagnostic results", scanner->diagnostic_results,
                  sizeof(scanner->diagnostic_results));
  return status;
}

static SANE_Status
read_low_power_timeout(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  scanner->low_power_timeout = 0;
  status = read_device_get(scanner, "GetLowPowerTimeout", KODAK_I2000_LOW_POWER_REQUEST,
                           KODAK_I2000_LOW_POWER_VALUE, KODAK_I2000_LOW_POWER_INDEX,
                           KODAK_I2000_LOW_POWER_LENGTH, &scanner->low_power_timeout);
  scanner->low_power_timeout_valid = status == SANE_STATUS_GOOD;
  if (scanner->low_power_timeout_valid)
    DBG(6, "%s: low-power-timeout=0x%02x\n", __func__,
        (unsigned)scanner->low_power_timeout);
  return status;
}

static SANE_Status
read_end_of_line(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  memset(scanner->end_of_line, 0, sizeof(scanner->end_of_line));
  status = read_device_get(scanner, "GetEndOfLine", KODAK_I2000_END_OF_LINE_REQUEST,
                           KODAK_I2000_END_OF_LINE_VALUE, KODAK_I2000_END_OF_LINE_INDEX,
                           KODAK_I2000_END_OF_LINE_LENGTH, scanner->end_of_line);
  scanner->end_of_line_valid = status == SANE_STATUS_GOOD;
  if (scanner->end_of_line_valid)
    log_raw_block("end-of-line", scanner->end_of_line, sizeof(scanner->end_of_line));
  return status;
}

static SANE_Status
read_fpga_register(kodak_i2000_handle *scanner, uint16_t value, SANE_Int length,
                   SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || data == NULL || length <= 0)
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_FPGA_REQUEST_TYPE,
                                 KODAK_I2000_FPGA_REQUEST, value,
                                 KODAK_I2000_FPGA_INDEX, length, data);
  if (status != SANE_STATUS_GOOD)
    DBG(3, "%s: FPGA read value=0x%04x length=%d failed: %s\n", __func__,
        (unsigned)value, (int)length, sane_strstatus(status));
  return status;
}

SANE_Status
read_fpga_byte(kodak_i2000_handle *scanner, uint16_t value, SANE_Byte *result)
{
  SANE_Status status;

  if (result == NULL)
    return SANE_STATUS_INVAL;
  *result = 0;
  status = read_fpga_register(scanner, value, KODAK_I2000_FPGA_BYTE_LENGTH, result);
  if (status == SANE_STATUS_GOOD)
    DBG(6, "%s: FPGA value=0x%04x -> 0x%02x\n", __func__, (unsigned)value,
        (unsigned)*result);
  return status;
}

SANE_Status
read_fpga_doc_id(kodak_i2000_handle *scanner, uint16_t value, uint32_t *result)
{
  SANE_Byte bytes[KODAK_I2000_FPGA_WORD_LENGTH];
  SANE_Status status;

  if (result == NULL)
    return SANE_STATUS_INVAL;
  *result = 0U;
  memset(bytes, 0, sizeof(bytes));
  status = read_fpga_register(scanner, value, KODAK_I2000_FPGA_WORD_LENGTH, bytes);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* FpgaBase::GetDocId decodes the four-byte register as big-endian. */
  *result = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
            ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
  DBG(6, "%s: FPGA doc-id value=0x%04x -> 0x%08x\n", __func__, (unsigned)value,
      (unsigned)*result);
  return status;
}

SANE_Status
write_device_set(kodak_i2000_handle *scanner, const char *name, uint8_t request,
                 uint16_t value, uint16_t index, SANE_Int length, SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || length < 0 || (length > 0 && data == NULL))
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_SET_REQUEST_TYPE, request,
                                 value, index, length, data);
  if (status != SANE_STATUS_GOOD)
    DBG(2,
        "%s: %s request=0x%02x value=0x%04x index=0x%04x length=%d "
        "failed: %s\n",
        __func__, name != NULL ? name : "Set", request, (unsigned)value,
        (unsigned)index, (int)length, sane_strstatus(status));
  return status;
}

SANE_Status
set_interrupt_events(kodak_i2000_handle *scanner, SANE_Bool enabled)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0)
    return SANE_STATUS_INVAL;
  if (scanner->interrupt_events_enabled == enabled)
    return SANE_STATUS_GOOD;

  status = write_device_set(
      scanner, enabled ? "EnableInterruptEvents" : "DisableInterruptEvents",
      KODAK_I2000_INTERRUPT_EVENTS_REQUEST, KODAK_I2000_INTERRUPT_EVENTS_VALUE,
      enabled ? KODAK_I2000_INTERRUPT_EVENTS_ENABLE_INDEX
              : KODAK_I2000_INTERRUPT_EVENTS_DISABLE_INDEX,
      KODAK_I2000_INTERRUPT_EVENTS_LENGTH, NULL);
  if (status == SANE_STATUS_GOOD) {
    scanner->interrupt_events_enabled = enabled;
    DBG(5, "%s: scanner interrupt events %s\n", __func__,
        enabled ? "enabled" : "disabled");
  }
  return status;
}

SANE_Status
write_fpga_register(kodak_i2000_handle *scanner, uint16_t value, SANE_Int length,
                    SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || length <= 0 || data == NULL)
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_FPGA_WRITE_REQUEST_TYPE,
                                 KODAK_I2000_FPGA_REQUEST, value,
                                 KODAK_I2000_FPGA_INDEX, length, data);
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: FPGA write value=0x%04x length=%d failed: %s\n", __func__,
        (unsigned)value, (int)length, sane_strstatus(status));
  return status;
}

SANE_Status
write_fpga_byte(kodak_i2000_handle *scanner, uint16_t value, SANE_Byte data)
{
  SANE_Byte byte = data;

  return write_fpga_register(scanner, value, KODAK_I2000_FPGA_BYTE_LENGTH, &byte);
}

SANE_Status
read_optional_device_state(kodak_i2000_handle *scanner)
{
  SANE_Status session_status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  (void)read_firmware_versions(scanner);
  session_status = read_session_info(scanner);
  if (session_status == SANE_STATUS_GOOD &&
      kodak_i2000_session_info_is_uninitialized(scanner->session_info,
                                                sizeof(scanner->session_info))) {
    session_status = initialize_session_info(scanner);
    if (session_status != SANE_STATUS_GOOD)
      DBG(1, "%s: transient session initialization failed: %s\n", __func__,
          sane_strstatus(session_status));
  }
  if (session_status == SANE_STATUS_GOOD)
    session_status = ensure_cold_session_ready(scanner);
  if (session_status != SANE_STATUS_GOOD) {
    DBG(1, "%s: scanner session preparation failed: %s\n", __func__,
        sane_strstatus(session_status));
    return session_status;
  }
  (void)read_energy_star(scanner);
  (void)read_meters(scanner);
  (void)read_consumable_meters(scanner);
  (void)read_diagnostic_results(scanner);
  (void)read_low_power_timeout(scanner);
  (void)read_end_of_line(scanner);

  scanner->front_doc_id_valid =
      read_fpga_doc_id(scanner, KODAK_I2000_FRONT_DOC_ID_VALUE,
                       &scanner->front_doc_id) == SANE_STATUS_GOOD;
  scanner->rear_doc_id_valid =
      read_fpga_doc_id(scanner, KODAK_I2000_REAR_DOC_ID_VALUE, &scanner->rear_doc_id) ==
      SANE_STATUS_GOOD;
  scanner->printer_control_valid =
      read_fpga_byte(scanner, KODAK_I2000_PRINTER_CONTROL_VALUE,
                     &scanner->printer_control) == SANE_STATUS_GOOD;
  scanner->printer_status_valid =
      read_fpga_byte(scanner, KODAK_I2000_PRINTER_STATUS_VALUE,
                     &scanner->printer_status) == SANE_STATUS_GOOD;
  scanner->feeder_clutch_valid =
      read_fpga_byte(scanner, KODAK_I2000_FEEDER_CLUTCH_VALUE,
                     &scanner->feeder_clutch) == SANE_STATUS_GOOD;
  scanner->common_general_control_valid =
      read_fpga_byte(scanner, KODAK_I2000_COMMON_GENERAL_CONTROL_VALUE,
                     &scanner->common_general_control) == SANE_STATUS_GOOD;
  scanner->common_ip_control_valid =
      read_fpga_byte(scanner, KODAK_I2000_COMMON_IP_CONTROL_VALUE,
                     &scanner->common_ip_control) == SANE_STATUS_GOOD;
  scanner->front_ip_control_valid =
      read_fpga_byte(scanner, KODAK_I2000_FRONT_IP_CONTROL_VALUE,
                     &scanner->front_ip_control) == SANE_STATUS_GOOD;
  scanner->rear_ip_control_valid =
      read_fpga_byte(scanner, KODAK_I2000_REAR_IP_CONTROL_VALUE,
                     &scanner->rear_ip_control) == SANE_STATUS_GOOD;
  scanner->rear_binary_threshold_valid =
      read_fpga_byte(scanner, KODAK_I2000_REAR_BINARY_THRESHOLD_VALUE,
                     &scanner->rear_binary_threshold) == SANE_STATUS_GOOD;
  scanner->rear_image_footer_valid =
      read_fpga_byte(scanner, KODAK_I2000_REAR_IMAGE_FOOTER_VALUE,
                     &scanner->rear_image_footer) == SANE_STATUS_GOOD;

  if (scanner->front_doc_id_valid && scanner->rear_doc_id_valid)
    DBG(6, "%s: front_doc_id=0x%08x rear_doc_id=0x%08x\n", __func__,
        (unsigned)scanner->front_doc_id, (unsigned)scanner->rear_doc_id);
  if (scanner->printer_control_valid && scanner->printer_status_valid &&
      scanner->feeder_clutch_valid)
    DBG(6,
        "%s: printer_control=0x%02x printer_status=0x%02x "
        "feeder_clutch=0x%02x\n",
        __func__, (unsigned)scanner->printer_control, (unsigned)scanner->printer_status,
        (unsigned)scanner->feeder_clutch);
  if (scanner->common_general_control_valid && scanner->common_ip_control_valid &&
      scanner->front_ip_control_valid && scanner->rear_ip_control_valid &&
      scanner->rear_binary_threshold_valid && scanner->rear_image_footer_valid)
    DBG(6,
        "%s: FPGA general=0x%02x common_ip=0x%02x front_ip=0x%02x "
        "rear_ip=0x%02x rear_threshold=0x%02x rear_footer=0x%02x\n",
        __func__, (unsigned)scanner->common_general_control,
        (unsigned)scanner->common_ip_control, (unsigned)scanner->front_ip_control,
        (unsigned)scanner->rear_ip_control, (unsigned)scanner->rear_binary_threshold,
        (unsigned)scanner->rear_image_footer);
  return SANE_STATUS_GOOD;
}
