/*
 * Host test for the Cedar decoder wrapper's fallback contract
 * (src/cedar/cedar.c): on a host, or on a firmware without the vendor
 * libraries, cedar_load() must report a clean error and
 * cedar_sdl_available() must return 0, which is what makes the SDL backend
 * fall back to FFmpeg instead of failing the stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cedar/cedar.h"
#include "video/cedar_sdl.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #cond);                                                        \
      failures++;                                                            \
    }                                                                        \
  } while (0)

int main(void) {
  char err[160] = "";
  int ret;
  struct cedar_config cfg;

  /* A host has no /usr/lib/libvdecoder.so, so this is the no-vendor path. */
  ret = cedar_load(err, sizeof(err));
  if (ret == 0) {
    printf("cedar: vendor libraries present, fallback path not exercised\n");
    return 0;
  }

  printf("cedar load error: %s\n", err);
  CHECK(ret == -1);
  CHECK(strlen(err) > 0);
  CHECK(strstr(err, "libvdecoder.so") != NULL);

  /* The SDL backend asks this before choosing a decoder. */
  CHECK(cedar_sdl_available() == 0);

  /* And an explicit init must fail with a usable message, not crash. */
  memset(&cfg, 0, sizeof(cfg));
  err[0] = 0;
  CHECK(cedar_sdl_init(VIDEO_FORMAT_H264, 1280, 720, 60, err, sizeof(err)) == -1);
  CHECK(strlen(err) > 0);

  if (failures) {
    fprintf(stderr, "cedar fallback: %d failure(s)\n", failures);
    return 1;
  }
  printf("cedar fallback: ok\n");
  return 0;
}
