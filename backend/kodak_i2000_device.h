#ifndef KODAK_I2000_DEVICE_H
#define KODAK_I2000_DEVICE_H

#include <signal.h>
#include "kodak_i2000.h"
#include "kodak_i2000_protocol.h"

struct kodak_i2000_device {
  struct kodak_i2000_device *next;
  SANE_Device sane;
  char *name;
  char *model;
  uint16_t vendor_id;
  uint16_t product_id;
  SANE_Bool missing;
};

struct kodak_i2000_handle {
  kodak_i2000_device *device;
  SANE_Int usb;
  SANE_Option_Descriptor options[KODAK_I2000_OPT_NUM_OPTIONS];
  SANE_Range resolution_range;
  SANE_Range tl_x_range;
  SANE_Range tl_y_range;
  SANE_Range br_x_range;
  SANE_Range br_y_range;
  SANE_Fixed tl_x;
  SANE_Fixed tl_y;
  SANE_Fixed br_x;
  SANE_Fixed br_y;
  SANE_Bool auto_crop;
  SANE_Bool deskew;
  SANE_Int brightness;
  SANE_Int contrast;
  SANE_Int sharpen;
  SANE_Int rotation;
  SANE_Int threshold;
  SANE_Bool blank_detection;
  SANE_Int blank_threshold;
  SANE_Bool blank_page_detected;
  SANE_Bool pending_blank_page_detected;
  SANE_Bool scan_button_pending;
  SANE_Bool interrupt_events_enabled;
  SANE_Word ocp_button_number;
  SANE_Int resolution;
  SANE_Int mode;
  SANE_Int source;
  SANE_Bool scanning;
  SANE_Bool continuous_job_active;
  volatile sig_atomic_t acquiring;
  volatile sig_atomic_t cancel_requested;
  SANE_Byte *image_data;
  size_t image_size;
  size_t image_offset;
  size_t image_remaining;
  SANE_Bool image_parameters_valid;
  SANE_Byte *pending_image_data;
  size_t pending_image_size;
  SANE_Parameters pending_parameters;
  SANE_Bool pending_image_valid;
  SANE_Bool status_valid;
  SANE_Byte status[KODAK_I2000_STATUS_LENGTH];
  SANE_Bool firmware_versions_valid;
  SANE_Byte firmware_versions[KODAK_I2000_FIRMWARE_LENGTH];
  SANE_Bool session_info_valid;
  SANE_Byte session_info[KODAK_I2000_SESSION_LENGTH];
  SANE_Bool energy_star_valid;
  SANE_Byte energy_star;
  SANE_Bool consumable_meters_valid;
  SANE_Bool consumable_meters_version_supported;
  SANE_Byte consumable_meters[KODAK_I2000_CONSUMABLE_METERS_LENGTH];
  SANE_Bool meters_valid;
  SANE_Byte meters[KODAK_I2000_METERS_LENGTH];
  SANE_Bool diagnostic_results_valid;
  SANE_Byte diagnostic_results[KODAK_I2000_DIAGNOSTIC_LENGTH];
  SANE_Bool low_power_timeout_valid;
  SANE_Byte low_power_timeout;
  SANE_Bool end_of_line_valid;
  SANE_Byte end_of_line[KODAK_I2000_END_OF_LINE_LENGTH];
  SANE_Bool front_doc_id_valid;
  uint32_t front_doc_id;
  SANE_Bool rear_doc_id_valid;
  uint32_t rear_doc_id;
  SANE_Bool printer_control_valid;
  SANE_Byte printer_control;
  SANE_Bool printer_status_valid;
  SANE_Byte printer_status;
  SANE_Bool feeder_clutch_valid;
  SANE_Byte feeder_clutch;
  SANE_Bool common_general_control_valid;
  SANE_Byte common_general_control;
  SANE_Bool common_ip_control_valid;
  SANE_Byte common_ip_control;
  SANE_Bool front_ip_control_valid;
  SANE_Byte front_ip_control;
  SANE_Bool rear_ip_control_valid;
  SANE_Byte rear_ip_control;
  SANE_Bool rear_binary_threshold_valid;
  SANE_Byte rear_binary_threshold;
  SANE_Bool rear_image_footer_valid;
  SANE_Byte rear_image_footer;
  SANE_Parameters parameters;
};

#define KODAK_I2000_DEFAULT_WIDTH_MM 210.0
#define KODAK_I2000_DEFAULT_HEIGHT_MM 297.0
#define KODAK_I2000_MAX_IMAGE_BYTES ((size_t)256U * 1024U * 1024U)
#define KODAK_I2000_COMMAND_TIMEOUT_MS 3000
#define KODAK_I2000_SCAN_TIMEOUT_MS 1000
#define KODAK_I2000_SCAN_IDLE_RETRIES 30U
#define KODAK_I2000_SUPPORTED_CAPTURE_PRODUCT 0x601cU

#endif
