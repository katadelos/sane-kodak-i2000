/*
 * Firmware-container support for the native Kodak i2000 backend.
 *
 * The firmware bytes are not distributed with SANE.  This module reads either
 * the resource trailer used by the vendor's devicemanager container or a
 * compact bundle extracted from it, verifies the exact i2400 v4.14 images,
 * and expands the FPGA bitstream into its wire representation.  It never
 * executes the container and has no USB access.
 */
#ifndef KODAK_I2000_FIRMWARE_H
#define KODAK_I2000_FIRMWARE_H

#include <stddef.h>
#include <stdint.h>

#define KODAK_I2000_FIRMWARE_SHA256_LENGTH 32U
#define KODAK_I2000_FIRMWARE_FPGA_TRAILER_LENGTH 300U
#define KODAK_I2000_FIRMWARE_COMPACT_HEADER_LENGTH 64U

typedef enum kodak_i2000_firmware_result {
  KODAK_I2000_FIRMWARE_OK = 0,
  KODAK_I2000_FIRMWARE_INVALID,
  KODAK_I2000_FIRMWARE_IO_ERROR,
  KODAK_I2000_FIRMWARE_NO_MEMORY,
  KODAK_I2000_FIRMWARE_BAD_CONTAINER,
  KODAK_I2000_FIRMWARE_UNSUPPORTED_BUNDLE
} kodak_i2000_firmware_result;

typedef struct kodak_i2000_firmware_resource {
  const uint8_t *data;
  size_t size;
  uint64_t offset;
} kodak_i2000_firmware_resource;

typedef enum kodak_i2000_firmware_format {
  KODAK_I2000_FIRMWARE_FORMAT_UNKNOWN = 0,
  KODAK_I2000_FIRMWARE_FORMAT_VENDOR_CONTAINER,
  KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE
} kodak_i2000_firmware_format;

typedef struct kodak_i2000_firmware_bundle {
  uint8_t *storage;
  size_t storage_size;
  kodak_i2000_firmware_format format;
  kodak_i2000_firmware_resource loader;
  kodak_i2000_firmware_resource fpga;
  kodak_i2000_firmware_resource boot_image;
  kodak_i2000_firmware_resource scanner;
  kodak_i2000_firmware_resource scanner_banks[4];
} kodak_i2000_firmware_bundle;

/* Locate one named resource in an in-memory KDS resource container. */
kodak_i2000_firmware_result
kodak_i2000_firmware_find_resource(const uint8_t *container, size_t container_size,
                                   const char *name,
                                   kodak_i2000_firmware_resource *resource);

/* A small one-shot SHA-256 seam used for strict firmware validation. */
void kodak_i2000_firmware_sha256(const uint8_t *data, size_t size,
                                 uint8_t digest[KODAK_I2000_FIRMWARE_SHA256_LENGTH]);

/* Read and verify the exact i2400 resources from a vendor container or native
 * compact bundle.  No proprietary code is loaded or executed. */
kodak_i2000_firmware_result
kodak_i2000_firmware_open_i2400(const char *path, kodak_i2000_firmware_bundle *bundle);
void kodak_i2000_firmware_close(kodak_i2000_firmware_bundle *bundle);

/* Serialize only the seven authenticated images used by volatile i2400
 * startup into the native compact bundle format.  The caller owns *data. */
kodak_i2000_firmware_result
kodak_i2000_firmware_create_compact_i2400(const kodak_i2000_firmware_bundle *bundle,
                                          uint8_t **data, size_t *data_size);

/* Expand each FPGA source bit, least-significant bit first, to one byte and
 * append the 300 zero bytes used by the vendor loader. */
kodak_i2000_firmware_result
kodak_i2000_firmware_expand_fpga(const kodak_i2000_firmware_resource *fpga,
                                 uint8_t **expanded, size_t *expanded_size);

const char *kodak_i2000_firmware_result_string(kodak_i2000_firmware_result result);

#endif
