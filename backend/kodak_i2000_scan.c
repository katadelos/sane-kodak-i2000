/* Page setup, transport control, and continuous-job cleanup. */
#include "sane/config.h"
#include <unistd.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_options.h"
#include "kodak_i2000_transport.h"
#include "kodak_i2000_camera.h"
#include "kodak_i2000_scan.h"
#include "kodak_i2000_calibration.h"
#include "kodak_i2000_capture_data.h"

SANE_Status
write_setup_operation(kodak_i2000_handle *scanner, uint16_t value)
{
  return write_device_set(
      scanner, "SetupOperation", KODAK_I2000_SETUP_OPERATION_REQUEST, value,
      KODAK_I2000_SETUP_OPERATION_INDEX, KODAK_I2000_SETUP_OPERATION_LENGTH, NULL);
}

static SANE_Status
write_setup_marker_payload(kodak_i2000_handle *scanner, const SANE_Byte marker[3])
{
  return write_device_set(scanner, "SetupMarker", KODAK_I2000_SETUP_MARKER_REQUEST,
                          KODAK_I2000_SETUP_MARKER_VALUE,
                          KODAK_I2000_SETUP_MARKER_INDEX,
                          KODAK_I2000_SETUP_MARKER_LENGTH, (SANE_Byte *)marker);
}

static SANE_Status
write_setup_marker(kodak_i2000_handle *scanner)
{
  static const SANE_Byte marker[KODAK_I2000_SETUP_MARKER_LENGTH] = {0x01, 0x00, 0x02};

  return write_setup_marker_payload(scanner, marker);
}

SANE_Status
write_completion_marker(kodak_i2000_handle *scanner)
{
  static const SANE_Byte marker[KODAK_I2000_SETUP_MARKER_LENGTH] = {0x02, 0x00, 0x00};

  return write_setup_marker_payload(scanner, marker);
}

static SANE_Status
write_scan_configuration(kodak_i2000_handle *scanner, const char *name,
                         const SANE_Byte *configuration)
{
  if (scanner == NULL)
    return SANE_STATUS_INVAL;

  if (configuration == NULL)
    return SANE_STATUS_INVAL;
  log_raw_block(name != NULL ? name : "scanner configuration", configuration,
                KODAK_I2000_CONFIG_LENGTH);
  return write_device_set(scanner, name != NULL ? name : "SetScannerConfiguration",
                          KODAK_I2000_CONFIG_REQUEST, KODAK_I2000_CONFIG_VALUE,
                          KODAK_I2000_CONFIG_INDEX, KODAK_I2000_CONFIG_LENGTH,
                          (SANE_Byte *)configuration);
}

static SANE_Status
write_normal_scan_configuration(kodak_i2000_handle *scanner)
{
  SANE_Byte configuration[KODAK_I2000_CONFIG_LENGTH];

  if (scanner == NULL ||
      !kodak_i2000_build_normal_scan_configuration(
          (unsigned)scanner->source, (unsigned)scanner->mode,
          (unsigned)scanner->resolution, configuration, sizeof(configuration))) {
    DBG(1, "%s: unsupported normal scan configuration\n", __func__);
    return SANE_STATUS_INVAL;
  }
  return write_scan_configuration(scanner, "SetNormalScanConfiguration", configuration);
}

static SANE_Status
write_baffle_scan_configuration(kodak_i2000_handle *scanner)
{
  SANE_Byte configuration[KODAK_I2000_CONFIG_LENGTH];

  if (scanner == NULL ||
      !kodak_i2000_build_baffle_scan_configuration(
          (unsigned)scanner->source, (unsigned)scanner->mode,
          (unsigned)scanner->resolution, configuration, sizeof(configuration))) {
    DBG(1, "%s: unsupported baffle scan configuration\n", __func__);
    return SANE_STATUS_INVAL;
  }
  return write_scan_configuration(scanner, "SetBaffleScanConfiguration", configuration);
}

SANE_Status
write_cold_background_scan_configuration(kodak_i2000_handle *scanner)
{
  SANE_Byte configuration[KODAK_I2000_CONFIG_LENGTH];

  if (scanner == NULL || !kodak_i2000_build_cold_background_scan_configuration(
                             configuration, sizeof(configuration)))
    return SANE_STATUS_INVAL;
  return write_scan_configuration(scanner, "SetColdBackgroundScanConfiguration",
                                  configuration);
}

static SANE_Status
write_scanner_operation(kodak_i2000_handle *scanner, uint16_t value)
{
  return write_device_set(
      scanner, "ScannerOperation", KODAK_I2000_SCANNER_OPERATION_REQUEST, value,
      KODAK_I2000_SCANNER_OPERATION_INDEX, KODAK_I2000_SCANNER_OPERATION_LENGTH, NULL);
}

SANE_Status
write_lamp_off(kodak_i2000_handle *scanner)
{
  return write_device_set(scanner, "TurnLampOff", KODAK_I2000_LAMP_OFF_REQUEST,
                          KODAK_I2000_LAMP_OFF_VALUE, KODAK_I2000_LAMP_OFF_INDEX,
                          KODAK_I2000_LAMP_OFF_LENGTH, NULL);
}

static SANE_Status
wait_for_page_idle(kodak_i2000_handle *scanner)
{
  unsigned attempt;

  for (attempt = 0U; attempt < 20U; ++attempt) {
    SANE_Status status = read_status(scanner);

    if (acquisition_cancelled(scanner))
      return SANE_STATUS_CANCELLED;
    if (status != SANE_STATUS_GOOD)
      return status;
    if (scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] != 0U)
      return SANE_STATUS_IO_ERROR;
    if (scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET] ==
            KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL &&
        scanner->status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET] == 0U)
      return SANE_STATUS_GOOD;
    usleep(100000U);
  }
  return SANE_STATUS_DEVICE_BUSY;
}

SANE_Status
finish_page_capture(kodak_i2000_handle *scanner, SANE_Bool normal_completion)
{
  SANE_Status operation_status;
  SANE_Status lamp_status;
  SANE_Status restore_status = SANE_STATUS_GOOD;

  /* The captured 400-dpi RGB completion becomes idle without a final scanner
   * stop.  Sending that gray-path stop at the RGB page boundary instead left
   * the i2400 in power/transport state 04/03.  On a normal high-resolution
   * RGB completion, wait for the already-finishing transport.  Error paths
   * and the captured gray path retain the conservative index-zero stop. */
  if (normal_completion && scanner->mode == KODAK_I2000_MODE_COLOR &&
      (scanner->resolution == 400 || scanner->resolution == 600))
    operation_status = wait_for_page_idle(scanner);
  else
    operation_status = write_scanner_operation(scanner, KODAK_I2000_SCANNER_STOP_VALUE);

  /* Always attempt lamp-off, including when stop/wait fails.  On normal
   * completion also replay the captured post-page camera reset so the next
   * sane_start does not inherit the acquisition state, then wait through the
   * short power-four/transport-two ejection interval observed on gray pages. */
  lamp_status = write_lamp_off(scanner);
  if (normal_completion)
    restore_status = restore_capture_hardware(scanner);
  if (normal_completion && restore_status == SANE_STATUS_GOOD)
    restore_status = wait_for_page_idle(scanner);
  if (operation_status != SANE_STATUS_GOOD)
    return operation_status;
  if (lamp_status != SANE_STATUS_GOOD)
    return lamp_status;
  return restore_status;
}

SANE_Status
finish_continuous_job(kodak_i2000_handle *scanner, SANE_Bool normal_completion)
{
  SANE_Status status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  if (!scanner->continuous_job_active)
    return SANE_STATUS_GOOD;

  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  status = finish_page_capture(scanner, normal_completion);
  scanner->continuous_job_active = SANE_FALSE;
  return status;
}

SANE_Status
write_start_capture(kodak_i2000_handle *scanner, uint16_t value)
{
  return write_device_set(scanner, "StartCapture", KODAK_I2000_START_REQUEST, value,
                          KODAK_I2000_START_INDEX, KODAK_I2000_START_LENGTH, NULL);
}

SANE_Status
finish_continuous_capture_error(kodak_i2000_handle *scanner, SANE_Status capture_status)
{
  SANE_Status finish_status;
  SANE_Status status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;

  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  if (capture_status == SANE_STATUS_NO_DOCS && !acquisition_cancelled(scanner)) {
    status = read_status(scanner);
    if (status == SANE_STATUS_GOOD && kodak_i2000_status_is_idle_and_empty(
                                          scanner->status, sizeof(scanner->status))) {
      DBG(2, "%s: continuous feeder is idle and empty; ending job\n", __func__);
      finish_status = finish_continuous_job(scanner, SANE_TRUE);
      return finish_status == SANE_STATUS_GOOD ? SANE_STATUS_NO_DOCS : finish_status;
    }
    DBG(1, "%s: empty endpoint was not an idle, empty feeder state\n", __func__);
    capture_status = status == SANE_STATUS_GOOD ? SANE_STATUS_IO_ERROR : status;
  }

  finish_status = finish_continuous_job(scanner, SANE_FALSE);
  if (finish_status != SANE_STATUS_GOOD)
    DBG(1, "%s: continuous-job abort cleanup failed: %s\n", __func__,
        sane_strstatus(finish_status));
  if (acquisition_cancelled(scanner) || capture_status == SANE_STATUS_CANCELLED)
    return SANE_STATUS_CANCELLED;
  return capture_status;
}

SANE_Bool
capture_parameters_supported(const kodak_i2000_handle *scanner)
{
  if (scanner == NULL || scanner->device == NULL)
    return SANE_FALSE;
  return scanner->device->product_id == KODAK_I2000_SUPPORTED_CAPTURE_PRODUCT &&
         scanner->mode >= 0 && (unsigned)scanner->mode <= KODAK_I2000_MODE_COLOR &&
         scanner->source >= 0 &&
         (unsigned)scanner->source <= KODAK_I2000_SOURCE_DUPLEX &&
         resolution_supported(scanner->resolution) && scanner->tl_x == 0 &&
         scanner->tl_y == 0 &&
         scanner->br_x == SANE_FIX(KODAK_I2000_DEFAULT_WIDTH_MM) &&
         scanner->br_y == SANE_FIX(KODAK_I2000_DEFAULT_HEIGHT_MM);
}

SANE_Status
setup_scan(kodak_i2000_handle *scanner)
{
  SANE_Bool paper_present;
  SANE_Bool dt_cameras;
  SANE_Byte flash_status;
  unsigned sensor_resolution;
  uint16_t fixed_page_length;
  SANE_Status status;

  if (scanner == NULL || scanner->device == NULL)
    return SANE_STATUS_INVAL;
  sensor_resolution = kodak_i2000_sensor_resolution((unsigned)scanner->resolution);
  fixed_page_length = kodak_i2000_scale_sensor_value(0x122aU, sensor_resolution);
  sanei_usb_set_endpoint(scanner->usb, USB_DIR_OUT | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_BULK_OUT_ENDPOINT);
  sanei_usb_set_endpoint(scanner->usb, USB_DIR_IN | USB_ENDPOINT_TYPE_BULK,
                         scanner->source == KODAK_I2000_SOURCE_REAR
                             ? KODAK_I2000_REAR_IMAGE_ENDPOINT
                             : KODAK_I2000_FRONT_IMAGE_ENDPOINT);
  sanei_usb_set_timeout(KODAK_I2000_SCAN_TIMEOUT_MS);

  if (acquisition_cancelled(scanner))
    return SANE_STATUS_CANCELLED;

  paper_present =
      scanner->status_valid && scanner->status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET] ==
                                   KODAK_I2000_STATUS_TRAY_PAPER_PRESENT;
  if (!paper_present) {
    DBG(3, "%s: feeder reports no paper (raw tray state=0x%02x)\n", __func__,
        scanner->status_valid
            ? (unsigned)scanner->status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET]
            : 0xffU);
    return SANE_STATUS_NO_DOCS;
  }

  status = write_setup_operation(scanner, KODAK_I2000_SETUP_OPERATION_VALUE);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_setup_marker(scanner);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_setup_operation(scanner, KODAK_I2000_SETUP_OPERATION_VALUE);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_document_ids(scanner);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DE37, &flash_status);
  if (status != SANE_STATUS_GOOD)
    return status;
  dt_cameras = (flash_status & KODAK_I2000_FLASH_PIRANHA2_MASK) == 0U;
  const kodak_i2000_color_profile *profile =
      dt_cameras ? &kodak_i2000_dt_profile : &kodak_i2000_piranha_profile;
  for (unsigned side = 0; side < 2; ++side) {
    if ((side == 0 && scanner->source == KODAK_I2000_SOURCE_REAR) ||
        (side == 1 && scanner->source == KODAK_I2000_SOURCE_FRONT))
      continue;
    status = acquisition_status(scanner, write_camera_profile(scanner, side, profile));
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = setup_capture_fpga(scanner, dt_cameras);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = write_baffle_scan_configuration(scanner);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_scanner_operation(scanner, KODAK_I2000_SCANNER_START_VALUE);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_start_capture(scanner,
                               kodak_i2000_capture_value((unsigned)scanner->source));
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = drain_selected_baffle_captures(scanner);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_scanner_operation(scanner, KODAK_I2000_SCANNER_STOP_VALUE);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (scanner->source != KODAK_I2000_SOURCE_REAR) {
    status = write_fpga_byte(
        scanner, KODAK_I2000_FPGA_REGISTER_DA0E,
        (SANE_Byte)kodak_i2000_scale_sensor_value(0x0012U, sensor_resolution));
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      return status;
    status =
        write_fpga_word(scanner, KODAK_I2000_FPGA_REGISTER_DA06, fixed_page_length);
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  if (scanner->source != KODAK_I2000_SOURCE_FRONT) {
    status = write_fpga_byte(
        scanner, KODAK_I2000_FPGA_REGISTER_DB0E,
        (SANE_Byte)kodak_i2000_scale_sensor_value(0x0012U, sensor_resolution));
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      return status;
    status =
        write_fpga_word(scanner, KODAK_I2000_FPGA_REGISTER_DB06, fixed_page_length);
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = write_normal_scan_configuration(scanner);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_scanner_operation(scanner, KODAK_I2000_SCANNER_START_VALUE);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD)
    return status;

  scanner->image_remaining = 0U;
  DBG(2, "%s: source=%d page capture started; awaiting raw image frame(s)\n", __func__,
      (int)scanner->source);
  return SANE_STATUS_GOOD;
}
