/* Extract and verify compact volatile firmware bundles for kodak_i2000. */
#include "backend/kodak_i2000_firmware.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(const char *program)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s verify SOURCE\n"
          "  %s extract SOURCE OUTPUT\n",
          program, program);
}

static const char *
format_name(kodak_i2000_firmware_format format)
{
  switch (format) {
  case KODAK_I2000_FIRMWARE_FORMAT_VENDOR_CONTAINER:
    return "authenticated vendor container";
  case KODAK_I2000_FIRMWARE_FORMAT_COMPACT_BUNDLE:
    return "compact i2400 bundle";
  case KODAK_I2000_FIRMWARE_FORMAT_UNKNOWN:
    break;
  }
  return "unknown format";
}

static int
write_exclusive(const char *path, const uint8_t *data, size_t size)
{
  size_t offset = 0U;
  int descriptor;

  descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (descriptor < 0)
    return 0;
  while (offset < size) {
    ssize_t result = write(descriptor, data + offset, size - offset);
    if (result < 0 && errno == EINTR)
      continue;
    if (result <= 0) {
      int saved_errno = result < 0 ? errno : EIO;
      close(descriptor);
      unlink(path);
      errno = saved_errno;
      return 0;
    }
    offset += (size_t)result;
  }
  if (close(descriptor) != 0) {
    int saved_errno = errno;
    unlink(path);
    errno = saved_errno;
    return 0;
  }
  return 1;
}

static int
verify_source(const char *path)
{
  kodak_i2000_firmware_bundle bundle;
  kodak_i2000_firmware_result result;

  result = kodak_i2000_firmware_open_i2400(path, &bundle);
  if (result != KODAK_I2000_FIRMWARE_OK) {
    fprintf(stderr, "%s: %s\n", path, kodak_i2000_firmware_result_string(result));
    return EXIT_FAILURE;
  }
  printf("%s: %s, %lu bytes; authenticated for 040a:601c\n", path,
         format_name(bundle.format), (unsigned long)bundle.storage_size);
  kodak_i2000_firmware_close(&bundle);
  return EXIT_SUCCESS;
}

static int
extract_source(const char *source, const char *output)
{
  kodak_i2000_firmware_bundle bundle;
  kodak_i2000_firmware_result result;
  uint8_t *compact = NULL;
  size_t compact_size = 0U;
  int exit_status = EXIT_FAILURE;

  result = kodak_i2000_firmware_open_i2400(source, &bundle);
  if (result != KODAK_I2000_FIRMWARE_OK) {
    fprintf(stderr, "%s: %s\n", source, kodak_i2000_firmware_result_string(result));
    return EXIT_FAILURE;
  }
  result = kodak_i2000_firmware_create_compact_i2400(&bundle, &compact, &compact_size);
  if (result != KODAK_I2000_FIRMWARE_OK) {
    fprintf(stderr, "%s: %s\n", source, kodak_i2000_firmware_result_string(result));
    goto done;
  }
  if (!write_exclusive(output, compact, compact_size)) {
    fprintf(stderr, "%s: %s\n", output, strerror(errno));
    goto done;
  }
  printf("wrote %s (%lu bytes); contains only authenticated volatile i2400 "
         "startup images\n",
         output, (unsigned long)compact_size);
  exit_status = EXIT_SUCCESS;

done:
  free(compact);
  kodak_i2000_firmware_close(&bundle);
  return exit_status;
}

int
main(int argc, char **argv)
{
  if (argc == 3 && strcmp(argv[1], "verify") == 0)
    return verify_source(argv[2]);
  if (argc == 4 && strcmp(argv[1], "extract") == 0)
    return extract_source(argv[2], argv[3]);
  usage(argv[0]);
  return EXIT_FAILURE;
}
