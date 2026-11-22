/* sane - Scanner Access Now Easy.
   Copyright (C) 2026 SANE Project

   This file is part of the SANE package and is distributed under the
   terms of the GNU General Public License version 3 or later.

   Scanner-free tests for capture-backed Kodak i2000 protocol records. */

#include "backend/kodak_i2000_capture_data.h"
#include "backend/kodak_i2000_protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
expect_true(const char *name, int condition)
{
  if (!condition) {
    fprintf(stderr, "%s: expectation failed\n", name);
    ++failures;
  }
}

static void
test_model_table(void)
{
  const kodak_i2000_model *models;
  const kodak_i2000_model *model;
  size_t count = 0U;

  models = kodak_i2000_models(&count);
  expect_true("model table is present", models != NULL);
  expect_true("six package-declared models", count == 6U);
  model = kodak_i2000_model_lookup(UINT16_C(0x040a), UINT16_C(0x601c));
  expect_true("i2400 lookup", model != NULL && strcmp(model->name, "i2400") == 0);
  expect_true("unknown model rejected",
              kodak_i2000_model_lookup(UINT16_C(0xffff), UINT16_C(0xffff)) == NULL);
}

static void
test_front_session_setup(void)
{
  static const uint8_t captured_get[KODAK_I2000_SESSION_LENGTH] = {
      0x01, 0xaa, 0x00, 0x00, 0x06, 0xff, 0x81, 0xa1, 0x66, 0xfa, 0x00, 0x00, 0x00,
      0x06, 0xff, 0x5d, 0xa2, 0x43, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xdf, 0xc8, 0x8c,
      0x80, 0x00, 0x00, 0x00, 0x05, 0xcd, 0x17, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x0c,
      0x6f, 0x46, 0x3f, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t captured_set[KODAK_I2000_SESSION_DATA_LENGTH] = {
      0x01, 0xaa, 0x00, 0x00, 0x06, 0xff, 0x81, 0xa1, 0x66, 0xfa, 0x00, 0x00, 0x00,
      0x06, 0x06, 0x5d, 0x5d, 0xa2, 0x43, 0x1f, 0x00, 0x00, 0x00, 0xdf, 0xdf, 0xc8,
      0x8c, 0x80, 0x00, 0x00, 0x05, 0xcd, 0x5c, 0xd1, 0x77, 0xb0, 0x00, 0x00, 0x0c,
      0x6f, 0xc6, 0xf4, 0x63, 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t source[KODAK_I2000_SESSION_LENGTH];
  uint8_t setup[KODAK_I2000_SESSION_DATA_LENGTH];

  memcpy(source, captured_get, sizeof(source));
  memset(setup, 0xcc, sizeof(setup));
  expect_true("build front session setup",
              kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                              sizeof(source), setup, sizeof(setup)));
  expect_true("session setup matches vendor transfer",
              memcmp(setup, captured_set, sizeof(setup)) == 0);
  expect_true("session source remains unchanged",
              memcmp(source, captured_get, sizeof(source)) == 0);

  static const uint8_t dt_front[] = {
      0x01, 0xaa, 0x00, 0x00, 0x06, 0xff, 0xa3, 0x8d, 0x80, 0x46, 0x00, 0x00, 0x00,
      0x00, 0x06, 0xff, 0x12, 0xc4, 0xc2, 0x41, 0x00, 0x00, 0x00, 0x00, 0x94, 0x77,
      0x84, 0x72, 0x00, 0x00, 0x00, 0x00, 0x12, 0xfb, 0x67, 0x6f, 0x00, 0x00, 0x00,
      0x00, 0xa9, 0x88, 0x20, 0xa2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  static const uint8_t dt_duplex[] = {
      0x01, 0xaa, 0x00, 0x00, 0x06, 0x06, 0xa3, 0x8d, 0x80, 0x46, 0xa3, 0x8d, 0x80,
      0x46, 0x06, 0x06, 0x12, 0xc4, 0xc2, 0x41, 0x12, 0xc4, 0xc2, 0x41, 0x94, 0x77,
      0x84, 0x72, 0x94, 0x77, 0x84, 0x72, 0x12, 0xfb, 0x67, 0x6f, 0x12, 0xfb, 0x67,
      0x6f, 0xa9, 0x88, 0x20, 0xa2, 0xa9, 0x88, 0x20, 0xa2, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  expect_true("DT rear upload preserves front and sets rear calibration",
              kodak_i2000_build_session_setup(&kodak_i2000_dt_profile, 1U, dt_front,
                                              sizeof(dt_front), setup, sizeof(setup)) &&
                  memcmp(setup, dt_duplex, sizeof(setup)) == 0);

  expect_true("reject short input", !kodak_i2000_build_session_setup(
                                        &kodak_i2000_piranha_profile, 0U, source,
                                        sizeof(source) - 1U, setup, sizeof(setup)));
  expect_true("reject short output", !kodak_i2000_build_session_setup(
                                         &kodak_i2000_piranha_profile, 0U, source,
                                         sizeof(source), setup, sizeof(setup) - 1U));
  source[KODAK_I2000_SESSION_VERSION_OFFSET] = 2U;
  expect_true("reject unknown session version",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                               sizeof(source), setup, sizeof(setup)));
  source[KODAK_I2000_SESSION_VERSION_OFFSET] = KODAK_I2000_SESSION_VERSION;
  source[KODAK_I2000_SESSION_MARKER_OFFSET] = KODAK_I2000_SESSION_INITIAL_MARKER;
  expect_true("reject cold-initialized session marker",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                               sizeof(source), setup, sizeof(setup)));
  source[KODAK_I2000_SESSION_MARKER_OFFSET] = 1U;
  expect_true("reject unknown session marker",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                               sizeof(source), setup, sizeof(setup)));
  source[KODAK_I2000_SESSION_MARKER_OFFSET] = KODAK_I2000_SESSION_MARKER;
  source[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] =
      KODAK_I2000_SESSION_BACKGROUND_UNKNOWN;
  expect_true("reject unresolved front background",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                               sizeof(source), setup, sizeof(setup)));
  source[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] =
      KODAK_I2000_SESSION_BACKGROUND_BLACK;
  source[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] =
      KODAK_I2000_SESSION_BACKGROUND_UNKNOWN;
  expect_true("reject unresolved rear background",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, source,
                                               sizeof(source), setup, sizeof(setup)));
  expect_true("reject null session",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U, NULL,
                                               sizeof(source), setup, sizeof(setup)));
  expect_true("reject null output", !kodak_i2000_build_session_setup(
                                        &kodak_i2000_piranha_profile, 0U, captured_get,
                                        sizeof(source), NULL, sizeof(setup)));
}

static void
test_initial_session_info(void)
{
  static const uint8_t expected[KODAK_I2000_SESSION_DATA_LENGTH] = {
      0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t expected_ready_setup[KODAK_I2000_SESSION_DATA_LENGTH] = {
      0x01, 0xaa, 0x00, 0x00, 0x06, 0xff, 0x81, 0xa1, 0x66, 0xfa, 0x00, 0x00, 0x00,
      0x00, 0x06, 0xff, 0x5d, 0xa2, 0x43, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xdf, 0xc8,
      0x8c, 0x80, 0x00, 0x00, 0x00, 0x00, 0x5c, 0xd1, 0x77, 0xb0, 0x00, 0x00, 0x00,
      0x00, 0xc6, 0xf4, 0x63, 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t session[KODAK_I2000_SESSION_DATA_LENGTH];
  uint8_t calibration_ready[KODAK_I2000_SESSION_DATA_LENGTH];
  uint8_t background_ready[KODAK_I2000_SESSION_DATA_LENGTH];
  uint8_t setup[KODAK_I2000_SESSION_DATA_LENGTH];

  memset(session, 0, sizeof(session));
  expect_true("recognize zero session after cold firmware start",
              kodak_i2000_session_info_is_uninitialized(session, sizeof(session)));
  expect_true("reject short zero session", !kodak_i2000_session_info_is_uninitialized(
                                               session, sizeof(session) - 1U));
  expect_true("build vendor initial session record",
              kodak_i2000_build_initial_session_info(session, sizeof(session)));
  expect_true("initial session record matches vendor code",
              memcmp(session, expected, sizeof(expected)) == 0);
  expect_true("cold session requires calibration",
              kodak_i2000_session_info_needs_calibration(session, sizeof(session)));
  expect_true(
      "cold session does not skip to background check",
      !kodak_i2000_session_info_needs_background_check(session, sizeof(session)));
  expect_true("refuse a scan before cold calibration",
              !kodak_i2000_build_session_setup(&kodak_i2000_piranha_profile, 0U,
                                               session, sizeof(session), setup,
                                               sizeof(setup)));
  expect_true(
      "build calibration-ready session",
      kodak_i2000_build_calibration_ready_session_info(
          session, sizeof(session), calibration_ready, sizeof(calibration_ready)));
  expect_true("calibration-ready marker is aa",
              calibration_ready[KODAK_I2000_SESSION_MARKER_OFFSET] ==
                  KODAK_I2000_SESSION_MARKER);
  expect_true("calibration leaves background unresolved",
              calibration_ready[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
                      KODAK_I2000_SESSION_BACKGROUND_UNKNOWN &&
                  calibration_ready[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
                      KODAK_I2000_SESSION_BACKGROUND_UNKNOWN);
  expect_true("calibration builder leaves source unchanged",
              memcmp(session, expected, sizeof(expected)) == 0);
  expect_true("calibration-ready session requires background check",
              kodak_i2000_session_info_needs_background_check(
                  calibration_ready, sizeof(calibration_ready)));
  expect_true("refuse a scan before background check",
              !kodak_i2000_build_session_setup(
                  &kodak_i2000_piranha_profile, 0U, calibration_ready,
                  sizeof(calibration_ready), setup, sizeof(setup)));
  expect_true("build background-ready session",
              kodak_i2000_build_background_ready_session_info(
                  calibration_ready, sizeof(calibration_ready), background_ready,
                  sizeof(background_ready)));
  expect_true("background-ready state resolves both baffles",
              background_ready[KODAK_I2000_SESSION_FRONT_BACKGROUND_OFFSET] ==
                      KODAK_I2000_SESSION_BACKGROUND_BLACK &&
                  background_ready[KODAK_I2000_SESSION_REAR_BACKGROUND_OFFSET] ==
                      KODAK_I2000_SESSION_BACKGROUND_BLACK);
  expect_true("ready session no longer needs cold initialization",
              !kodak_i2000_session_info_needs_calibration(background_ready,
                                                          sizeof(background_ready)) &&
                  !kodak_i2000_session_info_needs_background_check(
                      background_ready, sizeof(background_ready)));
  expect_true("build setup only after complete cold initialization",
              kodak_i2000_build_session_setup(
                  &kodak_i2000_piranha_profile, 0U, background_ready,
                  sizeof(background_ready), setup, sizeof(setup)));
  expect_true("ready session setup matches vendor transformation",
              memcmp(setup, expected_ready_setup, sizeof(expected_ready_setup)) == 0);
  expect_true("initialized session is no longer all-zero",
              !kodak_i2000_session_info_is_uninitialized(session, sizeof(session)));
  expect_true("reject short initial-session destination",
              !kodak_i2000_build_initial_session_info(session, sizeof(session) - 1U));
  expect_true("reject null initial-session destination",
              !kodak_i2000_build_initial_session_info(NULL, sizeof(session)));
  expect_true(
      "reject short calibration state",
      !kodak_i2000_session_info_needs_calibration(session, sizeof(session) - 1U));
  expect_true("reject null calibration builder source",
              !kodak_i2000_build_calibration_ready_session_info(
                  NULL, sizeof(session), calibration_ready, sizeof(calibration_ready)));
  expect_true(
      "reject short calibration builder destination",
      !kodak_i2000_build_calibration_ready_session_info(
          session, sizeof(session), calibration_ready, sizeof(calibration_ready) - 1U));
  expect_true("reject null background builder destination",
              !kodak_i2000_build_background_ready_session_info(
                  calibration_ready, sizeof(calibration_ready), NULL,
                  sizeof(background_ready)));
}

static void
test_calibration_sdram_verification(void)
{
  uint8_t flash_record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH];
  uint8_t sdram_record[KODAK_I2000_IMAGE_CAL_MEMORY_LENGTH];
  size_t offset;

  for (offset = 0U; offset < sizeof(flash_record); ++offset)
    flash_record[offset] = (uint8_t)(offset ^ 0x5aU);
  memset(sdram_record, 0xa5, sizeof(sdram_record));
  for (offset = 0U; offset < 29U * 4U; offset += 4U) {
    sdram_record[offset] = flash_record[offset + 3U];
    sdram_record[offset + 1U] = flash_record[offset + 2U];
    sdram_record[offset + 2U] = flash_record[offset + 1U];
    sdram_record[offset + 3U] = flash_record[offset];
  }
  sdram_record[offset + 2U] = flash_record[offset + 1U];
  sdram_record[offset + 3U] = flash_record[offset];

  expect_true("accept vendor byte-swapped calibration SRAM",
              kodak_i2000_calibration_sdram_matches(flash_record, sizeof(flash_record),
                                                    sdram_record,
                                                    sizeof(sdram_record)));
  sdram_record[42] ^= 0x01U;
  expect_true("reject mismatched calibration SRAM",
              !kodak_i2000_calibration_sdram_matches(flash_record, sizeof(flash_record),
                                                     sdram_record,
                                                     sizeof(sdram_record)));
  sdram_record[42] ^= 0x01U;
  expect_true(
      "reject short calibration flash record",
      !kodak_i2000_calibration_sdram_matches(flash_record, sizeof(flash_record) - 1U,
                                             sdram_record, sizeof(sdram_record)));
  expect_true("reject null calibration SRAM record",
              !kodak_i2000_calibration_sdram_matches(flash_record, sizeof(flash_record),
                                                     NULL, sizeof(sdram_record)));
}

static void
test_cold_background_configuration(void)
{
  static const uint8_t captured[KODAK_I2000_CONFIG_LENGTH] = {
      0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x71, 0x11, 0x00, 0x00, 0x5e, 0x16, 0x00,
      0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xc5, 0x00,
      0xc5, 0x00, 0x00, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t configuration[KODAK_I2000_CONFIG_LENGTH];

  memset(configuration, 0xcc, sizeof(configuration));
  expect_true("build one-time cold background configuration",
              kodak_i2000_build_cold_background_scan_configuration(
                  configuration, sizeof(configuration)));
  expect_true("cold background configuration is capture exact",
              memcmp(configuration, captured, sizeof(configuration)) == 0);
  expect_true("reject short cold background configuration",
              !kodak_i2000_build_cold_background_scan_configuration(
                  configuration, sizeof(configuration) - 1U));
  expect_true("reject null cold background configuration",
              !kodak_i2000_build_cold_background_scan_configuration(
                  NULL, sizeof(configuration)));
}

static uint16_t
get_uint16_le(const uint8_t *value)
{
  return (uint16_t)value[0] | (uint16_t)((uint16_t)value[1] << 8);
}

static void
test_normal_scan_configuration(void)
{
  static const uint8_t captured_front_gray_200[KODAK_I2000_CONFIG_LENGTH] = {
      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x5d, 0x0e, 0x00, 0x00, 0x5e, 0x16,
      0x00, 0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00,
      0x01, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t captured_duplex_gray_200[KODAK_I2000_CONFIG_LENGTH] = {
      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x5d, 0x0e, 0x00, 0x00, 0x5e, 0x16,
      0x00, 0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00,
      0x01, 0x00, 0xc5, 0x00, 0xc5, 0x00, 0x03, 0x0f, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t captured_duplex_baffle[KODAK_I2000_CONFIG_LENGTH] = {
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5d, 0x0e, 0x00, 0x00, 0x5e, 0x16,
      0x00, 0x77, 0x03, 0x73, 0x01, 0x01, 0x77, 0x03, 0x73, 0x01, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x36, 0x0c, 0x5a, 0x0c, 0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0xc5, 0x00, 0xc5, 0x00, 0x33, 0x0f, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t configuration[KODAK_I2000_CONFIG_LENGTH];
  uint8_t captured_duplex_color_400[KODAK_I2000_CONFIG_LENGTH];
  uint8_t captured_duplex_color_400_baffle[KODAK_I2000_CONFIG_LENGTH];
  uint8_t one_sheet_duplex_gray_200[KODAK_I2000_CONFIG_LENGTH];

  memset(configuration, 0xcc, sizeof(configuration));
  expect_true("build captured front gray 200 configuration",
              kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_FRONT, KODAK_I2000_MODE_GRAY, 200U, configuration,
                  sizeof(configuration)));
  expect_true("front gray 200 remains byte-for-byte capture exact",
              memcmp(configuration, captured_front_gray_200, sizeof(configuration)) ==
                  0);

  expect_true("build rear color 400 configuration",
              kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_REAR, KODAK_I2000_MODE_COLOR, 400U, configuration,
                  sizeof(configuration)));
  expect_true("400 dpi speed ID",
              configuration[KODAK_I2000_CONFIG_SPEED_ID_OFFSET] == 2U);
  expect_true("color image data ID",
              configuration[KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET] == 0U);
  expect_true("rear-only front overscan disabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET) ==
                  0U);
  expect_true("rear-only rear overscan enabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET) ==
                  UINT16_C(0x00c5));
  expect_true("rear-only page FIFO enabled",
              configuration[KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET] == 0x02U);

  expect_true("build duplex gray 600 configuration",
              kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_GRAY, 600U, configuration,
                  sizeof(configuration)));
  expect_true("600 dpi speed ID",
              configuration[KODAK_I2000_CONFIG_SPEED_ID_OFFSET] == 2U);
  expect_true("duplex front overscan enabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET) ==
                  UINT16_C(0x00c5));
  expect_true("duplex rear overscan enabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET) ==
                  UINT16_C(0x00c5));
  expect_true("duplex page FIFOs enabled",
              configuration[KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET] == 0x03U);

  memcpy(captured_duplex_color_400, captured_duplex_gray_200,
         sizeof(captured_duplex_color_400));
  captured_duplex_color_400[KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET] = 0x77U;
  captured_duplex_color_400[KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET + 1U] = 0x02U;
  captured_duplex_color_400[KODAK_I2000_CONFIG_SPEED_ID_OFFSET] = 0x02U;
  captured_duplex_color_400[KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET] = 0x00U;
  expect_true("build captured duplex color 400 configuration",
              kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_COLOR, 400U,
                  configuration, sizeof(configuration)));
  expect_true("duplex color 400 remains byte-for-byte capture exact",
              memcmp(configuration, captured_duplex_color_400, sizeof(configuration)) ==
                  0);

  expect_true("build captured duplex gray 200 configuration",
              kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_GRAY, 200U, configuration,
                  sizeof(configuration)));
  expect_true("duplex gray 200 remains byte-for-byte capture exact",
              memcmp(configuration, captured_duplex_gray_200, sizeof(configuration)) ==
                  0);
  expect_true("capture-exact normal configuration is continuous",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_PAGE_COUNT_OFFSET) ==
                  0U);

  expect_true("build one-sheet duplex configuration",
              kodak_i2000_build_normal_scan_configuration_for_pages(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_GRAY, 200U, UINT16_C(1),
                  configuration, sizeof(configuration)));
  expect_true("one-sheet page count is little endian",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_PAGE_COUNT_OFFSET) ==
                  1U);
  memcpy(one_sheet_duplex_gray_200, captured_duplex_gray_200,
         sizeof(one_sheet_duplex_gray_200));
  one_sheet_duplex_gray_200[KODAK_I2000_CONFIG_PAGE_COUNT_OFFSET] = 1U;
  expect_true("only page count differs from continuous capture",
              memcmp(configuration, one_sheet_duplex_gray_200, sizeof(configuration)) ==
                  0);

  expect_true("300 dpi gray IP control",
              kodak_i2000_ip_control(KODAK_I2000_MODE_GRAY, 300U) == 0x00U);
  expect_true("600 dpi gray IP control",
              kodak_i2000_ip_control(KODAK_I2000_MODE_GRAY, 600U) == 0x30U);
  expect_true("600 dpi color IP control",
              kodak_i2000_ip_control(KODAK_I2000_MODE_COLOR, 600U) == 0x10U);
  expect_true("gray footer enabled",
              kodak_i2000_image_footer_only(KODAK_I2000_MODE_GRAY) == 1U);
  expect_true("color footer-only mode disabled",
              kodak_i2000_image_footer_only(KODAK_I2000_MODE_COLOR) == 0U);
  expect_true("300 dpi resolution footer",
              kodak_i2000_resolution_footer(300U) == 0x22U);
  expect_true("600 dpi resolution footer",
              kodak_i2000_resolution_footer(600U) == 0x00U);
  expect_true("front capture mask",
              kodak_i2000_capture_value(KODAK_I2000_SOURCE_FRONT) == 1U);
  expect_true("rear capture mask",
              kodak_i2000_capture_value(KODAK_I2000_SOURCE_REAR) == 2U);
  expect_true("duplex capture mask",
              kodak_i2000_capture_value(KODAK_I2000_SOURCE_DUPLEX) == 3U);

  expect_true("build duplex baffle configuration",
              kodak_i2000_build_baffle_scan_configuration(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_GRAY, 200U, configuration,
                  sizeof(configuration)));
  expect_true("duplex baffle front overscan enabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_FRONT_OVERSCAN_OFFSET) ==
                  UINT16_C(0x00c5));
  expect_true("duplex baffle rear overscan enabled",
              get_uint16_le(configuration + KODAK_I2000_CONFIG_REAR_OVERSCAN_OFFSET) ==
                  UINT16_C(0x00c5));
  expect_true("duplex baffle FIFOs enabled",
              configuration[KODAK_I2000_CONFIG_FIFO_ENABLE_OFFSET] == 0x33U);
  expect_true("duplex baffle remains byte-for-byte capture exact",
              memcmp(configuration, captured_duplex_baffle, sizeof(configuration)) ==
                  0);

  memcpy(captured_duplex_color_400_baffle, captured_duplex_baffle,
         sizeof(captured_duplex_color_400_baffle));
  captured_duplex_color_400_baffle[KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET] = 0x77U;
  captured_duplex_color_400_baffle[KODAK_I2000_CONFIG_GAP_LENGTH_OFFSET + 1U] = 0x02U;
  captured_duplex_color_400_baffle[KODAK_I2000_CONFIG_IMAGE_DATA_ID_OFFSET] = 0x00U;
  expect_true("build captured duplex color 400 baffle configuration",
              kodak_i2000_build_baffle_scan_configuration(
                  KODAK_I2000_SOURCE_DUPLEX, KODAK_I2000_MODE_COLOR, 400U,
                  configuration, sizeof(configuration)));
  expect_true("duplex color 400 baffle remains byte-for-byte capture exact",
              memcmp(configuration, captured_duplex_color_400_baffle,
                     sizeof(configuration)) == 0);

  expect_true("reject unsupported resolution",
              !kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_FRONT, KODAK_I2000_MODE_GRAY, 1200U, configuration,
                  sizeof(configuration)));
  expect_true("reject short scan configuration output",
              !kodak_i2000_build_normal_scan_configuration(
                  KODAK_I2000_SOURCE_FRONT, KODAK_I2000_MODE_GRAY, 200U, configuration,
                  sizeof(configuration) - 1U));
  expect_true("reject unsupported baffle resolution",
              !kodak_i2000_build_baffle_scan_configuration(
                  KODAK_I2000_SOURCE_FRONT, KODAK_I2000_MODE_GRAY, 1200U, configuration,
                  sizeof(configuration)));
}

static void
test_continuous_feeder_state(void)
{
  uint8_t status[KODAK_I2000_STATUS_LENGTH];

  memset(status, 0, sizeof(status));
  status[KODAK_I2000_STATUS_POWER_STATE_OFFSET] =
      KODAK_I2000_STATUS_POWER_STATE_OPERATIONAL;
  expect_true("operational idle empty feeder ends continuous job",
              kodak_i2000_status_is_idle_and_empty(status, sizeof(status)));

  status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET] = KODAK_I2000_STATUS_TRAY_PAPER_PRESENT;
  expect_true("paper-present feeder is not at end",
              !kodak_i2000_status_is_idle_and_empty(status, sizeof(status)));
  status[KODAK_I2000_STATUS_TRAY_STATE_OFFSET] = 0U;
  status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET] = 2U;
  expect_true("moving transport is not at end",
              !kodak_i2000_status_is_idle_and_empty(status, sizeof(status)));
  status[KODAK_I2000_STATUS_TRANSPORT_STATE_OFFSET] = 0U;
  status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] = 1U;
  expect_true("device error is not normal feeder end",
              !kodak_i2000_status_is_idle_and_empty(status, sizeof(status)));
  status[KODAK_I2000_STATUS_ERROR_CODE_OFFSET] = 0U;
  status[KODAK_I2000_STATUS_POWER_STATE_OFFSET] =
      KODAK_I2000_STATUS_POWER_STATE_LOW_POWER;
  expect_true("low-power state is not active-job end",
              !kodak_i2000_status_is_idle_and_empty(status, sizeof(status)));
  expect_true("reject short status record",
              !kodak_i2000_status_is_idle_and_empty(status, sizeof(status) - 1U));
  expect_true("reject null status record",
              !kodak_i2000_status_is_idle_and_empty(NULL, sizeof(status)));
}

static void
test_wake_and_button_protocol(void)
{
  static const uint8_t button_event[KODAK_I2000_INTERRUPT_EVENT_LENGTH] = {
      0x20, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00};
  uint8_t non_button[KODAK_I2000_INTERRUPT_EVENT_LENGTH];
  uint16_t button_number = 0U;

  expect_true("i2400 low-power exit request",
              KODAK_I2000_EXIT_LOW_POWER_REQUEST == 0x39U);
  expect_true("i2400 low-power exit operation", KODAK_I2000_EXIT_LOW_POWER_VALUE == 2U);
  expect_true("cold loader component id", KODAK_I2000_FIRMWARE_ID_LOADER == 2U);
  expect_true("running scanner component id", KODAK_I2000_FIRMWARE_ID_SCANNER == 3U);
  expect_true("firmware processor-stop request",
              KODAK_I2000_FIRMWARE_PROCESSOR_STOP_REQUEST == 0x21U);
  expect_true("FX2 RAM request and reset address",
              KODAK_I2000_INTERNAL_FIRMWARE_REQUEST == 0xa0U &&
                  KODAK_I2000_INTERNAL_FIRMWARE_RESET_VALUE == 0xe600U);
  expect_true("banked firmware selector and window",
              KODAK_I2000_FIRMWARE_BANK_REQUEST == 0xa3U &&
                  KODAK_I2000_FIRMWARE_BANK_SELECT_VALUE == 0xd80eU &&
                  KODAK_I2000_FIRMWARE_BANK_WRITE_BASE == 0x4000U);
  expect_true("FPGA program begin/end operation",
              KODAK_I2000_FPGA_PROGRAM_REQUEST == 0x20U &&
                  KODAK_I2000_FPGA_PROGRAM_BEGIN_VALUE == 1U &&
                  KODAK_I2000_FPGA_PROGRAM_END_VALUE == 0U);
  expect_true("FPGA memory diagnostic operation",
              KODAK_I2000_FPGA_MEMORY_TEST_REQUEST == 0xf1U &&
                  KODAK_I2000_FPGA_MEMORY_TEST_VALUE == 3U);
  expect_true("firmware transfer bounds", KODAK_I2000_FIRMWARE_CHUNK_LENGTH == 0x800U &&
                                              KODAK_I2000_BULK_OUT_ENDPOINT == 0x02U);
  expect_true("captured interrupt endpoint", KODAK_I2000_INTERRUPT_ENDPOINT == 0x88U);
  expect_true("decode OCP button event",
              kodak_i2000_decode_button_event(button_event, sizeof(button_event),
                                              &button_number));
  expect_true("decode little-endian OCP sequence", button_number == UINT16_C(0x1234));
  memcpy(non_button, button_event, sizeof(non_button));
  non_button[0] = KODAK_I2000_TRAY_STATE_EVENT;
  expect_true(
      "ignore non-button event",
      !kodak_i2000_decode_button_event(non_button, sizeof(non_button), &button_number));
  expect_true("reject short button event",
              !kodak_i2000_decode_button_event(button_event, sizeof(button_event) - 1U,
                                               &button_number));
}

int
main(void)
{
  test_model_table();
  test_front_session_setup();
  test_initial_session_info();
  test_calibration_sdram_verification();
  test_cold_background_configuration();
  test_normal_scan_configuration();
  test_continuous_feeder_state();
  test_wake_and_button_protocol();

  if (failures != 0) {
    fprintf(stderr, "%d Kodak i2000 protocol test(s) failed\n", failures);
    return 1;
  }
  puts("Kodak i2000 protocol tests passed");
  return 0;
}
