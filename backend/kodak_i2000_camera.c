/* Camera register programming and FPGA setup for document capture. */
#include "sane/config.h"
#include <string.h>
#include <unistd.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_transport.h"
#include "kodak_i2000_camera.h"
#include "kodak_i2000_scan.h"

/* Kodak selects the shared FPGA line delays by camera variant and sensor
 * timing group before the two image streams are acquired. */
static SANE_Byte
red_rephase_for_request(SANE_Int resolution, SANE_Bool dt_cameras)
{
  if (resolution == 100 || resolution == 150)
    return dt_cameras ? 0x05 : 0x09;
  if (resolution == 400 || resolution == 600)
    return dt_cameras ? 0x0d : 0x19;
  return dt_cameras ? 0x07 : 0x0d;
}

static SANE_Byte
green_rephase_for_request(SANE_Int resolution, SANE_Bool dt_cameras)
{
  if (resolution == 100 || resolution == 150)
    return dt_cameras ? 0x03 : 0x05;
  if (resolution == 400 || resolution == 600)
    return dt_cameras ? 0x07 : 0x0d;
  return dt_cameras ? 0x04 : 0x07;
}

/* SetupScanner::SetupFifo() uses the vendor's abP2DuplexFifoSetup payload
 * for ordinary front, rear, and duplex scans.  This exact 78-byte record was
 * also verified against the paper-backed front capture. */
static SANE_Byte scan_fifo_setup[] = {
    0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9f, 0xc4, 0x01, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x97, 0xc4, 0x03, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01};

SANE_Status
write_fpga_fifo(kodak_i2000_handle *scanner)
{
  if (sizeof(scan_fifo_setup) != 78U) {
    DBG(1, "%s: recovered FIFO payload has unexpected length %u\n", __func__,
        (unsigned)sizeof(scan_fifo_setup));
    return SANE_STATUS_INVAL;
  }
  return write_fpga_register(scanner, KODAK_I2000_FPGA_SETUP_FIFO_VALUE,
                             (SANE_Int)sizeof(scan_fifo_setup), scan_fifo_setup);
}

SANE_Status
write_document_ids(kodak_i2000_handle *scanner)
{
  SANE_Byte bytes[KODAK_I2000_FPGA_WORD_LENGTH];
  uint32_t document_id = 1U;
  uint32_t current_document_id;
  SANE_Status status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  /* A one-sheet transaction may advance the firmware's document counters.
   * Re-read them before each subsequent sane_start() instead of replaying the
   * values cached when the handle was opened. */
  if (read_fpga_doc_id(scanner, KODAK_I2000_FRONT_DOC_ID_VALUE, &current_document_id) ==
      SANE_STATUS_GOOD) {
    scanner->front_doc_id = current_document_id;
    scanner->front_doc_id_valid = SANE_TRUE;
  }
  if (read_fpga_doc_id(scanner, KODAK_I2000_REAR_DOC_ID_VALUE, &current_document_id) ==
      SANE_STATUS_GOOD) {
    scanner->rear_doc_id = current_document_id;
    scanner->rear_doc_id_valid = SANE_TRUE;
  }
  if (scanner->front_doc_id_valid)
    document_id = scanner->front_doc_id;
  if (scanner->rear_doc_id_valid &&
      (!scanner->front_doc_id_valid || scanner->rear_doc_id < document_id))
    document_id = scanner->rear_doc_id;

  /* SetDocumentIds() uses the minimum current front/rear value and does not
   * increment it.  The live i2400 reports one on its first open. */
  bytes[0] = (SANE_Byte)(document_id >> 24);
  bytes[1] = (SANE_Byte)(document_id >> 16);
  bytes[2] = (SANE_Byte)(document_id >> 8);
  bytes[3] = (SANE_Byte)document_id;
  status = write_fpga_register(scanner, KODAK_I2000_FRONT_DOC_ID_VALUE,
                               KODAK_I2000_FPGA_WORD_LENGTH, bytes);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_register(scanner, KODAK_I2000_REAR_DOC_ID_VALUE,
                               KODAK_I2000_FPGA_WORD_LENGTH, bytes);
  if (status != SANE_STATUS_GOOD)
    return status;
  scanner->front_doc_id = document_id;
  scanner->rear_doc_id = document_id;
  scanner->front_doc_id_valid = SANE_TRUE;
  scanner->rear_doc_id_valid = SANE_TRUE;
  DBG(5, "%s: set front/rear document id to 0x%08x\n", __func__, (unsigned)document_id);
  return SANE_STATUS_GOOD;
}

static SANE_Status
write_camera_byte(kodak_i2000_handle *scanner, uint16_t camera, uint16_t value,
                  SANE_Byte data)
{
  SANE_Byte byte = data;
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0)
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_CAMERA_REQUEST_TYPE_OUT,
                                 KODAK_I2000_CAMERA_REQUEST, value, camera,
                                 KODAK_I2000_CAMERA_BYTE_LENGTH, &byte);
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: camera=%u register=0x%04x value=0x%02x failed: %s\n", __func__,
        (unsigned)camera, (unsigned)value, (unsigned)data, sane_strstatus(status));
  return status;
}

static SANE_Status
write_camera_data(kodak_i2000_handle *scanner, uint16_t camera, uint16_t value,
                  SANE_Int length, const SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || length <= 0 || data == NULL)
    return SANE_STATUS_INVAL;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_CAMERA_REQUEST_TYPE_OUT,
                                 KODAK_I2000_CAMERA_REQUEST, value, camera, length,
                                 (SANE_Byte *)data);
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: camera=%u register=0x%04x length=%d failed: %s\n", __func__,
        (unsigned)camera, (unsigned)value, (int)length, sane_strstatus(status));
  return status;
}

static SANE_Status
read_camera_byte(kodak_i2000_handle *scanner, uint16_t camera, uint16_t value,
                 SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || data == NULL)
    return SANE_STATUS_INVAL;
  *data = 0;
  status = sanei_usb_control_msg(scanner->usb, KODAK_I2000_CAMERA_REQUEST_TYPE_IN,
                                 KODAK_I2000_CAMERA_REQUEST, value, camera,
                                 KODAK_I2000_CAMERA_BYTE_LENGTH, data);
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: camera=%u register=0x%04x failed: %s\n", __func__, (unsigned)camera,
        (unsigned)value, sane_strstatus(status));
  return status;
}

static SANE_Status
read_camera_data(kodak_i2000_handle *scanner, uint16_t camera, uint16_t value,
                 SANE_Int length, SANE_Byte *data)
{
  SANE_Status status;

  if (scanner == NULL || scanner->usb < 0 || length <= 0 || data == NULL)
    return SANE_STATUS_INVAL;
  memset(data, 0, (size_t)length);
  status =
      sanei_usb_control_msg(scanner->usb, KODAK_I2000_CAMERA_REQUEST_TYPE_IN,
                            KODAK_I2000_CAMERA_REQUEST, value, camera, length, data);
  if (status != SANE_STATUS_GOOD)
    DBG(2, "%s: camera=%u register=0x%04x length=%d failed: %s\n", __func__,
        (unsigned)camera, (unsigned)value, (int)length, sane_strstatus(status));
  return status;
}

static SANE_Status
setup_camera_selector(kodak_i2000_handle *scanner, uint16_t camera, SANE_Byte selector)
{
  SANE_Byte observed;
  SANE_Status status;

  /* The vendor writes the selector twice before reading it back. */
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_SELECTOR, selector);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_SELECTOR, selector);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_camera_byte(scanner, camera, KODAK_I2000_CAMERA_SELECTOR, &observed);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (observed != selector)
    DBG(4, "%s: camera=%u selector requested=0x%02x observed=0x%02x\n", __func__,
        (unsigned)camera, (unsigned)selector, (unsigned)observed);
  return SANE_STATUS_GOOD;
}

static SANE_Status
setup_camera_from_blocks(kodak_i2000_handle *scanner, uint16_t camera,
                         const uint8_t *const camera_blocks[6],
                         SANE_Bool high_resolution, SANE_Byte final_mode)
{
  SANE_Status status;
  SANE_Byte selector;

  if (scanner == NULL || camera_blocks == NULL)
    return SANE_STATUS_INVAL;

  /* SetupFPGA writes six 32-byte camera pages.  The selector is set to zero
   * before each page and then advanced to the next page; the double write and
   * readback are observable on the i2400 and are retained here. */
  status = setup_camera_selector(scanner, camera, 0x00u);
  if (status != SANE_STATUS_GOOD)
    return status;
  for (selector = 0U; selector < 6U; ++selector) {
    status =
        write_camera_data(scanner, camera, 0x0000u, KODAK_I2000_CAMERA_BLOCK_LENGTH,
                          camera_blocks[selector]);
    if (status != SANE_STATUS_GOOD)
      return status;
    status = setup_camera_selector(scanner, camera, (SANE_Byte)(selector + 1U));
    if (status != SANE_STATUS_GOOD)
      return status;
  }

  status = write_camera_byte(scanner, camera, 0x0001u, 0x22u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, camera, 0x00u);
  if (status != SANE_STATUS_GOOD)
    return status;
  /* The 600-dpi camera table (also used for requested 400 dpi) transitions
   * directly to mode 0x09.  The intermediate 0x28/0x08 writes belong to the
   * captured 300-dpi camera sequence. */
  if (high_resolution)
    return write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER,
                             final_mode);
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER, 0x28u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, camera, 0x00u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER, 0x08u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, camera, 0x00u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status =
      write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER, final_mode);
  return status;
}

static SANE_Status
setup_camera_mode(kodak_i2000_handle *scanner, uint16_t camera)
{
  SANE_Status status;

  status = setup_camera_selector(scanner, camera, 0x00u);
  if (status != SANE_STATUS_GOOD)
    return status;
  return write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER,
                           KODAK_I2000_CAMERA_MODE_VALUE);
}

uint16_t
load_uint16_le(const SANE_Byte *value)
{
  return (uint16_t)value[0] | (uint16_t)((uint16_t)value[1] << 8);
}

SANE_Status
read_image_calibration_record(kodak_i2000_handle *scanner,
                              SANE_Byte record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH])
{
  if (scanner == NULL || record == NULL)
    return SANE_STATUS_INVAL;
  memset(record, 0, KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH);
  return read_device_get(
      scanner, "ReadImageCalResults", KODAK_I2000_IMAGE_CAL_FLASH_REQUEST,
      KODAK_I2000_IMAGE_CAL_FLASH_VALUE, KODAK_I2000_IMAGE_CAL_FLASH_INDEX,
      KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH, record);
}

static SANE_Status
setup_cameras(kodak_i2000_handle *scanner, SANE_Bool dt_cameras,
              const SANE_Byte *calibration)
{
  static const SANE_Byte camera_300_tails[2][3] = {{0x00, 0xa5, 0xa5},
                                                   {0x00, 0xb6, 0xb6}};
  SANE_Byte camera_600_tails[2][3];
  const uint8_t *const(*camera_blocks)[6];
  const SANE_Byte(*camera_tails)[3] = camera_300_tails;
  SANE_Bool high_resolution;
  SANE_Status status;
  uint16_t camera;

  SANE_Byte final_mode = 0x09U;

  high_resolution = scanner->resolution == 400 || scanner->resolution == 600;
  if (high_resolution) {
    camera_blocks =
        dt_cameras ? kodak_i2000_camera_600_dt_blocks : kodak_i2000_camera_600_blocks;
    for (camera = 0; camera < 2; ++camera) {
      unsigned offset = camera == 0 ? KODAK_I2000_IMAGE_CAL_FRONT_600_GAINS
                                    : KODAK_I2000_IMAGE_CAL_REAR_600_GAINS;
      for (unsigned channel = 0; channel < 3; ++channel)
        camera_600_tails[camera][channel] = calibration[offset + channel * 2U];
    }
    camera_tails = camera_600_tails;
  } else if (scanner->mode == KODAK_I2000_MODE_COLOR) {
    camera_blocks = dt_cameras ? kodak_i2000_camera_color_300_dt_blocks
                               : kodak_i2000_camera_color_300_blocks;
  } else {
    /* The DT grayscale tables also drive the background capture. */
    camera_blocks =
        dt_cameras ? kodak_i2000_background_camera_blocks : kodak_i2000_camera_blocks;
    final_mode = dt_cameras ? 0x19U : 0x09U;
  }

  /* The vendor primes both sensors in camera order, then programs camera 1
   * before camera 0. */
  for (camera = 0U; camera < 2U; ++camera) {
    status = setup_camera_mode(scanner, camera);
    if (status != SANE_STATUS_GOOD)
      return status;
  }

  status = setup_camera_from_blocks(scanner, 1U, camera_blocks[1], high_resolution,
                                    final_mode);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_from_blocks(scanner, 0U, camera_blocks[0], high_resolution,
                                    final_mode);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = setup_camera_selector(scanner, 0U, 0x01u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_data(scanner, 0U, 0x0000u, 3, camera_tails[0]);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, 0U, 0x01u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, 1U, 0x01u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_data(scanner, 1U, 0x0000u, 3, camera_tails[1]);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, 1U, 0x01u);
  return status;
}

SANE_Status
setup_background_cameras(kodak_i2000_handle *scanner)
{
  static const SANE_Byte camera_tails[2][3] = {{0x00, 0xa5, 0xa5}, {0x00, 0xb6, 0xb6}};
  SANE_Status status;
  SANE_Byte observed_tail[3];
  uint16_t camera;

  /* This is the fixed 300-dpi duplex camera sequence emitted by
   * CheckBackgroundColor(), including its distinct exposure pages. */
  for (camera = 0U; camera < 2U; ++camera) {
    status = setup_camera_mode(scanner, camera);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = setup_camera_from_blocks(
      scanner, 1U, kodak_i2000_background_camera_blocks[1], SANE_FALSE, 0x19U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_from_blocks(
      scanner, 0U, kodak_i2000_background_camera_blocks[0], SANE_FALSE, 0x19U);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = setup_camera_selector(scanner, 0U, 0x01u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_data(scanner, 0U, 0x0000u, (SANE_Int)sizeof(camera_tails[0]),
                             camera_tails[0]);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(100000U);
  status = write_camera_byte(scanner, 0U, KODAK_I2000_CAMERA_SELECTOR, 0x01U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_camera_data(scanner, 0U, 0x0000U, (SANE_Int)sizeof(observed_tail),
                            observed_tail);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (memcmp(observed_tail, camera_tails[0], sizeof(observed_tail)) != 0)
    DBG(4, "%s: camera 0 tail readback differs from requested values\n", __func__);
  status = setup_camera_selector(scanner, 1U, 0x01u);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_data(scanner, 1U, 0x0000u, (SANE_Int)sizeof(camera_tails[1]),
                             camera_tails[1]);
  if (status != SANE_STATUS_GOOD)
    return status;
  usleep(100000U);
  status = write_camera_byte(scanner, 1U, KODAK_I2000_CAMERA_SELECTOR, 0x01U);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_camera_data(scanner, 1U, 0x0000U, (SANE_Int)sizeof(observed_tail),
                            observed_tail);
  if (status == SANE_STATUS_GOOD &&
      memcmp(observed_tail, camera_tails[1], sizeof(observed_tail)) != 0)
    DBG(4, "%s: camera 1 tail readback differs from requested values\n", __func__);
  return status;
}

SANE_Status
restore_camera_after_capture(kodak_i2000_handle *scanner, uint16_t camera)
{
  SANE_Status status;
  uint16_t camera_register;

  status = setup_camera_selector(scanner, camera, KODAK_I2000_CAMERA_SELECTOR_RESET);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_MODE_REGISTER,
                             KODAK_I2000_CAMERA_MODE_VALUE);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_camera_selector(scanner, camera, KODAK_I2000_CAMERA_SELECTOR_MODE);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_camera_byte(scanner, camera, KODAK_I2000_CAMERA_GAIN_REGISTER,
                             KODAK_I2000_CAMERA_GAIN_VALUE);
  if (status != SANE_STATUS_GOOD)
    return status;

  for (camera_register = KODAK_I2000_CAMERA_DATA_FIRST_REGISTER;
       camera_register <= KODAK_I2000_CAMERA_DATA_LAST_REGISTER; ++camera_register) {
    status = setup_camera_selector(scanner, camera, KODAK_I2000_CAMERA_SELECTOR_DATA);
    if (status != SANE_STATUS_GOOD)
      return status;
    status = write_camera_byte(scanner, camera, camera_register,
                               KODAK_I2000_CAMERA_DATA_VALUE);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  return SANE_STATUS_GOOD;
}

SANE_Status
restore_capture_hardware(kodak_i2000_handle *scanner)
{
  SANE_Bool high_resolution;
  SANE_Byte observed;
  SANE_Status status;
  uint16_t camera;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  high_resolution = scanner->resolution == 400 || scanner->resolution == 600;

  status = write_completion_marker(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  (void)read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, &observed);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           high_resolution ? 0x80 : 0x20);
  if (status != SANE_STATUS_GOOD)
    return status;
  for (camera = 0U; camera < 2U; ++camera) {
    status = restore_camera_after_capture(scanner, camera);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           high_resolution ? 0x84 : 0x24);
  if (status != SANE_STATUS_GOOD)
    return status;
  return read_status(scanner);
}

static SANE_Status
write_camera_conversion_registers(kodak_i2000_handle *scanner,
                                  uint16_t correction_register, uint16_t red_register,
                                  uint16_t green_register, uint16_t blue_register,
                                  SANE_Byte correction)
{
  SANE_Status status;

  status = write_fpga_byte(scanner, correction_register, correction);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, red_register, 0xfd);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, green_register, 0x4b);
  if (status != SANE_STATUS_GOOD)
    return status;
  return write_fpga_byte(scanner, blue_register, 0x15);
}

SANE_Status
write_camera_profile(kodak_i2000_handle *scanner, unsigned side,
                     const kodak_i2000_color_profile *profile)
{
  uint16_t bank = (uint16_t)(side << 8);
  uint16_t base =
      side ? KODAK_I2000_FPGA_REGISTER_DB00 : KODAK_I2000_FPGA_REGISTER_DA00;
  SANE_Status status;
  size_t index;
  SANE_Byte session_setup[KODAK_I2000_SESSION_DATA_LENGTH];

  for (index = 0; index < 8; ++index) {
    status = write_device_set(
        scanner, "CameraCalibrationE5", KODAK_I2000_CALIBRATION_E5_REQUEST, bank,
        (uint16_t)(index * 0x200U), KODAK_I2000_CALIBRATION_E5_LENGTH,
        (SANE_Byte *)profile->e5[index]);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = write_camera_conversion_registers(
      scanner, base + 0x2f, base + 0x2c, base + 0x2d, base + 0x2e, profile->correction);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_session_info(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  for (index = 0; index < 4; ++index) {
    status = write_device_set(
        scanner, "CameraCalibrationE4", KODAK_I2000_CALIBRATION_E4_REQUEST,
        bank | kodak_i2000_calibration_e4_values[index], 0U,
        KODAK_I2000_CALIBRATION_E4_LENGTH, (SANE_Byte *)profile->e4[index]);
    if (status != SANE_STATUS_GOOD)
      return status;
  }

  if (!scanner->session_info_valid ||
      !kodak_i2000_build_session_setup(profile, side, scanner->session_info,
                                       sizeof(scanner->session_info), session_setup,
                                       sizeof(session_setup))) {
    DBG(1, "%s: refusing unrecognized session record\n", __func__);
    return SANE_STATUS_IO_ERROR;
  }
  log_raw_block("session setup", session_setup, sizeof(session_setup));
  return write_device_set(scanner, "SetSessionData", KODAK_I2000_SESSION_DATA_REQUEST,
                          KODAK_I2000_SESSION_DATA_VALUE,
                          KODAK_I2000_SESSION_DATA_INDEX,
                          KODAK_I2000_SESSION_DATA_LENGTH, session_setup);
}

SANE_Status
write_fpga_word(kodak_i2000_handle *scanner, uint16_t high_register, uint16_t value)
{
  SANE_Status status;

  status = write_fpga_byte(scanner, high_register, (SANE_Byte)((value >> 8) & 0xffU));
  if (status != SANE_STATUS_GOOD)
    return status;
  return write_fpga_byte(scanner, (uint16_t)(high_register + 1U),
                         (SANE_Byte)(value & 0xffU));
}

static SANE_Status
setup_camera_fpga_path(kodak_i2000_handle *scanner, uint16_t register_base,
                       unsigned sensor_resolution, const SANE_Byte *calibration)
{
  SANE_Bool rear_path;
  uint16_t scaled_value;
  SANE_Status status;

  rear_path = register_base == KODAK_I2000_FPGA_REGISTER_DB00;
  /* This leading timing field remains 0x0014 in both the captured 300- and
   * 600-dpi paths; it is not an image-space dimension. */
  status =
      write_fpga_word(scanner, (uint16_t)(register_base + 0x06U), UINT16_C(0x0014));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, (uint16_t)(register_base + 0x2bU), 0x00);
  if (status != SANE_STATUS_GOOD)
    return status;
  scaled_value = kodak_i2000_scale_sensor_value(0x050aU, sensor_resolution);
  status = write_fpga_word(scanner, (uint16_t)(register_base + 0x04U), scaled_value);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (rear_path)
    scaled_value = sensor_resolution > 300U ? UINT16_C(0x002d) : UINT16_C(0x0016);
  else
    scaled_value = kodak_i2000_scale_sensor_value(0x0007U, sensor_resolution);
  status = write_fpga_word(scanner, (uint16_t)(register_base + 0x08U), scaled_value);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(
      scanner, (uint16_t)(register_base + 0x01U),
      kodak_i2000_ip_control((unsigned)scanner->mode, sensor_resolution));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, (uint16_t)(register_base + 0x25U),
                           kodak_i2000_image_footer_only((unsigned)scanner->mode));
  if (status != SANE_STATUS_GOOD)
    return status;
  scaled_value = kodak_i2000_scale_sensor_value(0x0014U, sensor_resolution);
  status = write_fpga_word(scanner, (uint16_t)(register_base + 0x0cU), scaled_value);
  if (status != SANE_STATUS_GOOD)
    return status;
  scaled_value = kodak_i2000_scale_sensor_value(0x050aU, sensor_resolution);
  status = write_fpga_word(scanner, (uint16_t)(register_base + 0x0aU), scaled_value);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (rear_path)
    scaled_value = sensor_resolution > 300U ? UINT16_C(0x002d) : UINT16_C(0x0016);
  else
    scaled_value = kodak_i2000_scale_sensor_value(0x0007U, sensor_resolution);
  status = write_fpga_word(scanner, (uint16_t)(register_base + 0x02U), scaled_value);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (sensor_resolution <= 300U)
    return write_fpga_word(scanner, (uint16_t)(register_base + 0x11U),
                           rear_path ? 0x018aU : 0x016aU);

  unsigned offset = rear_path ? KODAK_I2000_IMAGE_CAL_REAR_600_TARGETS
                              : KODAK_I2000_IMAGE_CAL_FRONT_600_TARGETS;
  for (unsigned channel = 0; channel < 3; ++channel) {
    status = write_fpga_word(scanner, (uint16_t)(register_base + 0x11U + channel * 2U),
                             load_uint16_le(calibration + offset + channel * 2U));
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  return SANE_STATUS_GOOD;
}

static void
sample_camera_fpga_path(kodak_i2000_handle *scanner, uint16_t register_base)
{
  SANE_Byte observed;
  size_t index;

  for (index = 0x11U; index <= 0x16U; ++index)
    (void)read_fpga_byte(scanner, (uint16_t)(register_base + index), &observed);
  for (index = 0U; index < 3U; ++index) {
    (void)read_fpga_byte(scanner, (uint16_t)(register_base + 0x19U), &observed);
    (void)read_fpga_byte(scanner, (uint16_t)(register_base + 0x1aU), &observed);
  }
}

SANE_Status
setup_capture_fpga(kodak_i2000_handle *scanner, SANE_Bool dt_cameras)
{
  SANE_Bool high_resolution;
  SANE_Byte calibration[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH];
  unsigned sensor_resolution;
  SANE_Status status;
  SANE_Byte observed;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  sensor_resolution = kodak_i2000_sensor_resolution((unsigned)scanner->resolution);
  high_resolution = sensor_resolution > 300U;
  if (high_resolution) {
    status = read_image_calibration_record(scanner, calibration);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D900,
                           scanner->mode == KODAK_I2000_MODE_COLOR ? 0x00 : 0x01);
  if (status != SANE_STATUS_GOOD)
    return status;
  (void)read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, &observed);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901, 0x21);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           high_resolution ? 0x80 : 0x20);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D903, 0x00);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D904, 0x00);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D905,
                           red_rephase_for_request(scanner->resolution, dt_cameras));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D906,
                           green_rephase_for_request(scanner->resolution, dt_cameras));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D907, 0x28);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D920,
                           kodak_i2000_resolution_footer(sensor_resolution));
  if (status != SANE_STATUS_GOOD)
    return status;
  status = write_fpga_fifo(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = setup_cameras(scanner, dt_cameras, calibration);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* SetupImageHeight/SetupImageWidth operate in the selected camera's
   * coordinate space.  Program each enabled camera bank; 400/600 use the
   * 600-dpi sensor coordinate group. */
  if (scanner->source != KODAK_I2000_SOURCE_REAR) {
    status = setup_camera_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DA00,
                                    sensor_resolution, calibration);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  if (scanner->source != KODAK_I2000_SOURCE_FRONT) {
    status = setup_camera_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DB00,
                                    sensor_resolution, calibration);
    if (status != SANE_STATUS_GOOD)
      return status;
  }
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_D901,
                           high_resolution ? 0x88 : 0x28);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* SetupFPGA samples these timing/status registers before arming the
   * feeder.  They are not needed to form the SANE frame, but keeping the
   * reads makes the transaction sequence match the recovered implementation
   * and records useful diagnostics at debug level six. */
  if (scanner->source != KODAK_I2000_SOURCE_REAR)
    sample_camera_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DA00);
  if (scanner->source != KODAK_I2000_SOURCE_FRONT)
    sample_camera_fpga_path(scanner, KODAK_I2000_FPGA_REGISTER_DB00);
  status = write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0B, 0x46);
  if (status != SANE_STATUS_GOOD)
    return status;
  (void)read_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0A, &observed);
  return write_fpga_byte(scanner, KODAK_I2000_FPGA_REGISTER_DD0A, 0x40);
}
