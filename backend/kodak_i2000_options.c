/* SANE option descriptors, defaults, and values. */
#include "sane/config.h"
#include <string.h>

#define BACKEND_NAME kodak_i2000
#define DEBUG_DECLARE_ONLY
#include "sane/sanei_backend.h"
#include "sane/saneopts.h"
#include "sane/sanei.h"

#include "kodak_i2000_options.h"
#include "kodak_i2000_transport.h"
#include "kodak_i2000_capture.h"

#define KODAK_I2000_DEFAULT_RESOLUTION 200

static const char *const mode_list[] = {SANE_VALUE_SCAN_MODE_LINEART,
                                        SANE_VALUE_SCAN_MODE_GRAY,
                                        SANE_VALUE_SCAN_MODE_COLOR, NULL};

static const char *const source_list[] = {"ADF Front", "ADF Back", "ADF Duplex", NULL};

/* The vendor advertises these as the normal document resolutions.  The
 * camera is selected from resolution groups below; the intermediate values
 * are produced by the same resampler used by the vendor image chain. */
static const SANE_Word resolution_list[] = {7, 100, 150, 200, 240, 300, 400, 600};

static const SANE_Word rotation_list[] = {4, 0, 90, 180, 270};

static const SANE_Range percent_range = {-100, 100, 1};
static const SANE_Range sharpen_range = {0, 3, 1};
static const SANE_Range threshold_range = {0, 255, 1};
static const SANE_Range blank_threshold_range = {0, 20, 1};

SANE_Bool
resolution_supported(SANE_Int resolution)
{
  size_t index;

  for (index = 1U; index <= (size_t)resolution_list[0]; ++index)
    if (resolution_list[index] == resolution)
      return SANE_TRUE;
  return SANE_FALSE;
}

static void
set_range_option(SANE_Option_Descriptor *option, SANE_Value_Type type, SANE_Unit unit,
                 const SANE_Range *range, const char *name, const char *title,
                 const char *description)
{
  *option = (SANE_Option_Descriptor){
      .name = name,
      .title = title,
      .desc = description,
      .type = type,
      .unit = unit,
      .size = sizeof(SANE_Word),
      .cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT,
      .constraint_type = SANE_CONSTRAINT_RANGE,
      .constraint.range = range,
  };
}

SANE_Status
init_options(kodak_i2000_handle *scanner)
{
  SANE_Option_Descriptor *option;

  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  memset(scanner->options, 0, sizeof(scanner->options));

  scanner->tl_x_range.min = 0;
  scanner->tl_x_range.max = SANE_FIX(KODAK_I2000_DEFAULT_WIDTH_MM);
  scanner->tl_x_range.quant = 1;
  scanner->tl_y_range.min = 0;
  scanner->tl_y_range.max = SANE_FIX(KODAK_I2000_DEFAULT_HEIGHT_MM);
  scanner->tl_y_range.quant = 1;
  scanner->br_x_range = scanner->tl_x_range;
  scanner->br_y_range = scanner->tl_y_range;

  option = &scanner->options[KODAK_I2000_OPT_NUM_OPTS];
  option->name = SANE_NAME_NUM_OPTIONS;
  option->title = SANE_TITLE_NUM_OPTIONS;
  option->desc = SANE_DESC_NUM_OPTIONS;
  option->type = SANE_TYPE_INT;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_MODE_GROUP];
  option->title = SANE_I18N("Scan mode");
  option->desc = SANE_I18N("Scan mode options");
  option->type = SANE_TYPE_GROUP;
  option->cap = SANE_CAP_SOFT_DETECT;

  option = &scanner->options[KODAK_I2000_OPT_MODE];
  option->name = SANE_NAME_SCAN_MODE;
  option->title = SANE_TITLE_SCAN_MODE;
  option->desc = SANE_DESC_SCAN_MODE;
  option->type = SANE_TYPE_STRING;
  option->size = 16;
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  option->constraint.string_list = mode_list;

  option = &scanner->options[KODAK_I2000_OPT_SOURCE];
  option->name = SANE_NAME_SCAN_SOURCE;
  option->title = SANE_TITLE_SCAN_SOURCE;
  option->desc = SANE_DESC_SCAN_SOURCE;
  option->type = SANE_TYPE_STRING;
  option->size = 16;
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  option->constraint.string_list = source_list;

  option = &scanner->options[KODAK_I2000_OPT_RESOLUTION];
  option->name = SANE_NAME_SCAN_RESOLUTION;
  option->title = SANE_TITLE_SCAN_RESOLUTION;
  option->desc = SANE_DESC_SCAN_RESOLUTION;
  option->type = SANE_TYPE_INT;
  option->unit = SANE_UNIT_DPI;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_WORD_LIST;
  option->constraint.word_list = resolution_list;

  option = &scanner->options[KODAK_I2000_OPT_GEOMETRY_GROUP];
  option->title = SANE_I18N("Geometry");
  option->desc = SANE_I18N("Scan area");
  option->type = SANE_TYPE_GROUP;
  option->cap = SANE_CAP_SOFT_DETECT;

  set_range_option(&scanner->options[KODAK_I2000_OPT_TL_X], SANE_TYPE_FIXED,
                   SANE_UNIT_MM, &scanner->tl_x_range, SANE_NAME_SCAN_TL_X,
                   SANE_TITLE_SCAN_TL_X, SANE_DESC_SCAN_TL_X);
  set_range_option(&scanner->options[KODAK_I2000_OPT_TL_Y], SANE_TYPE_FIXED,
                   SANE_UNIT_MM, &scanner->tl_y_range, SANE_NAME_SCAN_TL_Y,
                   SANE_TITLE_SCAN_TL_Y, SANE_DESC_SCAN_TL_Y);
  set_range_option(&scanner->options[KODAK_I2000_OPT_BR_X], SANE_TYPE_FIXED,
                   SANE_UNIT_MM, &scanner->br_x_range, SANE_NAME_SCAN_BR_X,
                   SANE_TITLE_SCAN_BR_X, SANE_DESC_SCAN_BR_X);
  set_range_option(&scanner->options[KODAK_I2000_OPT_BR_Y], SANE_TYPE_FIXED,
                   SANE_UNIT_MM, &scanner->br_y_range, SANE_NAME_SCAN_BR_Y,
                   SANE_TITLE_SCAN_BR_Y, SANE_DESC_SCAN_BR_Y);

  option = &scanner->options[KODAK_I2000_OPT_ENHANCEMENT_GROUP];
  option->name = SANE_NAME_ENHANCEMENT;
  option->title = SANE_I18N("Perfect Page enhancements");
  option->desc = SANE_I18N("Host-side document cleanup and orientation");
  option->type = SANE_TYPE_GROUP;
  option->cap = SANE_CAP_SOFT_DETECT;

  option = &scanner->options[KODAK_I2000_OPT_AUTO_CROP];
  option->name = "automatic-crop";
  option->title = SANE_I18N("Automatic crop");
  option->desc = SANE_I18N("Detect and crop to the sheet boundary");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_DESKEW];
  option->name = "deskew";
  option->title = SANE_I18N("Automatic deskew");
  option->desc = SANE_I18N("Correct a small page angle in the rendered image");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  set_range_option(&scanner->options[KODAK_I2000_OPT_BRIGHTNESS], SANE_TYPE_INT,
                   SANE_UNIT_PERCENT, &percent_range, SANE_NAME_BRIGHTNESS,
                   SANE_TITLE_BRIGHTNESS, SANE_DESC_BRIGHTNESS);
  set_range_option(&scanner->options[KODAK_I2000_OPT_CONTRAST], SANE_TYPE_INT,
                   SANE_UNIT_PERCENT, &percent_range, SANE_NAME_CONTRAST,
                   SANE_TITLE_CONTRAST, SANE_DESC_CONTRAST);
  set_range_option(
      &scanner->options[KODAK_I2000_OPT_SHARPEN], SANE_TYPE_INT, SANE_UNIT_NONE,
      &sharpen_range, "sharpen", SANE_I18N("Sharpen"),
      SANE_I18N("Document sharpening (0 off, 1 normal, 2 high, 3 highest)"));
  set_range_option(&scanner->options[KODAK_I2000_OPT_THRESHOLD], SANE_TYPE_INT,
                   SANE_UNIT_NONE, &threshold_range, SANE_NAME_THRESHOLD,
                   SANE_TITLE_THRESHOLD, SANE_DESC_THRESHOLD);
  set_range_option(&scanner->options[KODAK_I2000_OPT_BLANK_THRESHOLD], SANE_TYPE_INT,
                   SANE_UNIT_PERCENT, &blank_threshold_range, "blank-page-threshold",
                   SANE_I18N("Blank-page threshold"),
                   SANE_I18N("Maximum percentage of dark pixels on a blank page"));

  option = &scanner->options[KODAK_I2000_OPT_ROTATION];
  option->name = "rotation";
  option->title = SANE_I18N("Rotation");
  option->desc = SANE_I18N("Rotate the output clockwise");
  option->type = SANE_TYPE_INT;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_WORD_LIST;
  option->constraint.word_list = rotation_list;

  option = &scanner->options[KODAK_I2000_OPT_BLANK_DETECTION];
  option->name = "blank-page-detection";
  option->title = SANE_I18N("Blank-page detection");
  option->desc = SANE_I18N("Measure whether each rendered side is blank");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_SENSORS_GROUP];
  option->name = SANE_NAME_SENSORS;
  option->title = SANE_I18N("Sensors and device buttons");
  option->desc = SANE_I18N("Read-only scanner events and page results");
  option->type = SANE_TYPE_GROUP;
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;

  option = &scanner->options[KODAK_I2000_OPT_SCAN_BUTTON];
  option->name = SANE_NAME_SCAN;
  option->title = SANE_TITLE_SCAN;
  option->desc = SANE_I18N("A scan event was received from the operator panel");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_HARD_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_OCP_BUTTON_NUMBER];
  option->name = "ocp-job-number";
  option->title = SANE_I18N("Operator-panel job number");
  option->desc = SANE_I18N("Job or sequence selected when the scan event fired");
  option->type = SANE_TYPE_INT;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_HARD_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  option = &scanner->options[KODAK_I2000_OPT_BLANK_PAGE];
  option->name = "blank-page";
  option->title = SANE_I18N("Blank page");
  option->desc = SANE_I18N("Whether the current rendered side is blank");
  option->type = SANE_TYPE_BOOL;
  option->unit = SANE_UNIT_NONE;
  option->size = sizeof(SANE_Word);
  option->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  option->constraint_type = SANE_CONSTRAINT_NONE;

  init_device_options(scanner);

  scanner->mode = 1;
  scanner->source = 0;
  scanner->resolution = KODAK_I2000_DEFAULT_RESOLUTION;
  scanner->tl_x = 0;
  scanner->tl_y = 0;
  scanner->br_x = SANE_FIX(KODAK_I2000_DEFAULT_WIDTH_MM);
  scanner->br_y = SANE_FIX(KODAK_I2000_DEFAULT_HEIGHT_MM);
  scanner->auto_crop = SANE_TRUE;
  scanner->deskew = SANE_FALSE;
  scanner->brightness = 0;
  scanner->contrast = 0;
  scanner->sharpen = 1;
  scanner->rotation = 0;
  scanner->threshold = 128;
  scanner->blank_detection = SANE_TRUE;
  scanner->blank_threshold = 1;
  return SANE_STATUS_GOOD;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;

  if (scanner == NULL || option < 0 || option >= KODAK_I2000_OPT_NUM_OPTIONS)
    return NULL;
  return &scanner->options[option];
}

SANE_Status
copy_string_value(SANE_String value, SANE_Int size, const char *source)
{
  size_t length;

  if (value == NULL || size <= 0 || source == NULL)
    return SANE_STATUS_INVAL;
  length = strlen(source);
  if (length + 1U > (size_t)size)
    return SANE_STATUS_INVAL;
  memcpy(value, source, length + 1U);
  return SANE_STATUS_GOOD;
}

static SANE_Status
refresh_device_info(kodak_i2000_handle *scanner)
{
  if (scanner == NULL)
    return SANE_STATUS_INVAL;
  if (read_status(scanner) != SANE_STATUS_GOOD)
    return SANE_STATUS_IO_ERROR;
  return read_optional_device_state(scanner);
}

static SANE_Status
get_scan_option(kodak_i2000_handle *scanner, SANE_Int option, void *value)
{
  SANE_Status status;
  switch (option) {
  case KODAK_I2000_OPT_MODE:
    return copy_string_value((SANE_String)value, scanner->options[option].size,
                             mode_list[scanner->mode]);
  case KODAK_I2000_OPT_SOURCE:
    return copy_string_value((SANE_String)value, scanner->options[option].size,
                             source_list[scanner->source]);
  case KODAK_I2000_OPT_RESOLUTION:
    *(SANE_Word *)value = scanner->resolution;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_TL_X:
    *(SANE_Fixed *)value = scanner->tl_x;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_TL_Y:
    *(SANE_Fixed *)value = scanner->tl_y;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BR_X:
    *(SANE_Fixed *)value = scanner->br_x;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BR_Y:
    *(SANE_Fixed *)value = scanner->br_y;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_AUTO_CROP:
    *(SANE_Bool *)value = scanner->auto_crop;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_DESKEW:
    *(SANE_Bool *)value = scanner->deskew;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BRIGHTNESS:
    *(SANE_Word *)value = scanner->brightness;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_CONTRAST:
    *(SANE_Word *)value = scanner->contrast;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_SHARPEN:
    *(SANE_Word *)value = scanner->sharpen;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_ROTATION:
    *(SANE_Word *)value = scanner->rotation;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_THRESHOLD:
    *(SANE_Word *)value = scanner->threshold;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BLANK_DETECTION:
    *(SANE_Bool *)value = scanner->blank_detection;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BLANK_THRESHOLD:
    *(SANE_Word *)value = scanner->blank_threshold;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_SCAN_BUTTON:
    status = poll_button_events(scanner);
    if (status != SANE_STATUS_GOOD)
      return status;
    *(SANE_Bool *)value = scanner->scan_button_pending;
    scanner->scan_button_pending = SANE_FALSE;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_OCP_BUTTON_NUMBER:
    status = poll_button_events(scanner);
    if (status != SANE_STATUS_GOOD)
      return status;
    *(SANE_Word *)value = scanner->ocp_button_number;
    return SANE_STATUS_GOOD;
  case KODAK_I2000_OPT_BLANK_PAGE:
    *(SANE_Bool *)value = scanner->blank_page_detected;
    return SANE_STATUS_GOOD;
  default:
    return get_device_option(scanner, option, value);
  }
}

SANE_Status
sane_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action,
                    void *value, SANE_Int *info)
{
  kodak_i2000_handle *scanner = (kodak_i2000_handle *)handle;
  SANE_Status status;

  if (info != NULL)
    *info = 0;
  if (scanner == NULL || value == NULL || option < 0 ||
      option >= KODAK_I2000_OPT_NUM_OPTIONS)
    return SANE_STATUS_INVAL;
  if (option == KODAK_I2000_OPT_NUM_OPTS) {
    if (action == SANE_ACTION_GET_VALUE) {
      *(SANE_Word *)value = KODAK_I2000_OPT_NUM_OPTIONS;
      return SANE_STATUS_GOOD;
    }
    return SANE_STATUS_INVAL;
  }
  if (action == SANE_ACTION_SET_AUTO)
    return SANE_STATUS_UNSUPPORTED;
  if (action == SANE_ACTION_GET_VALUE)
    return get_scan_option(scanner, option, value);
  if (action != SANE_ACTION_SET_VALUE)
    return SANE_STATUS_INVAL;

  if (scanner->scanning || scanner->pending_image_valid ||
      scanner->continuous_job_active || scanner->acquiring != 0)
    return SANE_STATUS_DEVICE_BUSY;

  if (option == KODAK_I2000_OPT_REFRESH_INFO) {
    status = refresh_device_info(scanner);
    if (status == SANE_STATUS_GOOD && info != NULL)
      *info |= SANE_INFO_RELOAD_OPTIONS;
    return status;
  }

  status = sanei_constrain_value(&scanner->options[option], value, info);
  if (status != SANE_STATUS_GOOD)
    return status;
  switch (option) {
  case KODAK_I2000_OPT_MODE:
    if (strcmp((const char *)value, mode_list[0]) == 0)
      scanner->mode = 0;
    else if (strcmp((const char *)value, mode_list[1]) == 0)
      scanner->mode = 1;
    else if (strcmp((const char *)value, mode_list[2]) == 0)
      scanner->mode = 2;
    else
      return SANE_STATUS_INVAL;
    break;
  case KODAK_I2000_OPT_SOURCE:
    if (strcmp((const char *)value, source_list[0]) == 0)
      scanner->source = 0;
    else if (strcmp((const char *)value, source_list[1]) == 0)
      scanner->source = 1;
    else if (strcmp((const char *)value, source_list[2]) == 0)
      scanner->source = 2;
    else
      return SANE_STATUS_INVAL;
    break;
  case KODAK_I2000_OPT_RESOLUTION:
    if (!resolution_supported(*(SANE_Word *)value))
      return SANE_STATUS_INVAL;
    scanner->resolution = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_TL_X:
    if (*(SANE_Fixed *)value >= scanner->br_x)
      return SANE_STATUS_INVAL;
    scanner->tl_x = *(SANE_Fixed *)value;
    break;
  case KODAK_I2000_OPT_TL_Y:
    if (*(SANE_Fixed *)value >= scanner->br_y)
      return SANE_STATUS_INVAL;
    scanner->tl_y = *(SANE_Fixed *)value;
    break;
  case KODAK_I2000_OPT_BR_X:
    if (*(SANE_Fixed *)value <= scanner->tl_x)
      return SANE_STATUS_INVAL;
    scanner->br_x = *(SANE_Fixed *)value;
    break;
  case KODAK_I2000_OPT_BR_Y:
    if (*(SANE_Fixed *)value <= scanner->tl_y)
      return SANE_STATUS_INVAL;
    scanner->br_y = *(SANE_Fixed *)value;
    break;
  case KODAK_I2000_OPT_AUTO_CROP:
    scanner->auto_crop = *(SANE_Bool *)value;
    break;
  case KODAK_I2000_OPT_DESKEW:
    scanner->deskew = *(SANE_Bool *)value;
    break;
  case KODAK_I2000_OPT_BRIGHTNESS:
    scanner->brightness = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_CONTRAST:
    scanner->contrast = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_SHARPEN:
    scanner->sharpen = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_ROTATION:
    scanner->rotation = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_THRESHOLD:
    scanner->threshold = *(SANE_Word *)value;
    break;
  case KODAK_I2000_OPT_BLANK_DETECTION:
    scanner->blank_detection = *(SANE_Bool *)value;
    break;
  case KODAK_I2000_OPT_BLANK_THRESHOLD:
    scanner->blank_threshold = *(SANE_Word *)value;
    break;
  default:
    return SANE_STATUS_INVAL;
  }
  if (scanner->tl_x < scanner->tl_x_range.min ||
      scanner->tl_y < scanner->tl_y_range.min ||
      scanner->br_x > scanner->br_x_range.max ||
      scanner->br_y > scanner->br_y_range.max || scanner->br_x <= scanner->tl_x ||
      scanner->br_y <= scanner->tl_y)
    return SANE_STATUS_INVAL;
  /* A completed page may still own its output buffer and parameters until the
   * next sane_start().  Once an option changes, discard that consumed frame so
   * SANE_INFO_RELOAD_PARAMS immediately reports the new configuration rather
   * than the previous page's rendered dimensions. */
  release_image_buffer(scanner);
  if (info != NULL)
    *info |= SANE_INFO_RELOAD_PARAMS;
  return SANE_STATUS_GOOD;
}
