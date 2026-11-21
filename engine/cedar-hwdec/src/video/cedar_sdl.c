/*
 * Cedar hardware decode for the SDL backend.
 *
 * This file is part of Moonlight Embedded.
 *
 * Ported from isalin/moonlight-tsp src/video_cedar.c (GPL-3.0-or-later,
 * same Allwinner A133P chip). tsp renders straight into a disp2 video layer;
 * here the decoded picture is converted to YV12 for sdl.c's texture instead,
 * so the engine keeps owning the framebuffer. The decode loop, the picture
 * accounting and the decoder rebuild follow tsp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cedar_sdl.h"

#include "../cedar/cedar.h"
#include "../cedar/cedar_sync.h"

#include <Limelight.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* How long to wait for the SBM parser to split a submitted frame. */
#define PARSE_WAIT_NS 10000000ull

/* Rebuild the decoder after this many frames without a picture. */
#define STALL_FRAMES 30

#define PTS_STEP 16667

static const unsigned char aud_h265[] = { 0, 0, 0, 1, 0x46, 0x01, 0x50 };

struct segs {
  struct cedar_segment* v;
  int n, cap;
};

static struct {
  struct cedar_config cfg;
  struct cedar_decoder* dec;
  unsigned char pps[256];   /* H.264: latest PPS, closes each frame */
  size_t pps_len;
  struct segs segs;
  unsigned long no_picture_run;
  int pending;              /* submitted frames not yet decoded */
  int logged;               /* one-off reports already made */
  int format_warned;

  /* YV12 planes handed to sdl.c's frame event; sized at init. */
  unsigned char* planes[3];
  int linesize[3];
  int frame_ready;

  /* Interval statistics, printed by cedar_sdl_dump_stats. */
  unsigned long frames, pictures, errors, recreated;
  double total_ms;
  double total_max_ms;
  uint64_t last_report_ns;
} C;

static uint64_t now_ns(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int cedar_sdl_available(void) {
  char err[160] = "";

  if (cedar_load(err, sizeof(err)) != 0) {
    fprintf(stderr, "cedar: %s\n", err);
    return 0;
  }
  return cedar_has_codec(CEDAR_CODEC_H264) || cedar_has_codec(CEDAR_CODEC_H265);
}

int cedar_sdl_init(int video_format, int width, int height, int fps, char* err, size_t errlen) {
  memset(&C.cfg, 0, sizeof(C.cfg));
  /* Never leave a caller printing uninitialised memory on a failure path. */
  if (err != NULL && errlen > 0)
    err[0] = '\0';
  if (video_format & VIDEO_FORMAT_MASK_H264)
    C.cfg.codec = CEDAR_CODEC_H264;
  else if (video_format & VIDEO_FORMAT_MASK_H265)
    C.cfg.codec = CEDAR_CODEC_H265;
  else {
    snprintf(err, errlen, "format %#x is not H.264 or HEVC", video_format);
    return -1;
  }
  /* cedar_load returns 0 on success, -1 with a message in err. */
  if (cedar_load(err, errlen) != 0)
    return -1;
  if (!cedar_has_codec(C.cfg.codec)) {
    snprintf(err, errlen, "no Cedar plugin for %s",
             C.cfg.codec == CEDAR_CODEC_H265 ? "HEVC" : "H.264");
    return -1;
  }

  C.cfg.width = width;
  C.cfg.height = height;
  C.cfg.fps = fps > 0 ? fps : 60;
  C.cfg.pixel_format = CEDAR_PIXEL_NV21;
  /*
   * One picture the display may hold and none for smoothing: every frame is
   * converted and released at once, so a deeper queue would only add latency.
   */
  C.cfg.display_hold = 1;
  C.cfg.smooth = 0;
  C.cfg.vbv_bytes = 4 << 20;
  C.pps_len = 0;
  C.pending = 0;
  C.no_picture_run = 0;
  C.logged = 0;
  C.format_warned = 0;
  memset(&C.segs, 0, sizeof(C.segs));

  C.dec = cedar_open(&C.cfg, err, errlen);
  if (C.dec == NULL)
    return -1;

  /* One YV12 allocation for the whole stream, as ffmpeg.c keeps its frames. */
  C.planes[0] = malloc((size_t)width * height * 3 / 2);
  if (C.planes[0] == NULL) {
    cedar_close(C.dec);
    C.dec = NULL;
    snprintf(err, errlen, "out of memory for the YV12 planes");
    return -1;
  }
  C.planes[1] = C.planes[0] + (size_t)width * height;
  C.planes[2] = C.planes[1] + (size_t)width * height / 4;
  C.linesize[0] = width;
  C.linesize[1] = width / 2;
  C.linesize[2] = width / 2;
  C.frame_ready = 0;

  printf("cedar: %s %dx%d@%d, NV21 -> YV12\n",
         C.cfg.codec == CEDAR_CODEC_H265 ? "HEVC" : "H.264", width, height, C.cfg.fps);
  fflush(stdout);
  return 0;
}

void cedar_sdl_destroy(void) {
  cedar_close(C.dec);
  C.dec = NULL;
  free(C.planes[0]);
  C.planes[0] = NULL;
  C.frame_ready = 0;
  free(C.segs.v);
  memset(&C.segs, 0, sizeof(C.segs));
}

static int segs_add(struct segs* s, const void* data, size_t len) {
  if (s->n == s->cap) {
    int cap = s->cap ? s->cap * 2 : 16;
    struct cedar_segment* v = realloc(s->v, (size_t)cap * sizeof(*v));

    if (v == NULL)
      return -1;
    s->v = v;
    s->cap = cap;
  }
  s->v[s->n].data = data;
  s->v[s->n].len = len;
  s->n++;
  return 0;
}

static int open_decoder(void) {
  char err[160] = "";

  C.dec = cedar_open(&C.cfg, err, sizeof(err));
  if (C.dec == NULL) {
    fprintf(stderr, "cedar: %s\n", err);
    return -1;
  }
  return 0;
}

static int rebuild_decoder(void) {
  cedar_close(C.dec);
  C.dec = NULL;
  C.pending = 0;
  C.recreated++;
  fprintf(stderr, "cedar: rebuilding decoder\n");
  return open_decoder();
}

/* Visible rectangle of a picture: the crop if the decoder reports one. */
static void picture_rect(const CedarVideoPicture* p, int* x, int* y, int* w, int* h) {
  *x = 0;
  *y = 0;
  *w = p->nWidth;
  *h = p->nHeight;
  if (p->nRightOffset > p->nLeftOffset && p->nBottomOffset > p->nTopOffset) {
    *x = p->nLeftOffset;
    *y = p->nTopOffset;
    *w = p->nRightOffset - p->nLeftOffset;
    *h = p->nBottomOffset - p->nTopOffset;
  }
}

/*
 * Copies the visible picture into YV12: luma rows, then the interleaved
 * chroma plane split into U and V (NV12 has U first, NV21 V first).
 */
int cedar_sdl_convert(const CedarVideoPicture* p, unsigned char* y, unsigned char* u,
                      unsigned char* v, int y_stride, int u_stride, int v_stride) {
  const unsigned char* src_y;
  const unsigned char* src_c;
  unsigned stride;
  int x, top, w, h, row, col;
  int nv21;

  if (p->ePixelFormat == CEDAR_PIXEL_NV21)
    nv21 = 1;
  else if (p->ePixelFormat == CEDAR_PIXEL_NV12)
    nv21 = 0;
  else
    return -1;
  if (p->pData0 == NULL || p->pData1 == NULL || p->nLineStride <= 0)
    return -1;

  src_y = (const unsigned char*)p->pData0;
  src_c = (const unsigned char*)p->pData1;
  stride = (unsigned)p->nLineStride;
  picture_rect(p, &x, &top, &w, &h);

  for (row = 0; row < h; row++)
    memcpy(y + (size_t)row * y_stride, src_y + (size_t)(top + row) * stride + x, (size_t)w);

  for (row = 0; row < h / 2; row++) {
    const unsigned char* s = src_c + (size_t)(top / 2 + row) * stride + (x & ~1);
    unsigned char* du = u + (size_t)row * u_stride;
    unsigned char* dv = v + (size_t)row * v_stride;

    for (col = 0; col < w / 2; col++) {
      unsigned char a = s[2 * col], b = s[2 * col + 1];

      du[col] = nv21 ? b : a;
      dv[col] = nv21 ? a : b;
    }
  }
  return 0;
}

/* One interval of decode latency, printed by cedar_sdl_dump_stats. */
static void stats_add(double total_ms) {
  C.total_ms += total_ms;
  if (total_ms > C.total_max_ms)
    C.total_max_ms = total_ms;
  if (C.last_report_ns == 0)
    C.last_report_ns = now_ns();
  else if (now_ns() - C.last_report_ns >= 5000000000ull) {
    cedar_sdl_dump_stats();
    C.last_report_ns = now_ns();
  }
}

static void log_semwait(void) {
  struct cedar_sync_stats ss;

  cedar_sync_stats(&ss);
  printf("cedar: semwait %s calls=%lu blocked=%lu\n",
         ss.vendor ? "vendor" : ss.calls ? "interposed" : "NOT interposed",
         ss.calls, ss.blocked);
  fflush(stdout);
}

/*
 * Takes the newest decoded picture, converts it into the YV12 planes and
 * returns it to the decoder. Called from cedar_sdl_submit() right after the
 * decode loop; sdl.c then pushes the frame event.
 */
static void take_pictures(void) {
  CedarVideoPicture* pic, *latest = NULL;
  int errors = 0;

  while ((pic = cedar_request_picture(C.dec)) != NULL) {
    if (pic->bFrameErrorFlag)
      errors++;
    if (latest != NULL)
      cedar_return_picture(C.dec, latest);
    latest = pic;
    C.pictures++;
  }
  C.errors += (unsigned long)errors;
  if (latest == NULL) {
    C.no_picture_run++;
    return;
  }
  C.no_picture_run = 0;

  if (latest->nWidth > C.cfg.width || latest->nHeight > C.cfg.height) {
    /* The stream grew: let the rebuild path below take over. */
    cedar_return_picture(C.dec, latest);
    C.no_picture_run = STALL_FRAMES;
    return;
  }
  if (cedar_sdl_convert(latest, C.planes[0], C.planes[1], C.planes[2],
                        C.linesize[0], C.linesize[1], C.linesize[2]) != 0) {
    if (!C.format_warned) {
      fprintf(stderr, "cedar: picture format %d cannot be converted\n",
              latest->ePixelFormat);
      C.format_warned = 1;
    }
    C.errors++;
    cedar_return_picture(C.dec, latest);
    return;
  }
  cedar_return_picture(C.dec, latest);
  C.frame_ready = 1;
}

int cedar_sdl_frame(unsigned char* planes[3], int linesize[3]) {
  if (!C.frame_ready)
    return 0;
  C.frame_ready = 0;
  /* Same shape as AVFrame.data/linesize, which sdl.c already passes on. */
  memcpy(planes, C.planes, sizeof(C.planes));
  memcpy(linesize, C.linesize, sizeof(C.linesize));
  return 1;
}

void cedar_sdl_dump_stats(void) {
  unsigned long n = C.frames;

  if (n == 0)
    return;
  printf("cedar: frames=%lu pictures=%lu errors=%lu recreated=%lu "
         "submit=%.2fms max=%.2fms\n",
         n, C.pictures, C.errors, C.recreated, C.total_ms / (double)n,
         C.total_max_ms);
  fflush(stdout);
  C.frames = C.pictures = C.errors = C.recreated = 0;
  C.total_ms = C.total_max_ms = 0;
}

/* Called by sdl_submit_decode_unit (src/video/sdl.c) for every decode unit. */
int cedar_sdl_submit(PDECODE_UNIT du) {
  PLENTRY e;
  int64_t pts = (int64_t)du->frameNumber * PTS_STEP;
  uint64_t t_submit, deadline;
  unsigned seq;
  int ret = CEDAR_RESULT_OK, tries, parsed = 0, errors = 0;

  if (C.dec == NULL && open_decoder() != 0)
    return DR_NEED_IDR;

  C.segs.n = 0;
  for (e = du->bufferList; e != NULL; e = e->next) {
    if (e->bufferType == BUFFER_TYPE_PPS && C.cfg.codec == CEDAR_CODEC_H264 &&
        (size_t)e->length <= sizeof(C.pps)) {
      memcpy(C.pps, e->data, (size_t)e->length);
      C.pps_len = (size_t)e->length;
    }
    if (segs_add(&C.segs, e->data, (size_t)e->length) != 0)
      return DR_NEED_IDR;
  }
  /*
   * The SBM parser closes a frame only when the next one starts. A trailing
   * PPS (H.264) or AUD (HEVC) closes it at once.
   */
  if (C.cfg.codec == CEDAR_CODEC_H265)
    segs_add(&C.segs, aud_h265, sizeof(aud_h265));
  else if (C.pps_len > 0)
    segs_add(&C.segs, C.pps, C.pps_len);

  seq = cedar_sync_seq();
  t_submit = now_ns();
  if (cedar_submit(C.dec, C.segs.v, C.segs.n, pts) != 0) {
    /* Stream buffer full or broken: start over from an IDR frame. */
    fprintf(stderr, "cedar: submit failed (%d bytes)\n", du->fullLength);
    cedar_reset(C.dec);
    C.pending = 0;
    return DR_NEED_IDR;
  }
  /*
   * Picture PTS values are rewritten by the decoder, so count instead: every
   * decoded result finishes the oldest submitted frame, and ours is done when
   * none is left. A frame the decoder dropped would leave the count high, so
   * it is capped.
   */
  if (C.pending < 2)
    C.pending++;
  deadline = t_submit + PARSE_WAIT_NS;

  for (tries = 0; tries < 32; tries++) {
    if (!parsed) {
      /* Sleep until the parser has consumed the submission. */
      parsed = cedar_sync_wait(seq, deadline);
      seq = cedar_sync_seq();
    }
    ret = cedar_decode(C.dec);
    if (ret == CEDAR_RESULT_FRAME_DECODED || ret == CEDAR_RESULT_KEYFRAME_DECODED) {
      if (C.pending > 0)
        C.pending--;
      if (C.pending == 0)
        break;
      continue;   /* an older frame: keep going for ours */
    }
    if (ret == CEDAR_RESULT_NO_BITSTREAM) {
      if (now_ns() < deadline) {
        parsed = 0;
        continue;
      }
      break;
    }
    if (ret == CEDAR_RESULT_CONTINUE || ret == CEDAR_RESULT_OK)
      continue;
    if (ret == CEDAR_RESULT_NO_FRAME_BUFFER) {
      /* We hold no pictures, so this is transient. */
      usleep(1000);
      continue;
    }
    errors++;
    break;
  }
  if (C.pending > 0)
    errors++;   /* the frame did not finish in time */

  if (ret == CEDAR_RESULT_FRAME_DECODED || ret == CEDAR_RESULT_KEYFRAME_DECODED)
    take_pictures();

  if (ret == CEDAR_RESULT_RESOLUTION_CHANGE || C.no_picture_run >= STALL_FRAMES) {
    C.no_picture_run = 0;
    if (rebuild_decoder() != 0)
      fprintf(stderr, "cedar: decoder rebuild failed\n");
    errors++;
  }

  /* Interval statistics; the same line shape as the spike's report. */
  C.frames++;
  C.errors += (unsigned long)errors;
  stats_add((double)(now_ns() - t_submit) / 1e6);
  if (C.logged == 0 && C.frames >= 240) {
    log_semwait();
    C.logged = 1;
  }

  if (errors > 0)
    return DR_NEED_IDR;
  return DR_OK;
}
