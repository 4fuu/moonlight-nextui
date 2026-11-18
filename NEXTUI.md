# NextUI port (TrimUI Brick / tg5040)

Moonlight game streaming for NextUI, built from source in this repository.
The streaming engine is the spruce 2.7.0 moonlight-embedded build, bundled
unmodified; everything else — the Apostrophe UI wrapper, the launcher, the
cooperation seam, and the full-fidelity overlay renderer — is built here.

- Upstream wrapper: <https://github.com/richieszemeredi/nextui-moonlight-pak> (MIT)
- This port: <https://github.com/4fuu/moonlight-nextui>
- Cooperation contract: <https://github.com/4fuu/nextui-app-sdk>
- System side: <https://github.com/4fuu/NextUI>

## Status

| Area | State |
|---|---|
| Wrapper rebuild (Apostrophe fork, `ap_host_*` seam) | done, S1 |
| tg5040 cross-build + `Moonlight.pakz` packaging | done, S2 (CI is authoritative) |
| C3 session launcher, engine shim, CA/log/config paths | done, S3 |
| `pak.json` identity + guarded overlay placeholder | done, S4 |
| `ml_stream` controlled spawn + cooperation seam | done, S5 |
| Full-fidelity overlay renderer vendored and building | done, S1/S2 scaffolding |
| In-process HOME overlay in the menu | **M2, waits for phase A** |
| Volume/brightness HUD cooperation | **M3, waits for phase A** |
| Sleep/wake handshake + stream inhibit | **M4, waits for A+B** |
| Battery/status rows from the SDK | **M5, waits for phase A** |
| Streaming-time HOME ownership | **M6, waits for phase A dynamic ownership** |

The wrapper runs and streams today. What is missing is the SDK cooperation
layer, not the baseline app.

## Rendering route: full fidelity

The owner chose **full fidelity**: the app-hosted HOME panel and HUD must match
NextUI's system overlay pixel for pixel. This repository therefore vendors
NextUI's own renderer instead of drawing with Apostrophe primitives:

- `third_party/nextui-overlay/vendor/ui_overlay.c` — the shared B5 panel.
- `third_party/nextui-overlay/vendor/apostrophe_overlay_host.c` — composites
  that panel into Apostrophe's existing `SDL_Renderer`; no second window, no
  disp2 layer.
- The GFX blit path, theme/palette loader (`config.c`), fonts and
  `assets@3x.png` atlas come from the same snapshot.
- On device the renderer loads the user's real theme and font from
  `/mnt/SDCARD/.system/res`, so colors and metrics follow the installed NextUI
  theme rather than a copy baked into the pak.

`third_party/nextui-overlay/` is build-only in this phase: `nextui-overlay`
is a static archive, compiled on every host and cross build to prove the
closure. M2 attaches it to `src/main.c` with `ap_host_attach()` and switches
the menu loop to `ap_host_poll()` / `ap_host_present()`. See
[docs/THIRD-PARTY.md](docs/THIRD-PARTY.md) for provenance and the NextUI
noncommercial license.

Why not link NextUI's full `api.c` behavior? We do vendor `api.c` verbatim, but
link with `-ffunction-sections -Wl,--gc-sections`: only the GFX/PAD/CFG path the
overlay reaches is kept, so the pak does not pull NextUI's power, video or LED
subsystems into the app process.

## Dynamic overlay ownership (important)

`pak.json` deliberately does **not** declare `"capabilities": ["overlay"]` yet.
NextUI's C4 contract currently treats that declaration as static: an adapted app
owns HOME for its whole lifetime. Moonlight has two different states:

- **Menu / pair / servers / settings** — the wrapper owns the display and can
  host the panel in-process (`NEXTUI_OVERLAY_APP`).
- **Streaming** — the engine is a separate process owning fb0; the wrapper's SDL
  is torn down. HOME/HUD must be drawn by the system fallback
  (`NEXTUI_OVERLAY_SYSTEM`) or the stream must end first.

Declaring `overlay` statically would claim HOME during streaming, where the
wrapper cannot draw it. The capability stays off until phase A defines dynamic
ownership. `scripts/nextui/make-pak.sh` contains the guarded switch:

```sh
# Only after phase A supports per-request/per-state ownership:
MOONLIGHT_OVERLAY_CAPABILITY=1 ./scripts/nextui/make-pak.sh
```

`src/nextui_coop.c` already answers the ownership question in code
(`nextui_coop_overlay_owner()`), so M6 has one place to wire the protocol.

## Build

The aarch64 build is the authoritative one. It runs inside the NextUI
toolchain image (`ghcr.io/loveretro/tg5040-toolchain`); CI does it on every
push and pull request.

### CI

`.github/workflows/build.yml`:

- `host-checks` — Ubuntu, system SDL2, CMake host build, parser tests, the
  full-fidelity overlay render smoke test, shellcheck, manifest validation.
- `tg5040` — the toolchain container, aarch64 CMake cross-build of the wrapper
  and the vendored overlay, then `scripts/nextui/make-pak.sh`.
- Artifact: `Moonlight.pakz`.

### Local Docker

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest bash -lc '
    apt-get update && apt-get install -y --no-install-recommends cmake zip file python3
    ./scripts/nextui/build.sh tg5040
    ./scripts/nextui/make-pak.sh
  '
```

Outputs:

- `build-nextui/moonlight-pak` — aarch64 wrapper
- `build-nextui/libnextui-overlay.a` — vendored overlay archive
- `dist/Moonlight.pak/` — complete pak directory
- `dist/Moonlight.pakz` — archive whose contents are the pak (`launch.sh` at
  the archive root)

### Host checks (orb / Linux / macOS)

```sh
sudo apt install cmake pkg-config libsdl2-dev libsdl2-ttf-dev \
                 libsdl2-image-dev libsamplerate0-dev shellcheck
./scripts/nextui/build.sh host
ctest --test-dir build-host --output-on-failure
```

`ctest` runs the upstream parser tests and `overlay-smoke`, which renders the
real panel at 1024x768 into `build-host/overlay-smoke.png` using the vendored
atlas and font.

### Toolchain layout

`cmake/toolchain-tg5040.cmake` follows the image's `CROSS_TRIPLE`,
`CROSS_ROOT`, and `SYSROOT` variables and pins `-mcpu=cortex-a53`. The sysroot
ships `sdl2.pc` but not `SDL2_ttf.pc`/`SDL2_image.pc`; CMake falls back to
`find_library` for those two, as the upstream port Makefile did.

## Install

1. Download `Moonlight.pakz` from CI or a release.
2. Install it with NextUI's app installer, or unzip it so that
   `/Tools/tg5040/Moonlight.pak/launch.sh` exists.
3. Launch **Moonlight** from Tools.

First launch creates `$SHARED_USERDATA_PATH/moonlight/`.

## First-time setup

1. **Servers** → **X** to add a PC.
2. Enter a display name and the **IP address** (hostnames do not resolve on
   the device).
3. **Pair**, then enter the PIN in Sunshine.
4. **Stream**, pick an app.

## Controls

| Screen | Button | Action |
|---|---|---|
| Main menu | A | Select |
| Main menu | B | Quit |
| Servers | A | Open server details |
| Servers | X | Add server |
| Servers | Select | Set as active |
| Settings | Left/Right | Change value |
| Settings | A | Save |
| Pairing / querying | B | Cancel |
| Streaming | engine defaults | exit combo is the engine's (see below) |

The engine's own exit combination is unchanged by this port. The spruce build
uses the standard moonlight-embedded SDL input path; `-quitappafter` asks the
host to close the streamed app when the local session ends.

## Runtime paths

| Data | Path |
|---|---|
| Config | `$SHARED_USERDATA_PATH/moonlight/config.txt` (`/mnt/SDCARD/.userdata/shared/moonlight/`) |
| Pairing keys | `$SHARED_USERDATA_PATH/moonlight/keys` (`-keydir` via the shim) |
| Log | `$LOGS_PATH/Moonlight.txt` (`/mnt/SDCARD/.userdata/tg5040/logs/`) |
| CA bundle | `/mnt/SDCARD/.system/res/ca-bundle.crt`, pak fallback `res/ca-bundle.crt` |
| Engine | `bin/moonlight` + `bin/libs/` inside the pak |
| Shim | `shim/moonlight` first on `PATH` |

The launcher sets `PATH`, `LD_LIBRARY_PATH`, `SDL_AUDIODRIVER=alsa`,
`MOONLIGHT_KEY_DIR`, `CURL_CA_BUNDLE`, and `SSL_CERT_FILE`, then runs the app
inside a C3 session (`nextui-session.sh`). It does not `exec` over itself, so
the session's EXIT cleanup always runs. It does not write `/tmp/stay_awake`:
NextUI does not read that file, and sleep inhibition moves to the SDK
(`sleep.inhibit`) in M4.

## Engine shim

The wrapper calls the bare name `moonlight`, exactly as upstream did. The shim
preserves the shipped behavior:

```sh
moonlight stream <host> ...   ->   bin/moonlight -platform sdl -keydir ... -quitappafter stream <host> ...
moonlight pair|list ...       ->   bin/moonlight -platform sdl -keydir ... pair|list ...
```

`-quitappafter` is stream-only. The stream call itself is now a direct
`fork`/`execvp` with `setpgid`, not `system()`, so the wrapper stays alive to
pump SDK events and to observe the engine's exit status. Pair/list still run
through `/bin/sh -c` because they parse CLI stdout.

## Known limits

- **Streaming-time HOME/HUD is not in-process.** The engine owns the display;
  until phase A provides dynamic ownership, NextUI's system fallback is the
  only HOME/HUD owner during a stream. M6 either defers to the system or ends
  the stream and reopens the menu.
- **No sleep inhibit yet.** M4 adds `sleep.prepare` / `sleep.inhibit` /
  `wake.done`. Until then the desktop's idle-sleep behavior is whatever NextUI
  does for a foreground app.
- **Overlay is built, not attached.** M2/A decides the data shape; the menu
  still uses Apostrophe's own widgets today.
- **Input mapping is the largest baseline risk.** The engine uses SDL game
  controllers; if the Brick pad is not in SDL's controller database, a
  `gamecontrollerdb` mapping is required. Verify on device.
- **1024x768 is the default and never actually streamed in host tests.** Host
  tests cover packaging and rendering, not the network/video path.
- **1024x768 / 1280x720 / 640x480 only** are exposed in Settings; the Brick's
  native panel is 1024x768.
- **SD card is FAT32.** Logs and config are small text files; no symlinks are
  used by the pak.
- **NextUI overlay code is PolyForm Noncommercial.** The pak is free; see
  `docs/THIRD-PARTY.md` before redistributing commercially.

## Cooperation roadmap (M1-M6)

`src/nextui_coop.c` is the single seam. It currently tracks menu vs streaming
state and answers the overlay-owner question; every SDK call is behind
`NEXTUI_SDK` and a `#ifdef` block labelled with its M item.

- **M1** — connect to devserv, event pump, `ap_host_backend`.
- **M2** — attach the host backend and use the vendored overlay in the menu.
- **M3** — `system-feedback` HUD ownership (app in menu, system in stream).
- **M4** — sleep/wake handshake; `sleep.inhibit("streaming")` held by the
  stream lifetime; `nextui_coop_tick()` already runs in the stream wait loop.
- **M5** — battery/status rows from the SDK.
- **M6** — dynamic ownership: menu `handled_by_app`, streaming
  `handled_by_system`; `pak.json` `overlay` capability enabled only then.

`NEXTUI_SDK=ON` configures the build once phase A's C client lands; today it
fails with a clear message rather than silently compiling a stub against a
non-existent header.

## License

The wrapper and the files added by this fork are MIT (`LICENSE`). The engine is
GPL-3.0; the vendored NextUI overlay is PolyForm Noncommercial 1.0.0;
Apostrophe is MIT. See [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).
