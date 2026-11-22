/*
 * Transport-independent firmware-container support for Kodak i2000 devices.
 *
 * The native backend does not ship, link, dlopen, or execute proprietary
 * objects.  A user may point it at the vendor's v4.14 devicemanager container
 * or at a compact bundle previously extracted from that container.  Both
 * inputs are authenticated before any volatile i2400 firmware bytes are used.
 */
#include "kodak_i2000_firmware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KDS_RESOURCE_NAME_LENGTH 32U
#define KDS_RESOURCE_RECORD_LENGTH 56U
#define KDS_RESOURCE_OFFSET_OFFSET 32U
#define KDS_RESOURCE_SIZE_OFFSET 40U
#define KDS_RESOURCE_RESERVED_OFFSET 48U
#define KDS_RESOURCE_MAX_RECORDS 256U
#define KODAK_I2000_FIRMWARE_MAX_CONTAINER_SIZE ((size_t)16U * 1024U * 1024U)
#define KODAK_I2000_COMPACT_FORMAT_VERSION UINT32_C(1)
#define KODAK_I2000_COMPACT_VENDOR_ID UINT16_C(0x040a)
#define KODAK_I2000_COMPACT_PRODUCT_ID UINT16_C(0x601c)
#define KODAK_I2000_COMPACT_VERSION_OFFSET 8U
#define KODAK_I2000_COMPACT_HEADER_SIZE_OFFSET 12U
#define KODAK_I2000_COMPACT_VENDOR_ID_OFFSET 16U
#define KODAK_I2000_COMPACT_PRODUCT_ID_OFFSET 18U
#define KODAK_I2000_COMPACT_SCANNER_VERSION_OFFSET 20U
#define KODAK_I2000_COMPACT_PAYLOAD_SIZE_OFFSET 24U
#define KODAK_I2000_COMPACT_PAYLOAD_DIGEST_OFFSET 32U

static const uint8_t kds_resource_magic[] = {0x40, 0x21, 0x40, 0x6b, 0x64,
                                             0x73, 0x40, 0x21, 0x40};

static const uint8_t compact_bundle_magic[] = {0x4b, 0x44, 0x49, 0x32,
                                               0x46, 0x57, 0x0d, 0x0a};

typedef struct expected_resource {
  const char *name;
  size_t size;
  const char *sha256;
} expected_resource;

static const uint8_t i2400_package_version[] = {'0', '1', '8', '0', '0', '0', '0', '0'};
static const uint8_t i2400_booter_version[] = {0x0d, 0x02, 0x01, 0x00};
static const uint8_t i2400_fpga_version[] = {0x00, 0x1f, 0x00};
static const uint8_t i2400_loader_version[] = {0x0e, 0x01, 0x04, 0x00};
static const uint8_t i2400_scanner_version[] = {0x0f, 0x02, 0x01, 0x00};

static const expected_resource i2400_booter = {
    "BOOTER", 0x1b43U,
    "ed15c2d034dfd0f1e2c5aad7d3834ed133261ca3d126ec6d240fc83566c15299"};
static const expected_resource i2400_loader = {
    "LOADER", 0x4000U,
    "ebd9fce4f04288dca0e2a7a33a938f7b2f943faaf81cd6531fe43261a78dd547"};
static const expected_resource i2400_fpga = {
    "FPGA", 0xaf6e9U,
    "5696cf568925aef48c92c88843b64c5a28007d1955a1c5d5407932e4f1ab924a"};
static const expected_resource i2400_boot_image = {
    "BOOTIMAGE", 0x4000U,
    "56f2cf6f33dd7269c38a2a360f54899660b938ab0b4895701cea03f64ff29f95"};
static const expected_resource i2400_scanner_banks[4] = {
    {"SCANNER0", 0x8000U,
     "c35020473aed1b4642cd726cad727b63fff2824ad68cedd7ffb73c7cbd890479"},
    {"SCANNER1", 0x8000U,
     "a86d8c526e33671d67a559f7d56c273a8842febe737001ba5e64cdfbec32cb03"},
    {"SCANNER2", 0x8000U,
     "c745bac511102f529d989190baddd0a88c4a11f6c6013e54ec86f016d3c0b31c"},
    {"SCANNER3", 0x8000U,
     "b4bbf27d3ad8ef183fe7f50effd1efd2ce9f39d65b24e5c1ba140234ff6e00f3"}};
static const expected_resource i2400_scanner = {
    "SCANNER", 0x4000U,
    "af8499a4607621181b0c9b9b5c21c61b46b94ed8b77631cd2c4735b62d349dee"};

#define KODAK_I2000_COMPACT_PAYLOAD_LENGTH (0x4000U + 0xaf6e9U + 4U * 0x8000U + 0x4000U)

static uint16_t
read_le16(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8);
}

static uint32_t
read_le32(const uint8_t *data)
{
  return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
         (uint32_t)data[3] << 24;
}

static uint64_t
read_le64(const uint8_t *data)
{
  uint64_t value = 0U;
  unsigned index;

  for (index = 0U; index < 8U; ++index)
    value |= (uint64_t)data[index] << (index * 8U);
  return value;
}

static void
write_le16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static void
write_le32(uint8_t *data, uint32_t value)
{
  unsigned index;

  for (index = 0U; index < 4U; ++index)
    data[index] = (uint8_t)(value >> (index * 8U));
}

static void
write_le64(uint8_t *data, uint64_t value)
{
  unsigned index;

  for (index = 0U; index < 8U; ++index)
    data[index] = (uint8_t)(value >> (index * 8U));
}

static int
valid_resource_name(const uint8_t *record, size_t *name_length)
{
  size_t index;
  size_t length = KDS_RESOURCE_NAME_LENGTH;

  if (record == NULL || name_length == NULL || record[0] == 0U)
    return 0;
  for (index = 0U; index < KDS_RESOURCE_NAME_LENGTH; ++index) {
    uint8_t value = record[index];
    if (value == 0U) {
      length = index;
      break;
    }
    if (!((value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
          (value >= (uint8_t)'0' && value <= (uint8_t)'9') || value == (uint8_t)'_'))
      return 0;
  }
  if (length == KDS_RESOURCE_NAME_LENGTH)
    return 0;
  for (index = length; index < KDS_RESOURCE_NAME_LENGTH; ++index)
    if (record[index] != 0U)
      return 0;
  *name_length = length;
  return 1;
}

static int
decode_resource_record(const uint8_t *container, size_t record_offset,
                       size_t *name_length, kodak_i2000_firmware_resource *resource)
{
  const uint8_t *record;
  uint64_t offset;
  uint64_t size;
  size_t index;

  if (container == NULL || name_length == NULL || resource == NULL)
    return 0;
  record = container + record_offset;
  if (!valid_resource_name(record, name_length))
    return 0;
  for (index = KDS_RESOURCE_RESERVED_OFFSET; index < KDS_RESOURCE_RECORD_LENGTH;
       ++index)
    if (record[index] != 0U)
      return 0;

  offset = read_le64(record + KDS_RESOURCE_OFFSET_OFFSET);
  size = read_le64(record + KDS_RESOURCE_SIZE_OFFSET);
  /* Resources precede their trailer.  This also prevents an overflow while
   * converting the resource bounds to the host's size_t. */
  if (size == 0U || offset > (uint64_t)record_offset ||
      size > (uint64_t)record_offset - offset || offset > (uint64_t)SIZE_MAX ||
      size > (uint64_t)SIZE_MAX)
    return 0;
  resource->data = container + (size_t)offset;
  resource->size = (size_t)size;
  resource->offset = offset;
  return 1;
}

static int
find_resource_magic(const uint8_t *container, size_t container_size,
                    size_t *magic_offset)
{
  size_t position;

  if (container == NULL || magic_offset == NULL ||
      container_size < sizeof(kds_resource_magic))
    return 0;
  position = container_size - sizeof(kds_resource_magic) + 1U;
  while (position != 0U) {
    --position;
    if (memcmp(container + position, kds_resource_magic, sizeof(kds_resource_magic)) ==
        0) {
      *magic_offset = position;
      return 1;
    }
  }
  return 0;
}

kodak_i2000_firmware_result
kodak_i2000_firmware_find_resource(const uint8_t *container, size_t container_size,
                                   const char *name,
                                   kodak_i2000_firmware_resource *resource)
{
  size_t magic_offset;
  size_t record_offset;
  size_t wanted_length;
  unsigned record_count = 0U;
  int found = 0;

  if (container == NULL || name == NULL || resource == NULL)
    return KODAK_I2000_FIRMWARE_INVALID;
  memset(resource, 0, sizeof(*resource));
  wanted_length = strlen(name);
  if (wanted_length == 0U || wanted_length >= KDS_RESOURCE_NAME_LENGTH ||
      !find_resource_magic(container, container_size, &magic_offset))
    return KODAK_I2000_FIRMWARE_BAD_CONTAINER;

  record_offset = magic_offset;
  while (record_offset >= KDS_RESOURCE_RECORD_LENGTH &&
         record_count < KDS_RESOURCE_MAX_RECORDS) {
    kodak_i2000_firmware_resource candidate;
    size_t candidate_name_length;

    record_offset -= KDS_RESOURCE_RECORD_LENGTH;
    memset(&candidate, 0, sizeof(candidate));
    if (!decode_resource_record(container, record_offset, &candidate_name_length,
                                &candidate))
      break;
    ++record_count;
    if (candidate_name_length == wanted_length &&
        memcmp(container + record_offset, name, wanted_length) == 0) {
      if (found)
        return KODAK_I2000_FIRMWARE_BAD_CONTAINER;
      *resource = candidate;
      found = 1;
    }
  }
  return found ? KODAK_I2000_FIRMWARE_OK : KODAK_I2000_FIRMWARE_BAD_CONTAINER;
}

static uint32_t
rotate_right(uint32_t value, unsigned amount)
{
  return (value >> amount) | (value << (32U - amount));
}

static uint32_t
read_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void
sha256_transform(uint32_t state[8], const uint8_t block[64])
{
  static const uint32_t constants[64] = {
      UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
      UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
      UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
      UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
      UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
      UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
      UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
      UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
      UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
      UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
      UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
      UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
      UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
      UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
      UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
      UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
      UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
      UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
      UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
      UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
      UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
      UINT32_C(0xc67178f2)};
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  unsigned index;

  for (index = 0U; index < 16U; ++index)
    words[index] = read_be32(block + index * 4U);
  for (index = 16U; index < 64U; ++index) {
    uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                  rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
    uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                  rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];
  for (index = 0U; index < 64U; ++index) {
    uint32_t sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
    uint32_t choice = (e & f) ^ ((~e) & g);
    uint32_t temporary1 = h + sum1 + choice + constants[index] + words[index];
    uint32_t sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temporary2 = sum0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void
kodak_i2000_firmware_sha256(const uint8_t *data, size_t size,
                            uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH])
{
  uint32_t state[8] = {UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
                       UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
                       UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)};
  uint8_t final_blocks[128];
  size_t remaining;
  size_t final_size;
  size_t index;
  uint64_t bit_length;

  if (digest == NULL)
    return;
  if (data == NULL && size != 0U) {
    memset(digest, 0, KODAK_I2000_FIRMWARE_SHA256_LENGTH);
    return;
  }
  bit_length = (uint64_t)size * UINT64_C(8);
  while (size >= 64U) {
    sha256_transform(state, data);
    data += 64U;
    size -= 64U;
  }
  remaining = size;
  memset(final_blocks, 0, sizeof(final_blocks));
  if (remaining != 0U)
    memcpy(final_blocks, data, remaining);
  final_blocks[remaining] = 0x80U;
  final_size = remaining < 56U ? 64U : 128U;
  for (index = 0U; index < 8U; ++index)
    final_blocks[final_size - 1U - index] = (uint8_t)(bit_length >> (index * 8U));
  sha256_transform(state, final_blocks);
  if (final_size == 128U)
    sha256_transform(state, final_blocks + 64U);
  for (index = 0U; index < 8U; ++index) {
    digest[index * 4U] = (uint8_t)(state[index] >> 24);
    digest[index * 4U + 1U] = (uint8_t)(state[index] >> 16);
    digest[index * 4U + 2U] = (uint8_t)(state[index] >> 8);
    digest[index * 4U + 3U] = (uint8_t)state[index];
  }
}

static int
hex_nibble(char value, uint8_t *nibble)
{
  if (value >= '0' && value <= '9')
    *nibble = (uint8_t)(value - '0');
  else if (value >= 'a' && value <= 'f')
    *nibble = (uint8_t)(value - 'a' + 10);
  else if (value >= 'A' && value <= 'F')
    *nibble = (uint8_t)(value - 'A' + 10);
  else
    return 0;
  return 1;
}

static int
digest_matches(const uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH],
               const char *expected)
{
  size_t index;

  if (expected == NULL || strlen(expected) != 64U)
    return 0;
  for (index = 0U; index < KODAK_I2000_FIRMWARE_SHA256_LENGTH; ++index) {
    uint8_t high;
    uint8_t low;
    if (!hex_nibble(expected[index * 2U], &high) ||
        !hex_nibble(expected[index * 2U + 1U], &low) ||
        digest[index] != (uint8_t)((high << 4) | low))
      return 0;
  }
  return 1;
}

static kodak_i2000_firmware_result
verify_pinned_resource(const kodak_i2000_firmware_resource *resource,
                       const expected_resource *expected)
{
  uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH];

  if (resource == NULL || expected == NULL || resource->data == NULL ||
      resource->size != expected->size)
    return KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE;
  kodak_i2000_firmware_sha256(resource->data, resource->size, digest);
  return digest_matches(digest, expected->sha256)
             ? KODAK_I2000_FIRMWARE_OK
             : KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE;
}

static kodak_i2000_firmware_result
verify_resource(const uint8_t *container, size_t container_size,
                const expected_resource *expected,
                kodak_i2000_firmware_resource *resource)
{
  kodak_i2000_firmware_result result;

  result = kodak_i2000_firmware_find_resource(container, container_size, expected->name,
                                              resource);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  return verify_pinned_resource(resource, expected);
}

static kodak_i2000_firmware_result
verify_literal_resource(const uint8_t *container, size_t container_size,
                        const char *name, const uint8_t *expected, size_t expected_size)
{
  kodak_i2000_firmware_resource resource;
  kodak_i2000_firmware_result result;

  result =
      kodak_i2000_firmware_find_resource(container, container_size, name, &resource);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  return resource.size == expected_size &&
                 memcmp(resource.data, expected, expected_size) == 0
             ? KODAK_I2000_FIRMWARE_OK
             : KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE;
}

static kodak_i2000_firmware_result
read_container(const char *path, uint8_t **storage, size_t *storage_size)
{
  FILE *file;
  long length;
  uint8_t *buffer;
  size_t read_size;
  int close_result;

  if (path == NULL || storage == NULL || storage_size == NULL || *path == '\0')
    return KODAK_I2000_FIRMWARE_INVALID;
  *storage = NULL;
  *storage_size = 0U;
  file = fopen(path, "rb");
  if (file == NULL)
    return KODAK_I2000_FIRMWARE_IO_ERROR;
  if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) <= 0L ||
      (unsigned long)length > (unsigned long)KODAK_I2000_FIRMWARE_MAX_CONTAINER_SIZE ||
      fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    return KODAK_I2000_FIRMWARE_IO_ERROR;
  }
  buffer = (uint8_t *)malloc((size_t)length);
  if (buffer == NULL) {
    fclose(file);
    return KODAK_I2000_FIRMWARE_NO_MEMORY;
  }
  read_size = fread(buffer, 1U, (size_t)length, file);
  /* Close independently of the short-read result; using || directly would
   * skip fclose() when fread() returned fewer bytes than requested. */
  close_result = fclose(file);
  if (read_size != (size_t)length || close_result != 0) {
    free(buffer);
    return KODAK_I2000_FIRMWARE_IO_ERROR;
  }
  *storage = buffer;
  *storage_size = read_size;
  return KODAK_I2000_FIRMWARE_OK;
}

static kodak_i2000_firmware_result
validate_vendor_i2400(kodak_i2000_firmware_bundle *candidate)
{
  kodak_i2000_firmware_resource unused;
  kodak_i2000_firmware_result result;
  unsigned index;

#define VERIFY_LITERAL(name, bytes)                                                    \
  do {                                                                                 \
    result = verify_literal_resource(candidate->storage, candidate->storage_size,      \
                                     (name), (bytes), sizeof(bytes));                  \
    if (result != KODAK_I2000_FIRMWARE_OK)                                             \
      return result;                                                                   \
  } while (0)
  VERIFY_LITERAL("FWPKGVERSION", i2400_package_version);
  VERIFY_LITERAL("FWVERSION_BOOTER", i2400_booter_version);
  VERIFY_LITERAL("FWVERSION_FPGA", i2400_fpga_version);
  VERIFY_LITERAL("FWVERSION_LOADER", i2400_loader_version);
  VERIFY_LITERAL("FWVERSION_SCANNER", i2400_scanner_version);
#undef VERIFY_LITERAL

  result = verify_resource(candidate->storage, candidate->storage_size, &i2400_booter,
                           &unused);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  result = verify_resource(candidate->storage, candidate->storage_size, &i2400_loader,
                           &candidate->loader);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  result = verify_resource(candidate->storage, candidate->storage_size, &i2400_fpga,
                           &candidate->fpga);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  result = verify_resource(candidate->storage, candidate->storage_size,
                           &i2400_boot_image, &candidate->boot_image);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  for (index = 0U; index < 4U; ++index) {
    result =
        verify_resource(candidate->storage, candidate->storage_size,
                        &i2400_scanner_banks[index], &candidate->scanner_banks[index]);
    if (result != KODAK_I2000_FIRMWARE_OK)
      return result;
  }
  result = verify_resource(candidate->storage, candidate->storage_size, &i2400_scanner,
                           &candidate->scanner);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  candidate->format = KODAK_I2000_FIRMWARE_FORMAT_VENDOR_CONTAINER;
  return KODAK_I2000_FIRMWARE_OK;
}

static kodak_i2000_firmware_result
take_compact_resource(kodak_i2000_firmware_bundle *candidate, size_t *offset,
                      const expected_resource *expected,
                      kodak_i2000_firmware_resource *resource)
{
  if (candidate == NULL || offset == NULL || expected == NULL || resource == NULL ||
      *offset > candidate->storage_size ||
      expected->size > candidate->storage_size - *offset)
    return KODAK_I2000_FIRMWARE_BAD_CONTAINER;
  resource->data = candidate->storage + *offset;
  resource->size = expected->size;
  resource->offset = (uint64_t)*offset;
  *offset += expected->size;
  return verify_pinned_resource(resource, expected);
}

static kodak_i2000_firmware_result
validate_compact_i2400(kodak_i2000_firmware_bundle *candidate)
{
  uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH];
  kodak_i2000_firmware_result result;
  size_t offset;
  unsigned index;

  if (candidate == NULL ||
      candidate->storage_size != KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH +
                                     KODAK_I2000_COMPACT_PAYLOAD_LENGTH ||
      read_le32(candidate->storage + KODAK_I2000_COMPACT_VERSION_OFFSET) !=
          KODAK_I2000_COMPACT_FORMAT_VERSION ||
      read_le32(candidate->storage + KODAK_I2000_COMPACT_HEADER_SIZE_OFFSET) !=
          KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH ||
      read_le64(candidate->storage + KODAK_I2000_COMPACT_PAYLOAD_SIZE_OFFSET) !=
          KODAK_I2000_COMPACT_PAYLOAD_LENGTH)
    return KODAK_I2000_FIRMWARE_BAD_CONTAINER;
  if (read_le16(candidate->storage + KODAK_I2000_COMPACT_VENDOR_ID_OFFSET) !=
          KODAK_I2000_COMPACT_VENDOR_ID ||
      read_le16(candidate->storage + KODAK_I2000_COMPACT_PRODUCT_ID_OFFSET) !=
          KODAK_I2000_COMPACT_PRODUCT_ID ||
      memcmp(candidate->storage + KODAK_I2000_COMPACT_SCANNER_VERSION_OFFSET,
             i2400_scanner_version, sizeof(i2400_scanner_version)) != 0)
    return KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE;

  kodak_i2000_firmware_sha256(candidate->storage +
                                  KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH,
                              KODAK_I2000_COMPACT_PAYLOAD_LENGTH, digest);
  if (memcmp(digest, candidate->storage + KODAK_I2000_COMPACT_PAYLOAD_DIGEST_OFFSET,
             sizeof(digest)) != 0)
    return KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE;

  offset = KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH;
  result = take_compact_resource(candidate, &offset, &i2400_loader, &candidate->loader);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  result = take_compact_resource(candidate, &offset, &i2400_fpga, &candidate->fpga);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  for (index = 0U; index < 4U; ++index) {
    result = take_compact_resource(candidate, &offset, &i2400_scanner_banks[index],
                                   &candidate->scanner_banks[index]);
    if (result != KODAK_I2000_FIRMWARE_OK)
      return result;
  }
  result =
      take_compact_resource(candidate, &offset, &i2400_scanner, &candidate->scanner);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  if (offset != candidate->storage_size)
    return KODAK_I2000_FIRMWARE_BAD_CONTAINER;
  candidate->format = KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE;
  return KODAK_I2000_FIRMWARE_OK;
}

kodak_i2000_firmware_result
kodak_i2000_firmware_open_i2400(const char *path, kodak_i2000_firmware_bundle *bundle)
{
  kodak_i2000_firmware_bundle candidate;
  kodak_i2000_firmware_result result;

  if (bundle == NULL)
    return KODAK_I2000_FIRMWARE_INVALID;
  memset(bundle, 0, sizeof(*bundle));
  memset(&candidate, 0, sizeof(candidate));
  result = read_container(path, &candidate.storage, &candidate.storage_size);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;

  if (candidate.storage_size >= sizeof(compact_bundle_magic) &&
      memcmp(candidate.storage, compact_bundle_magic, sizeof(compact_bundle_magic)) ==
          0)
    result = validate_compact_i2400(&candidate);
  else
    result = validate_vendor_i2400(&candidate);
  if (result == KODAK_I2000_FIRMWARE_OK) {
    *bundle = candidate;
    return result;
  }

  free(candidate.storage);
  return result;
}

void
kodak_i2000_firmware_close(kodak_i2000_firmware_bundle *bundle)
{
  if (bundle == NULL)
    return;
  free(bundle->storage);
  memset(bundle, 0, sizeof(*bundle));
}

static kodak_i2000_firmware_result
append_compact_resource(uint8_t *output, size_t output_size, size_t *offset,
                        const kodak_i2000_firmware_resource *resource,
                        const expected_resource *expected)
{
  kodak_i2000_firmware_result result;

  if (output == NULL || offset == NULL || expected == NULL || *offset > output_size ||
      expected->size > output_size - *offset)
    return KODAK_I2000_FIRMWARE_INVALID;
  result = verify_pinned_resource(resource, expected);
  if (result != KODAK_I2000_FIRMWARE_OK)
    return result;
  memcpy(output + *offset, resource->data, resource->size);
  *offset += resource->size;
  return KODAK_I2000_FIRMWARE_OK;
}

kodak_i2000_firmware_result
kodak_i2000_firmware_create_compact_i2400(const kodak_i2000_firmware_bundle *bundle,
                                          uint8_t **data, size_t *data_size)
{
  const size_t total_size =
      KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH + KODAK_I2000_COMPACT_PAYLOAD_LENGTH;
  kodak_i2000_firmware_result result;
  uint8_t *output;
  size_t offset;
  unsigned index;

  if (bundle == NULL || data == NULL || data_size == NULL)
    return KODAK_I2000_FIRMWARE_INVALID;
  *data = NULL;
  *data_size = 0U;
  output = (uint8_t *)calloc(total_size, 1U);
  if (output == NULL)
    return KODAK_I2000_FIRMWARE_NO_MEMORY;

  memcpy(output, compact_bundle_magic, sizeof(compact_bundle_magic));
  write_le32(output + KODAK_I2000_COMPACT_VERSION_OFFSET,
             KODAK_I2000_COMPACT_FORMAT_VERSION);
  write_le32(output + KODAK_I2000_COMPACT_HEADER_SIZE_OFFSET,
             KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH);
  write_le16(output + KODAK_I2000_COMPACT_VENDOR_ID_OFFSET,
             KODAK_I2000_COMPACT_VENDOR_ID);
  write_le16(output + KODAK_I2000_COMPACT_PRODUCT_ID_OFFSET,
             KODAK_I2000_COMPACT_PRODUCT_ID);
  memcpy(output + KODAK_I2000_COMPACT_SCANNER_VERSION_OFFSET, i2400_scanner_version,
         sizeof(i2400_scanner_version));
  write_le64(output + KODAK_I2000_COMPACT_PAYLOAD_SIZE_OFFSET,
             KODAK_I2000_COMPACT_PAYLOAD_LENGTH);

  offset = KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH;
  result = append_compact_resource(output, total_size, &offset, &bundle->loader,
                                   &i2400_loader);
  if (result != KODAK_I2000_FIRMWARE_OK)
    goto failure;
  result =
      append_compact_resource(output, total_size, &offset, &bundle->fpga, &i2400_fpga);
  if (result != KODAK_I2000_FIRMWARE_OK)
    goto failure;
  for (index = 0U; index < 4U; ++index) {
    result = append_compact_resource(output, total_size, &offset,
                                     &bundle->scanner_banks[index],
                                     &i2400_scanner_banks[index]);
    if (result != KODAK_I2000_FIRMWARE_OK)
      goto failure;
  }
  result = append_compact_resource(output, total_size, &offset, &bundle->scanner,
                                   &i2400_scanner);
  if (result != KODAK_I2000_FIRMWARE_OK)
    goto failure;
  if (offset != total_size) {
    result = KODAK_I2000_FIRMWARE_INVALID;
    goto failure;
  }
  kodak_i2000_firmware_sha256(output + KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH,
                              KODAK_I2000_COMPACT_PAYLOAD_LENGTH,
                              output + KODAK_I2000_COMPACT_PAYLOAD_DIGEST_OFFSET);
  *data = output;
  *data_size = total_size;
  return KODAK_I2000_FIRMWARE_OK;

failure:
  free(output);
  return result;
}

kodak_i2000_firmware_result
kodak_i2000_firmware_expand_fpga(const kodak_i2000_firmware_resource *fpga,
                                 uint8_t **expanded, size_t *expanded_size)
{
  uint8_t *output;
  size_t output_size;
  size_t source_index;

  if (fpga == NULL || fpga->data == NULL || fpga->size == 0U || expanded == NULL ||
      expanded_size == NULL ||
      fpga->size > (SIZE_MAX - KODAK_I2000_FIRMWARE_FPGA_TRAILER_LENGTH) / 8U)
    return KODAK_I2000_FIRMWARE_INVALID;
  *expanded = NULL;
  *expanded_size = 0U;
  output_size = fpga->size * 8U + KODAK_I2000_FIRMWARE_FPGA_TRAILER_LENGTH;
  output = (uint8_t *)calloc(output_size, 1U);
  if (output == NULL)
    return KODAK_I2000_FIRMWARE_NO_MEMORY;
  for (source_index = 0U; source_index < fpga->size; ++source_index) {
    unsigned bit;
    for (bit = 0U; bit < 8U; ++bit)
      output[source_index * 8U + bit] =
          (uint8_t)((fpga->data[source_index] >> bit) & 0x01U);
  }
  *expanded = output;
  *expanded_size = output_size;
  return KODAK_I2000_FIRMWARE_OK;
}

const char *
kodak_i2000_firmware_result_string(kodak_i2000_firmware_result result)
{
  switch (result) {
  case KODAK_I2000_FIRMWARE_OK:
    return "success";
  case KODAK_I2000_FIRMWARE_INVALID:
    return "invalid argument";
  case KODAK_I2000_FIRMWARE_IO_ERROR:
    return "firmware source I/O error";
  case KODAK_I2000_FIRMWARE_NO_MEMORY:
    return "out of memory";
  case KODAK_I2000_FIRMWARE_BAD_CONTAINER:
    return "invalid firmware container";
  case KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE:
    return "unsupported or corrupted firmware bundle";
  }
  return "unknown firmware error";
}
