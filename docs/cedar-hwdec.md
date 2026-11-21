# Cedar hardware decode (tg5040)

Hardware H.264/HEVC decoding for the bundled moonlight-embedded engine on the
TrimUI Brick Pro (Allwinner A133P). The decoder is the firmware's own CedarC
v1.3.0 library, driven through `dlopen()`, and its output is converted to the
YV12 texture the engine's existing SDL backend already uploads.

Nothing here links against vendor libraries: `engine/bin/moonlight` keeps its
current link-time dependencies, and a firmware without the Cedar libraries
still streams on FFmpeg.

## Status

| Step | State |
|---|---|
| Cedar ABI + decode spike on the Brick Pro (`engine/cedar-hwdec/`) | done, device-verified |
| H.264 720p60 decode, 20 s | 598/600 pictures, 0 errors, 3.74 ms mean decode, 6.4% of one core |
| HEVC 720p60 decode | 2.72 ms mean decode, 2.8% of one core (`libvdecoder` registers HEVC internally; no `libawh265.so`) |
| Decoder output format | NV21 (5), stride = width, no crop, `full_range=0`, `matrix=6` |
| Engine integration (`video/cedar_sdl.c`, `MOONLIGHT_HWDEC=1`) | patch ready, cross-builds, host tests pass |
| Live stream on the device | **pending owner test** |

The spike measured with `MOONLIGHT_HWDEC` unset; the engine integration has not
yet been run against a Sunshine host on the device.

## How it fits

The engine keeps its display path. `src/video/sdl.c` owns the SDL window and
the YV12 texture; that is what the wrapper's overlay contract assumes (the app
owns `fb0` through SDL). tsp's disp2 video layer is therefore **not** ported.

```
common-c receive thread
  → decode unit (Annex-B NALs, one frame)
    → sdl_submit_decode_unit()            src/video/sdl.c
       MOONLIGHT_HWDEC=1 → cedar_sdl_submit()
         CedarC: RequestVideoStreamBuffer → SubmitVideoStreamData
                 → DecodeVideoStream → RequestPicture
         picture is NV21 in ION memory
         → cedar_sdl_convert(): deinterleave VU → YV12 planes
         → SDL_USEREVENT(SDL_CODE_FRAME): data1 = planes, data2 = linesizes
         → SDL_UpdateYUVTexture
       otherwise        → ffmpeg.c (unchanged)
```

Because the SDL callbacks are unchanged, common-c keeps
`CAPABILITY_DIRECT_SUBMIT`. That capability means common-c creates **no**
`VideoDec` thread (`VideoStream.c`: the thread is only started when
`DIRECT_SUBMIT | PULL_RENDERER` are both clear) and the RTP receive thread calls
`submitDecodeUnit()` itself. So the Cedar decode (~3.7 ms measured on the
device) plus the NV21→YV12 conversion run on the receive thread, inside the
16.7 ms frame budget. That is fine on paper and matched the spike's per-frame
cost, but it has not been confirmed with a live stream: the device run must
look at the `VideoRecv` thread's CPU split between receive and decode.

If that thread turns out to be the bottleneck, dropping the capability
(`.capabilities = 0`) moves the same work to common-c's dedicated decoder
thread behind its 15-frame bounded queue; that is a one-line change in
`sdl.c` and the fallback path to try, not something this patch does.

Selection is one environment variable, read in `sdl_setup()`:

- `MOONLIGHT_HWDEC=1` — try Cedar; on any failure (no vendor libraries,
  missing HEVC plugin, `InitializeVideoDecoder` error) log why and use FFmpeg.
- unset or any other value — FFmpeg, as today.

The pak does not set the variable, so the shelf build is unchanged until the
launcher opts in.

## Files

Engine-side (one patch in `engine/patches/`, applied by `engine.yml`):

| Path | Purpose |
|---|---|
| `engine/patches/sdl-cedar-hwdec.patch` | the whole engine change, applied by `engine.yml` after `sdl-buffer-1` and `latency-debug`; it carries `src/cedar/*` and `src/video/cedar_sdl.[ch]` plus the hooks in `CMakeLists.txt`, `src/video/sdl.c` and `src/main.c` |
| `engine/cedar-hwdec/src/cedar/` | sources for the patch: the dlopen wrapper, `cedar_sync.c`, `cedarc_abi.h` (tsp, with the local changes below) |
| `engine/cedar-hwdec/src/video/cedar_sdl.[ch]` | sources for the patch: port of tsp `video_cedar.c` |

The patch is generated from those sources, so the two never drift:

```sh
git clone https://github.com/moonlight-stream/moonlight-embedded.git /tmp/me
git -C /tmp/me checkout 274d3db34da764344a7a402ee74e6080350ac0cd
git -C /tmp/me submodule update --init --recursive
for p in engine/patches/latency-debug.patch engine/patches/sdl-buffer-1.patch; do
  git -C /tmp/me apply "$p"
done
# copy the sources in, make the three hook edits, then:
git -C /tmp/me diff > engine/patches/sdl-cedar-hwdec.patch
```

The host tests are `.github/workflows/cedar-host-tests.yml`; the engine build
is the existing `.github/workflows/engine.yml`, which picks the patch up from
`engine/patches/` and emits its `manifest.txt` for `engine/bin/`.

Spike-side (standalone, not part of the engine build): `tools/tsp-videotest.c`,
`src/disp.c`, `src/g2d_rot.c`, `src/ion.c`, `scripts/`, `media/`. See
[`README.md`](README.md) for the spike itself.

## Local changes to the ported code

The vendored files keep their upstream SPDX headers. Changes, all marked in
place:

1. **`cedar.c`** — plugins are registered only when the library exists
   (`access()`, `cedar_has_codec()`). tsp called
   `AddVDPluginSingle("/usr/lib/libawh265.so")` unconditionally; the Brick Pro
   firmware has no such file, and HEVC is registered inside `libvdecoder`
   itself. This is what the device spike confirmed. `MOONLIGHT_CEDAR_LIBDIR`
   redirects `dlopen()` and the plugin lookup to a directory (the host test's
   mock); unset, it is `/usr/lib`.
2. **`cedar_sync.c`** — `sem_timedwait()` at an absolute `CLOCK_REALTIME`
   instead of `sem_clockwait()` (glibc 2.30+). The tg5040 toolchain sysroot is
   glibc 2.28. The exported `SemTimedWait` is a trampoline over
   `cedar_sem_timed_wait()`, so the engine's own `cedar_sync_wait()` keeps
   using the interposed implementation even under `TSP_SEMWAIT=vendor`.
3. **`cedar_sdl.c`** — tsp's display half (G2D rotation, ION ring, disp2
   layer, vsync release) is gone; replaced by the picture → YV12 conversion.
   Decode loop, pending-picture accounting, PPS/AUD frame closing, stall
   rebuild and `DR_NEED_IDR` handling are tsp's.
4. **`cedar_sdl.c`** — `display_hold = 1`, `smooth = 0`. tsp used 3/1 for a
   display queue; here every picture is converted and returned immediately, so
   a deeper queue would only add latency (the device run measured the same
   3.7 ms decode with hold=3, so this trades nothing measurable for less
   buffering).
5. **Exporting `SemTimedWait`** — the engine must expose the symbol in its
   *dynamic* table for libcdc_base/libsbm to bind to. tsp links with
   `-Wl,--export-dynamic-symbol=SemTimedWait`; the tg5040 binutils 2.31 has no
   such option, so the engine patch sets `ENABLE_EXPORTS` on the `moonlight`
   target (`-rdynamic`), and `engine.yml` fails the build if the symbol is not
   in `--dyn-syms`. Without it the vendor polling implementation runs: the
   first device run showed `semwait NOT interposed calls=0` and 14 ms per
   submit instead of the spike's 3.7 ms.
6. **Latency report** (`latency-debug.patch`) — measured per frame at present
   time (present time minus that frame's own receive time), aggregated over 60
   samples with avg/min/p50/p95/max. The receive time travels in its own
   atomic word via `sdl_latency_receive()`; the frame event keeps its payload
   (data1 planes, data2 linesizes) because that is what `sdl_loop()` passes to
   `SDL_UpdateYUVTexture`. An earlier version put the timestamp in data2 and
   the picture came out garbled on device — the present side read strides out
   of it. `engine.yml` now asserts the payload shape for both decoders.
7. **`cedar_sdl.c` frame hand-off** — the decoded picture waits in the backend
   until `cedar_sdl_frame()` converts it, and `src/video/sdl.c` calls that
   while holding the SDL mutex, so a conversion can never rewrite the buffer
   that the texture upload is reading. A picture the presenter never took is
   returned to the decoder when the next one arrives.

## Verification

Host, in this repository (`engine/cedar-hwdec/tests/`):

- `test_cedar_convert.c` — NV21 → YV12 deinterleaving, NV12, padded strides,
  crop offsets, unsupported formats. The padding case is the device's real
  layout: 1280-wide picture in a 1280-byte stride, luma then chroma in one
  allocation.
- `test_cedar_fallback.c` — without vendor libraries `cedar_load()` fails
  cleanly, `cedar_sdl_available()` is 0 and `cedar_sdl_init()` returns -1 with
  a message, which is the condition that makes `sdl_setup()` pick FFmpeg.
- `test_cedar_init.c` — with mock vendor libraries in
  `MOONLIGHT_CEDAR_LIBDIR`, `cedar_sdl_init()` must return 0 for H.264 and
  HEVC. This is the regression test for the first device run, where a reversed
  `cedar_load()` test made the engine fall back to FFmpeg with an empty error
  and nothing noticed. A deliberately corrupt library is not covered: glibc's
  loader keeps a record of a failed object that crashes at exit.

Cross-build: `engine/patches/sdl-cedar-hwdec.patch` applies cleanly after
`sdl-buffer-1.patch` and `latency-debug.patch` on `moonlight-embedded`
`274d3db3`, and the patched tree builds for aarch64
(`aarch64-linux-gnu-gcc`, glibc 2.31 sysroot, `SDL;ALSA`) to a `moonlight`
binary with no new warnings. The authoritative build (device toolchain
`ghcr.io/loveretro/tg5040-toolchain`, glibc 2.28 sysroot) is
`.github/workflows/engine.yml`.

Not verified here: anything that needs `/dev/cedar_dev`, `/dev/ion` or the VE —
that is the device thread's run.

## Device test plan

The engine build from `engine.yml` goes into `engine/bin/`, the pak keeps
its `shim/moonlight` (`-platform sdl`), and the launcher exports
`MOONLIGHT_HWDEC=1` for the stream process only. What to compare against the
current FFmpeg build, at 1280x720@60, one 5-minute session each:

- `stream.log`: `Hardware decode: MOONLIGHT_HWDEC=1` and
  `cedar: h264 1280x720@60, NV21 -> YV12`, then every 5 s
  `cedar: frames=… errors=0 … decode=…ms convert=…ms total=…ms`.
- CPU (`top -b -n1` during the stream): expect a few percent of one core
  instead of the software decoder's share.
- No orphaned ION buffers after exit:
  `/sys/kernel/debug/ion/heaps/sys_user` and the `dmabuf: cache` field of
  `/sys/class/disp/disp/attr/sys`.
- If the picture is decoded but looks wrong (colour channel swap), the
  conversion's VU order is the first thing to check; the spike reported NV21
  on the Smart Pro, and `cedar_sdl_convert()` handles both NV21 and NV12.
- Thread profile (`top -H` or the hang log's threads) of the engine process:
  `VideoRecv` now carries receive + Cedar decode + conversion. Compare its CPU
  with the FFmpeg build's `VideoRecv`/`VideoDec` pair; if `VideoRecv` is
  saturated, switch the callbacks to `capabilities = 0`.

Rollback is unsetting `MOONLIGHT_HWDEC`; no binary swap is needed.

## License

The ported files are GPL-3.0-or-later from
[isalin/moonlight-tsp](https://github.com/isalin/moonlight-tsp) at `b36a39c`.
The engine is GPL-3.0. See [THIRD-PARTY.md](THIRD-PARTY.md) for the notice.
