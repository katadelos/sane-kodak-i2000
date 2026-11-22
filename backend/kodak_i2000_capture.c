/* Duplex endpoint reads, page rendering, and SANE frame buffering. */
#include "sane/config.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/sanei_usb.h"

#include "kodak_i2000_transport.h"
#include "kodak_i2000_capture.h"
#include "kodak_i2000_image.h"

void
release_image_buffer(kodak_i2000_handle *scanner)
{
  if (scanner == NULL)
    return;
  free(scanner->image_data);
  scanner->image_data = NULL;
  scanner->image_size = 0U;
  scanner->image_offset = 0U;
  scanner->image_remaining = 0U;
  scanner->image_parameters_valid = SANE_FALSE;
  scanner->blank_page_detected = SANE_FALSE;
}

static void
release_pending_image_buffer(kodak_i2000_handle *scanner)
{
  if (scanner == NULL)
    return;
  free(scanner->pending_image_data);
  scanner->pending_image_data = NULL;
  scanner->pending_image_size = 0U;
  memset(&scanner->pending_parameters, 0, sizeof(scanner->pending_parameters));
  scanner->pending_image_valid = SANE_FALSE;
  scanner->pending_blank_page_detected = SANE_FALSE;
}

void
release_all_image_buffers(kodak_i2000_handle *scanner)
{
  release_image_buffer(scanner);
  release_pending_image_buffer(scanner);
}

SANE_Status
activate_pending_image(kodak_i2000_handle *scanner)
{
  if (scanner == NULL || !scanner->pending_image_valid ||
      scanner->pending_image_data == NULL || scanner->pending_image_size == 0U)
    return SANE_STATUS_INVAL;

  release_image_buffer(scanner);
  scanner->image_data = scanner->pending_image_data;
  scanner->image_size = scanner->pending_image_size;
  scanner->image_offset = 0U;
  scanner->image_remaining = scanner->pending_image_size;
  scanner->parameters = scanner->pending_parameters;
  scanner->blank_page_detected = scanner->pending_blank_page_detected;
  scanner->image_parameters_valid = SANE_TRUE;
  scanner->pending_image_data = NULL;
  scanner->pending_image_size = 0U;
  memset(&scanner->pending_parameters, 0, sizeof(scanner->pending_parameters));
  scanner->pending_image_valid = SANE_FALSE;
  scanner->pending_blank_page_detected = SANE_FALSE;
  return SANE_STATUS_GOOD;
}

static SANE_Status
capture_raw_image_endpoint(kodak_i2000_handle *scanner, SANE_Byte endpoint,
                           unsigned empty_idle_retries, SANE_Byte **raw_image,
                           size_t *raw_size)
{
  SANE_Byte transfer[0x4000];
  SANE_Byte *buffer = NULL;
  size_t capacity = 0U;
  size_t length = 0U;
  unsigned idle_retries = 0U;

  if (scanner == NULL || empty_idle_retries == 0U || raw_image == NULL ||
      raw_size == NULL)
    return SANE_STATUS_INVAL;
  *raw_image = NULL;
  *raw_size = 0U;

  for (;;) {
    size_t received = sizeof(transfer);
    SANE_Status status;

    if (acquisition_cancelled(scanner)) {
      free(buffer);
      return SANE_STATUS_CANCELLED;
    }
    status = sanei_usb_read_bulk_endpoint(scanner->usb, endpoint, transfer, &received);
    if (acquisition_cancelled(scanner)) {
      free(buffer);
      return SANE_STATUS_CANCELLED;
    }
    if (status != SANE_STATUS_GOOD) {
      kodak_i2000_image_info image_info;

      /* Use short USB reads so sane_cancel() is responsive.  A timeout after
       * a complete footer is the normal stream boundary; otherwise retain
       * the original aggregate 30-second tolerance by retrying. */
      if (length != 0U) {
        if (!kodak_i2000_image_parse(buffer, length, &image_info) &&
            status == SANE_STATUS_IO_ERROR &&
            ++idle_retries < KODAK_I2000_SCAN_IDLE_RETRIES)
          continue;
        DBG(4, "%s: endpoint 0x%02x ended after %lu bytes: %s\n", __func__,
            (unsigned)endpoint, (unsigned long)length, sane_strstatus(status));
        break;
      }
      if (status == SANE_STATUS_IO_ERROR && ++idle_retries < empty_idle_retries)
        continue;
      DBG(2, "%s: endpoint 0x%02x produced no page data: %s\n", __func__,
          (unsigned)endpoint, sane_strstatus(status));
      free(buffer);
      return status == SANE_STATUS_IO_ERROR || status == SANE_STATUS_EOF
                 ? SANE_STATUS_NO_DOCS
                 : status;
    }
    if (received == 0U) {
      kodak_i2000_image_info image_info;

      if (length != 0U && kodak_i2000_image_parse(buffer, length, &image_info))
        break;
      if (++idle_retries <
          (length != 0U ? KODAK_I2000_SCAN_IDLE_RETRIES : empty_idle_retries))
        continue;
      free(buffer);
      return length == 0U ? SANE_STATUS_NO_DOCS : SANE_STATUS_IO_ERROR;
    }
    idle_retries = 0U;
    if (received > KODAK_I2000_MAX_IMAGE_BYTES - length) {
      free(buffer);
      return SANE_STATUS_NO_MEM;
    }
    if (length + received > capacity) {
      size_t new_capacity = capacity == 0U ? sizeof(transfer) : capacity * 2U;
      SANE_Byte *new_buffer;

      while (new_capacity < length + received) {
        if (new_capacity > KODAK_I2000_MAX_IMAGE_BYTES / 2U) {
          new_capacity = KODAK_I2000_MAX_IMAGE_BYTES;
          break;
        }
        new_capacity *= 2U;
      }
      if (new_capacity > KODAK_I2000_MAX_IMAGE_BYTES)
        new_capacity = KODAK_I2000_MAX_IMAGE_BYTES;
      new_buffer = (SANE_Byte *)realloc(buffer, new_capacity);
      if (new_buffer == NULL) {
        free(buffer);
        return SANE_STATUS_NO_MEM;
      }
      buffer = new_buffer;
      capacity = new_capacity;
    }
    memcpy(buffer + length, transfer, received);
    length += received;
    if (received < sizeof(transfer)) {
      kodak_i2000_image_info image_info;

      /* In continuous mode libusb may return a successful partial transfer
       * when the one-second timeout expires mid-page.  A short transfer is a
       * page boundary only when the accumulated stream contains a structurally
       * valid Kodak footer. */
      if (kodak_i2000_image_parse(buffer, length, &image_info))
        break;
      DBG(5,
          "%s: endpoint 0x%02x short read (%lu bytes) before footer; "
          "continuing page\n",
          __func__, (unsigned)endpoint, (unsigned long)received);
    }
  }

  if (length == 0U) {
    DBG(2, "%s: endpoint 0x%02x completed without page data\n", __func__,
        (unsigned)endpoint);
    free(buffer);
    return SANE_STATUS_NO_DOCS;
  }
  *raw_image = buffer;
  *raw_size = length;
  DBG(4, "%s: captured %lu raw bytes from endpoint 0x%02x\n", __func__,
      (unsigned long)length, (unsigned)endpoint);
  return SANE_STATUS_GOOD;
}

typedef struct kodak_i2000_capture_task {
  kodak_i2000_handle *scanner;
  SANE_Byte endpoint;
  unsigned empty_idle_retries;
  SANE_Byte *raw_image;
  size_t raw_size;
  SANE_Status status;
} kodak_i2000_capture_task;

static void *
capture_raw_image_worker(void *argument)
{
  kodak_i2000_capture_task *task = (kodak_i2000_capture_task *)argument;

  task->status = capture_raw_image_endpoint(task->scanner, task->endpoint,
                                            task->empty_idle_retries, &task->raw_image,
                                            &task->raw_size);
  return NULL;
}

static SANE_Status
capture_selected_raw_images(kodak_i2000_handle *scanner, unsigned empty_idle_retries,
                            SANE_Byte **front_image, size_t *front_size,
                            SANE_Byte **rear_image, size_t *rear_size)
{
  kodak_i2000_capture_task front_task;
  pthread_t front_thread;
  SANE_Status rear_status;
  int thread_error;

  if (scanner == NULL || empty_idle_retries == 0U || front_image == NULL ||
      front_size == NULL || rear_image == NULL || rear_size == NULL)
    return SANE_STATUS_INVAL;
  *front_image = NULL;
  *front_size = 0U;
  *rear_image = NULL;
  *rear_size = 0U;

  if (scanner->source == KODAK_I2000_SOURCE_FRONT)
    return capture_raw_image_endpoint(scanner, KODAK_I2000_FRONT_IMAGE_ENDPOINT,
                                      empty_idle_retries, front_image, front_size);
  if (scanner->source == KODAK_I2000_SOURCE_REAR)
    return capture_raw_image_endpoint(scanner, KODAK_I2000_REAR_IMAGE_ENDPOINT,
                                      empty_idle_retries, rear_image, rear_size);

  front_task.scanner = scanner;
  front_task.endpoint = KODAK_I2000_FRONT_IMAGE_ENDPOINT;
  front_task.empty_idle_retries = empty_idle_retries;
  front_task.raw_image = NULL;
  front_task.raw_size = 0U;
  front_task.status = SANE_STATUS_IO_ERROR;
  thread_error =
      pthread_create(&front_thread, NULL, capture_raw_image_worker, &front_task);
  if (thread_error != 0) {
    DBG(1, "%s: could not create front page reader: %s\n", __func__,
        strerror(thread_error));
    return SANE_STATUS_IO_ERROR;
  }

  rear_status = capture_raw_image_endpoint(scanner, KODAK_I2000_REAR_IMAGE_ENDPOINT,
                                           empty_idle_retries, rear_image, rear_size);
  thread_error = pthread_join(front_thread, NULL);
  if (thread_error != 0) {
    DBG(1, "%s: could not join front page reader: %s\n", __func__,
        strerror(thread_error));
    free(front_task.raw_image);
    free(*rear_image);
    *rear_image = NULL;
    *rear_size = 0U;
    return SANE_STATUS_IO_ERROR;
  }
  if (front_task.status != SANE_STATUS_GOOD || rear_status != SANE_STATUS_GOOD) {
    SANE_Status status =
        rear_status != SANE_STATUS_GOOD ? rear_status : front_task.status;
    free(front_task.raw_image);
    free(*rear_image);
    *rear_image = NULL;
    *rear_size = 0U;
    return status;
  }
  *front_image = front_task.raw_image;
  *front_size = front_task.raw_size;
  return SANE_STATUS_GOOD;
}

static SANE_Status
convert_gray_to_lineart(SANE_Byte *gray, size_t width, size_t height,
                        SANE_Int threshold, SANE_Byte **lineart, size_t *lineart_size)
{
  size_t bytes_per_line;
  size_t output_size;
  SANE_Byte *output;
  size_t y;

  if (gray == NULL || lineart == NULL || lineart_size == NULL || width == 0U ||
      height == 0U || width > SIZE_MAX - 7U)
    return SANE_STATUS_INVAL;
  bytes_per_line = (width + 7U) / 8U;
  if (bytes_per_line > SIZE_MAX / height)
    return SANE_STATUS_NO_MEM;
  output_size = bytes_per_line * height;
  if (output_size > KODAK_I2000_MAX_IMAGE_BYTES)
    return SANE_STATUS_NO_MEM;
  output = (SANE_Byte *)calloc(1U, output_size);
  if (output == NULL)
    return SANE_STATUS_NO_MEM;

  /* The scanner's native gray stream is deliberately used for lineart too;
   * this keeps the recovered feeder/camera transaction identical while
   * exposing SANE's MSB-first one-bit frame. */
  for (y = 0U; y < height; ++y) {
    size_t x;
    for (x = 0U; x < width; ++x)
      if (gray[y * width + x] < (SANE_Byte)threshold)
        output[y * bytes_per_line + x / 8U] |= (SANE_Byte)(0x80U >> (x & 7U));
  }
  free(gray);
  *lineart = output;
  *lineart_size = output_size;
  return SANE_STATUS_GOOD;
}

static SANE_Status
render_captured_image(kodak_i2000_handle *scanner, const char *side,
                      SANE_Byte *raw_image, size_t raw_size, SANE_Bool last_frame,
                      SANE_Byte **image, size_t *image_size,
                      SANE_Parameters *parameters, SANE_Bool *blank_page)
{
  SANE_Byte *rendered_image = NULL;
  size_t rendered_size = 0U;
  size_t width = 0U;
  size_t height = 0U;
  size_t bytes_per_line;
  kodak_i2000_image_info image_info;
  kodak_i2000_image_options image_options;
  int detected_blank = 0;
  SANE_Status status;

  if (scanner == NULL || raw_image == NULL || raw_size == 0U || image == NULL ||
      image_size == NULL || parameters == NULL || blank_page == NULL) {
    free(raw_image);
    return SANE_STATUS_INVAL;
  }
  *image = NULL;
  *image_size = 0U;
  *blank_page = SANE_FALSE;
  memset(parameters, 0, sizeof(*parameters));
  if (acquisition_cancelled(scanner)) {
    free(raw_image);
    return SANE_STATUS_CANCELLED;
  }

  if (!kodak_i2000_image_parse(raw_image, raw_size, &image_info)) {
    DBG(1, "%s: %s raw capture has no recognized image footer\n", __func__,
        side != NULL ? side : "page");
    free(raw_image);
    return SANE_STATUS_IO_ERROR;
  }
  if (scanner->mode == KODAK_I2000_MODE_COLOR) {
    image_info.rear = side != NULL && strcmp(side, "rear") == 0;
    if (!kodak_i2000_image_render_color(
            raw_image, &image_info, (unsigned)scanner->resolution, scanner->auto_crop,
            &rendered_image, &rendered_size, &width, &height)) {
      DBG(1, "%s: %s RGB image could not be rendered (footer format=0x%04x)\n",
          __func__, side != NULL ? side : "page", image_info.format);
      free(raw_image);
      return SANE_STATUS_IO_ERROR;
    }
  } else if (!kodak_i2000_image_render_gray(raw_image, &image_info,
                                            (unsigned)scanner->resolution,
                                            scanner->auto_crop, &rendered_image,
                                            &rendered_size, &width, &height)) {
    DBG(1, "%s: %s gray image could not be rendered (footer format=0x%04x)\n", __func__,
        side != NULL ? side : "page", image_info.format);
    free(raw_image);
    return SANE_STATUS_IO_ERROR;
  }
  free(raw_image);
  if (acquisition_cancelled(scanner)) {
    free(rendered_image);
    return SANE_STATUS_CANCELLED;
  }

  memset(&image_options, 0, sizeof(image_options));
  image_options.deskew = scanner->deskew;
  image_options.brightness = scanner->brightness;
  image_options.contrast = scanner->contrast;
  image_options.sharpen = (unsigned)scanner->sharpen;
  image_options.rotation = (unsigned)scanner->rotation;
  image_options.detect_blank = scanner->blank_detection;
  image_options.blank_threshold = (unsigned)scanner->blank_threshold;
  if (!kodak_i2000_image_process(&rendered_image, &rendered_size, &width, &height,
                                 scanner->mode == KODAK_I2000_MODE_COLOR ? 3U : 1U,
                                 &image_options, &detected_blank)) {
    free(rendered_image);
    return SANE_STATUS_NO_MEM;
  }
  *blank_page = detected_blank ? SANE_TRUE : SANE_FALSE;
  if (acquisition_cancelled(scanner)) {
    free(rendered_image);
    return SANE_STATUS_CANCELLED;
  }

  if (scanner->mode == KODAK_I2000_MODE_LINEART) {
    SANE_Byte *lineart_image = NULL;
    size_t lineart_size = 0U;

    status = convert_gray_to_lineart(rendered_image, width, height, scanner->threshold,
                                     &lineart_image, &lineart_size);
    if (status != SANE_STATUS_GOOD) {
      free(rendered_image);
      return status;
    }
    rendered_image = lineart_image;
    rendered_size = lineart_size;
  }

  if (scanner->mode == KODAK_I2000_MODE_COLOR) {
    if (width > SIZE_MAX / 3U) {
      free(rendered_image);
      return SANE_STATUS_NO_MEM;
    }
    bytes_per_line = width * 3U;
  } else if (scanner->mode == KODAK_I2000_MODE_LINEART) {
    if (width > SIZE_MAX - 7U) {
      free(rendered_image);
      return SANE_STATUS_NO_MEM;
    }
    bytes_per_line = (width + 7U) / 8U;
  } else {
    bytes_per_line = width;
  }
  if (height == 0U || bytes_per_line > (size_t)INT_MAX || height > (size_t)INT_MAX ||
      rendered_size > KODAK_I2000_MAX_IMAGE_BYTES ||
      bytes_per_line > KODAK_I2000_MAX_IMAGE_BYTES / height ||
      bytes_per_line * height > KODAK_I2000_MAX_IMAGE_BYTES) {
    free(rendered_image);
    return SANE_STATUS_NO_MEM;
  }

  parameters->format =
      scanner->mode == KODAK_I2000_MODE_COLOR ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
  parameters->last_frame = last_frame;
  parameters->bytes_per_line = (SANE_Int)bytes_per_line;
  parameters->pixels_per_line = (SANE_Int)width;
  parameters->lines = (SANE_Int)height;
  parameters->depth = scanner->mode == KODAK_I2000_MODE_LINEART ? 1 : 8;
  *image = rendered_image;
  *image_size = rendered_size;
  DBG(2,
      "%s: %s raw %lux%lu at %ux%u format=0x%04x, crop x=%lu y=%lu "
      "w=%lu h=%lu; output %lux%lu (%lu bytes), last=%d blank=%d\n",
      __func__, side != NULL ? side : "page", (unsigned long)image_info.raw_width,
      (unsigned long)image_info.raw_height, image_info.resolution_x,
      image_info.resolution_y, image_info.format, (unsigned long)image_info.crop_left,
      (unsigned long)image_info.crop_top, (unsigned long)image_info.crop_width,
      (unsigned long)image_info.crop_height, (unsigned long)width,
      (unsigned long)height, (unsigned long)rendered_size, (int)last_frame,
      (int)*blank_page);
  return SANE_STATUS_GOOD;
}

SANE_Status
prepare_captured_images(kodak_i2000_handle *scanner, unsigned empty_idle_retries)
{
  SANE_Byte *front_raw = NULL;
  SANE_Byte *rear_raw = NULL;
  SANE_Byte *front_image = NULL;
  SANE_Byte *rear_image = NULL;
  size_t front_raw_size = 0U;
  size_t rear_raw_size = 0U;
  size_t front_image_size = 0U;
  size_t rear_image_size = 0U;
  SANE_Parameters front_parameters;
  SANE_Parameters rear_parameters;
  SANE_Bool front_blank = SANE_FALSE;
  SANE_Bool rear_blank = SANE_FALSE;
  SANE_Status status;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  status = capture_selected_raw_images(scanner, empty_idle_retries, &front_raw,
                                       &front_raw_size, &rear_raw, &rear_raw_size);
  status = acquisition_status(scanner, status);
  if (status != SANE_STATUS_GOOD) {
    free(front_raw);
    free(rear_raw);
    return status;
  }

  if (scanner->source != KODAK_I2000_SOURCE_REAR) {
    status = render_captured_image(scanner, "front", front_raw, front_raw_size,
                                   SANE_TRUE, &front_image, &front_image_size,
                                   &front_parameters, &front_blank);
    front_raw = NULL;
    if (status != SANE_STATUS_GOOD) {
      free(rear_raw);
      return status;
    }
  }
  if (scanner->source != KODAK_I2000_SOURCE_FRONT) {
    status = render_captured_image(scanner, "rear", rear_raw, rear_raw_size, SANE_TRUE,
                                   &rear_image, &rear_image_size, &rear_parameters,
                                   &rear_blank);
    rear_raw = NULL;
    if (status != SANE_STATUS_GOOD) {
      free(front_image);
      return status;
    }
  }

  if (scanner->source == KODAK_I2000_SOURCE_REAR) {
    scanner->image_data = rear_image;
    scanner->image_size = rear_image_size;
    scanner->parameters = rear_parameters;
    scanner->blank_page_detected = rear_blank;
  } else {
    scanner->image_data = front_image;
    scanner->image_size = front_image_size;
    scanner->parameters = front_parameters;
    scanner->blank_page_detected = front_blank;
  }
  scanner->image_offset = 0U;
  scanner->image_remaining = scanner->image_size;
  scanner->image_parameters_valid = SANE_TRUE;

  if (scanner->source == KODAK_I2000_SOURCE_DUPLEX) {
    scanner->pending_image_data = rear_image;
    scanner->pending_image_size = rear_image_size;
    scanner->pending_parameters = rear_parameters;
    scanner->pending_image_valid = SANE_TRUE;
    scanner->pending_blank_page_detected = rear_blank;
  }
  return SANE_STATUS_GOOD;
}
