# Vendored NextUI overlay renderer

This directory is a snapshot of NextUI's production overlay renderer. It is
vendored so the app-hosted HOME panel and HUD can be **pixel-identical** to the
system overlay instead of being redrawn with Apostrophe primitives.

## Provenance

- Repository: <https://github.com/4fuu/NextUI>
- Commit: `2cc6eaf708785a598499128e2f7eb9849679c3ed` (`2026-11-18`)
- License: PolyForm Noncommercial 1.0.0 (`LICENSE`)
- Source paths (under `workspace/all/common/`):
  `ui_overlay.c`, `ui_overlay.h`, `apostrophe_overlay_host.c`,
  `apostrophe_overlay_host.h`, `api.c`, `api.h`, `config.c`, `config.h`,
  `i18n.c`, `i18n.h`, `palette.c`, `palette.h`, `utils.c`, `utils.h`,
  `scaler.c`, `scaler.h`, `sdl.h`, `defines.h`, `fn_action.h`,
  `pak_manifest.c`, `pak_manifest.h`, `json_lite.c`, `json_lite.h`
- `vendor/platform.h` is `workspace/tg5040/platform/platform.h`.
- `vendor/msettings.h` is `workspace/tg5040/libmsettings/msettings.h`; the
  renderer only needs its declarations.
- `res/assets@3x.png`, `res/inputs@3x.png`, `res/font2.ttf` are host-test
  copies of `skeleton/SYSTEM/res/` files. Device builds read the real files
  from `/mnt/SDCARD/.system/res`.

`src/platform_stubs.c` and `tests/overlay_smoke.c` are this repository's MIT
code, not NextUI code.

`pak_manifest.c` is NextUI's pak.json reader. It is vendored so the pak's own
manifest is validated by the parser the launcher actually uses
(`tests/test_pak_manifest.c`), rather than by a second, hand-written check that
could drift from the real contract.

## Local changes

1. `vendor/platform.h`: `SDCARD_PATH` is wrapped in `#ifndef`, so the host
   render test can compile against a scratch root. Device builds keep
   `/mnt/SDCARD`.
2. No other vendored line is changed. Diff against the commit above to verify.

## Build model

- `api.c` is compiled whole but linked with `-ffunction-sections` and
  `-Wl,--gc-sections`. Only the GFX/PAD/CFG path reachable from `ui_overlay.c`
  and `apostrophe_overlay_host.c` survives, so NextUI's power, video and LED
  subsystems do not enter the app process.
- `src/platform_stubs.c` supplies the platform seam the surviving functions
  reach (`is_brick`, msettings getters, Wi-Fi/Bluetooth status, input hooks).
  M3/M5 replace the data source with SDK values; the renderer stays untouched.
- The archive is linked into the wrapper. `apostrophe_overlay_host.c` is
  attached through `ap_host_attach()`, and the Apostrophe fork's widget bridge
  drives poll/draw from `ap_poll_input()`/`ap_present()`.
- The device must provide `libsamplerate` (NextUI ships it in
  `.system/<platform>/lib`), because `api.c`'s audio path references it even
  though the overlay itself does not use audio.

## Refreshing the snapshot

```sh
git clone https://github.com/4fuu/NextUI /tmp/NextUI
git -C /tmp/NextUI checkout <new-commit>
# copy the files listed above; keep the SDCARD_PATH guard in platform.h
# rerun: scripts/nextui/build.sh host && ctest --test-dir build-host
# inspect build-host/overlay-smoke.png before committing
```

If phase A ships a shared overlay renderer in `nextui-app-sdk`, prefer linking
that component and delete this snapshot; the license then follows the SDK
component instead of NextUI's noncommercial license.
