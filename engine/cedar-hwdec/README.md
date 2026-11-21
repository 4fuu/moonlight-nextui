# Cedar hardware decode spike (tg5040)

Phase 1 of hardware H.264/HEVC decoding for the bundled moonlight-embedded
engine. This directory is a **standalone tool**, not part of the engine build:
it proves the CedarC ABI and measures decode on the actual device before
anything is wired into the engine.

Ported from [isalin/moonlight-tsp](https://github.com/isalin/moonlight-tsp)
at `b36a39c` (GPL-3.0-or-later, same A133P chip, verified on a TrimUI Smart
Pro). Files keep their upstream SPDX headers; local changes are listed below.

## Contents

| Path | Origin | Purpose |
|---|---|---|
| `src/cedar/cedarc_abi.h` | tsp, verbatim | CedarC v1.3.0 structs from the device `libvdecoder.so` DWARF, with `_Static_assert` offsets |
| `include/sunxi_disp_abi.h`, `include/sunxi_g2d_abi.h` | tsp, verbatim | `/dev/disp` layer and G2D/ION/IOMMU ABI |
| `src/cedar/cedar.c/.h` | tsp | dlopen wrapper for `libvdecoder.so` (no link-time vendor deps); also the engine's copy |
| `src/cedar/cedar_sync.c/.h` | tsp | blocking `SemTimedWait` that replaces the vendor polling one |
| `src/video/cedar_sdl.c/.h` | port of tsp `video_cedar.c` | the engine-side backend: decode → YV12 → SDL frame event |
| `tests/test_cedar_convert.c`, `tests/test_cedar_fallback.c`, `tests/test_cedar_init.c` | this repo | host tests for the conversion, the fallback contract and the init path (mock vendor libraries) |
| `src/ion.c/.h`, `src/disp.c/.h`, `src/g2d_rot.c/.h` | tsp | used by the tool's optional `-d` / `-G` experiments only |
| `tools/tsp-videotest.c` | tsp | the decode tool |
| `tools/spike/README-device.txt` | this repo | what to run on the device and which lines to send back |
| `scripts/make-test-streams.sh` | tsp | 720p60 H.264/H.265 test clips with one AUD per frame |
| `media/test720p60.264`, `.265` | generated | 600 frames, 10 s at 60 fps, no B-frames, smpte170m |
| `scripts/fetch-toolchain.sh` | this repo | extracts the tg5040 toolchain image without Docker |
| `scripts/build-spike.sh` | this repo | cross-builds the tool and the device bundle |

## Build

```sh
engine/cedar-hwdec/scripts/make-test-streams.sh 10 1280x720   # needs ffmpeg
engine/cedar-hwdec/scripts/fetch-toolchain.sh /tmp/tc-rootfs  # once, no Docker
TOOLCHAIN_ROOT=/tmp/tc-rootfs \
  SYSROOT=/tmp/tc-rootfs/opt/aarch64-nextui-linux-gnu/libc \
  SPIKE_HEVC=1 engine/cedar-hwdec/scripts/build-spike.sh
```

Inside the CI container (`CROSS_TRIPLE`/`SYSROOT` exported) the same script
needs no `TOOLCHAIN_ROOT`. `CROSS_CC=aarch64-linux-gnu-gcc` works too, since
the tool only `dlopen()`s vendor libraries.

Output: `build-spike/tsp-videotest` and `build-spike/spike-bundle.tar.gz`
(tool, both clips, `README.txt`). Extract on the device and follow
`README.txt`.

## Run on the device

```sh
cd /tmp/tsp-spike
./tsp-videotest -r 60 -t 20 test720p60.264      # decode only, no display
./tsp-videotest -d -t 5 test720p60.264          # optional: disp2 layer
./tsp-videotest -X -F -C 0 -L 0                 # always after -d
```

What the run must show:

- `plugins: h264=registered h265=registered|missing` — does this firmware
  ship `libawh265.so`? The Brick Pro scan found `libawh264.so` only.
- `picture: fmt=5|6 ... stride=...` — the real output format and stride. The
  Smart Pro decodes to NV21 (5) with a 16-aligned stride.
- `summary: ... pictures=600 errors=0` — the ABI is right.
- `cpu: ... % of one core` and `submit->picture: ... p95=... ms` — the cost
  (Smart Pro: 3.3 ms per 720p frame, 3.2% of a core).
- `semwait: interposed|vendor|NOT interposed`.

## Engine integration

The decode side is wired into the bundled engine's SDL backend, not into a new
display path. The engine change is one patch, `engine/patches/sdl-cedar-hwdec.patch`
(applied by `engine.yml` after the buffer and latency patches); it is generated
from `src/cedar/` and `src/video/` here. `docs/cedar-hwdec.md` has the design,
the file map, the device test plan and the status. Host tests run in
`.github/workflows/cedar-host-tests.yml`.

## Local changes to the ported files

1. `src/cedar.c/.h` — `cedar_has_codec()` and `add_plugin()`: a plugin is
   registered only when `/usr/lib/libawh265.so` (or `libawh264.so`) exists.
   tsp's `AddVDPluginSingle("/usr/lib/libawh265.so")` was called blindly; on
   a firmware without it, HEVC would fail inside the vendor library.
2. `tools/tsp-videotest.c` — prints `plugins: ...` after `cedar_load()` and
   exits with a clear message when the requested codec has no plugin.
3. `src/cedar_sync.c` — `sem_timedwait()` at an absolute `CLOCK_REALTIME`
   instead of `sem_clockwait()` (glibc 2.30+); the tg5040 toolchain sysroot
   is glibc 2.28.
4. `scripts/build-spike.sh` — links with `-rdynamic` instead of
   `-Wl,--export-dynamic-symbol=SemTimedWait`, because the toolchain's
   binutils 2.31 predates that option. `SemTimedWait` is exported either way.
5. `scripts/make-test-streams.sh` — target bitrate instead of fixed 10 Mb/s
   (a 10 s clip stays well under the 4 MiB decoder stream buffer), output in
   `media/`, HEVC skipped when ffmpeg has no libx265.

## Verified locally (not on device)

- Cross-build with the tg5040 toolchain (gcc 8.3.0, glibc 2.28 sysroot):
  aarch64 ELF, `SemTimedWait` exported, GLIBC_2.17 symbols only.
- Rebuilding after the source move changes the bundle's sha256 (debug paths),
  but the `.text` section is byte-identical to the device-verified binary
  (`3964a0f6...`).
- qemu-user + focal glibc 2.31 (same loader CI uses): without vendor
  libraries the tool exits 1 with `cedar_load: dlopen libvdecoder.so: ...`.
- qemu-user with a mock `libvdecoder.so`/`libvideoengine.so`/`libawh264.so`
  in a bind-mounted `/usr/lib`: plugin detection, decoder open, the decode
  loop and all statistics run; H.265 without `libawh265.so` exits 1 with
  `plugins: h264=registered h265=missing (REQUESTED CODEC UNAVAILABLE)`.

Not verified: anything involving the VE, IOMMU, G2D or display engine. Those
need the device.
