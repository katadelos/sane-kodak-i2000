/* Cold-session image calibration and background capture. */
#include "sane/config.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_transport.h"
#include "kodak_i2000_camera.h"
#include "kodak_i2000_scan.h"
#include "kodak_i2000_calibration.h"

#define KODAK_I2000_COMMON_GENERAL_RESTORE_VALUE 0x27u
#define KODAK_I2000_COMMON_GENERAL_SETUP_VALUE 0x23u

static SANE_Status
drain_baffle_capture_endpoint(kodak_i2000_handle *scanner, SANE_Byte endpoint)
{
  SANE_Byte buffer[0x4000];
  size_t total = 0U;
  unsigned idle_retries = 0U;
  SANE_Bool complete = SANE_FALSE;

  DBG(5, "%s: device=%d endpoint=0x%02x\n", __func__,
      scanner != NULL ? (int)scanner->usb : -1, (unsigned)endpoint);

  while (total < KODAK_I2000_BAFFLE_CAPTURE_MAX_LENGTH) {
    size_t requested = sizeof(buffer);
    size_t received;
    SANE_Status status;

    if (acquisition_cancelled(scanner))
      return SANE_STATUS_CANCELLED;
    received = requested;
    status = sanei_usb_read_bulk_endpoint(scanner->usb, endpoint, buffer, &received);
    if (acquisition_cancelled(scanner))
      return SANE_STATUS_CANCELLED;
    if (status != SANE_STATUS_GOOD || received == 0U) {
      if (total >= KODAK_I2000_BAFFLE_CAPTURE_MIN_LENGTH) {
        DBG(5, "%s: endpoint 0x%02x completed after %lu bytes: %s\n", __func__,
            (unsigned)endpoint, (unsigned long)total, sane_strstatus(status));
        complete = SANE_TRUE;
        break;
      }
      if (status == SANE_STATUS_IO_ERROR &&
          ++idle_retries < KODAK_I2000_SCAN_IDLE_RETRIES)
        continue;
      DBG(2, "%s: endpoint 0x%02x stopped after %lu bytes: %s\n", __func__,
          (unsigned)endpoint, (unsigned long)total, sane_strstatus(status));
      return status == SANE_STATUS_GOOD ? SANE_STATUS_IO_ERROR : status;
    }
    idle_retries = 0U;
    if (received > requested ||
        received > KODAK_I2000_BAFFLE_CAPTURE_MAX_LENGTH - total)
      return SANE_STATUS_IO_ERROR;
    total += received;
    if (received < requested) {
      complete = total >= KODAK_I2000_BAFFLE_CAPTURE_MIN_LENGTH;
      break;
    }
  }
  if (!complete)
    return SANE_STATUS_IO_ERROR;
  DBG(5, "%s: discarded %lu-byte baffle capture from endpoint 0x%02x\n", __func__,
      (unsigned long)total, (unsigned)endpoint);
  return SANE_STATUS_GOOD;
}

typedef struct kodak_i2000_baffle_task {
  kodak_i2000_handle *scanner;
  SANE_Byte endpoint;
  SANE_Status status;
} kodak_i2000_baffle_task;

static void *
drain_baffle_capture_worker(void *argument)
{
  kodak_i2000_baffle_task *task = (kodak_i2000_baffle_task *)argument;

  task->status = drain_baffle_capture_endpoint(task->scanner, task->endpoint);
  return NULL;
}

static SANE_Status
drain_duplex_baffle_captures(kodak_i2000_handle *scanner)
{
  kodak_i2000_baffle_task front_task;
  pthread_t front_thread;
  SANE_Status rear_status;
  int thread_error;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;

  /* Both FIFOs are live at once.  Keep one libusb transfer pending on each
   * endpoint so a full side cannot back-pressure the other side. */
  front_task.scanner = scanner;
  front_task.endpoint = KODAK_I2000_FRONT_IMAGE_ENDPOINT;
  front_task.status = SANE_STATUS_IO_ERROR;
  thread_error =
      pthread_create(&front_thread, NULL, drain_baffle_capture_worker, &front_task);
  if (thread_error != 0) {
    DBG(1, "%s: could not create front baffle reader: %s\n", __func__,
        strerror(thread_error));
    return SANE_STATUS_IO_ERROR;
  }
  rear_status = drain_baffle_capture_endpoint(scanner, KODAK_I2000_REAR_IMAGE_ENDPOINT);
  thread_error = pthread_join(front_thread, NULL);
  if (thread_error != 0) {
    DBG(1, "%s: could not join front baffle reader: %s\n", __func__,
        strerror(thread_error));
    return SANE_STATUS_IO_ERROR;
  }
  return rear_status != SANE_STATUS_GOOD ? rear_status : front_task.status;
}

SANE_Status
drain_selected_baffle_captures(kodak_i2000_handle *scanner)
{
  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  if (scanner->source == KODAK_I2000_SOURCE_FRONT)
    return drain_baffle_capture_endpoint(scanner, KODAK_I2000_FRONT_IMAGE_ENDPOINT);
  if (scanner->source == KODAK_I2000_SOURCE_REAR)
    return drain_baffle_capture_endpoint(scanner, KODAK_I2000_REAR_IMAGE_ENDPOINT);
  return drain_duplex_baffle_captures(scanner);
}

static SANE_Status
write_auto_white(kodak_i2000_handle *scanner, SANE_Bool enabled)
{
  SANE_Byte setting[KODAK_I2000_AUTO_WHITE_LENGTH] = {enabled ? 0x01U : 0x00U, 0x00U,
                                                      0x00U, 0x00U};

  return write_device_set(scanner, enabled ? "EnableAutoWhite" : "DisableAutoWhite",
                          KODAK_I2000_AUTO_WHITE_REQUEST, KODAK_I2000_AUTO_WHITE_VALUE,
                          KODAK_I2000_AUTO_WHITE_INDEX, KODAK_I2000_AUTO_WHITE_LENGTH,
                          setting);
}

static SANE_Status
move_flash_calibration_to_sdram(
    kodak_i2000_handle *scanner,
    const SANE_Byte flash_record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH])
{
  SANE_Byte original_general = 0U;
  SANE_Byte flash_status = KODAK_I2000_FLASH_BUSY_MASK;
  SANE_Byte sdram_record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH];
  SANE_Bool target_selected = SANE_FALSE;
  SANE_Bool copy_started = SANE_FALSE;
  SANE_Status cleanup_status;
  SANE_Status status;
  unsigned attempt;

  if (scanner == NULL || flash_record == NULL)
    return SANE_STATUS_INVAL;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DC4D, 0x01U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D800,
                           KODAK_I2000_FLASH_TO_SDRAM_TARGET);
  if (status != SANE_STATUS_GOOD)
    return status;
  target_selected = SANE_TRUE;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D908,
                           KODAK_I2000_NORMALIZATION_DATA_START);
  if (status != SANE_STATUS_GOOD)
    goto restore;
  status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, &original_general);
  if (status != SANE_STATUS_GOOD)
    goto restore;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           (SANE_Byte)(original_general | 0x10U));
  if (status != SANE_STATUS_GOOD)
    goto restore;
  copy_started = SANE_TRUE;

  usleep(30000U);
  for (attempt = 0U; attempt < 100U; ++attempt) {
    status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DE37, &flash_status);
    if (status != SANE_STATUS_GOOD)
      goto restore;
    usleep(30000U);
    if ((flash_status & KODAK_I2000_FLASH_BUSY_MASK) == 0U)
      break;
  }
  if ((flash_status & KODAK_I2000_FLASH_BUSY_MASK) != 0U)
    status = SANE_STATUS_DEVICE_BUSY;

restore:
  if (copy_started) {
    cleanup_status =
        write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, original_general);
    if (status == SANE_STATUS_GOOD && cleanup_status != SANE_STATUS_GOOD)
      status = cleanup_status;
  }
  if (target_selected) {
    cleanup_status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D800, 0x00U);
    if (status == SANE_STATUS_GOOD && cleanup_status != SANE_STATUS_GOOD)
      status = cleanup_status;
  }
  if (status != SANE_STATUS_GOOD)
    return status;

  memset(sdram_record, 0, sizeof(sdram_record));
  status = read_device_get(
      scanner, "VerifySdramCalData", KODAK_I2000_IMAGE_CAL_SDRAM_REQUEST,
      KODAK_I2000_IMAGE_CAL_SDRAM_VALUE, KODAK_I2000_IMAGE_CAL_SDRAM_INDEX,
      KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH, sdram_record);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (!kodak_i2000_calibration_sdram_matches(flash_record,
                                             KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH,
                                             sdram_record, sizeof(sdram_record))) {
    DBG(1, "%s: volatile calibration did not match flash source\n", __func__);
    return SANE_STATUS_IO_ERROR;
  }
  return SANE_STATUS_GOOD;
}

static SANE_Status
write_white_patch_settings(kodak_i2000_handle *scanner,
                           const SANE_Byte record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH])
{
  static const uint16_t front_registers[3] = {KODAK_I2000_FPGA_REGISTER_DA11,
                                              KODAK_I2000_FPGA_REGISTER_DA13,
                                              KODAK_I2000_FPGA_REGISTER_DA15};
  static const uint16_t rear_registers[3] = {KODAK_I2000_FPGA_REGISTER_DB11,
                                             KODAK_I2000_FPGA_REGISTER_DB13,
                                             KODAK_I2000_FPGA_REGISTER_DB15};
  SANE_Status status;
  size_t index;

  if (scanner == NULL || record == NULL)
    return SANE_STATUS_INVAL;
  status = write_fpga_word(
      scanner, KODAK_I2000_FPGA_REGISTER_DA0F,
      load_uint16_le(record + KODAK_I2000_IMAGE_CAL_FRONT_LIMIT_OFFSET));
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_fpga_word(scanner, KODAK_I2000_FPGA_REGISTER_DB0F,
                      load_uint16_le(record + KODAK_I2000_IMAGE_CAL_REAR_LIMIT_OFFSET));
  if (status != SANE_STATUS_GOOD)
    return status;
  for (index = 0U; index < 3U; ++index) {
    status = write_fpga_word(scanner, front_registers[index],
                             load_uint16_le(record +
                                            KODAK_I2000_IMAGE_CAL_FRONT_TARGETS_OFFSET +
                                            index * 2U));
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  for (index = 0U; index < 3U; ++index) {
    status = write_fpga_word(scanner, rear_registers[index],
                             load_uint16_le(record +
                                            KODAK_I2000_IMAGE_CAL_REAR_TARGETS_OFFSET +
                                            index * 2U));
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  return SANE_STATUS_GOOD;
}

static SANE_Status
initialize_image_calibration(kodak_i2000_handle *scanner)
{
  SANE_Byte flash_record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH];
  SANE_Byte ready_session[KODAK_I2000_SESSION_DATA_LENGTH];
  SANE_Status restore_status;
  SANE_Status status;

  if (scanner == NULL || !scanner->session_info_valid ||
      !kodak_i2000_session_info_needs_calibration(scanner->session_info,
                                                  sizeof(scanner->session_info)))
    return SANE_STATUS_INVAL;
  DBG(2, "%s: restoring persistent image calibration into volatile SRAM\n", __func__);
  status = write_document_ids(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_image_calibration_record(scanner, flash_record);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_auto_white(scanner, SANE_FALSE);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D908,
                           KODAK_I2000_NORMALIZATION_DATA_START);
  if (status == SANE_STATUS_GOOD)
    status = move_flash_calibration_to_sdram(scanner, flash_record);
  if (status == SANE_STATUS_GOOD)
    status = write_white_patch_settings(scanner, flash_record);
  restore_status = write_auto_white(scanner, SANE_TRUE);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (restore_status != SANE_STATUS_GOOD)
    return restore_status;

  if (!kodak_i2000_build_calibration_ready_session_info(
          scanner->session_info, sizeof(scanner->session_info), ready_session,
          sizeof(ready_session)))
    return SANE_STATUS_IO_ERROR;
  log_raw_block("calibration-ready session", ready_session, sizeof(ready_session));
  return write_session_info_checked(scanner, "SetCalibrationReadySession",
                                    ready_session);
}

static SANE_Status
setup_background_fpga_path(kodak_i2000_handle *scanner, uint16_t register_base)
{
  SANE_Status status;

  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x06U), UINT16_C(0x0014));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, (uint16_t)(register_base + 0x2bU), 0x00U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x04U), UINT16_C(0x0536));
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x08U), UINT16_C(0x0000));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, (uint16_t)(register_base + 0x01U), 0x07U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, (uint16_t)(register_base + 0x25U), 0x01U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x0cU), UINT16_C(0x0014));
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x0aU), UINT16_C(0x0536));
  if (status != SANE_STATUS_GOOD)
    return status;
  return write_fpga_word(scanner, (uint16_t)(register_base + 0x02U), UINT16_C(0x0000));
}

static SANE_Status
setup_cold_background_capture_fpga(kodak_i2000_handle *scanner)
{
  SANE_Byte observed;
  SANE_Status status;

  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D900, 0x01U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, &observed);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, 0x20U);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(20000U);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           KODAK_I2000_COMMON_GENERAL_SETUP_VALUE);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(20000U);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D903, 0x00U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D904, 0x00U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D905, 0x07U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D906, 0x04U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D907, 0x28U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D920, 0x22U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_fifo(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_background_cameras(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_background_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DA00);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_background_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DB00);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, 0x2bU);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(100000U);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0B, 0x46U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0A, &observed);
  if (status != SANE_STATUS_GOOD)
    return status;
  return write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0A, 0x40U);
}

static SANE_Status
restore_cold_background_hardware(kodak_i2000_handle *scanner)
{
  SANE_Byte observed;
  SANE_Status status;
  uint16_t camera;

  status = read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, &observed);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           KODAK_I2000_COMMON_GENERAL_SETUP_VALUE);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(20000U);
  for (camera = 0U; camera < 2U; ++camera) {
    status = restore_camera_after_capture(scanner, camera);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  return write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                         KODAK_I2000_COMMON_GENERAL_RESTORE_VALUE);
}

static SANE_Status
initialize_background_state(kodak_i2000_handle *scanner)
{
  SANE_Byte ready_session[KODAK_I2000_SESSION_DATA_LENGTH];
  SANE_Bool fpga_configured = SANE_FALSE;
  SANE_Status cleanup_status;
  SANE_Status lamp_status;
  SANE_Status status;

  if (scanner == NULL || !scanner->session_info_valid ||
      !kodak_i2000_session_info_needs_background_check(scanner->session_info,
                                                       sizeof(scanner->session_info)))
    return SANE_STATUS_INVAL;
  DBG(2, "%s: running one-time duplex background capture\n", __func__);
  sanei_usb_set_timeout(KODAK_I2000_SCAN_TIMEOUT_MS);
  status = read_status(scanner);
  if (status == SANE_STATUS_GOOD)
    status = write_setup_operation(scanner, KODAK_I2000_SETUP_OPERATION_VALUE);
  if (status == SANE_STATUS_GOOD)
    status = write_setup_operation(scanner, KODAK_I2000_SETUP_OPERATION_VALUE);
  if (status == SANE_STATUS_GOOD)
    status = read_status(scanner);
  if (status == SANE_STATUS_GOOD)
    fpga_configured = SANE_TRUE;
  if (status == SANE_STATUS_GOOD)
    status = setup_cold_background_capture_fpga(scanner);
  if (status == SANE_STATUS_GOOD) {
    status = write_cold_background_scan_configuration(scanner);
  }
  if (status == SANE_STATUS_GOOD)
    status = write_start_capture(scanner, KODAK_I2000_DUPLEX_CAPTURE_VALUE);
  if (status == SANE_STATUS_GOOD)
    status = drain_duplex_baffle_captures(scanner);
  if (status == SANE_STATUS_GOOD) {
    /* The supported i2400 has the capture-proven black front and rear
     * background baffles.  Do not clear the unknown sentinels unless both
     * finite camera streams completed successfully. */
    if (!kodak_i2000_build_background_ready_session_info(
            scanner->session_info, sizeof(scanner->session_info), ready_session,
            sizeof(ready_session)))
      status = SANE_STATUS_IO_ERROR;
    else
      status = write_session_info_checked(scanner, "SetBackgroundReadySession",
                                          ready_session);
  }

  lamp_status = write_lamp_off(scanner);
  if (lamp_status == SANE_STATUS_GOOD) {
    SANE_Status post_capture_status = read_status(scanner);

    if (status == SANE_STATUS_GOOD && post_capture_status != SANE_STATUS_GOOD)
      status = post_capture_status;
  }
  if (fpga_configured)
    usleep(20000U);
  cleanup_status =
      fpga_configured ? restore_cold_background_hardware(scanner) : SANE_STATUS_GOOD;
  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  if (status == SANE_STATUS_GOOD && lamp_status != SANE_STATUS_GOOD)
    status = lamp_status;
  if (status == SANE_STATUS_GOOD && cleanup_status != SANE_STATUS_GOOD)
    status = cleanup_status;
  if (status == SANE_STATUS_GOOD)
    status = read_status(scanner);
  return status;
}

SANE_Status
ensure_cold_session_ready(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  if (scanner == NULL || scanner->device == NULL || !scanner->session_info_valid)
    return SANE_STATUS_INVAL;
  if ((kodak_i2000_session_info_needs_calibration(scanner->session_info,
                                                  sizeof(scanner->session_info)) ||
       kodak_i2000_session_info_needs_background_check(
           scanner->session_info, sizeof(scanner->session_info))) &&
      scanner->device->product_id != KODAK_I2000_SUPPORTED_CAPTURE_PRODUCT) {
    DBG(1, "%s: post-cold calibration is not recovered for product 0x%04x\n", __func__,
        (unsigned)scanner->device->product_id);
    return SANE_STATUS_UNSUPPORTED;
  }
  if (kodak_i2000_session_info_needs_calibration(scanner->session_info,
                                                 sizeof(scanner->session_info))) {
    status = initialize_image_calibration(scanner);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  if (kodak_i2000_session_info_needs_background_check(scanner->session_info,
                                                      sizeof(scanner->session_info))) {
    status = initialize_background_state(scanner);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  if (scanner->session_info[KODAK_I2000_SESSION_VERSION_OFFSET] !=
          KODAK_I2000_SESSION_VERSION ||
      scanner->session_info[KODAK_I2000_SESSION_MARKER_OFFSET] !=
          KODAK_I2000_SESSION_MARKER ||
      scanner->session_info[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
          KODAK_I2000_SESSION_BACKGROUND_UNKNOWN ||
      scanner->session_info[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
          KODAK_I2000_SESSION_BACKGROUND_UNKNOWN) {
    DBG(1, "%s: scanner session is not ready for image capture\n", __func__);
    return SANE_STATUS_IO_ERROR;
  }
  return SANE_STATUS_GOOD;
}
