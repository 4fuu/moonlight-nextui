/*
 * Cedar hardware decode feeding the SDL YV12 texture.
 *
 * Ported from isalin/moonlight-tsp src/video_cedar.c (GPL-3.0-or-later);
 * the display half (disp2 layer, G2D rotation, ION ring) is not used: the
 * engine keeps its own SDL framebuffer path, so decoded NV21 pictures are
 * converted to YV12 in CPU and handed to sdl.c's frame event.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MOONLIGHT_CEDAR_SDL_H
#define MOONLIGHT_CEDAR_SDL_H

#include <stddef.h>
#include <stdint.h>

#include <Limelight.h>

#include "../cedar/cedarc_abi.h"

/* 1 when the vendor decoder libraries exist; 0 on a firmware without them. */
int cedar_sdl_available(void);

/*
 * Opens the decoder. video_format is common-c's negotiated format, width and
 * height the requested stream size. Returns 0, or -1 with a message in err.
 */
int cedar_sdl_init(int video_format, int width, int height, int fps, char* err, size_t errlen);

void cedar_sdl_destroy(void);

/*
 * Converts a decoded picture to YV12 (luma, then U and V). The picture's
 * visible rectangle is copied, interleaved chroma is deinterleaved (NV21 V
 * first, NV12 U first). Exposed for the host test.
 */
int cedar_sdl_convert(const CedarVideoPicture* pic, unsigned char* y, unsigned char* u,
                      unsigned char* v, int y_stride, int u_stride, int v_stride);

/* Submits one decode unit to the decoder; DR_OK or DR_NEED_IDR. */
int cedar_sdl_submit(PDECODE_UNIT du);

/*
 * Returns the YV12 planes of the last decoded picture, once per picture, in
 * the same shape as AVFrame.data/linesize so sdl.c can push its frame event.
 * 1 if a frame is ready, 0 otherwise.
 */
int cedar_sdl_frame(unsigned char* planes[3], int linesize[3]);

/*
 * Receive time (LiGetMillis) of the decode unit behind the frame last handed
 * out by cedar_sdl_frame(), for the latency report in src/sdl.c.
 */
uint64_t cedar_sdl_receive_ms(void);

void cedar_sdl_dump_stats(void);

#endif
