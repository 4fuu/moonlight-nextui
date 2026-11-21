/*
 * Host test for the Cedar picture -> YV12 conversion (video/cedar_sdl.c).
 *
 * The conversion is the only part of the hardware path that runs on every
 * frame and cannot be checked on the host any other way: it has no vendor
 * library dependency, so the engine's own copy is linked in directly and
 * fed hand-built pictures.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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

/* A picture whose luma is (row * 16 + col) and chroma words (u << 8 | v). */
static void fill_picture(CedarVideoPicture* p, unsigned char* y,
                         unsigned char* c, int stride, int w, int h,
                         int fmt) {
  memset(p, 0, sizeof(*p));
  p->ePixelFormat = fmt;
  p->nWidth = w;
  p->nHeight = h;
  p->nLineStride = stride;
  p->nLeftOffset = 0;
  p->nTopOffset = 0;
  p->nRightOffset = w;
  p->nBottomOffset = h;
  p->pData0 = (char*)y;
  p->pData1 = (char*)c;
  for (int r = 0; r < h; r++)
    for (int x = 0; x < stride; x++)
      y[r * stride + x] = (unsigned char)(r * 16 + x);
  for (int r = 0; r < h / 2; r++)
    for (int x = 0; x < stride / 2; x++) {
      unsigned char u = (unsigned char)(100 + r);
      unsigned char v = (unsigned char)(200 + x);
      c[r * stride + 2 * x] = fmt == CEDAR_PIXEL_NV21 ? v : u;
      c[r * stride + 2 * x + 1] = fmt == CEDAR_PIXEL_NV21 ? u : v;
    }
}

static void test_nv21_to_yv12(void) {
  enum { W = 16, H = 8, STRIDE = 32 };  /* padded stride: the real 1280 case */
  unsigned char y[STRIDE * H], c[STRIDE * H / 2];
  unsigned char dy[W * H], du[W * H / 4], dv[W * H / 4];
  CedarVideoPicture p;
  int ok = 1;

  fill_picture(&p, y, c, STRIDE, W, H, CEDAR_PIXEL_NV21);
  CHECK(cedar_sdl_convert(&p, dy, du, dv, W, W / 2, W / 2) == 0);
  for (int r = 0; r < H; r++)
    for (int x = 0; x < W; x++)
      if (dy[r * W + x] != (unsigned char)(r * 16 + x))
        ok = 0;
  CHECK(ok);
  /* NV21 stores V first: U must come from the second byte. */
  ok = 1;
  for (int r = 0; r < H / 2; r++)
    for (int x = 0; x < W / 2; x++) {
      if (du[r * W / 2 + x] != (unsigned char)(100 + r))
        ok = 0;
      if (dv[r * W / 2 + x] != (unsigned char)(200 + x))
        ok = 0;
    }
  CHECK(ok);
}

static void test_nv12_to_yv12(void) {
  enum { W = 16, H = 8, STRIDE = 16 };
  unsigned char y[STRIDE * H], c[STRIDE * H / 2];
  unsigned char dy[W * H], du[W * H / 4], dv[W * H / 4];
  CedarVideoPicture p;
  int ok = 1;

  fill_picture(&p, y, c, STRIDE, W, H, CEDAR_PIXEL_NV12);
  CHECK(cedar_sdl_convert(&p, dy, du, dv, W, W / 2, W / 2) == 0);
  for (int r = 0; r < H / 2; r++)
    for (int x = 0; x < W / 2; x++) {
      if (du[r * W / 2 + x] != (unsigned char)(100 + r))
        ok = 0;
      if (dv[r * W / 2 + x] != (unsigned char)(200 + x))
        ok = 0;
    }
  CHECK(ok);
}

static void test_crop(void) {
  enum { W = 16, H = 8, STRIDE = 32 };
  unsigned char y[STRIDE * H], c[STRIDE * H / 2];
  unsigned char dy[W * H], du[W * H / 4], dv[W * H / 4];
  CedarVideoPicture p;
  int ok = 1;

  fill_picture(&p, y, c, STRIDE, W, H, CEDAR_PIXEL_NV21);
  /* A 10x6 window at (4, 2), as a cropped stream would report. */
  p.nLeftOffset = 4;
  p.nTopOffset = 2;
  p.nRightOffset = 14;
  p.nBottomOffset = 8;
  CHECK(cedar_sdl_convert(&p, dy, du, dv, 10, 5, 5) == 0);
  for (int r = 0; r < 6; r++)
    for (int x = 0; x < 10; x++)
      if (dy[r * 10 + x] != (unsigned char)((r + 2) * 16 + x + 4))
        ok = 0;
  CHECK(ok);
}

static void test_unsupported_format(void) {
  CedarVideoPicture p;
  unsigned char y[16], u[4], v[4];

  memset(&p, 0, sizeof(p));
  p.ePixelFormat = CEDAR_PIXEL_YV12;  /* planar: not converted here */
  p.nWidth = 4;
  p.nHeight = 4;
  p.nLineStride = 4;
  p.pData0 = (char*)y;
  p.pData1 = (char*)u;
  CHECK(cedar_sdl_convert(&p, y, u, v, 4, 2, 2) == -1);
}

int main(void) {
  test_nv21_to_yv12();
  test_nv12_to_yv12();
  test_crop();
  test_unsupported_format();
  if (failures) {
    fprintf(stderr, "cedar convert: %d failure(s)\n", failures);
    return 1;
  }
  printf("cedar convert: ok\n");
  return 0;
}
