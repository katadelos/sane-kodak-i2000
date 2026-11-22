/* Firmware upload, power transitions, and scanner session startup. */
#include "sane/config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_transport.h"
#include "kodak_i2000_boot.h"
#include "kodak_i2000_scan.h"
#include "kodak_i2000_firmware.h"

#define KODAK_I2000_FIRMWARE_BULK_TIMEOUT_MS 30000
#define KODAK_I2000_FIRMWARE_POLL_INTERVAL_US 100000U
#define KODAK_I2000_FIRMWARE_POLL_ATTEMPTS 50U
#define KODAK_I2000_WAKE_POLL_INTERVAL_US 100000U
#define KODAK_I2000_WAKE_POLL_ATTEMPTS 50U

static SANE_Status
write_exit_low_power(kodak_i2000_handle *scanner)
{
  return write_device_set(
      scanner, "ExitLowPowerMode", KODAK_I2000_EXIT_LOW_POWER_REQUEST,
      KODAK_I2000_EXIT_LOW_POWER_VALUE, KODAK_I2000_EXIT_LOW_POWER_INDEX,
      KODAK_I2000_EXIT_LOW_POWER_LENGTH, NULL);
}

static SANE_Status
firmware_result_to_sane(kodak_i2000_firmware_result result)
{
  if (result == KODAK_I2000_FIRMWARE_NO_MEMORY)
    return SANE_STATUS_NO_MEM;
  if (result == KODAK_I2000_FIRMWARE_INVALID ||
      result == KODAK_I2000_FIRMWARE_BAD_CONTAINER ||
      result == KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE)
    return SANE_STATUS_INVAL;
  return result == KODAK_I2000_FIRMWARE_OK ? SANE_STATUS_GOOD : SANE_STATUS_IO_ERROR;
}

static SANE_Status
wait_for_firmware_component(kodak_i2000_handle *scanner, SANE_Byte component)
{
  SANE_Status last_status = SANE_STATUS_IO_ERROR;
  unsigned attempt;

  for (attempt = 0U; attempt < KODAK_I2000_FIRMWARE_POLL_ATTEMPTS; ++attempt) {
    last_status = read_status(scanner);
    if (last_status == SANE_STATUS_GOOD &&
        scanner->status[KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET] == component)
      return SANE_STATUS_GOOD;
    usleep(KODAK_I2000_FIRMWARE_POLL_INTERVAL_US);
  }
  if (last_status == SANE_STATUS_GOOD)
    DBG(1, "%s: firmware component 0x%02x did not start; last was 0x%02x\n", __func__,
        (unsigned)component,
        (unsigned)scanner->status[KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET]);
  return last_status == SANE_STATUS_GOOD ? SANE_STATUS_DEVICE_BUSY : last_status;
}

static SANE_Status
write_internal_firmware(kodak_i2000_handle *scanner,
                        const kodak_i2000_firmware_resource *resource, uint16_t target,
                        SANE_Bool already_held, SANE_Bool leave_held)
{
  SANE_Byte reset_value;
  size_t offset;
  SANE_Status status;

  if (scanner == NULL || resource == NULL || resource->data == NULL ||
      resource->size == 0U || resource->size > (size_t)UINT16_MAX + 1U - (size_t)target)
    return SANE_STATUS_INVAL;
  if (!already_held) {
    status = write_device_set(scanner, "StopFirmwareProcessor",
                              KODAK_I2000_FIRMWARE_PROCESSOR_STOP_REQUEST,
                              KODAK_I2000_FIRMWARE_PROCESSOR_STOP_VALUE,
                              KODAK_I2000_FIRMWARE_PROCESSOR_STOP_INDEX,
                              KODAK_I2000_FIRMWARE_PROCESSOR_STOP_LENGTH, NULL);
    if (status != SANE_STATUS_GOOD)
      return status;
    reset_value = KODAK_I2000_INTERNAL_FIRMWARE_HOLD_RESET;
    status = write_device_set(
        scanner, "HoldFirmwareProcessorReset", KODAK_I2000_INTERNAL_FIRMWARE_REQUEST,
        KODAK_I2000_INTERNAL_FIRMWARE_RESET_VALUE, KODAK_I2000_INTERNAL_FIRMWARE_INDEX,
        KODAK_I2000_INTERNAL_FIRMWARE_RESET_LENGTH, &reset_value);
    if (status != SANE_STATUS_GOOD)
      return status;
  }

  for (offset = 0U; offset < resource->size;) {
    size_t remaining = resource->size - offset;
    SANE_Int chunk = (SANE_Int)(remaining > KODAK_I2000_FIRMWARE_CHUNK_LENGTH
                                    ? KODAK_I2000_FIRMWARE_CHUNK_LENGTH
                                    : remaining);
    status = write_device_set(
        scanner, "WriteInternalFirmware", KODAK_I2000_INTERNAL_FIRMWARE_REQUEST,
        (uint16_t)((size_t)target + offset), KODAK_I2000_INTERNAL_FIRMWARE_INDEX, chunk,
        (SANE_Byte *)(resource->data + offset));
    if (status != SANE_STATUS_GOOD) {
      /* Never release a processor onto a partially uploaded image.  A power
       * cycle is the deterministic recovery from this explicit cold path. */
      DBG(1,
          "%s: firmware upload stopped at offset 0x%04lx; processor "
          "remains held in reset\n",
          __func__, (unsigned long)offset);
      return status;
    }
    offset += (size_t)chunk;
  }

  if (leave_held)
    return SANE_STATUS_GOOD;
  reset_value = KODAK_I2000_INTERNAL_FIRMWARE_RELEASE_RESET;
  return write_device_set(
      scanner, "ReleaseFirmwareProcessorReset", KODAK_I2000_INTERNAL_FIRMWARE_REQUEST,
      KODAK_I2000_INTERNAL_FIRMWARE_RESET_VALUE, KODAK_I2000_INTERNAL_FIRMWARE_INDEX,
      KODAK_I2000_INTERNAL_FIRMWARE_RESET_LENGTH, &reset_value);
}

static SANE_Status
write_firmware_bank(kodak_i2000_handle *scanner, SANE_Byte bank,
                    const kodak_i2000_firmware_resource *resource)
{
  size_t offset;
  SANE_Status status;

  if (scanner == NULL || resource == NULL || resource->data == NULL ||
      resource->size == 0U ||
      resource->size > (size_t)UINT16_MAX + 1U - KODAK_I2000_FIRMWARE_BANK_WRITE_BASE)
    return SANE_STATUS_INVAL;
  status = write_device_set(
      scanner, "SelectFirmwareBank", KODAK_I2000_FIRMWARE_BANK_REQUEST,
      KODAK_I2000_FIRMWARE_BANK_SELECT_VALUE, KODAK_I2000_FIRMWARE_BANK_INDEX,
      KODAK_I2000_FIRMWARE_BANK_SELECT_LENGTH, &bank);
  if (status != SANE_STATUS_GOOD)
    return status;

  for (offset = 0U; offset < resource->size;) {
    size_t remaining = resource->size - offset;
    SANE_Int chunk = (SANE_Int)(remaining > KODAK_I2000_FIRMWARE_CHUNK_LENGTH
                                    ? KODAK_I2000_FIRMWARE_CHUNK_LENGTH
                                    : remaining);
    /* LoadFirmwareBank supplies the 16-bit chunk offset in wIndex as well as
     * adding it to the banked 0x4000 write window in wValue. */
    status = write_device_set(
        scanner, "WriteFirmwareBank", KODAK_I2000_FIRMWARE_BANK_REQUEST,
        (uint16_t)(KODAK_I2000_FIRMWARE_BANK_WRITE_BASE + offset), (uint16_t)offset,
        chunk, (SANE_Byte *)(resource->data + offset));
    if (status != SANE_STATUS_GOOD)
      return status;
    offset += (size_t)chunk;
  }
  return SANE_STATUS_GOOD;
}

static void
reset_firmware_endpoints(kodak_i2000_handle *scanner)
{
  SANE_Status status;

  sanei_usb_set_endpoint(scanner->usb, USB_DIR_OUT | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_BULK_OUT_ENDPOINT);
  sanei_usb_set_endpoint(scanner->usb, USB_DIR_IN | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_FRONT_IMAGE_ENDPOINT);
  status = sanei_usb_clear_halt(scanner->usb);
  if (status != SANE_STATUS_GOOD)
    DBG(3, "%s: front/out endpoint reset returned %s\n", __func__,
        sane_strstatus(status));
  sanei_usb_set_endpoint(scanner->usb, USB_DIR_IN | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_REAR_IMAGE_ENDPOINT);
  status = sanei_usb_clear_halt(scanner->usb);
  if (status != SANE_STATUS_GOOD)
    DBG(3, "%s: rear/out endpoint reset returned %s\n", __func__,
        sane_strstatus(status));
  sanei_usb_set_endpoint(scanner->usb, USB_DIR_IN | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_FRONT_IMAGE_ENDPOINT);
}

static SANE_Status
program_cold_fpga(kodak_i2000_handle *scanner,
                  const kodak_i2000_firmware_resource *fpga)
{
  SANE_Byte diagnostic[KODAK_I2000_DIAGNOSTIC_LENGTH];
  kodak_i2000_firmware_result firmware_result;
  SANE_Byte *expanded = NULL;
  size_t expanded_size = 0U;
  size_t written;
  SANE_Status status;
  SANE_Status end_status;

  firmware_result = kodak_i2000_firmware_expand_fpga(fpga, &expanded, &expanded_size);
  if (firmware_result != KODAK_I2000_FIRMWARE_OK)
    return firmware_result_to_sane(firmware_result);

  sanei_usb_set_endpoint(scanner->usb, USB_DIR_OUT | USB_ENDPOINT_TYPE_BULK,
                         KODAK_I2000_BULK_OUT_ENDPOINT);
  status = write_device_set(
      scanner, "BeginFpgaProgramming", KODAK_I2000_FPGA_PROGRAM_REQUEST,
      KODAK_I2000_FPGA_PROGRAM_BEGIN_VALUE, KODAK_I2000_FPGA_PROGRAM_INDEX,
      KODAK_I2000_FPGA_PROGRAM_LENGTH, NULL);
  if (status != SANE_STATUS_GOOD)
    goto done;
  sanei_usb_set_timeout(KODAK_I2000_FIRMWARE_BULK_TIMEOUT_MS);
  written = expanded_size;
  status = sanei_usb_write_bulk(scanner->usb, expanded, &written);
  if (status == SANE_STATUS_GOOD && written != expanded_size) {
    DBG(1, "%s: short FPGA write: %lu of %lu bytes\n", __func__, (unsigned long)written,
        (unsigned long)expanded_size);
    status = SANE_STATUS_IO_ERROR;
  }
  end_status = write_device_set(
      scanner, "EndFpgaProgramming", KODAK_I2000_FPGA_PROGRAM_REQUEST,
      KODAK_I2000_FPGA_PROGRAM_END_VALUE, KODAK_I2000_FPGA_PROGRAM_INDEX,
      KODAK_I2000_FPGA_PROGRAM_LENGTH, NULL);
  if (status == SANE_STATUS_GOOD)
    status = end_status;
  reset_firmware_endpoints(scanner);
  if (status != SANE_STATUS_GOOD)
    goto done;

  status = write_device_set(
      scanner, "TestFpgaMemory", KODAK_I2000_FPGA_MEMORY_TEST_REQUEST,
      KODAK_I2000_FPGA_MEMORY_TEST_VALUE, KODAK_I2000_FPGA_MEMORY_TEST_INDEX,
      KODAK_I2000_FPGA_MEMORY_TEST_LENGTH, NULL);
  if (status != SANE_STATUS_GOOD)
    goto done;
  memset(diagnostic, 0, sizeof(diagnostic));
  status = sanei_usb_control_msg(
      scanner->usb, KODAK_I2000_STATUS_REQUEST_TYPE, KODAK_I2000_DIAGNOSTIC_REQUEST,
      KODAK_I2000_DIAGNOSTIC_VALUE, KODAK_I2000_DIAGNOSTIC_INDEX,
      KODAK_I2000_DIAGNOSTIC_LENGTH, diagnostic);
  if (status == SANE_STATUS_GOOD && diagnostic[0] != 0U) {
    DBG(1, "%s: FPGA memory diagnostic failed with 0x%02x\n", __func__,
        (unsigned)diagnostic[0]);
    status = SANE_STATUS_IO_ERROR;
  }

done:
  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  free(expanded);
  return status;
}

static SANE_Status
cold_boot_i2400(kodak_i2000_handle *scanner, const char *firmware_source)
{
  kodak_i2000_firmware_bundle bundle;
  kodak_i2000_firmware_result firmware_result;
  SANE_Status status;
  unsigned bank;

  if (scanner == NULL || scanner->device == NULL ||
      scanner->device->product_id != KODAK_I2000_SUPPORTED_CAPTURE_PRODUCT)
    return SANE_STATUS_UNSUPPORTED;
  if (firmware_source == NULL) {
    DBG(1,
        "%s: scanner firmware is not active and no firmware-source is "
        "configured\n",
        __func__);
    return SANE_STATUS_UNSUPPORTED;
  }
  memset(&bundle, 0, sizeof(bundle));
  firmware_result = kodak_i2000_firmware_open_i2400(firmware_source, &bundle);
  if (firmware_result != KODAK_I2000_FIRMWARE_OK) {
    DBG(1, "%s: refusing cold boot: %s\n", __func__,
        kodak_i2000_firmware_result_string(firmware_result));
    return firmware_result_to_sane(firmware_result);
  }

  DBG(2,
      "%s: loading authenticated volatile i2400 firmware from %s; "
      "persistent booter update is disabled\n",
      __func__,
      bundle.format == KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE ? "compact bundle"
                                                                  : "vendor container");
  status = write_internal_firmware(scanner, &bundle.loader, 0U, SANE_FALSE, SANE_FALSE);
  if (status != SANE_STATUS_GOOD)
    goto done;
  status = wait_for_firmware_component(scanner, KODAK_I2000_FIRMWARE_ID_LOADER);
  if (status != SANE_STATUS_GOOD)
    goto done;
  status = program_cold_fpga(scanner, &bundle.fpga);
  if (status != SANE_STATUS_GOOD)
    goto done;
  for (bank = 0U; bank < 4U; ++bank) {
    status = write_firmware_bank(scanner, (SANE_Byte)bank, &bundle.scanner_banks[bank]);
    if (status != SANE_STATUS_GOOD)
      goto done;
  }
  /* Match LoadBankedFirmware's normal embedded-resource branch: SCANNER is
   * the internal image loaded at address zero after SCANNER0..SCANNER3.  The
   * optional external SCANNERX override is loaded at 0xc000 while SCANNER is
   * held; this backend intentionally does not emulate that override.  The
   * packaged BOOTIMAGE is authenticated but belongs to the separate booter
   * programming path and must not be used for this volatile handoff. */
  status =
      write_internal_firmware(scanner, &bundle.scanner, 0U, SANE_FALSE, SANE_FALSE);
  if (status != SANE_STATUS_GOOD)
    goto done;
  status = wait_for_firmware_component(scanner, KODAK_I2000_FIRMWARE_ID_SCANNER);

done:
  kodak_i2000_firmware_close(&bundle);
  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  return status;
}

static SANE_Status
wait_for_started_power_state(kodak_i2000_handle *scanner)
{
  SANE_Status last_status = SANE_STATUS_IO_ERROR;
  unsigned attempt;

  for (attempt = 0U; attempt < KODAK_I2000_FIRMWARE_POLL_ATTEMPTS; ++attempt) {
    SANE_Byte power;
    last_status = read_status(scanner);
    if (last_status == SANE_STATUS_GOOD) {
      power = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
      if (power == KODAK_I2000_STATUS_POWER_STATE_LOW_POWER ||
          power == KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL)
        return SANE_STATUS_GOOD;
    }
    usleep(KODAK_I2000_FIRMWARE_POLL_INTERVAL_US);
  }
  return last_status == SANE_STATUS_GOOD ? SANE_STATUS_DEVICE_BUSY : last_status;
}

static SANE_Status
wait_for_operational_power(kodak_i2000_handle *scanner)
{
  unsigned attempt;

  for (attempt = 0U; attempt < KODAK_I2000_WAKE_POLL_ATTEMPTS; ++attempt) {
    SANE_Status status = read_status(scanner);

    if (acquisition_cancelled(scanner))
      return SANE_STATUS_CANCELLED;
    if (status != SANE_STATUS_GOOD)
      return status;
    if (scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] != 0U)
      return SANE_STATUS_IO_ERROR;
    if (scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET] ==
        KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL)
      return SANE_STATUS_GOOD;
    usleep(KODAK_I2000_WAKE_POLL_INTERVAL_US);
  }
  return SANE_STATUS_DEVICE_BUSY;
}

SANE_Status
prepare_scanner_session(kodak_i2000_handle *scanner, const char *firmware_source)
{
  SANE_Bool cold_started = SANE_FALSE;
  SANE_Byte firmware_id;
  SANE_Byte power_state;
  SANE_Status status;

  if (scanner == NULL || !scanner->status_valid)
    return SANE_STATUS_INVAL;
  firmware_id = scanner->status[KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET];
  power_state = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
  if (firmware_id != KODAK_I2000_FIRMWARE_ID_SCANNER) {
    if (firmware_id > KODAK_I2000_FIRMWARE_ID_LOADER) {
      DBG(1, "%s: unsupported firmware component 0x%02x\n", __func__,
          (unsigned)firmware_id);
      return SANE_STATUS_UNSUPPORTED;
    }
    status = cold_boot_i2400(scanner, firmware_source);
    if (status != SANE_STATUS_GOOD)
      return status;
    cold_started = SANE_TRUE;
    firmware_id = scanner->status[KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET];
    if (firmware_id != KODAK_I2000_FIRMWARE_ID_SCANNER)
      return SANE_STATUS_IO_ERROR;
    power_state = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
  }

  /* The final ID=3 status can precede the scanner application's completed
   * power-state transition.  Accept only the two understood ready states,
   * but give every post-cold intermediate state the same bounded settling
   * period as the documented uninitialized state.  Unknown warm states stay
   * fail-closed instead of being treated as part of a cold transition. */
  if (power_state == KODAK_I2000_STATUS_POWER_STATE_UNINITIALIZED ||
      (cold_started && power_state != KODAK_I2000_STATUS_POWER_STATE_LOW_POWER &&
       power_state != KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL)) {
    status = wait_for_started_power_state(scanner);
    if (status != SANE_STATUS_GOOD) {
      DBG(1, "%s: scanner firmware did not finish power initialization: %s\n", __func__,
          sane_strstatus(status));
      return status;
    }
    power_state = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
  }

  if (power_state == KODAK_I2000_STATUS_POWER_STATE_LOW_POWER) {
    DBG(3, "%s: waking scanner from low-power state\n", __func__);
    status = write_exit_low_power(scanner);
    if (status != SANE_STATUS_GOOD)
      return status;
    status = wait_for_operational_power(scanner);
    if (status != SANE_STATUS_GOOD) {
      DBG(1, "%s: scanner did not reach operational power: %s\n", __func__,
          sane_strstatus(status));
      return status;
    }
  } else if (power_state != KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL) {
    DBG(1, "%s: unsupported scanner power state 0x%02x\n", __func__,
        (unsigned)power_state);
    return SANE_STATUS_DEVICE_BUSY;
  }

  /* A warm vendor open sends the independently identified lamp-off command.
   * Keep that cleanup after wake as well, then verify the resulting state. */
  status = write_lamp_off(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  status = read_status(scanner);
  if (status != SANE_STATUS_GOOD)
    return status;
  power_state = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
  if (power_state != KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL) {
    DBG(1, "%s: scanner left operational power state (0x%02x)\n", __func__,
        (unsigned)power_state);
    return SANE_STATUS_DEVICE_BUSY;
  }
  return SANE_STATUS_GOOD;
}
