# Third-party components

`Moonlight.pakz` bundles a wrapper, a prebuilt streaming engine, its shared
library closure, and a vendored snapshot of NextUI's overlay renderer. This
file records provenance and licenses for every piece.

## Summary

| Component | Location | License | Source |
|---|---|---|---|
| Moonlight wrapper (fork of `richieszemeredi/nextui-moonlight-pak`) | `src/`, `tests/` | MIT | <https://github.com/richieszemeredi/nextui-moonlight-pak> at `84ead2e`; fork <https://github.com/4fuu/moonlight-nextui> |
| Apostrophe (UI framework) | `third_party/apostrophe` submodule | MIT | <https://github.com/4fuu/Apostrophe> at `70ae5a4` (fork of `Helaas/Apostrophe`) |
| nextui-app-sdk | `third_party/nextui-app-sdk` submodule, linked into the wrapper | MIT | <https://github.com/4fuu/nextui-app-sdk> at `27e5597` (client 1.0.0, protocol 1) |
| NextUI full-fidelity overlay renderer (vendored) | `third_party/nextui-overlay/vendor/` | PolyForm Noncommercial 1.0.0 | <https://github.com/4fuu/NextUI> at `2cc6eaf` |
| moonlight-embedded engine and GameStream libs | `engine/bin/moonlight`, `engine/bin/libs/libgamestream.so.4`, `libmoonlight-common.so.4` | GPL-3.0 | spruce 2.7.0 build; see below |
| Bundled runtime libraries (22 files) | `engine/bin/libs/` | per-library, see below | spruceOS / Ubuntu 20.04 aarch64 multiarch |

The pak also ships `licenses/` inside the archive with the wrapper MIT text,
the Apostrophe MIT text, the NextUI PolyForm text, the GPL-3.0 text, and this
document.

## NextUI overlay renderer

`third_party/nextui-overlay/` is a snapshot of NextUI's production overlay
code, not a reimplementation:

- `ui_overlay.c/.h` — the shared B5 panel (layout, rows, dim wash, input).
- `apostrophe_overlay_host.c/.h` — the Apostrophe host adapter that composites
  the panel into the app's own `SDL_Renderer` (no second window, no disp2).
- `api.c/.h`, `config.c/.h`, `i18n.c/.h`, `palette.c/.h`, `scaler.c/.h`,
  `utils.c/.h`, `defines.h`, `platform.h`, `sdl.h`, `fn_action.h`,
  `msettings.h` — the GFX blit path, theme/palette loader, fonts and asset
  atlas that the panel uses.
- `pak_manifest.c/.h`, `json_lite.c/.h` — NextUI's pak.json reader, used by
  `tests/test_pak_manifest.c` so this pak's manifest is validated by the same
  parser the launcher runs.
- `res/assets@3x.png`, `res/inputs@3x.png`, `res/font2.ttf` — host-test copies
  of NextUI's default atlas and font. The device build reads the real files
  from `/mnt/SDCARD/.system/res`, so a user's installed theme/font is used
  unchanged.

Upstream license: **PolyForm Noncommercial License 1.0.0** (see
`third_party/nextui-overlay/LICENSE`). The license permits use for
noncommercial purposes. This pak is distributed free of charge; the vendored
files keep their license and may not be sold. A shared renderer in `nextui-app-sdk` remains a design proposal; if it lands,
this snapshot is replaced and the license follows that component.

Local changes to the snapshot (kept to a minimum so it can be refreshed):

1. `vendor/platform.h` — `SDCARD_PATH` is wrapped in `#ifndef` so the host
   render smoke test can point the resource loader at a scratch directory.
   Device builds keep `/mnt/SDCARD`.
2. `vendor/` files are otherwise byte-identical to NextUI `2cc6eaf`.
3. `src/platform_stubs.c` (MIT, this repo) supplies the platform seam the
   overlay path reaches: `is_brick`/`is_brickpro`, the msettings getters, and
   the Wi-Fi/Bluetooth status calls. M3/M5 replace the data source with the
   SDK; the renderer itself is untouched.

## Streaming engine

The engine is **not rebuilt here**. `engine/bin/` is copied unmodified from
spruceOS's shipped Moonlight app:

- `spruceUI/spruceOS` `Development`, `App/Moonlight/ports/moonlightnew/moonlight/`
  (binary `moonlight` and its `libs/` closure).
- Version: moonlight-embedded **2.7.0**, built by
  <https://github.com/spruceUI/Moonlight-spruce> against moonlight-embedded
  commit `274d3db34da764344a7a402ee74e6080350ac0cd`, OpenSSL 3.3.1,
  curl 8.7.1, and a minimal FFmpeg 4.4.5. The build script is the authoritative
  recipe: <https://github.com/spruceUI/Moonlight-spruce/blob/main/build.sh>.
- Corresponding source: <https://github.com/moonlight-stream/moonlight-embedded>
  at the commit above, plus the build inputs named by that script.
- `engine/SHA256SUMS` records every bundled binary. No file in `engine/` is
  modified by this repository.

The wrapper executes the engine as a separate process through the PATH shim
(`shim/moonlight`). GPL-3.0 applies to the engine and its GameStream libraries;
the source offer above satisfies redistribution.

The wrapper also links the MIT `nextui-app-sdk` client (`27e5597`) and the MIT
Apostrophe host bridge. The cooperation layer talks to `devserv` over its Unix
socket; the protocol contract is in the SDK repository.

## Bundled runtime libraries

All files live in `engine/bin/libs/` and are the spruce 2.7.0 closure. They are
aarch64, glibc <= 2.29 symbols, and were taken unmodified from the same spruce
package.

| Library | Project | License |
|---|---|---|
| `libavcodec.so.58`, `libavutil.so.56`, `libswresample.so.3` | FFmpeg 4.4.5 | LGPL-2.1-or-later (built without `--enable-gpl`) |
| `libavahi-client.so.3`, `libavahi-common.so.3` | Avahi | LGPL-2.1-or-later |
| `libcrypto.so.3`, `libssl.so.3` | OpenSSL 3.3.1 | Apache-2.0 |
| `libcurl.so.4` | curl 8.7.1 | curl license (MIT-like) |
| `libdbus-1.so.3` | D-Bus | AFL-2.1 or GPL-2.0-or-later (dual) |
| `libevdev.so.2` | libevdev | MIT |
| `libexpat.so.1` | Expat | MIT |
| `libgamestream.so.4`, `libmoonlight-common.so.4` | moonlight-embedded 2.7.0 | GPL-3.0 |
| `libgcrypt.so.20` | libgcrypt | LGPL-2.1-or-later |
| `libgpg-error.so.0` | libgpg-error | LGPL-2.1-or-later |
| `liblz4.so.1` | LZ4 | BSD-2-Clause |
| `liblzma.so.5` | XZ Utils | 0BSD / public domain |
| `libopus.so.0` | Opus | BSD-3-Clause |
| `libpulse.so.0`, `libpulse-simple.so.0` | PulseAudio | LGPL-2.1-or-later |
| `libsystemd.so.0` | systemd | LGPL-2.1-or-later |
| `libuuid.so.1` | util-linux | BSD-3-Clause |

Device-provided, deliberately **not** bundled (the firmware image supplies
them): glibc, `libSDL2-2.0.so.0` (zlib), `libasound.so.2` (LGPL-2.1),
`libudev.so.1` (LGPL-2.1), `libz.so.1`, `libsamplerate.so.0` (BSD-2-Clause;
NextUI stages it in `.system/<platform>/lib`), and PulseAudio's private
`libpulsecommon-13.99.so` (LGPL-2.1). The device test checklist verifies these
resolve on a real Brick.

## This repository

The wrapper source and the files added by this fork (build system, launch
script, cooperation seam, docs) are MIT, inherited from
`richieszemeredi/nextui-moonlight-pak`; see `LICENSE`. `src/nextui_coop.c` is
part of that MIT work. `third_party/nextui-overlay/src/platform_stubs.c` is
MIT; the vendored renderer beside it is PolyForm Noncommercial as stated above.
