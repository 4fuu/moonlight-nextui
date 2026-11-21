/*
 * Host test for the decoder selection contract (src/video/cedar_sdl.c):
 * when the vendor libraries load, cedar_sdl_init() must succeed; when they
 * do not, it must fail and leave a message in err.
 *
 * A bug here is invisible at the API level: a reversed cedar_load() test made
 * the engine fall back to FFmpeg with an empty error on the device. The test
 * therefore builds mock libvdecoder.so/libvideoengine.so with the symbols the
 * wrapper resolves, points cedar_load() at them with MOONLIGHT_CEDAR_LIBDIR,
 * and requires cedar_sdl_init() to return 0 for H.264 and HEVC; without the
 * libraries it must return -1 with a message. The mock decoder decodes
 * nothing: this is about the path from "libraries load" to "init succeeds".
 *
 * (A library that dlopen()s but is unusable is not covered: glibc's loader
 * keeps a record of a failed object that crashes the process at exit, so that
 * case is left to the device.)
 *
 * With the same mocks it also covers the SemTimedWait interposition: the
 * mock vendor library looks the symbol up with dlsym(RTLD_DEFAULT), as
 * libcdc_base does, and must find our implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video/cedar_sdl.h"

#include <Limelight.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #cond);                                                        \
      failures++;                                                            \
    }                                                                        \
  } while (0)

/*
 * The mock's own source, compiled at test time with the same compiler that
 * builds the test, so no second toolchain is needed. Layouts follow
 * cedarc_abi.h; only the fields the wrapper and the init path touch.
 */
static const char mock_source[] =
  "#define _GNU_SOURCE\n"
  "#include <dlfcn.h>\n"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include <stdlib.h>\n"
  "#include <string.h>\n"
  "typedef struct { int32_t codec, w, h, fps, dur, ar, is3d, csdlen; char *csd;\n"
  "  int32_t s1, s2, framePackage, refNum, reopen, cts; } Info;\n"
  "typedef struct { int32_t v[0x34]; } VConfig; /* 0xd0 bytes */\n"
  "typedef struct { int32_t id, index, fmt, w, h, stride, top, left, bottom,\n"
  "  right, fps, ar, prog, tff, rtf, pad; int64_t pts, pcr;\n"
  "  char *d0, *d1, *d2, *d3; char rest[0x150 - 0x70]; } Picture;\n"
  "typedef struct { char *data; int32_t len, pad; int64_t pts, pcr;\n"
  "  int32_t first, last, id, index, valid; uint32_t flags; void *vi; } DataInfo;\n"
  "typedef struct { Info info; VConfig vc; int submitted; char buf[4096]; } Dec;\n"
  "void *CreateVideoDecoder(void) { return calloc(1, sizeof(Dec)); }\n"
  "void DestroyVideoDecoder(void *d) { free(d); }\n"
  "int InitializeVideoDecoder(void *dp, Info *info, VConfig *vc) {\n"
  "  Dec *d = dp; d->info = *info; d->vc = *vc; return 0; }\n"
  "void ResetVideoDecoder(void *d) { ((Dec *)d)->submitted = 0; }\n"
  "int RequestVideoStreamBuffer(void *dp, int size, char **buf, int *bs,\n"
  "                             char **ring, int *rs, int i) {\n"
  "  Dec *d = dp; (void)i; if (size > (int)sizeof(d->buf)) return -1;\n"
  "  *buf = d->buf; *bs = size; *ring = d->buf; *rs = 0; return 0; }\n"
  "int SubmitVideoStreamData(void *dp, DataInfo *di, int i) {\n"
  "  Dec *d = dp; (void)i; if (!di->valid || di->len <= 0) return -1;\n"
  "  d->submitted++; return 0; }\n"
  "int DecodeVideoStream(void *dp, int e, int k, int dr, int64_t now) {\n"
  "  Dec *d = dp; (void)e; (void)k; (void)dr; (void)now;\n"
  "  if (d->submitted == 0) return 5; d->submitted--; return 1; }\n"
  "Picture *RequestPicture(void *d, int i) { (void)d; (void)i; return 0; }\n"
  "int ReturnPicture(void *d, Picture *p) { (void)d; (void)p; return 0; }\n"
  "int ValidPictureNum(void *d, int i) { (void)d; (void)i; return 0; }\n"
  "int VideoStreamFrameNum(void *d, int i) { (void)d; (void)i; return 0; }\n"
  "int AddVDPluginSingle(char *path) { (void)path; return 0; }\n"
  "/* What libcdc_base does at its first wait. */\n"
  "int mock_interposed(void) {\n"
  "  return dlsym(RTLD_DEFAULT, \"SemTimedWait\") != NULL ? 1 : 0; }\n";

static char libdir[512];

static void write_file(const char *path, const char *data) {
  FILE *f = fopen(path, "w");

  if (f == NULL) {
    perror(path);
    exit(1);
  }
  fputs(data, f);
  fclose(f);
}

static int run(const char *cmd) {
  int rc = system(cmd);

  return rc == 0 ? 0 : -1;
}

static void build_mocks(void) {
  char src[600], cmd[4096];

  snprintf(libdir, sizeof(libdir), "/tmp/cedar-mock-XXXXXX");
  if (mkdtemp(libdir) == NULL) {
    perror("mkdtemp");
    exit(1);
  }
  snprintf(src, sizeof(src), "%s/mock.c", libdir);
  write_file(src, mock_source);

  snprintf(cmd, sizeof(cmd),
           "cc -shared -fPIC -O1 -Wl,--no-as-needed -o %s/libvdecoder.so %s && "
           "cp %s/libvdecoder.so %s/libvideoengine.so && "
           ": > %s/libawh264.so && : > %s/libawh265.so",
           libdir, src, libdir, libdir, libdir, libdir);
  if (run(cmd) != 0) {
    fprintf(stderr, "cedar mock: cannot build the mock libraries\n");
    exit(1);
  }
}

int main(void) {
  char err[160];
  int rc;

  /* 1. No vendor libraries: fail, and say why. */
  unsetenv("MOONLIGHT_CEDAR_LIBDIR");
  memset(err, 'X', sizeof(err));   /* a caller that did not initialise it */
  rc = cedar_sdl_init(VIDEO_FORMAT_H264, 1024, 768, 60, err, sizeof(err));
  CHECK(rc == -1);
  CHECK(memchr(err, 'X', sizeof(err)) != (void *)err);  /* NUL-terminated */
  CHECK(strlen(err) > 0);
  CHECK(cedar_sdl_available() == 0);

  /* 2. Libraries that load: init must succeed (the regression). */
  build_mocks();
  setenv("MOONLIGHT_CEDAR_LIBDIR", libdir, 1);
  err[0] = 'X';
  rc = cedar_sdl_init(VIDEO_FORMAT_H264, 1024, 768, 60, err, sizeof(err));
  if (rc != 0)
    fprintf(stderr, "cedar mock: init failed with %s\n", err);
  CHECK(rc == 0);
  CHECK(cedar_sdl_available() == 1);
  cedar_sdl_destroy();

  /* The vendor libraries must find our SemTimedWait through dlsym. */
  {
    char path[600];
    void *h;

    snprintf(path, sizeof(path), "%s/libvdecoder.so", libdir);
    /*
     * As on the device: the vendor libraries are loaded with RTLD_GLOBAL and
     * find SemTimedWait in the executable's dynamic symbol table, which
     * sdl-cedar-hwdec.patch guarantees (ENABLE_EXPORTS). This test binary
     * needs the same -rdynamic when it is linked (see the workflow).
     */
    h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    int (*interposed)(void);

    CHECK(h != NULL);
    interposed = (int (*)(void))dlsym(h, "mock_interposed");
    CHECK(interposed != NULL);
    if (interposed != NULL)
      CHECK(interposed() == 1);
  }

  /* HEVC is registered by the same mock plugin set. */
  rc = cedar_sdl_init(VIDEO_FORMAT_H265, 1024, 768, 60, err, sizeof(err));
  CHECK(rc == 0);
  cedar_sdl_destroy();

  if (failures) {
    fprintf(stderr, "cedar init: %d failure(s)\n", failures);
    return 1;
  }
  printf("cedar init: ok\n");
  return 0;
}
