/*
 * Small, transport-independent protocol seam for Kodak i2000 devices.
 *
 * The USB identifiers are package evidence.  The control tuples and scanner
 * configuration layout below were recovered from the vendor binary with
 * radare2 and from USB traces.  Captured image framing and rendering live in
 * kodak_i2000_image.[ch], keeping this header focused on control traffic.
 */
#ifndef KODAK_I2000_PROTOCOL_H
#define KODAK_I2000_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

typedef struct kodak_i2000_model {
  uint16_t vendor_id;
  uint16_t product_id;
  const char *name;
} kodak_i2000_model;

const kodak_i2000_model *kodak_i2000_models(size_t *count);
const kodak_i2000_model *kodak_i2000_model_lookup(uint16_t vendor_id,
                                                  uint16_t product_id);

/* Confirmed by the vendor binary's Get() wrapper and sent by the backend. */
#define KODAK_I2000_STATUS_REQUEST_TYPE 0xc0u
#define KODAK_I2000_STATUS_REQUEST 0x00u
#define KODAK_I2000_STATUS_VALUE 0x0000u
#define KODAK_I2000_STATUS_INDEX 0x0000u
#define KODAK_I2000_STATUS_LENGTH 0x0020u

/* Field offsets recovered from DeviceSettings::GetStatus(). */
#define KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET 0x00u
#define KODAK_I2000_FIRMWARE_ID_BOOTER 0x01u
#define KODAK_I2000_FIRMWARE_ID_LOADER 0x02u
#define KODAK_I2000_FIRMWARE_ID_SCANNER 0x03u
#define KODAK_I2000_STATUS_VERSION_OFFSET 0x01u
#define KODAK_I2000_STATUS_BOARD_TYPE_OFFSET 0x06u
#define KODAK_I2000_STATUS_POWER_STATE_OFFSET 0x0cu
#define KODAK_I2000_STATUS_POWER_STATE_UNINITIALIZED 0x00u
#define KODAK_I2000_STATUS_POWER_STATE_LOW_POWER 0x01u
/* Full-power/operational on the attached i2400. */
#define KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL 0x03u
#define KODAK_I2000_STATUS_BUFFER_STATE_OFFSET 0x0du
#define KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET 0x0eu
#define KODAK_I2000_STATUS_TRAY_STATE_OFFSET 0x0fu
#define KODAK_I2000_STATUS_TRAY_PAPER_PRESENT 0x02u
#define KODAK_I2000_STATUS_UDDS_STATE_OFFSET 0x10u
#define KODAK_I2000_STATUS_LAMP_STATE_OFFSET 0x11u
#define KODAK_I2000_STATUS_INTERLOCK_STATE_OFFSET 0x13u
#define KODAK_I2000_STATUS_ERROR_CODE_OFFSET 0x1fu

/* A continuous feeder transaction reports this state after its final sheet
 * has cleared the transport.  Keep the classifier transport-independent so
 * the backend can distinguish normal end-of-feeder from a bulk-read fault. */
int kodak_i2000_status_is_idle_and_empty(const uint8_t *status, size_t status_size);

/* GetEndOfLine() decodes these camera-presence flags from byte five. */
#define KODAK_I2000_END_OF_LINE_CAMERA_FLAGS_OFFSET 0x05u
#define KODAK_I2000_END_OF_LINE_FRONT_CAMERA_MASK 0x01u
#define KODAK_I2000_END_OF_LINE_REAR_CAMERA_MASK 0x02u

/* Additional read-only DeviceSettings::Get() calls recovered with radare2. */
#define KODAK_I2000_SESSION_REQUEST 0x37u
#define KODAK_I2000_SESSION_VALUE 0x0000u
#define KODAK_I2000_SESSION_INDEX 0x0000u
#define KODAK_I2000_SESSION_LENGTH 0x0040u
#define KODAK_I2000_SESSION_VERSION_OFFSET 0x00u
#define KODAK_I2000_SESSION_VERSION 0x01u
#define KODAK_I2000_SESSION_MARKER_OFFSET 0x01u
#define KODAK_I2000_SESSION_INITIAL_MARKER 0x00u
#define KODAK_I2000_SESSION_MARKER 0xaau
#define KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET 0x02u
#define KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET 0x03u
#define KODAK_I2000_SESSION_BACKGROUND_UNKNOWN 0xffu
#define KODAK_I2000_SESSION_BACKGROUND_BLACK 0x00u
#define KODAK_I2000_SESSION_CORRECTION_SELECTOR_OFFSET 0x04u
#define KODAK_I2000_SESSION_LINEARIZATION_SELECTOR_OFFSET 0x0eu
#define KODAK_I2000_SESSION_LUT_SELECTOR 0x06u
#define KODAK_I2000_SESSION_LUT_CRC_BASE_OFFSET 0x10u
#define KODAK_I2000_SESSION_LUT_CRC_STRIDE 0x08u
/* InitializeSessionInfo() sends this transient record when a freshly loaded
 * scanner application returns an all-zero session block. */
int kodak_i2000_session_info_is_uninitialized(const uint8_t *session_info,
                                              size_t session_info_size);
int kodak_i2000_build_initial_session_info(uint8_t *session_info,
                                           size_t session_info_size);
/* InitImageCalValues() changes the marker only after the persistent image
 * calibration has been restored into volatile scanner RAM.  The following
 * background check then resolves the two ff sentinel bytes. */
int kodak_i2000_session_info_needs_calibration(const uint8_t *session_info,
                                               size_t session_info_size);
int kodak_i2000_session_info_needs_background_check(const uint8_t *session_info,
                                                    size_t session_info_size);
int kodak_i2000_build_calibration_ready_session_info(const uint8_t *session_info,
                                                     size_t session_info_size,
                                                     uint8_t *ready_session,
                                                     size_t ready_session_size);
int kodak_i2000_build_background_ready_session_info(const uint8_t *session_info,
                                                    size_t session_info_size,
                                                    uint8_t *ready_session,
                                                    size_t ready_session_size);
#define KODAK_I2000_ENERGY_STAR_REQUEST 0x31u
#define KODAK_I2000_ENERGY_STAR_VALUE 0x0000u
#define KODAK_I2000_ENERGY_STAR_INDEX 0x0000u
#define KODAK_I2000_ENERGY_STAR_LENGTH 0x0001u
#define KODAK_I2000_CONSUMABLE_METERS_REQUEST 0x35u
#define KODAK_I2000_CONSUMABLE_METERS_VALUE 0x0001u
#define KODAK_I2000_CONSUMABLE_METERS_INDEX 0x0000u
#define KODAK_I2000_CONSUMABLE_METERS_LENGTH 0x0100u
/* ValidateConsumableMetersData() accepts only record version one. */
#define KODAK_I2000_CONSUMABLE_METERS_VERSION_OFFSET 0x00u
#define KODAK_I2000_CONSUMABLE_METERS_SUPPORTED_VERSION 0x01u
#define KODAK_I2000_METERS_REQUEST 0x33u
#define KODAK_I2000_METERS_VALUE 0x0000u
#define KODAK_I2000_METERS_INDEX 0x0000u
#define KODAK_I2000_METERS_LENGTH 0x0014u
#define KODAK_I2000_DIAGNOSTIC_REQUEST 0xf2u
#define KODAK_I2000_DIAGNOSTIC_VALUE 0x0000u
#define KODAK_I2000_DIAGNOSTIC_INDEX 0x0000u
#define KODAK_I2000_DIAGNOSTIC_LENGTH 0x0200u
#define KODAK_I2000_LOW_POWER_REQUEST 0x32u
#define KODAK_I2000_LOW_POWER_VALUE 0x0000u
#define KODAK_I2000_LOW_POWER_INDEX 0x0000u
#define KODAK_I2000_LOW_POWER_LENGTH 0x0001u
#define KODAK_I2000_END_OF_LINE_REQUEST 0x34u
#define KODAK_I2000_END_OF_LINE_VALUE 0x0000u
#define KODAK_I2000_END_OF_LINE_INDEX 0x0000u
#define KODAK_I2000_END_OF_LINE_LENGTH 0x0037u

/* DeviceSettings writes used by the first ADF acquisition path. */
#define KODAK_I2000_SET_REQUEST_TYPE 0x40u
#define KODAK_I2000_CONFIG_REQUEST 0x30u
#define KODAK_I2000_CONFIG_VALUE 0x0000u
#define KODAK_I2000_CONFIG_INDEX 0x0000u
#define KODAK_I2000_CONFIG_LENGTH 0x0078u
/* ScannerOperation() dispatches the normal feeder start/stop to request
 * 0x10.  Request 0x11 is a different operation family (the trace's op 4),
 * and must not be used for the ordinary page start. */
#define KODAK_I2000_SCANNER_OPERATION_REQUEST 0x10u
#define KODAK_I2000_SCANNER_START_VALUE 0x0001u
#define KODAK_I2000_SCANNER_STOP_VALUE 0x0000u
#define KODAK_I2000_SCANNER_OPERATION_INDEX 0x0000u
#define KODAK_I2000_SCANNER_OPERATION_LENGTH 0x0000u
/* DeviceSettings::TurnLampOff(), observed after the vendor has drained the
 * page image.  Request 0x11 value one belongs to the setup operation below;
 * the value is therefore part of the command identity. */
#define KODAK_I2000_LAMP_OFF_REQUEST 0x11u
#define KODAK_I2000_LAMP_OFF_VALUE 0x0000u
#define KODAK_I2000_LAMP_OFF_INDEX 0x0000u
#define KODAK_I2000_LAMP_OFF_LENGTH 0x0000u
#define KODAK_I2000_SETUP_OPERATION_REQUEST 0x11u
#define KODAK_I2000_SETUP_OPERATION_VALUE 0x0001u
#define KODAK_I2000_SETUP_OPERATION_INDEX 0x0000u
#define KODAK_I2000_SETUP_OPERATION_LENGTH 0x0000u
#define KODAK_I2000_TRAY_STATE_EVENT 0x13u

/* The i2400 vendor open sends this transient operation while status reports
 * power state one, then polls until state three.  DeviceSettingsA4 has a
 * different diagnostic-action implementation and is not the i2400 path. */
#define KODAK_I2000_EXIT_LOW_POWER_REQUEST 0x39u
#define KODAK_I2000_EXIT_LOW_POWER_VALUE 0x0002u
#define KODAK_I2000_EXIT_LOW_POWER_INDEX 0x0000u
#define KODAK_I2000_EXIT_LOW_POWER_LENGTH 0x0000u

/* DeviceSettings::Set(0x3a, 1, enabled, 0, NULL) gates delivery on the
 * scanner-event interrupt endpoint.  The i2400 has two interrupt endpoints;
 * the vendor selects value one and receives the eight-byte event records on
 * endpoint 0x88.  This switch is volatile and is cleared when the interface
 * closes or the device resets. */
#define KODAK_I2000_INTERRUPT_EVENTS_REQUEST 0x3au
#define KODAK_I2000_INTERRUPT_EVENTS_VALUE 0x0001u
#define KODAK_I2000_INTERRUPT_EVENTS_ENABLE_INDEX 0x0001u
#define KODAK_I2000_INTERRUPT_EVENTS_DISABLE_INDEX 0x0000u
#define KODAK_I2000_INTERRUPT_EVENTS_LENGTH 0x0000u

/* Volatile physical-cold boot operations recovered from the exact i2400
 * v4.14 initializer.  The native path deliberately does not implement the
 * adjacent persistent booter-update/flash operation. */
#define KODAK_I2000_FIRMWARE_CHUNK_LENGTH 0x0800u
#define KODAK_I2000_INTERNAL_FIRMWARE_REQUEST 0xa0u
#define KODAK_I2000_INTERNAL_FIRMWARE_INDEX 0x0000u
#define KODAK_I2000_INTERNAL_FIRMWARE_RESET_VALUE 0xe600u
#define KODAK_I2000_INTERNAL_FIRMWARE_RESET_LENGTH 0x0001u
#define KODAK_I2000_INTERNAL_FIRMWARE_HOLD_RESET 0x01u
#define KODAK_I2000_INTERNAL_FIRMWARE_RELEASE_RESET 0x00u
#define KODAK_I2000_FIRMWARE_PROCESSOR_STOP_REQUEST 0x21u
#define KODAK_I2000_FIRMWARE_PROCESSOR_STOP_VALUE 0x0000u
#define KODAK_I2000_FIRMWARE_PROCESSOR_STOP_INDEX 0x0000u
#define KODAK_I2000_FIRMWARE_PROCESSOR_STOP_LENGTH 0x0000u
#define KODAK_I2000_FIRMWARE_BANK_REQUEST 0xa3u
#define KODAK_I2000_FIRMWARE_BANK_SELECT_VALUE 0xd80eu
#define KODAK_I2000_FIRMWARE_BANK_INDEX 0x0000u
#define KODAK_I2000_FIRMWARE_BANK_SELECT_LENGTH 0x0001u
#define KODAK_I2000_FIRMWARE_BANK_WRITE_BASE 0x4000u
#define KODAK_I2000_FPGA_PROGRAM_REQUEST 0x20u
#define KODAK_I2000_FPGA_PROGRAM_BEGIN_VALUE 0x0001u
#define KODAK_I2000_FPGA_PROGRAM_END_VALUE 0x0000u
#define KODAK_I2000_FPGA_PROGRAM_INDEX 0x0000u
#define KODAK_I2000_FPGA_PROGRAM_LENGTH 0x0000u
#define KODAK_I2000_FPGA_MEMORY_TEST_REQUEST 0xf1u
#define KODAK_I2000_FPGA_MEMORY_TEST_VALUE 0x0003u
#define KODAK_I2000_FPGA_MEMORY_TEST_INDEX 0x0000u
#define KODAK_I2000_FPGA_MEMORY_TEST_LENGTH 0x0000u

/* SetupScannerA4's preamble and calibration transfers. */
#define KODAK_I2000_SETUP_MARKER_REQUEST 0x45u
#define KODAK_I2000_SETUP_MARKER_VALUE 0x0000u
#define KODAK_I2000_SETUP_MARKER_INDEX 0x0000u
#define KODAK_I2000_SETUP_MARKER_LENGTH 0x0003u
#define KODAK_I2000_CALIBRATION_E5_REQUEST 0xe5u
#define KODAK_I2000_CALIBRATION_E5_VALUE 0x0000u
#define KODAK_I2000_CALIBRATION_E5_LENGTH 0x0800u
#define KODAK_I2000_CALIBRATION_E4_REQUEST 0xe4u
#define KODAK_I2000_CALIBRATION_E4_VALUE 0x0000u
#define KODAK_I2000_CALIBRATION_E4_LENGTH 0x0400u
#define KODAK_I2000_SESSION_DATA_REQUEST 0x37u
#define KODAK_I2000_SESSION_DATA_VALUE 0x0000u
#define KODAK_I2000_SESSION_DATA_INDEX 0x0000u
#define KODAK_I2000_SESSION_DATA_LENGTH 0x0040u
#define KODAK_I2000_CAMERA_BLOCK_LENGTH 0x0020u

/* Volatile post-cold image-calibration restore.  These are read-only flash
 * and SDRAM windows plus FPGA control registers; no persistent write request
 * is used by this path. */
#define KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH 0x0100u
#define KODAK_I2000_IMAGE_CAL_FLASH_REQUEST 0xe3u
#define KODAK_I2000_IMAGE_CAL_FLASH_VALUE 0x0003u
#define KODAK_I2000_IMAGE_CAL_FLASH_INDEX 0xfc00u
#define KODAK_I2000_IMAGE_CAL_SDRAM_REQUEST 0xe2u
#define KODAK_I2000_IMAGE_CAL_SDRAM_VALUE 0x01ffu
#define KODAK_I2000_IMAGE_CAL_SDRAM_INDEX 0x9f00u
#define KODAK_I2000_IMAGE_CAL_FRONT_600_GAINS 0x2au
#define KODAK_I2000_IMAGE_CAL_REAR_600_GAINS 0x36u
#define KODAK_I2000_IMAGE_CAL_FRONT_600_TARGETS 0x62u
#define KODAK_I2000_IMAGE_CAL_REAR_600_TARGETS 0x6au
#define KODAK_I2000_IMAGE_CAL_FRONT_TARGETS_OFFSET 0x52u
#define KODAK_I2000_IMAGE_CAL_REAR_TARGETS_OFFSET 0x5au
#define KODAK_I2000_IMAGE_CAL_FRONT_LIMIT_OFFSET 0x72u
#define KODAK_I2000_IMAGE_CAL_REAR_LIMIT_OFFSET 0x74u
#define KODAK_I2000_AUTO_WHITE_REQUEST 0x18u
#define KODAK_I2000_AUTO_WHITE_VALUE 0x0000u
#define KODAK_I2000_AUTO_WHITE_INDEX 0x0000u
#define KODAK_I2000_AUTO_WHITE_LENGTH 0x0004u
int kodak_i2000_calibration_sdram_matches(const uint8_t *flash_record,
                                          size_t flash_record_size,
                                          const uint8_t *sdram_record,
                                          size_t sdram_record_size);

/* SDmScanConfig wire offsets (the vendor sends 0x78 bytes verbatim).  The
 * CScannerConfiguration setters operate on an object with an eight-byte
 * leading vptr; these are the corresponding offsets in the transmitted
 * SDmScanConfig, not the in-memory C++ object offsets. */
#define KODAK_I2000_CONFIG_FEED_TYPE_OFFSET 0x00u
#define KODAK_I2000_CONFIG_FEED_STYLE_OFFSET 0x01u
#define KODAK_I2000_CONFIG_PAGE_COUNT_OFFSET 0x02u
#define KODAK_I2000_CONFIG_TRANSPORT_TIMEOUT_OFFSET 0x04u
#define KODAK_I2000_CONFIG_MAX_PAGE_LENGTH_OFFSET 0x06u
#define KODAK_I2000_CONFIG_MULTIFEED_LENGTH_OFFSET 0x08u
#define KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET 0x0au
#define KODAK_I2000_CONFIG_FRONT_CAPTURE_LE_OFFSET 0x0du
#define KODAK_I2000_CONFIG_REAR_CAPTURE_LE_OFFSET 0x0fu
#define KODAK_I2000_CONFIG_TRAY_SENSOR_MODE_OFFSET 0x11u
#define KODAK_I2000_CONFIG_FRONT_CAPTURE_TE_OFFSET 0x12u
#define KODAK_I2000_CONFIG_REAR_CAPTURE_TE_OFFSET 0x14u
#define KODAK_I2000_CONFIG_PRINTER_OFFSET 0x16u
#define KODAK_I2000_CONFIG_PAUSE_ENABLE_OFFSET 0x18u
#define KODAK_I2000_CONFIG_MAX_PRINT_COUNT_OFFSET 0x19u
#define KODAK_I2000_CONFIG_HOLE_SIZE_OFFSET 0x1du
#define KODAK_I2000_CONFIG_SPEED_ID_OFFSET 0x20u
#define KODAK_I2000_CONFIG_DYNAMIC_SPEED_ID_OFFSET 0x21u
#define KODAK_I2000_CONFIG_THROTTLE_TIMEOUT_OFFSET 0x22u
#define KODAK_I2000_CONFIG_RAMP_ID_OFFSET 0x23u
#define KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET 0x24u
#define KODAK_I2000_CONFIG_STOP_OFFSET 0x25u
#define KODAK_I2000_CONFIG_NOTIFICATION_OFFSET 0x26u
#define KODAK_I2000_CONFIG_FIXED_LENGTH_OFFSET 0x28u
#define KODAK_I2000_CONFIG_CALIBRATION_LENGTH_OFFSET 0x2au
#define KODAK_I2000_CONFIG_CAPTURE_MODE_OFFSET 0x30u
#define KODAK_I2000_CONFIG_CAPTURE_STYLE_OFFSET 0x31u
#define KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET 0x32u
#define KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET 0x34u
#define KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET 0x36u
#define KODAK_I2000_CONFIG_CAPTURE_RATE_OFFSET 0x37u
#define KODAK_I2000_CONFIG_MULTIFEED_RESPONSE_OFFSET 0x38u
#define KODAK_I2000_CONFIG_PATCH_TOGGLE_MODE_OFFSET 0x40u
#define KODAK_I2000_CONFIG_FEEDER_ON_DISTANCE_OFFSET 0x50u

/* The SANE option indexes intentionally match the recovered vendor enums. */
#define KODAK_I2000_SOURCE_FRONT 0u
#define KODAK_I2000_SOURCE_REAR 1u
#define KODAK_I2000_SOURCE_DUPLEX 2u
#define KODAK_I2000_MODE_LINEART 0u
#define KODAK_I2000_MODE_GRAY 1u
#define KODAK_I2000_MODE_COLOR 2u

/* Build the normal 120-byte SDmScanConfig from the capture-proven front,
 * gray, 200-dpi record.  Mechanical calibration fields stay in feeder
 * encoder units; only fields changed by the vendor option setters vary. */
int kodak_i2000_build_normal_scan_configuration(unsigned source, unsigned mode,
                                                unsigned resolution,
                                                uint8_t *configuration,
                                                size_t configuration_size);
/* Page count zero preserves the capture-exact continuous feeder record.  A
 * nonzero count is retained as a protocol/testing seam. */
int kodak_i2000_build_normal_scan_configuration_for_pages(
    unsigned source, unsigned mode, unsigned resolution, uint16_t page_count,
    uint8_t *configuration, size_t configuration_size);
int kodak_i2000_build_baffle_scan_configuration(unsigned source, unsigned mode,
                                                unsigned resolution,
                                                uint8_t *configuration,
                                                size_t configuration_size);
/* Fixed duplex-gray 300-dpi record used only by the vendor's post-cold
 * CheckBackgroundColor() capture. */
int kodak_i2000_build_cold_background_scan_configuration(uint8_t *configuration,
                                                         size_t configuration_size);
unsigned kodak_i2000_sensor_resolution(unsigned resolution);
uint16_t kodak_i2000_scale_sensor_value(uint16_t value, unsigned sensor_resolution);
uint8_t kodak_i2000_ip_control(unsigned mode, unsigned sensor_resolution);
uint8_t kodak_i2000_image_footer_only(unsigned mode);
uint8_t kodak_i2000_resolution_footer(unsigned sensor_resolution);
uint16_t kodak_i2000_capture_value(unsigned source);

/* Camera register access seen in the vendor's SetupFPGA sequence. */
#define KODAK_I2000_CAMERA_REQUEST_TYPE_OUT 0x40u
#define KODAK_I2000_CAMERA_REQUEST_TYPE_IN 0xc0u
#define KODAK_I2000_CAMERA_REQUEST 0xe0u
#define KODAK_I2000_CAMERA_BYTE_LENGTH 0x0001u
#define KODAK_I2000_CAMERA_SELECTOR 0x001fu
#define KODAK_I2000_CAMERA_MODE_REGISTER 0x0002u
#define KODAK_I2000_CAMERA_GAIN_REGISTER 0x0005u
#define KODAK_I2000_CAMERA_DATA_FIRST_REGISTER 0x0000u
#define KODAK_I2000_CAMERA_DATA_LAST_REGISTER 0x0004u
#define KODAK_I2000_CAMERA_SELECTOR_RESET 0x00u
#define KODAK_I2000_CAMERA_SELECTOR_MODE 0x02u
#define KODAK_I2000_CAMERA_SELECTOR_DATA 0x03u
#define KODAK_I2000_CAMERA_MODE_VALUE 0x20u
#define KODAK_I2000_CAMERA_GAIN_VALUE 0x00u
#define KODAK_I2000_CAMERA_DATA_VALUE 0x88u

/* FPGA writes and the image endpoints for the i2400 front-simplex path. */
#define KODAK_I2000_FPGA_WRITE_REQUEST_TYPE 0x40u
#define KODAK_I2000_FPGA_SETUP_FIFO_VALUE 0xdc00u
#define KODAK_I2000_FPGA_REGISTER_DC00 0xdc00u
#define KODAK_I2000_FPGA_REGISTER_DC4C 0xdc4cu
#define KODAK_I2000_FPGA_REGISTER_DC4D 0xdc4du
#define KODAK_I2000_FPGA_REGISTER_DC00_VALUE 0x04u
#define KODAK_I2000_FPGA_REGISTER_DC4C_VALUE 0x01u
#define KODAK_I2000_FRONT_IMAGE_ENDPOINT 0x82u
#define KODAK_I2000_REAR_IMAGE_ENDPOINT 0x86u
#define KODAK_I2000_INTERRUPT_ENDPOINT 0x88u
#define KODAK_I2000_BULK_OUT_ENDPOINT 0x02u
#define KODAK_I2000_FRONT_CAPTURE_VALUE 0x0001u
#define KODAK_I2000_REAR_CAPTURE_VALUE 0x0002u
#define KODAK_I2000_DUPLEX_CAPTURE_VALUE 0x0003u

/* Eight-byte records delivered by the device's interrupt endpoint.  The
 * normal OCP button event carries the selected job/sequence number as a
 * little-endian word; it is not a physical-key bit mask. */
#define KODAK_I2000_INTERRUPT_EVENT_LENGTH 0x08u
#define KODAK_I2000_INTERRUPT_EVENT_TYPE_OFFSET 0x00u
#define KODAK_I2000_INTERRUPT_EVENT_DATA_OFFSET 0x02u
#define KODAK_I2000_INTERRUPT_EVENT_BUTTON_PRESS 0x20u
int kodak_i2000_decode_button_event(const uint8_t *event, size_t event_size,
                                    uint16_t *button_number);

/* The two-byte response observed after the no-paper feeder start. */
#define KODAK_I2000_SCAN_STATE_NO_DOCUMENT_0 0x0eu
#define KODAK_I2000_SCAN_STATE_NO_DOCUMENT_1 0x3cu

/* Confirmed by GetFirmwareVersions() and the embedded vendor data. */
#define KODAK_I2000_FIRMWARE_REQUEST_TYPE 0xc0u
#define KODAK_I2000_FIRMWARE_REQUEST 0x02u
#define KODAK_I2000_FIRMWARE_VALUE 0x0000u
#define KODAK_I2000_FIRMWARE_INDEX 0x0000u
#define KODAK_I2000_FIRMWARE_LENGTH 0x0038u

/* Confirmed by the named FpgaBase read wrappers; all are read-only. */
#define KODAK_I2000_FPGA_REQUEST_TYPE 0xc0u
#define KODAK_I2000_FPGA_REQUEST 0xa3u
#define KODAK_I2000_FPGA_INDEX 0x0000u
#define KODAK_I2000_FPGA_BYTE_LENGTH 0x0001u
#define KODAK_I2000_FPGA_WORD_LENGTH 0x0004u
#define KODAK_I2000_FRONT_DOC_ID_VALUE 0xda27u
#define KODAK_I2000_REAR_DOC_ID_VALUE 0xdb27u
#define KODAK_I2000_PRINTER_CONTROL_VALUE 0xde3du
#define KODAK_I2000_PRINTER_STATUS_VALUE 0xde3eu
#define KODAK_I2000_FEEDER_CLUTCH_VALUE 0xde3bu
#define KODAK_I2000_COMMON_GENERAL_CONTROL_VALUE 0xd901u
#define KODAK_I2000_COMMON_IP_CONTROL_VALUE 0xd900u
#define KODAK_I2000_FPGA_REGISTER_D900 0xd900u
#define KODAK_I2000_FPGA_REGISTER_D901 0xd901u
#define KODAK_I2000_FPGA_REGISTER_D800 0xd800u
#define KODAK_I2000_FPGA_REGISTER_D908 0xd908u
#define KODAK_I2000_FPGA_REGISTER_D903 0xd903u
#define KODAK_I2000_FPGA_REGISTER_D904 0xd904u
#define KODAK_I2000_FPGA_REGISTER_D905 0xd905u
#define KODAK_I2000_FPGA_REGISTER_D906 0xd906u
#define KODAK_I2000_FPGA_REGISTER_D907 0xd907u
#define KODAK_I2000_FPGA_REGISTER_D920 0xd920u
#define KODAK_I2000_FPGA_REGISTER_DA00 0xda00u
#define KODAK_I2000_FPGA_REGISTER_DA01 0xda01u
#define KODAK_I2000_FPGA_REGISTER_DA02 0xda02u
#define KODAK_I2000_FPGA_REGISTER_DA03 0xda03u
#define KODAK_I2000_FPGA_REGISTER_DA04 0xda04u
#define KODAK_I2000_FPGA_REGISTER_DA05 0xda05u
#define KODAK_I2000_FPGA_REGISTER_DA06 0xda06u
#define KODAK_I2000_FPGA_REGISTER_DA07 0xda07u
#define KODAK_I2000_FPGA_REGISTER_DA08 0xda08u
#define KODAK_I2000_FPGA_REGISTER_DA09 0xda09u
#define KODAK_I2000_FPGA_REGISTER_DA0A 0xda0au
#define KODAK_I2000_FPGA_REGISTER_DA0B 0xda0bu
#define KODAK_I2000_FPGA_REGISTER_DA0C 0xda0cu
#define KODAK_I2000_FPGA_REGISTER_DA0D 0xda0du
#define KODAK_I2000_FPGA_REGISTER_DA0E 0xda0eu
#define KODAK_I2000_FPGA_REGISTER_DA0F 0xda0fu
#define KODAK_I2000_FPGA_REGISTER_DA11 0xda11u
#define KODAK_I2000_FPGA_REGISTER_DA12 0xda12u
#define KODAK_I2000_FPGA_REGISTER_DA13 0xda13u
#define KODAK_I2000_FPGA_REGISTER_DA14 0xda14u
#define KODAK_I2000_FPGA_REGISTER_DA15 0xda15u
#define KODAK_I2000_FPGA_REGISTER_DA16 0xda16u
#define KODAK_I2000_FPGA_REGISTER_DA19 0xda19u
#define KODAK_I2000_FPGA_REGISTER_DA1A 0xda1au
#define KODAK_I2000_FPGA_REGISTER_DA25 0xda25u
#define KODAK_I2000_FPGA_REGISTER_DA2B 0xda2bu
#define KODAK_I2000_FPGA_REGISTER_DA2C 0xda2cu
#define KODAK_I2000_FPGA_REGISTER_DA2D 0xda2du
#define KODAK_I2000_FPGA_REGISTER_DA2E 0xda2eu
#define KODAK_I2000_FPGA_REGISTER_DA2F 0xda2fu
#define KODAK_I2000_FPGA_REGISTER_DB00 0xdb00u
#define KODAK_I2000_FPGA_REGISTER_DB01 0xdb01u
#define KODAK_I2000_FPGA_REGISTER_DB02 0xdb02u
#define KODAK_I2000_FPGA_REGISTER_DB03 0xdb03u
#define KODAK_I2000_FPGA_REGISTER_DB04 0xdb04u
#define KODAK_I2000_FPGA_REGISTER_DB05 0xdb05u
#define KODAK_I2000_FPGA_REGISTER_DB06 0xdb06u
#define KODAK_I2000_FPGA_REGISTER_DB07 0xdb07u
#define KODAK_I2000_FPGA_REGISTER_DB08 0xdb08u
#define KODAK_I2000_FPGA_REGISTER_DB09 0xdb09u
#define KODAK_I2000_FPGA_REGISTER_DB0A 0xdb0au
#define KODAK_I2000_FPGA_REGISTER_DB0B 0xdb0bu
#define KODAK_I2000_FPGA_REGISTER_DB0C 0xdb0cu
#define KODAK_I2000_FPGA_REGISTER_DB0D 0xdb0du
#define KODAK_I2000_FPGA_REGISTER_DB0E 0xdb0eu
#define KODAK_I2000_FPGA_REGISTER_DB0F 0xdb0fu
#define KODAK_I2000_FPGA_REGISTER_DB11 0xdb11u
#define KODAK_I2000_FPGA_REGISTER_DB12 0xdb12u
#define KODAK_I2000_FPGA_REGISTER_DB13 0xdb13u
#define KODAK_I2000_FPGA_REGISTER_DB14 0xdb14u
#define KODAK_I2000_FPGA_REGISTER_DB15 0xdb15u
#define KODAK_I2000_FPGA_REGISTER_DB16 0xdb16u
#define KODAK_I2000_FPGA_REGISTER_DB19 0xdb19u
#define KODAK_I2000_FPGA_REGISTER_DB1A 0xdb1au
#define KODAK_I2000_FPGA_REGISTER_DB25 0xdb25u
#define KODAK_I2000_FPGA_REGISTER_DB2B 0xdb2bu
#define KODAK_I2000_FPGA_REGISTER_DB2C 0xdb2cu
#define KODAK_I2000_FPGA_REGISTER_DB2D 0xdb2du
#define KODAK_I2000_FPGA_REGISTER_DB2E 0xdb2eu
#define KODAK_I2000_FPGA_REGISTER_DB2F 0xdb2fu
#define KODAK_I2000_FPGA_REGISTER_DD0A 0xdd0au
#define KODAK_I2000_FPGA_REGISTER_DD0B 0xdd0bu
#define KODAK_I2000_FPGA_REGISTER_DE37 0xde37u
#define KODAK_I2000_NORMALIZATION_DATA_START 0x2au
#define KODAK_I2000_FLASH_TO_SDRAM_TARGET 0x08u
#define KODAK_I2000_FLASH_BUSY_MASK 0x01u
#define KODAK_I2000_FLASH_PIRANHA2_MASK 0x02u
#define KODAK_I2000_FRONT_IP_CONTROL_VALUE 0xda01u
#define KODAK_I2000_REAR_IP_CONTROL_VALUE 0xdb01u
#define KODAK_I2000_REAR_BINARY_THRESHOLD_VALUE 0xdb26u
#define KODAK_I2000_REAR_IMAGE_FOOTER_VALUE 0xdb25u

/* StartCapture(unsigned short), used once the feeder reports a document. */
#define KODAK_I2000_START_REQUEST_TYPE 0x40u
#define KODAK_I2000_START_REQUEST 0x17u
#define KODAK_I2000_START_INDEX 0x0000u
#define KODAK_I2000_START_LENGTH 0x0000u

/* The paper-backed trace contains one 51,824-byte baffle pre-scan before
 * the normal page stream. */
/* Gray baffle data ends with a 2,672-byte short transfer at 51,824 bytes.
 * The live 400-dpi color path instead ends after three full 16-KiB transfers
 * (49,152 bytes) and reports endpoint completion on the next read. */
#define KODAK_I2000_BAFFLE_CAPTURE_MIN_LENGTH 49152u
#define KODAK_I2000_BAFFLE_CAPTURE_MAX_LENGTH (1024u * 1024u)

#endif /* KODAK_I2000_PROTOCOL_H */
