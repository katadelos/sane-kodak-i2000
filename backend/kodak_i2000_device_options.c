/* SANE descriptors and cached values for scanner diagnostics. */
#include "sane/config.h"
#include "sane/saneopts.h"
#include "kodak_i2000_options.h"
#include <stdio.h>
#include <string.h>

#define KODAK_I2000_END_OF_LINE_HEX_SIZE (KODAK_I2000_END_OF_LINE_LENGTH * 2U + 1U)
#define KODAK_I2000_DIAGNOSTIC_HEX_SIZE (KODAK_I2000_DIAGNOSTIC_LENGTH * 2U + 1U)
#define KODAK_I2000_CONSUMABLE_METERS_HEX_SIZE                                         \
  (KODAK_I2000_CONSUMABLE_METERS_LENGTH * 2U + 1U)
#define KODAK_I2000_METERS_HEX_SIZE (KODAK_I2000_METERS_LENGTH * 2U + 1U)
#define KODAK_I2000_SESSION_HEX_SIZE (KODAK_I2000_SESSION_LENGTH * 2U + 1U)
#define KODAK_I2000_FIRMWARE_HEX_SIZE (KODAK_I2000_FIRMWARE_LENGTH * 2U + 1U)

static void
set_device_option(SANE_Option_Descriptor *option, SANE_Value_Type type, SANE_Int size,
                  const char *name, const char *title, const char *description)
{
  *option = (SANE_Option_Descriptor){
      .name = name,
      .title = title,
      .desc = description,
      .type = type,
      .unit = SANE_UNIT_NONE,
      .size = size,
      .cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED,
      .constraint_type = SANE_CONSTRAINT_NONE,
  };
}

void
init_device_options(kodak_i2000_handle *scanner)
{
  SANE_Option_Descriptor *option;
  option = &scanner->options[KODAK_I2000_OPT_DEVICE_GROUP];
  option->name = "device-info";
  option->title = SANE_I18N("Device information");
  option->desc = SANE_I18N("Read-only information reported by the scanner");
  option->type = SANE_TYPE_GROUP;
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;

  option = &scanner->options[KODAK_I2000_OPT_REFRESH_INFO];
  option->name = "refresh-device-info";
  option->title = SANE_I18N("Refresh device information");
  option->desc = SANE_I18N("Re-read the scanner status and read-only data");
  option->type = SANE_TYPE_BUTTON;
  option->size = 0;
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  set_device_option(&scanner->options[KODAK_I2000_OPT_FIRMWARE_ID], SANE_TYPE_INT,
                    sizeof(SANE_Word), "firmware-id", SANE_I18N("Firmware ID"),
                    SANE_I18N("Firmware identifier from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_BOARD_TYPE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "board-type", SANE_I18N("Board type"),
                    SANE_I18N("Board type from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_POWER_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "power-state", SANE_I18N("Power state"),
                    SANE_I18N("Power state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_BUFFER_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "buffer-state", SANE_I18N("Buffer state"),
                    SANE_I18N("Buffer state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_TRANSPORT_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "transport-state", SANE_I18N("Transport state"),
                    SANE_I18N("Transport state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_TRAY_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "tray-state", SANE_I18N("Tray state"),
                    SANE_I18N("Tray state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_UDDS_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "udds-state", SANE_I18N("UDDS state"),
                    SANE_I18N("UDDS state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_LAMP_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "lamp-state", SANE_I18N("Lamp state"),
                    SANE_I18N("Lamp state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_INTERLOCK_STATE], SANE_TYPE_INT,
                    sizeof(SANE_Word), "interlock-state", SANE_I18N("Interlock state"),
                    SANE_I18N("Interlock state from scanner status"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_FRONT_DOC_ID], SANE_TYPE_INT,
                    sizeof(SANE_Word), "front-document-id",
                    SANE_I18N("Front document ID"),
                    SANE_I18N("Front FPGA document identifier"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_REAR_DOC_ID], SANE_TYPE_INT,
                    sizeof(SANE_Word), "rear-document-id",
                    SANE_I18N("Rear document ID"),
                    SANE_I18N("Rear FPGA document identifier"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_PRINTER_CONTROL], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-printer-control",
                    SANE_I18N("FPGA printer control"),
                    SANE_I18N("Read-only printer-control FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_PRINTER_STATUS], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-printer-status",
                    SANE_I18N("FPGA printer status"),
                    SANE_I18N("Read-only printer-status FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_FEEDER_CLUTCH], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-feeder-clutch",
                    SANE_I18N("FPGA feeder clutch"),
                    SANE_I18N("Read-only feeder-clutch FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_COMMON_GENERAL_CONTROL],
                    SANE_TYPE_INT, sizeof(SANE_Word), "fpga-common-general-control",
                    SANE_I18N("FPGA common general control"),
                    SANE_I18N("Read-only common-general FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_COMMON_IP_CONTROL], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-common-ip-control",
                    SANE_I18N("FPGA common image control"),
                    SANE_I18N("Read-only common-image FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_FRONT_IP_CONTROL], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-front-ip-control",
                    SANE_I18N("FPGA front image control"),
                    SANE_I18N("Read-only front-image FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_REAR_IP_CONTROL], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-rear-ip-control",
                    SANE_I18N("FPGA rear image control"),
                    SANE_I18N("Read-only rear-image FPGA register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_REAR_BINARY_THRESHOLD],
                    SANE_TYPE_INT, sizeof(SANE_Word), "fpga-rear-binary-threshold",
                    SANE_I18N("FPGA rear binary threshold"),
                    SANE_I18N("Read-only rear binary-threshold register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_REAR_IMAGE_FOOTER], SANE_TYPE_INT,
                    sizeof(SANE_Word), "fpga-rear-image-footer",
                    SANE_I18N("FPGA rear image footer"),
                    SANE_I18N("Read-only rear image-footer register"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_ENERGY_STAR], SANE_TYPE_INT,
                    sizeof(SANE_Word), "energy-star", SANE_I18N("Energy Star state"),
                    SANE_I18N("Energy Star flag reported by the scanner"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_LOW_POWER_TIMEOUT], SANE_TYPE_INT,
                    sizeof(SANE_Word), "low-power-timeout",
                    SANE_I18N("Low-power timeout"),
                    SANE_I18N("Low-power timeout reported by the scanner"));

  set_device_option(&scanner->options[KODAK_I2000_OPT_FRONT_CAMERA_EOL], SANE_TYPE_BOOL,
                    sizeof(SANE_Word), "front-camera-eol",
                    SANE_I18N("Front camera EOL"),
                    SANE_I18N("Front end-of-line camera flag"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_REAR_CAMERA_EOL], SANE_TYPE_BOOL,
                    sizeof(SANE_Word), "rear-camera-eol", SANE_I18N("Rear camera EOL"),
                    SANE_I18N("Rear end-of-line camera flag"));

  option = &scanner->options[KODAK_I2000_OPT_STATUS_VERSION];
  option->name = "status-version";
  option->title = SANE_I18N("Status version");
  option->desc = SANE_I18N("Status version word reported by the scanner");
  option->type = SANE_TYPE_STRING;
  option->size = 11;
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_FIRMWARE_VERSIONS];
  option->name = "firmware-versions";
  option->title = SANE_I18N("Firmware versions");
  option->desc = SANE_I18N("Raw firmware-version block reported by the scanner");
  option->type = SANE_TYPE_STRING;
  option->size = KODAK_I2000_FIRMWARE_HEX_SIZE;
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  set_device_option(&scanner->options[KODAK_I2000_OPT_SESSION_INFO], SANE_TYPE_STRING,
                    KODAK_I2000_SESSION_HEX_SIZE, "session-info",
                    SANE_I18N("Session information"),
                    SANE_I18N("Raw session-information block from scanner"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_METERS], SANE_TYPE_STRING,
                    KODAK_I2000_METERS_HEX_SIZE, "meters", SANE_I18N("Meters"),
                    SANE_I18N("Raw meter block reported by the scanner"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_CONSUMABLE_METERS],
                    SANE_TYPE_STRING, KODAK_I2000_CONSUMABLE_METERS_HEX_SIZE,
                    "consumable-meters", SANE_I18N("Consumable meters"),
                    SANE_I18N("Raw consumable-meter block from scanner"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_DIAGNOSTIC_RESULTS],
                    SANE_TYPE_STRING, KODAK_I2000_DIAGNOSTIC_HEX_SIZE,
                    "diagnostic-results", SANE_I18N("Diagnostic results"),
                    SANE_I18N("Raw diagnostic-result block from scanner"));
  set_device_option(&scanner->options[KODAK_I2000_OPT_END_OF_LINE_DATA],
                    SANE_TYPE_STRING, KODAK_I2000_END_OF_LINE_HEX_SIZE,
                    "end-of-line-data", SANE_I18N("End-of-line data"),
                    SANE_I18N("Raw end-of-line block reported by scanner"));

  option = &scanner->options[KODAK_I2000_OPT_CONSUMABLE_METERS_VERSION_SUPPORTED];
  option->name = "consumable-meters-version-supported";
  option->title = SANE_I18N("Consumable-meter version supported");
  option->desc = SANE_I18N("Whether the scanner's consumable record is version 1");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;
}

static SANE_Status
copy_hex_value(SANE_String value, SANE_Int size, const SANE_Byte *source,
               size_t source_length)
{
  static const char hex[] = "0123456789abcdef";
  size_t index;

  if (value == NULL || size <= 0 || source == NULL ||
      source_length > ((size_t)size - 1U) / 2U)
    return SANE_STATUS_INVAL;
  for (index = 0U; index < source_length; ++index) {
    value[index * 2U] = hex[source[index] >> 4];
    value[index * 2U + 1U] = hex[source[index] & 0x0fU];
  }
  value[source_length * 2U] = '\0';
  return SANE_STATUS_GOOD;
}

SANE_Status
get_device_option(kodak_i2000_handle *scanner, SANE_Int option, void *value)
{
  switch (option) {
  case KODAK_I2000_OPT_FIRMWARE_ID:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_FIRMWARE_ID_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_STATUS_VERSION:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    {
      char version[11];
      unsigned version_word =
          (unsigned)scanner->status[KODAK_I2000_STATUS_VERSION_OFFSET] |
          ((unsigned)scanner->status[KODAK_I2000_STATUS_VERSION_OFFSET + 1U] << 8) |
          ((unsigned)scanner->status[KODAK_I2000_STATUS_VERSION_OFFSET + 2U] << 16) |
          ((unsigned)scanner->status[KODAK_I2000_STATUS_VERSION_OFFSET + 3U] << 24);
      (void)snprintf(version, sizeof(version), "0x%08x", version_word);
      return copy_string_value((SANE_String)value, scanner->options[option].size,
                               version);
    }
  case KODAK_I2000_OPT_BOARD_TYPE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_BOARD_TYPE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_POWER_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_POWER_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BUFFER_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_BUFFER_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_TRANSPORT_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_TRAY_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_UDDS_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_UDDS_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_LAMP_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value =
        (SANE_Word)scanner->status[KODAK_I2000_STATUS_LAMP_STATE_OFFSET] |
        (SANE_Word)((SANE_Word)
                        scanner->status[KODAK_I2000_STATUS_LAMP_STATE_OFFSET + 1U]
                    << 8);
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_INTERLOCK_STATE:
    if (!scanner->status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->status[KODAK_I2000_STATUS_INTERLOCK_STATE_OFFSET];
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_FRONT_DOC_ID:
    if (!scanner->front_doc_id_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = (SANE_Word)scanner->front_doc_id;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_REAR_DOC_ID:
    if (!scanner->rear_doc_id_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = (SANE_Word)scanner->rear_doc_id;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_PRINTER_CONTROL:
    if (!scanner->printer_control_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->printer_control;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_PRINTER_STATUS:
    if (!scanner->printer_status_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->printer_status;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_FEEDER_CLUTCH:
    if (!scanner->feeder_clutch_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->feeder_clutch;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_COMMON_GENERAL_CONTROL:
    if (!scanner->common_general_control_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->common_general_control;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_COMMON_IP_CONTROL:
    if (!scanner->common_ip_control_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->common_ip_control;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_FRONT_IP_CONTROL:
    if (!scanner->front_ip_control_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->front_ip_control;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_REAR_IP_CONTROL:
    if (!scanner->rear_ip_control_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->rear_ip_control;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_REAR_BINARY_THRESHOLD:
    if (!scanner->rear_binary_threshold_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->rear_binary_threshold;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_REAR_IMAGE_FOOTER:
    if (!scanner->rear_image_footer_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->rear_image_footer;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_ENERGY_STAR:
    if (!scanner->energy_star_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->energy_star;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_LOW_POWER_TIMEOUT:
    if (!scanner->low_power_timeout_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Word *)value = scanner->low_power_timeout;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_FRONT_CAMERA_EOL:
    if (!scanner->end_of_line_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Bool *)value =
        (scanner->end_of_line[KODAK_I2000_END_OF_LINE_CAMERA_FLAGS_OFFSET] &
         KODAK_I2000_END_OF_LINE_FRONT_CAMERA_MASK) != 0U;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_REAR_CAMERA_EOL:
    if (!scanner->end_of_line_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Bool *)value =
        (scanner->end_of_line[KODAK_I2000_END_OF_LINE_CAMERA_FLAGS_OFFSET] &
         KODAK_I2000_END_OF_LINE_REAR_CAMERA_MASK) != 0U;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_FIRMWARE_VERSIONS:
    if (!scanner->firmware_versions_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->firmware_versions,
                          sizeof(scanner->firmware_versions));
  case KODAK_I2000_OPT_SESSION_INFO:
    if (!scanner->session_info_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->session_info, sizeof(scanner->session_info));
  case KODAK_I2000_OPT_METERS:
    if (!scanner->meters_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->meters, sizeof(scanner->meters));
  case KODAK_I2000_OPT_CONSUMABLE_METERS:
    if (!scanner->consumable_meters_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->consumable_meters,
                          sizeof(scanner->consumable_meters));
  case KODAK_I2000_OPT_CONSUMABLE_METERS_VERSION_SUPPORTED:
    if (!scanner->consumable_meters_valid)
      return SANE_STATUS_IO_ERROR;
    *(SANE_Bool *)value = scanner->consumable_meters_version_supported;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_DIAGNOSTIC_RESULTS:
    if (!scanner->diagnostic_results_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->diagnostic_results,
                          sizeof(scanner->diagnostic_results));
  case KODAK_I2000_OPT_END_OF_LINE_DATA:
    if (!scanner->end_of_line_valid)
      return SANE_STATUS_IO_ERROR;
    return copy_hex_value((SANE_String)value, scanner->options[option].size,
                          scanner->end_of_line, sizeof(scanner->end_of_line));
  default:
    return SANE_STATUS_INVAL;
  }
}
