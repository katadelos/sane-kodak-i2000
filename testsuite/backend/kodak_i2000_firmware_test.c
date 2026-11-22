/* Scanner-free tests for the Kodak i2000 firmware-container seam. */
#include "backend/kodak_i2000_firmware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_RECORD_LENGTH 56U
#define TEST_MAGIC_LENGTH 9U

static unsigned failures;

static void
expect_true(const char *name, int condition)
{
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

static uint32_t
mutation_next(uint32_t *state)
{
  *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
  return *state;
}

static void
write_le64(uint8_t *destination, uint64_t value)
{
  unsigned index;

  for (index = 0U; index < 8U; ++index)
    destination[index] = (uint8_t)(value >> (index * 8U));
}

static void
write_record(uint8_t *container, size_t record_offset, const char *name,
             uint64_t data_offset, uint64_t data_size)
{
  size_t name_size = strlen(name);

  memset(container + record_offset, 0, TEST_RECORD_LENGTH);
  memcpy(container + record_offset, name, name_size);
  write_le64(container + record_offset + 32U, data_offset);
  write_le64(container + record_offset + 40U, data_size);
}

static void
digest_to_hex(const uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH], char hex[65])
{
  static const char digits[] = "0123456789abcdef";
  size_t index;

  for (index = 0U; index < KODAK_I2000_FIRMWARE_SHA256_LENGTH; ++index) {
    hex[index * 2U] = digits[digest[index] >> 4];
    hex[index * 2U + 1U] = digits[digest[index] & 0x0fU];
  }
  hex[64] = '\0';
}

static void
test_sha256(void)
{
  static const char empty_hash[] = "e3b0c44298fc1c149afbf4c8996fb924"
                                   "27ae41e4649b934ca495991b7852b855";
  static const char abc_hash[] = "ba7816bf8f01cfea414140de5dae2223"
                                 "b00361a396177a9cb410ff61f20015ad";
  uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH];
  char hex[65];

  kodak_i2000_firmware_sha256(NULL, 0U, digest);
  digest_to_hex(digest, hex);
  expect_true("SHA-256 empty vector", strcmp(hex, empty_hash) == 0);
  kodak_i2000_firmware_sha256((const uint8_t *)"abc", 3U, digest);
  digest_to_hex(digest, hex);
  expect_true("SHA-256 abc vector", strcmp(hex, abc_hash) == 0);
}

static void
test_resource_table(void)
{
  static const uint8_t magic[TEST_MAGIC_LENGTH] = {0x40, 0x21, 0x40, 0x6b, 0x64,
                                                   0x73, 0x40, 0x21, 0x40};
  uint8_t container[512];
  const size_t first_record = 320U;
  const size_t second_record = first_record + TEST_RECORD_LENGTH;
  const size_t magic_offset = second_record + TEST_RECORD_LENGTH;
  kodak_i2000_firmware_resource resource;
  kodak_i2000_firmware_result result;

  memset(container, 0xa5, sizeof(container));
  memcpy(container + 32U, "first", 5U);
  memcpy(container + 64U, "second-data", 11U);
  write_record(container, first_record, "FIRST", 32U, 5U);
  write_record(container, second_record, "SECOND", 64U, 11U);
  memcpy(container + magic_offset, magic, sizeof(magic));
  memset(container + magic_offset + sizeof(magic), 0,
         sizeof(container) - magic_offset - sizeof(magic));

  result = kodak_i2000_firmware_find_resource(container, sizeof(container), "SECOND",
                                              &resource);
  expect_true("find latest resource", result == KODAK_I2000_FIRMWARE_OK);
  expect_true("latest resource offset", resource.offset == 64U);
  expect_true("latest resource size", resource.size == 11U);
  expect_true("latest resource contents",
              memcmp(resource.data, "second-data", 11U) == 0);

  result = kodak_i2000_firmware_find_resource(container, sizeof(container), "FIRST",
                                              &resource);
  expect_true("find earlier resource", result == KODAK_I2000_FIRMWARE_OK);
  expect_true("earlier resource contents",
              resource.size == 5U && memcmp(resource.data, "first", 5U) == 0);

  result = kodak_i2000_firmware_find_resource(container, sizeof(container), "MISSING",
                                              &resource);
  expect_true("reject missing resource", result == KODAK_I2000_FIRMWARE_BAD_CONTAINER);

  container[second_record + 48U] = 1U;
  result = kodak_i2000_firmware_find_resource(container, sizeof(container), "SECOND",
                                              &resource);
  expect_true("reject nonzero record reservation",
              result == KODAK_I2000_FIRMWARE_BAD_CONTAINER);
  container[second_record + 48U] = 0U;

  write_record(container, first_record, "SECOND", 32U, 5U);
  result = kodak_i2000_firmware_find_resource(container, sizeof(container), "SECOND",
                                              &resource);
  expect_true("reject duplicate resource name",
              result == KODAK_I2000_FIRMWARE_BAD_CONTAINER);
}

static void
test_resource_mutation_sweep(void)
{
  static const uint8_t magic[TEST_MAGIC_LENGTH] = {0x40, 0x21, 0x40, 0x6b, 0x64,
                                                   0x73, 0x40, 0x21, 0x40};
  uint8_t base[512];
  uint8_t candidate[512];
  uint8_t random_bytes[512];
  const size_t first_record = 320U;
  const size_t second_record = first_record + TEST_RECORD_LENGTH;
  const size_t magic_offset = second_record + TEST_RECORD_LENGTH;
  uint32_t state = UINT32_C(0x4b445332);
  unsigned accepted = 0U;
  unsigned rejected = 0U;
  unsigned iteration;

  memset(base, 0xa5, sizeof(base));
  memcpy(base + 32U, "first", 5U);
  memcpy(base + 64U, "second-data", 11U);
  write_record(base, first_record, "FIRST", 32U, 5U);
  write_record(base, second_record, "SECOND", 64U, 11U);
  memcpy(base + magic_offset, magic, sizeof(magic));
  memset(base + magic_offset + sizeof(magic), 0,
         sizeof(base) - magic_offset - sizeof(magic));

  /* Exercise valid, damaged, and truncated table layouts with a fixed seed so
   * every normal and sanitizer test run covers the same parser boundaries. */
  for (iteration = 0U; iteration < 2048U; ++iteration) {
    kodak_i2000_firmware_resource resource;
    kodak_i2000_firmware_result result;
    size_t supplied_size = sizeof(candidate);
    unsigned flips = 1U + mutation_next(&state) % 4U;
    unsigned flip;

    memcpy(candidate, base, sizeof(candidate));
    for (flip = 0U; flip < flips; ++flip) {
      size_t offset = mutation_next(&state) % sizeof(candidate);
      candidate[offset] ^= (uint8_t)(1U << (mutation_next(&state) & UINT32_C(7)));
    }
    if ((iteration & 3U) == 0U)
      supplied_size = mutation_next(&state) % (sizeof(candidate) + 1U);
    result = kodak_i2000_firmware_find_resource(candidate, supplied_size, "SECOND",
                                                &resource);
    expect_true("mutated table has a bounded result",
                result == KODAK_I2000_FIRMWARE_OK ||
                    result == KODAK_I2000_FIRMWARE_BAD_CONTAINER);
    if (result == KODAK_I2000_FIRMWARE_OK) {
      int bounds_ok = resource.offset <= (uint64_t)supplied_size;
      if (bounds_ok)
        bounds_ok = resource.size <= supplied_size - (size_t)resource.offset;
      expect_true("mutated resource remains inside supplied data", bounds_ok);
      if (bounds_ok)
        expect_true("mutated resource pointer matches decoded offset",
                    resource.data == candidate + (size_t)resource.offset);
      ++accepted;
    } else {
      ++rejected;
    }
  }

  /* Unstructured input should also fail safely at every possible short size. */
  for (iteration = 0U; iteration < 1024U; ++iteration) {
    kodak_i2000_firmware_resource resource;
    kodak_i2000_firmware_result result;
    size_t supplied_size = mutation_next(&state) % (sizeof(random_bytes) + 1U);
    size_t index;

    for (index = 0U; index < supplied_size; ++index)
      random_bytes[index] = (uint8_t)(mutation_next(&state) >> 24);
    result = kodak_i2000_firmware_find_resource(random_bytes, supplied_size, "LOADER",
                                                &resource);
    expect_true("random table has a bounded result",
                result == KODAK_I2000_FIRMWARE_OK ||
                    result == KODAK_I2000_FIRMWARE_BAD_CONTAINER);
    if (result == KODAK_I2000_FIRMWARE_OK) {
      int bounds_ok = resource.offset <= (uint64_t)supplied_size;
      if (bounds_ok)
        bounds_ok = resource.size <= supplied_size - (size_t)resource.offset;
      expect_true("random resource remains inside supplied data", bounds_ok);
    }
  }
  expect_true("mutation sweep exercises accepted tables", accepted != 0U);
  expect_true("mutation sweep exercises rejected tables", rejected != 0U);
}

static void
test_fpga_expansion(void)
{
  static const uint8_t input[] = {0x81, 0x02};
  static const uint8_t expected[] = {1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0};
  kodak_i2000_firmware_resource fpga;
  kodak_i2000_firmware_result result;
  uint8_t *expanded = NULL;
  size_t expanded_size = 0U;
  size_t index;

  memset(&fpga, 0, sizeof(fpga));
  fpga.data = input;
  fpga.size = sizeof(input);
  result = kodak_i2000_firmware_expand_fpga(&fpga, &expanded, &expanded_size);
  expect_true("expand FPGA fixture", result == KODAK_I2000_FIRMWARE_OK);
  expect_true("expanded FPGA size",
              expanded_size ==
                  sizeof(expected) + KODAK_I2000_FIRMWARE_FPGA_TRAILER_LENGTH);
  expect_true("expanded FPGA bit order",
              expanded != NULL && memcmp(expanded, expected, sizeof(expected)) == 0);
  for (index = sizeof(expected); index < expanded_size; ++index)
    if (expanded[index] != 0U) {
      expect_true("expanded FPGA zero trailer", 0);
      break;
    }
  free(expanded);
}

static void
test_real_bundle(const char *path)
{
  kodak_i2000_firmware_bundle bundle;
  kodak_i2000_firmware_bundle compact_bundle;
  kodak_i2000_firmware_result result;
  uint8_t *compact = NULL;
  size_t compact_size = 0U;
  char temporary_path[] = "/tmp/kodak-i2000-fw-test-XXXXXX";
  FILE *temporary_file;
  int descriptor = -1;
  unsigned index;

  result = kodak_i2000_firmware_open_i2400(path, &bundle);
  if (result != KODAK_I2000_FIRMWARE_OK) {
    fprintf(stderr, "FAIL: %s: %s\n", path, kodak_i2000_firmware_result_string(result));
    ++failures;
    return;
  }
  expect_true("recognized firmware source format",
              bundle.format == KODAK_I2000_FIRMWARE_FORMAT_VENDOR_CONTAINER ||
                  bundle.format == KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE);
  expect_true("real loader size", bundle.loader.size == 0x4000U);
  expect_true("real FPGA size", bundle.fpga.size == 0xaf6e9U);
  if (bundle.format == KODAK_I2000_FIRMWARE_FORMAT_VENDOR_CONTAINER)
    expect_true("vendor boot image size", bundle.boot_image.size == 0x4000U);
  else
    expect_true("compact source omits boot image",
                bundle.boot_image.data == NULL && bundle.boot_image.size == 0U);
  expect_true("real scanner image size", bundle.scanner.size == 0x4000U);
  printf("validated i2400 firmware bundle: %s (%lu bytes)\n", path,
         (unsigned long)bundle.storage_size);

  result = kodak_i2000_firmware_create_compact_i2400(&bundle, &compact, &compact_size);
  expect_true("create compact firmware bundle",
              result == KODAK_I2000_FIRMWARE_OK && compact != NULL);
  expect_true("compact firmware bundle size", compact_size == 882473U);
  if (bundle.format == KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE)
    expect_true("compact serialization is deterministic",
                compact != NULL && compact_size == bundle.storage_size &&
                    memcmp(compact, bundle.storage, compact_size) == 0);
  if (compact != NULL && compact_size > KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH) {
    descriptor = mkstemp(temporary_path);
    expect_true("create compact firmware test file", descriptor >= 0);
    temporary_file = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
    expect_true("open compact firmware test stream", temporary_file != NULL);
  } else {
    temporary_file = NULL;
  }
  if (temporary_file != NULL) {
    expect_true("write compact firmware test file",
                fwrite(compact, 1U, compact_size, temporary_file) == compact_size);
    expect_true("close compact firmware test file", fclose(temporary_file) == 0);
    result = kodak_i2000_firmware_open_i2400(temporary_path, &compact_bundle);
    expect_true("reopen compact firmware bundle", result == KODAK_I2000_FIRMWARE_OK);
    if (result == KODAK_I2000_FIRMWARE_OK) {
      expect_true("compact source format",
                  compact_bundle.format == KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE);
      expect_true("compact omits boot image", compact_bundle.boot_image.data == NULL &&
                                                  compact_bundle.boot_image.size == 0U);
      expect_true("compact loader round trip",
                  memcmp(compact_bundle.loader.data, bundle.loader.data,
                         bundle.loader.size) == 0);
      expect_true(
          "compact FPGA round trip",
          memcmp(compact_bundle.fpga.data, bundle.fpga.data, bundle.fpga.size) == 0);
      expect_true("compact scanner round trip",
                  memcmp(compact_bundle.scanner.data, bundle.scanner.data,
                         bundle.scanner.size) == 0);
      for (index = 0U; index < 4U; ++index)
        expect_true("compact scanner bank round trip",
                    memcmp(compact_bundle.scanner_banks[index].data,
                           bundle.scanner_banks[index].data,
                           bundle.scanner_banks[index].size) == 0);
      kodak_i2000_firmware_close(&compact_bundle);
    }

    compact[KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH] ^= 0x01U;
    temporary_file = fopen(temporary_path, "wb");
    expect_true("reopen compact mutation file", temporary_file != NULL);
    if (temporary_file != NULL) {
      expect_true("write compact mutation",
                  fwrite(compact, 1U, compact_size, temporary_file) == compact_size);
      expect_true("close compact mutation file", fclose(temporary_file) == 0);
      result = kodak_i2000_firmware_open_i2400(temporary_path, &compact_bundle);
      expect_true("reject compact payload mutation",
                  result == KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE);
      if (result == KODAK_I2000_FIRMWARE_OK)
        kodak_i2000_firmware_close(&compact_bundle);
    }
  } else if (descriptor >= 0) {
    close(descriptor);
  }
  unlink(temporary_path);
  free(compact);
  kodak_i2000_firmware_close(&bundle);
}

int
main(int argc, char **argv)
{
  test_sha256();
  test_resource_table();
  test_resource_mutation_sweep();
  test_fpga_expansion();
  if (argc > 1)
    test_real_bundle(argv[1]);
  if (failures != 0U) {
    fprintf(stderr, "%u firmware test(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  puts("Kodak i2000 firmware tests passed");
  return EXIT_SUCCESS;
}
