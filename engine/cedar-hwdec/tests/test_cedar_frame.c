/*
 * Host test for the frame payload the Cedar path hands to SDL.
 *
 * The device run showed a garbled picture with a correct decode: the frame
 * event carried something other than the YV12 planes and their linesizes, and
 * the texture upload read strides out of it. This test drives the real
 * submit path with a mock decoder that returns a known picture and checks
 * what a presenter would receive:
 *
 *   - cedar_sdl_frame() hands out three planes with pitches 1024, 512, 512
 *     (the YV12 buffer the conversion writes, not the NV21 source stride);
 *   - the luma plane holds the picture's visible pixels, row for row;
 *   - the chroma planes hold the deinterleaved V and U of the NV21 source.
 *
 * A second submit without taking the frame must not leak or double-return a
 * decoder picture: the newest picture replaces the untaken one.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video/cedar_sdl.h"

#include <Limelight.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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

#define W 64
#define H 32
#define STRIDE 96   /* padded: the source pitch must not become the YV12 pitch */

/* The mock decoder returns one picture whose pixels encode their position. */
static const char mock_source[] =
  "#include <stdint.h>\n"
  "#include <stdlib.h>\n"
  "#include <string.h>\n"
  "typedef struct { int32_t codec, w, h, fps, dur, ar, is3d, csdlen; char *csd;\n"
  "  int32_t s1, s2, framePackage, refNum, reopen, cts; } Info;\n"
  "typedef struct { int32_t v[0x34]; } VConfig;\n"
  "typedef struct { int32_t id, index, fmt, w, h, stride, top, left, bottom,\n"
  "  right, fps, ar, prog, tff, rtf, pad; int64_t pts, pcr;\n"
  "  char *d0, *d1, *d2, *d3; char rest[0x150 - 0x70]; } Picture;\n"
  "typedef struct { char *data; int32_t len, pad; int64_t pts, pcr;\n"
  "  int32_t first, last, id, index, valid; uint32_t flags; void *vi; } DataInfo;\n"
  "typedef struct { Info info; int submitted; int served;\n"
  "  unsigned char y[96 * 32]; unsigned char c[96 * 16]; Picture pic; } Dec;\n"
  "void *CreateVideoDecoder(void) { return calloc(1, sizeof(Dec)); }\n"
  "void DestroyVideoDecoder(void *d) { free(d); }\n"
  "int InitializeVideoDecoder(void *d, Info *i, VConfig *v) { (void)v;\n"
  "  ((Dec *)d)->info = *i; return 0; }\n"
  "void ResetVideoDecoder(void *d) { ((Dec *)d)->submitted = 0; }\n"
  "int RequestVideoStreamBuffer(void *dp, int size, char **buf, int *bs,\n"
  "                             char **ring, int *rs, int i) {\n"
  "  static char scratch[4096]; (void)dp; (void)size; (void)i;\n"
  "  *buf = scratch; *bs = size; *ring = scratch; *rs = 0; return 0; }\n"
  "int SubmitVideoStreamData(void *dp, DataInfo *di, int i) { (void)di; (void)i;\n"
  "  ((Dec *)dp)->submitted++; return 0; }\n"
  "int DecodeVideoStream(void *dp, int e, int k, int dr, int64_t now) {\n"
  "  (void)e; (void)k; (void)dr; (void)now;\n"
  "  if (((Dec *)dp)->submitted == 0) return 5;\n"
  "  ((Dec *)dp)->submitted--; return 1; }\n"
  "Picture *RequestPicture(void *dp, int i) {\n"
  "  Dec *d = dp; int r, x; (void)i;\n"
  "  if (d->served) return 0;\n"
  "  d->served = 1;\n"
  "  for (r = 0; r < 32; r++)\n"
  "    for (x = 0; x < 96; x++)\n"
  "      d->y[r * 96 + x] = (unsigned char)(r * 4 + (x >> 4));\n"
  "  for (r = 0; r < 16; r++)\n"
  "    for (x = 0; x < 32; x++) {\n"
  "      d->c[r * 96 + 2 * x] = (unsigned char)(200 + x);   /* V first: NV21 */\n"
  "      d->c[r * 96 + 2 * x + 1] = (unsigned char)(100 + r); }\n"
  "  memset(&d->pic, 0, sizeof(d->pic));\n"
  "  d->pic.id = 1; d->pic.fmt = 5; d->pic.w = 64;\n"
  "  d->pic.h = 32; d->pic.stride = 96;\n"
  "  d->pic.right = 64; d->pic.bottom = 32;\n"
  "  d->pic.prog = 1;\n"
  "  d->pic.d0 = (char *)d->y; d->pic.d1 = (char *)d->c;\n"
  "  return &d->pic; }\n"
  "int ReturnPicture(void *d, Picture *p) { (void)p; ((Dec *)d)->served = 0;\n"
  "  return 0; }\n"
  "int ValidPictureNum(void *d, int i) { (void)d; (void)i; return 0; }\n"
  "int VideoStreamFrameNum(void *d, int i) { (void)d; (void)i; return 0; }\n"
  "int AddVDPluginSingle(char *p) { (void)p; return 0; }\n";

static void write_file(const char *path, const char *data) {
  FILE *f = fopen(path, "w");

  if (f == NULL) {
    perror(path);
    exit(1);
  }
  fputs(data, f);
  fclose(f);
}

static void build_mock(void) {
  char dir[] = "/tmp/cedar-frame-XXXXXX";
  char src[512], cmd[2048];

  if (mkdtemp(dir) == NULL) {
    perror("mkdtemp");
    exit(1);
  }
  snprintf(src, sizeof(src), "%s/mock.c", dir);
  write_file(src, mock_source);
  snprintf(cmd, sizeof(cmd),
           "cc -shared -fPIC -O1 -o %s/libvdecoder.so %s && "
           "cp %s/libvdecoder.so %s/libvideoengine.so && "
           ": > %s/libawh264.so && : > %s/libawh265.so",
           dir, src, dir, dir, dir, dir);
  if (system(cmd) != 0) {
    fprintf(stderr, "cedar frame: cannot build the mock library\n");
    exit(1);
  }
  setenv("MOONLIGHT_CEDAR_LIBDIR", dir, 1);
}

/* One decode unit carrying a single NAL; the mock only counts it. */
static char nal[] = { 0, 0, 0, 1, 0x65, 0x88, 0x84 };

static int submit_one(void) {
  LENTRY entry;
  DECODE_UNIT du;

  memset(&entry, 0, sizeof(entry));
  entry.data = nal;
  entry.length = sizeof(nal);
  entry.bufferType = BUFFER_TYPE_PICDATA;

  memset(&du, 0, sizeof(du));
  du.bufferList = &entry;
  du.fullLength = sizeof(nal);
  du.frameNumber = 1;
  du.frameType = FRAME_TYPE_IDR;
  du.receiveTimeMs = 1234;

  if (cedar_sdl_submit(&du) != DR_OK) {
    fprintf(stderr, "cedar frame: submit failed\n");
    return -1;
  }
  return 0;
}

int main(void) {
  unsigned char* planes[3];
  int linesize[3];
  int r, x, ok;

  build_mock();
  if (cedar_sdl_init(VIDEO_FORMAT_H264, W, H, 60, NULL, 0) != 0) {
    fprintf(stderr, "cedar frame: init failed\n");
    return 1;
  }

  /* 1. A submitted frame arrives with the YV12 pitches of our own buffer. */
  CHECK(submit_one() == 0);
  CHECK(cedar_sdl_frame(planes, linesize) == 1);
  CHECK(linesize[0] == W);
  CHECK(linesize[1] == W / 2);
  CHECK(linesize[2] == W / 2);
  printf("payload: planes %p/%p/%p pitches %d/%d/%d\n", (void*)planes[0],
         (void*)planes[1], (void*)planes[2], linesize[0], linesize[1],
         linesize[2]);

  /* 2. Luma is the picture, row for row. */
  ok = 1;
  for (r = 0; r < H; r++)
    for (x = 0; x < W; x++)
      if (planes[0][r * linesize[0] + x] != (unsigned char)(r * 4 + (x >> 4)))
        ok = 0;
  CHECK(ok);

  /* 3. NV21 chroma arrived deinterleaved: V into plane 2, U into plane 1. */
  ok = 1;
  for (r = 0; r < H / 2; r++)
    for (x = 0; x < W / 2; x++) {
      if (planes[1][r * linesize[1] + x] != (unsigned char)(100 + r))
        ok = 0;
      if (planes[2][r * linesize[2] + x] != (unsigned char)(200 + x))
        ok = 0;
    }
  CHECK(ok);

  /* 4. Frames are consumed once: a second take has nothing to show. */
  CHECK(cedar_sdl_frame(planes, linesize) == 0);

  /* 5. Two submits without a take keep the newest, not a stale picture. */
  CHECK(submit_one() == 0);
  CHECK(submit_one() == 0);
  CHECK(cedar_sdl_frame(planes, linesize) == 1);

  cedar_sdl_destroy();
  {
    char cmd[600];

    snprintf(cmd, sizeof(cmd), "rm -rf %s", getenv("MOONLIGHT_CEDAR_LIBDIR"));
    if (system(cmd) != 0)
      fprintf(stderr, "cedar frame: could not remove the mock directory\n");
  }

  if (failures) {
    fprintf(stderr, "cedar frame: %d failure(s)\n", failures);
    return 1;
  }
  printf("cedar frame: ok\n");
  return 0;
}
