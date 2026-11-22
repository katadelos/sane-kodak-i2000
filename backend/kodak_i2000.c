/* SANE device discovery, handle lifetime, and scan entry points. */
#include "sane/config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BACKEND_NAME kodak_i2000
/* Share the SANE debug printer across backend translation units. */
__attribute__((visibility("hidden"))) void
sanei_debug_kodak_i2000_call(int level, const char *format, ...);
#define DEBUG_NOT_STATIC
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"
#include "sane/sanei_config.h"
#include "sane/saneopts.h"

#include "kodak_i2000_options.h"
#include "kodak_i2000_transport.h"
#include "kodak_i2000_boot.h"
#include "kodak_i2000_scan.h"
#include "kodak_i2000_capture.h"

#define KODAK_I2000_CONTINUOUS_EMPTY_RETRIES 4U
#define KODAK_I2000_BUILD 0
#define KODAK_I2000_CONFIG_FILE "kodak_i2000.conf"

static kodak_i2000_device *device_list;
static const SANE_Device **device_array;
static size_t device_count;
static SANE_Bool backend_initialized;
static char *firmware_source;

static char *
duplicate_string(const char *text)
{
  size_t length;
  char *copy;

  if (text == NULL)
    return NULL;
  length = strlen(text);
  copy = (char *)malloc(length + 1U);
  if (copy != NULL)
    memcpy(copy, text, length + 1U);
  return copy;
}

static kodak_i2000_device *
find_device(const char *name)
{
  kodak_i2000_device *device;

  for (device = device_list; device != NULL; device = device->next) {
    if (device->name != NULL && name != NULL && strcmp(device->name, name) == 0)
      return device;
  }
  return NULL;
}

static SANE_Status
attach_device(SANE_String_Const name, uint16_t vendor_id, uint16_t product_id)
{
  const kodak_i2000_model *model;
  kodak_i2000_device *device;

  if (name == NULL)
    return SANE_STATUS_INVAL;
  model = kodak_i2000_model_lookup(vendor_id, product_id);
  if (model == NULL)
    return SANE_STATUS_UNSUPPORTED;

  device = find_device(name);
  if (device != NULL) {
    device->vendor_id = vendor_id;
    device->product_id = product_id;
    device->missing = SANE_FALSE;
    return SANE_STATUS_GOOD;
  }

  device = (kodak_i2000_device *)calloc(1U, sizeof(*device));
  if (device == NULL)
    return SANE_STATUS_NO_MEM;
  device->name = duplicate_string(name);
  device->model = duplicate_string(model->name);
  if (device->name == NULL || device->model == NULL) {
    free(device->name);
    free(device->model);
    free(device);
    return SANE_STATUS_NO_MEM;
  }

  device->vendor_id = vendor_id;
  device->product_id = product_id;
  device->missing = SANE_FALSE;
  device->sane.name = device->name;
  device->sane.vendor = "Kodak Alaris";
  device->sane.model = device->model;
  device->sane.type = SANE_I18N("sheet-fed scanner");
  device->next = device_list;
  device_list = device;
  DBG(5, "%s: attached %s (0x%04x:0x%04x)\n", __func__, name, (unsigned)vendor_id,
      (unsigned)product_id);
  return SANE_STATUS_GOOD;
}

static SANE_Status
attach_one_usb(SANE_String_Const name)
{
  SANE_Word vendor = 0;
  SANE_Word product = 0;
  SANE_Status status;

  status = sanei_usb_get_vendor_product_byname(name, &vendor, &product);
  if (status != SANE_STATUS_GOOD)
    return status;
  return attach_device(name, (uint16_t)vendor, (uint16_t)product);
}

static SANE_Status
configure_firmware_source(const char *line)
{
  static const char keyword[] = "firmware-source";
  const char *position;
  char *path = NULL;

  if (strncmp(line, keyword, sizeof(keyword) - 1U) != 0 ||
      !(line[sizeof(keyword) - 1U] == '\0' || line[sizeof(keyword) - 1U] == ' ' ||
        line[sizeof(keyword) - 1U] == '\t'))
    return SANE_STATUS_UNSUPPORTED;
  position = sanei_config_get_string(line + sizeof(keyword) - 1U, &path);
  if (position != NULL)
    position = sanei_config_skip_whitespace(position);
  if (path == NULL || path[0] == '\0' || position == NULL || position[0] != '\0') {
    DBG(1, "%s: firmware-source requires exactly one path\n", __func__);
    free(path);
    return SANE_STATUS_INVAL;
  }
  free(firmware_source);
  firmware_source = path;
  DBG(5, "%s: configured an external i2400 firmware source\n", __func__);
  return SANE_STATUS_GOOD;
}

static SANE_Status
attach_one_config(SANEI_Config *config, const char *line, void *data)
{
  int vendor;
  int product;
  size_t count = 0U;
  size_t i;
  const kodak_i2000_model *models;

  (void)config;
  (void)data;
  if (line == NULL)
    return SANE_STATUS_INVAL;

  {
    SANE_Status firmware_status = configure_firmware_source(line);
    if (firmware_status != SANE_STATUS_UNSUPPORTED)
      return firmware_status;
  }

  if (sscanf(line, "usb %i %i", &vendor, &product) == 2) {
    if (vendor < 0 || vendor > UINT16_MAX || product < 0 || product > UINT16_MAX ||
        kodak_i2000_model_lookup((uint16_t)vendor, (uint16_t)product) == NULL)
      return SANE_STATUS_INVAL;
    sanei_usb_attach_matching_devices(line, attach_one_usb);
    return SANE_STATUS_GOOD;
  }

  if (strcmp(line, "usb") != 0)
    return SANE_STATUS_GOOD;
  models = kodak_i2000_models(&count);
  for (i = 0U; i < count; ++i) {
    (void)sanei_usb_find_devices((SANE_Int)models[i].vendor_id,
                                 (SANE_Int)models[i].product_id, attach_one_usb);
  }
  return SANE_STATUS_GOOD;
}

static void
remove_missing_devices(void)
{
  kodak_i2000_device *device;
  kodak_i2000_device *previous = NULL;

  for (device = device_list; device != NULL;) {
    kodak_i2000_device *next = device->next;
    if (device->missing) {
      if (previous == NULL)
        device_list = next;
      else
        previous->next = next;
      free(device->name);
      free(device->model);
      free(device);
    } else {
      previous = device;
    }
    device = next;
  }
}

static void
probe_devices(void)
{
  kodak_i2000_device *device;
  SANE_Status status;

  for (device = device_list; device != NULL; device = device->next)
    device->missing = SANE_TRUE;

  free(firmware_source);
  firmware_source = NULL;

  status =
      sanei_configure_attach(KODAK_I2000_CONFIG_FILE, NULL, attach_one_config, NULL);
  if (status != SANE_STATUS_GOOD) {
    size_t count = 0U;
    size_t i;
    const kodak_i2000_model *models = kodak_i2000_models(&count);
    DBG(3, "%s: config probe failed: %s; using built-in IDs\n", __func__,
        sane_strstatus(status));
    for (i = 0U; i < count; ++i) {
      (void)sanei_usb_find_devices((SANE_Int)models[i].vendor_id,
                                   (SANE_Int)models[i].product_id, attach_one_usb);
    }
  }
  remove_missing_devices();
}

static void
free_device_list(void)
{
  kodak_i2000_device *device = device_list;

  while (device != NULL) {
    kodak_i2000_device *next = device->next;
    free(device->name);
    free(device->model);
    free(device);
    device = next;
  }
  device_list = NULL;
  free(device_array);
  device_array = NULL;
  device_count = 0U;
}

static SANE_Status
rebuild_device_array(void)
{
  kodak_i2000_device *device;
  size_t count = 0U;
  size_t index = 0U;

  free(device_array);
  device_array = NULL;
  for (device = device_list; device != NULL; device = device->next)
    ++count;
  device_array = (const SANE_Device **)calloc(count + 1U, sizeof(*device_array));
  if (device_array == NULL && count != 0U)
    return SANE_STATUS_NO_MEM;
  for (device = device_list; device != NULL; device = device->next)
    device_array[index++] = &device->sane;
  device_array[index] = NULL;
  device_count = count;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
{
  (void)authorize;
  DBG_INIT();
  if (version_code != NULL)
    *version_code =
        SANE_VERSION_CODE(SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, KODAK_I2000_BUILD);
  if (!backend_initialized) {
    sanei_usb_init();
    backend_initialized = SANE_TRUE;
  }
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_devices(const SANE_Device ***devices, SANE_Bool local_only)
{
  (void)local_only;
  if (devices == NULL || !backend_initialized)
    return SANE_STATUS_INVAL;
  probe_devices();
  if (rebuild_device_array() != SANE_STATUS_GOOD)
    return SANE_STATUS_NO_MEM;
  *devices = device_array;
  DBG(5, "%s: found %lu scanner(s)\n", __func__, (unsigned long)device_count);
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open(SANE_String_Const name, SANE_Handle *handle)
{
  kodak_i2000_device *device;
  kodak_i2000_handle *scanner;
  SANE_Status status;

  if (name == NULL || handle == NULL)
    return SANE_STATUS_INVAL;
  device = find_device(name);
  if (device == NULL) {
    /* The SANE API permits sane_open() without a prior device enumeration. */
    probe_devices();
    device = find_device(name);
  }
  if (device == NULL)
    return SANE_STATUS_INVAL;
  scanner = (kodak_i2000_handle *)calloc(1U, sizeof(*scanner));
  if (scanner == NULL)
    return SANE_STATUS_NO_MEM;
  scanner->device = device;
  scanner->usb = -1;
  status = init_options(scanner);
  if (status == SANE_STATUS_GOOD)
    status = sanei_usb_open(device->name, &scanner->usb);
  if (status == SANE_STATUS_GOOD) {
    sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
    /* GetStatus() is safe immediately after a power cycle.  It selects the
     * authenticated i2400 volatile cold-boot path, captured low-power wake,
     * or ordinary warm-open path without guessing from USB enumeration. */
    status = read_status(scanner);
  }
  if (status == SANE_STATUS_GOOD)
    status = prepare_scanner_session(scanner, firmware_source);
  if (status != SANE_STATUS_GOOD) {
    if (scanner->usb >= 0)
      sanei_usb_close(scanner->usb);
    free(scanner);
    return status;
  }
  status = read_optional_device_state(scanner);
  if (status != SANE_STATUS_GOOD) {
    sanei_usb_close(scanner->usb);
    free(scanner);
    return status;
  }
  *handle = (SANE_Handle)scanner;
  return SANE_STATUS_GOOD;
}

static SANE_Int
mm_to_pixels(SANE_Fixed millimeters, SANE_Int dpi)
{
  long double value = ((long double)SANE_UNFIX(millimeters) * (long double)dpi) / 25.4L;
  if (value <= 0.0L)
    return 0;
  if (value >= (long double)INT_MAX)
    return INT_MAX;
  return (SANE_Int)(value + 0.5L);
}

SANE_Status
sane_get_parameters(SANE_Handle handle, SANE_Parameters *parameters)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  SANE_Int width;
  SANE_Int height;

  if (scanner == NULL || parameters == NULL)
    return SANE_STATUS_INVAL;
  if (scanner->image_parameters_valid) {
    *parameters = scanner->parameters;
    return SANE_STATUS_GOOD;
  }
  memset(&scanner->parameters, 0, sizeof(scanner->parameters));
  width = mm_to_pixels(scanner->br_x - scanner->tl_x, scanner->resolution);
  height = mm_to_pixels(scanner->br_y - scanner->tl_y, scanner->resolution);
  if (width <= 0 || height <= 0)
    return SANE_STATUS_INVAL;
  if (scanner->rotation == 90 || scanner->rotation == 270) {
    SANE_Int temporary = width;
    width = height;
    height = temporary;
  }
  scanner->parameters.format = scanner->mode == 2 ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
  /* Front and rear are separate SANE images/pages, not component frames of
   * one image.  Duplex frontends obtain the buffered rear side with their
   * next sane_start(), so every side is a last frame. */
  scanner->parameters.last_frame = SANE_TRUE;
  scanner->parameters.bytes_per_line =
      scanner->mode == 0 ? (width + 7) / 8 : width * (scanner->mode == 2 ? 3 : 1);
  scanner->parameters.pixels_per_line = width;
  scanner->parameters.lines = height;
  scanner->parameters.depth = scanner->mode == 0 ? 1 : 8;
  if ((size_t)scanner->parameters.bytes_per_line >
          KODAK_I2000_MAX_IMAGE_BYTES / (size_t)height ||
      (size_t)scanner->parameters.bytes_per_line * (size_t)height >
          KODAK_I2000_MAX_IMAGE_BYTES)
    return SANE_STATUS_NO_MEM;
  *parameters = scanner->parameters;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_start(SANE_Handle handle)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  SANE_Bool continuing_job;
  SANE_Status cleanup_status;
  SANE_Status status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  if (scanner->cancel_requested != 0 && scanner->acquiring == 0) {
    scanner->scanning = SANE_FALSE;
    release_all_image_buffers(scanner);
    cleanup_status = finish_continuous_job(scanner, SANE_FALSE);
    scanner->cancel_requested = 0;
    if (cleanup_status != SANE_STATUS_GOOD)
      return cleanup_status;
  }
  if (scanner->scanning)
    return SANE_STATUS_DEVICE_BUSY;

  if (scanner->pending_image_valid) {
    if (acquisition_cancelled(scanner)) {
      release_all_image_buffers(scanner);
      (void)finish_continuous_job(scanner, SANE_FALSE);
      return SANE_STATUS_CANCELLED;
    }
    status = activate_pending_image(scanner);
    if (status != SANE_STATUS_GOOD) {
      release_all_image_buffers(scanner);
      cleanup_status = finish_continuous_job(scanner, SANE_FALSE);
      if (cleanup_status != SANE_STATUS_GOOD)
        return cleanup_status;
      return status;
    }
    scanner->scanning = SANE_TRUE;
    if (acquisition_cancelled(scanner)) {
      scanner->scanning = SANE_FALSE;
      release_all_image_buffers(scanner);
      (void)finish_continuous_job(scanner, SANE_FALSE);
      return SANE_STATUS_CANCELLED;
    }
    DBG(3, "%s: exposing buffered rear duplex frame\n", __func__);
    return SANE_STATUS_GOOD;
  }

  release_all_image_buffers(scanner);
  status = sane_get_parameters(handle, &scanner->parameters);
  if (status != SANE_STATUS_GOOD)
    return status;
  if (!capture_parameters_supported(scanner)) {
    DBG(2,
        "%s: only i2400 front/rear/duplex ADF, "
        "100/150/200/240/300/400/600 dpi, "
        "lineart/gray/color, "
        "default A4 parameters are enabled in this capture path\n",
        __func__);
    return SANE_STATUS_UNSUPPORTED;
  }
  continuing_job = scanner->continuous_job_active;
  scanner->scanning = SANE_FALSE;
  scanner->acquiring = 1;
  scanner->image_remaining = 0U;
  if (!continuing_job) {
    status = read_status(scanner);
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      goto acquisition_done;
    status = prepare_scanner_session(scanner, firmware_source);
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD)
      goto acquisition_done;
    if (scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] != 0U) {
      DBG(1, "%s: refusing scan while device error code is 0x%02x\n", __func__,
          scanner->status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET]);
      status = SANE_STATUS_IO_ERROR;
      goto acquisition_done;
    }
    status = setup_scan(scanner);
    status = acquisition_status(scanner, status);
    if (status != SANE_STATUS_GOOD) {
      if (status != SANE_STATUS_NO_DOCS)
        (void)finish_page_capture(scanner, SANE_FALSE);
      goto acquisition_done;
    }
    scanner->continuous_job_active = SANE_TRUE;
  } else {
    DBG(3, "%s: reading next sheet from active continuous feeder job\n", __func__);
  }

  sanei_usb_set_timeout(KODAK_I2000_SCAN_TIMEOUT_MS);
  status = prepare_captured_images(scanner, continuing_job
                                                ? KODAK_I2000_CONTINUOUS_EMPTY_RETRIES
                                                : KODAK_I2000_SCAN_IDLE_RETRIES);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD) {
    release_all_image_buffers(scanner);
    status = finish_continuous_capture_error(scanner, status);
    goto acquisition_done;
  }
  scanner->scanning = SANE_TRUE;
  /* If a signal arrived between the last acquisition check and exposing the
   * frame, honor it before clearing the acquisition marker. */
  if (acquisition_cancelled(scanner)) {
    scanner->scanning = SANE_FALSE;
    release_all_image_buffers(scanner);
    status = finish_continuous_capture_error(scanner, SANE_STATUS_CANCELLED);
  }

acquisition_done:
  scanner->acquiring = 0;
  sanei_usb_set_timeout(KODAK_I2000_COMMAND_TIMEOUT_MS);
  return status;
}

SANE_Status
sane_read(SANE_Handle handle, SANE_Byte *data, SANE_Int max_length, SANE_Int *length)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  SANE_Status cleanup_status;
  size_t requested;
  size_t copy_size;

  if (length != NULL)
    *length = 0;
  if (scanner == NULL || data == NULL || length == NULL || max_length <= 0)
    return SANE_STATUS_INVAL;
  if (acquisition_cancelled(scanner)) {
    scanner->scanning = SANE_FALSE;
    scanner->acquiring = 1;
    release_all_image_buffers(scanner);
    cleanup_status = finish_continuous_job(scanner, SANE_FALSE);
    scanner->acquiring = 0;
    if (cleanup_status != SANE_STATUS_GOOD)
      DBG(1, "%s: cancellation cleanup failed: %s\n", __func__,
          sane_strstatus(cleanup_status));
    return SANE_STATUS_CANCELLED;
  }
  if (!scanner->scanning)
    return SANE_STATUS_EOF;
  if (scanner->image_data == NULL || scanner->image_offset > scanner->image_size)
    return SANE_STATUS_IO_ERROR;
  if (scanner->image_remaining == 0U) {
    scanner->scanning = SANE_FALSE;
    return SANE_STATUS_EOF;
  }

  requested = (size_t)max_length;
  if (requested > scanner->image_remaining)
    requested = scanner->image_remaining;
  copy_size = requested;
  memcpy(data, scanner->image_data + scanner->image_offset, copy_size);
  scanner->image_offset += copy_size;
  scanner->image_remaining -= copy_size;
  *length = (SANE_Int)copy_size;
  if (scanner->image_remaining == 0U)
    scanner->scanning = SANE_FALSE;
  return SANE_STATUS_GOOD;
}

void
sane_cancel(SANE_Handle handle)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  if (scanner != NULL) {
    /* scanimage invokes this from its signal handler.  Limit this path to
     * scalar stores; the acquisition thread performs USB cleanup and all
     * allocation work after it observes cancel_requested. */
    scanner->cancel_requested = 1;
  }
}

SANE_Status
sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
  (void)handle;
  (void)non_blocking;
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status
sane_get_select_fd(SANE_Handle handle, SANE_Int *fd)
{
  (void)handle;
  (void)fd;
  return SANE_STATUS_UNSUPPORTED;
}

void
sane_close(SANE_Handle handle)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  SANE_Status cleanup_status;

  if (scanner == NULL)
    return;
  scanner->scanning = SANE_FALSE;
  cleanup_status = finish_continuous_job(scanner, SANE_FALSE);
  if (cleanup_status != SANE_STATUS_GOOD)
    DBG(1, "%s: close cleanup failed: %s\n", __func__, sane_strstatus(cleanup_status));
  cleanup_status = set_interrupt_events(scanner, SANE_FALSE);
  if (cleanup_status != SANE_STATUS_GOOD)
    DBG(1, "%s: interrupt-event cleanup failed: %s\n", __func__,
        sane_strstatus(cleanup_status));
  release_all_image_buffers(scanner);
  if (scanner->usb >= 0)
    sanei_usb_close(scanner->usb);
  free(scanner);
}

void
sane_exit(void)
{
  free_device_list();
  free(firmware_source);
  firmware_source = NULL;
  if (backend_initialized) {
    sanei_usb_exit();
    backend_initialized = SANE_FALSE;
  }
}
