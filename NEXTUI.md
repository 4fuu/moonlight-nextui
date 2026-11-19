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
| `pak.json` identity + `overlay` capability | done, S4/M6 |
| `ml_stream` controlled spawn + cooperation seam | done, S5 |
| Full-fidelity overlay renderer vendored and building | done, S1/S2 |
| HOME panel | **system fallback by default** (v0.3.5) |
| In-process full-fidelity panel | opt-in, `MOONLIGHT_INPROCESS_OVERLAY=1` |
| Volume/brightness HUD cooperation | done, M3 (system HUD by default) |
| Sleep/wake handshake + stream inhibit | done, M4 |
| Battery/status rows from the SDK | done, M5 |
| Streaming-time HOME ownership (dynamic) | done, M6 |
| Device acceptance (handshake, panel, HUD) | **pending owner test** |

The wrapper now links `nextui-app-sdk` at `27e5597` and implements the
cooperation contract. A missing devserv is not fatal: the app logs the failure,
reads HOME from `KEY_HOMEPAGE` itself, and otherwise behaves like an
uncooperative app.

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

`third_party/nextui-overlay/` is linked into the wrapper. The app is built
from Apostrophe's blocking widgets, which own their frame loops, so the
Apostrophe fork's host bridge drives the same backend from `ap_poll_input()`
and `ap_present()`: the panel is drawn before every present, host input is
polled once per frame, and a quit request injects one synthetic B press so the
widget unwinds to `main()`. A custom-loop app can still use
`ap_host_poll()`/`ap_host_present()` directly. See
[docs/THIRD-PARTY.md](docs/THIRD-PARTY.md) for provenance and the NextUI
noncommercial license.

Why not link NextUI's full `api.c` behavior? We do vendor `api.c` verbatim, but
link with `-ffunction-sections -Wl,--gc-sections`: only the GFX/PAD/CFG path the
overlay reaches is kept, so the pak does not pull NextUI's power, video or LED
subsystems into the app process.

## HOME panel and overlay ownership

`pak.json` declares `"capabilities": ["overlay"]` — the permission, not a
lifetime claim. The effective host is runtime session state (contract §7.9).

**Default (v0.3.5): the app yields hosting for the whole session.** It sends
`overlay.host:false` at connect, so HOME and the HUD are NextUI's system
panel/keymon. That panel is the full-fidelity reference, and it is the
responsive path: the app process does not render, does not read pad input for
a panel, and cannot hold HOME. Sleep inhibit, battery/status, the event channel
and streaming all stay active.

**Opt-in: `MOONLIGHT_INPROCESS_OVERLAY=1`** restores the app-hosted panel for
development. It is not the shipping default until the owner's device run
confirms it. The SDK request/event lock split (`27e5597`) removes the client
side of the v0.3.0 deadlock; in that mode:

- menu hosts the panel in-process (`overlay.host:true`, HUD app for
  volume/brightness/colortemp);
- streaming yields it, closes the panel and tears down SDL in the §7.9.3 order;
- the SDK event pushes an `SDL_USEREVENT` to wake an idle widget loop, and the
  `overlay.state` report is queued to the SDK thread instead of doing socket
  I/O on the UI thread;
- while the panel owns the frame, Apostrophe's host bridge stops calling
  `coop_poll()`, so the panel input (including the HOME close) is driven from
  the draw path; without that the panel would draw but ignore every button;
- a failed `overlay.state` report is re-queued (up to three times) so a
  timeout cannot leave devserv believing the panel is still open;
- a 2 s watchdog yields hosting if the panel never opens.

Diagnostics in `Moonlight.txt` for the opt-in path:

```text
nextui: init overlay_mode=in-process
nextui: overlay renderer created
nextui: requesting overlay.host(1) (in-process)
nextui: overlay.host(1) -> ok
nextui: connected: session=... hosting=1 declared_host=1
nextui: overlay.home event
nextui: HOME consumed by main loop
nextui: panel open begin
nextui: overlay opened (state queued)
nextui: panel open returned open=1
nextui: panel draw #1 begin
nextui: panel draw #1 end
nextui: reporting overlay.state(1)
nextui: overlay.state(1) ok
nextui: overlay.home event
nextui: HOME consumed by main loop
nextui: panel close begin
nextui: panel close returned open=0
nextui: reporting overlay.state(0)
nextui: overlay.state(0) ok
```

The step that is missing is where the device stalls. The system-fallback build
logs `overlay_mode=system-fallback`, `requesting overlay.host(0)`, `overlay.host(0) -> ok`
and `hosting=0` instead.

## Stream exit cleanup

`ml_stream` spawns the engine as its own process group and registers the pid
process-wide. Every wrapper exit path stops it before the wrapper returns:

- normal engine exit and the cooperation quit flag;
- the overlay's Exit action, which sets the same quit flag instead of
  unwinding the widget loop past `ml_stream`;
- `atexit`, so an `exit(0)` from a widget or power path cannot orphan the
  engine;
- SIGTERM/SIGINT/SIGHUP/SIGQUIT, which set a stream-loop flag so the engine is
  stopped before the wrapper re-raises the signal.

The stop sequence is SIGTERM to the whole process group, a wait of up to
2.5 s for the engine's own signal loop to tear down video/audio and restore the
display, then SIGKILL and `waitpid` as the fallback. The engine's `loop.c`
handles SIGTERM/SIGINT/SIGQUIT/SIGHUP, so the normal path is graceful; the
SIGKILL is only for a wedged engine.

The wrapper's `config.txt` is written as `key = value` because
moonlight-embedded's `config_file_parse()` requires spaces around the equals
sign. `load_config()` still accepts legacy `key=value` files. The pak does not
pass that file to the engine: stream resolution is the `-width/-height/-fps/
-bitrate` argv built by `ml_stream`, so a config-format change cannot alter the
requested resolution.

## Power key ownership

The pak disables Apostrophe's built-in power handler
(`ap_set_power_handler(false)`) on every `ap_init()`. That fallback reads
`/dev/input/event1` itself and runs `suspend` on a short press or
`touch /tmp/poweroff; exit(0)` on a long press, bypassing NextUI's
`sleep.request` handshake and fighting keymon. In NextUI mode the power key
belongs to keymon: a short press sends `sleep.prepare`, the app acks or inhibits
it, and `wake.done` follows. While streaming, the app keeps a
`sleep.inhibit("streaming", 300000)` lease; the pak never calls `suspend` or
`poweroff` itself. See `docs/NEXTUI-DEVICE-TEST.md` section 9.

`tests/test_coop.c` asserts both modes' request order against the SDK fake
devserv (`coop-transitions` and `coop-system-fallback`).

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

`ctest` runs seven host tests: the upstream parser tests, the stream-spawn
argv/exit/ownership checks, the widget host bridge, NextUI's manifest parser,
the full-fidelity panel render (`build-host/overlay-smoke.png`), the HUD render
(`build-host/hud-smoke.png`), and the dynamic-ownership transition against the
SDK's fake devserv.

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
| Any menu screen | HOME | opens the NextUI panel (Resume/Close, Wi-Fi, Bluetooth, Exit) |
| Panel | D-pad up/down, A | move, activate |
| Panel | B | close, back to the app |
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

`bin/libs/` is the complete non-system closure: 35 libraries, including the
PulseAudio/audio tail (`libpulsecommon-13.99`, libsndfile, FLAC, Vorbis, Ogg),
xcb/X11, and the focal glibc `libnsl.so.1` that `libwrap.so.0` expects. `scripts/nextui/check-libs.py`
resolves every `NEEDED` entry in `bin/moonlight` and `bin/libs/*` and fails on
anything that is neither bundled nor on the explicit device-library list;
`make-pak.sh` runs it before zipping and CI runs it again with the toolchain
sysroot. CI also dry-runs `bin/moonlight --version` through
`qemu-aarch64-static` with focal glibc and the sysroot runtime, so
symbol-version mismatches fail the build. v0.3.2 shipped without the second-level dependencies and could not
start `bin/moonlight` on device; v0.3.3 then shipped a libnsl without the
`GLIBC_2.17` symbols `libwrap.so.0` references.

The launcher sets `PATH`, `LD_LIBRARY_PATH`, `SDL_AUDIODRIVER=alsa`,
`MOONLIGHT_KEY_DIR`, `CURL_CA_BUNDLE`, and `SSL_CERT_FILE`, then runs the app
inside a C3 session (`nextui-session.sh`). It does not `exec` over itself, so
the session's EXIT cleanup always runs. It does not write `/tmp/stay_awake`:
NextUI does not read that file; while streaming the wrapper holds an SDK
`sleep.inhibit("streaming", 300000)` lease and refreshes it every 60 s.

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

- **Streaming-time HOME/HUD is system-owned.** The engine owns fb0, so the
  wrapper yields hosting and NextUI's fallback panel/HUD is used during a
  stream. The panel's Exit action ends the session through the launch parent.
- **The app cannot host if devserv is absent.** `pak.json` declares `overlay`,
  so HOME is never routed to the system panel for a live session. Without a
  devserv the wrapper reads `KEY_HOMEPAGE` itself and opens its own panel; that
  covers the menu, but there is no system fallback in that state.
- **Mute HUD stays with the system.** The app draws volume, brightness and
  colortemp with NextUI's `GFX_blitHardwareIndicator`; it has no mute icon, so
  `hud.policy` leaves mute to keymon.
- **Wi-Fi/Bluetooth toggles are data rows only.** `net.wifi`/`net.bluetooth`
  have no backend in this NextUI build; the rows show state, and a toggle
  returns "unavailable" rather than pretending to succeed.
- **Input mapping is the largest baseline risk.** The engine uses SDL game
  controllers; if the Brick pad is not in SDL's controller database, a
  `gamecontrollerdb` mapping is required. Verify on device.
- **1024x768 is the default and never actually streamed in host tests.** Host
  tests cover packaging, rendering and the protocol transitions, not the
  network/video path.
- **1024x768 / 1280x720 / 640x480 only** are exposed in Settings; the Brick's
  native panel is 1024x768.
- **SD card is FAT32.** Logs and config are small text files; no symlinks are
  used by the pak.
- **`libsamplerate.so.0` is a runtime dependency.** The vendored NextUI
  renderer references it; NextUI ships it in `.system/tg5040/lib`, reached
  through the `LD_LIBRARY_PATH` the launcher inherits. The pak does not bundle
  it. CI links an aarch64 binary to prove the dependency resolves.
- **NextUI overlay code is PolyForm Noncommercial.** The pak is free; see
  `docs/THIRD-PARTY.md` before redistributing commercially.

## Cooperation layer (M1-M6)

`src/nextui_coop.c` owns the app-side policy; `third_party/nextui-app-sdk`
owns the protocol.

- **M1** — connect + hello/subscribe, dedicated event thread, status snapshot.
- **M2** — `ap_host_attach` + the Apostrophe widget bridge; HOME opens the
  vendored panel; Exit requests a clean quit.
- **M3** — `hud.policy` app for volume/brightness/colortemp; `feedback` events
  draw NextUI's indicator for 1.6 s; mute stays system.
- **M4** — `sleep.prepare` is acked in the menu and inhibited while streaming;
  the stream holds and refreshes an automatic `sleep.inhibit` lease;
  `wake.done` refreshes status.
- **M5** — `status.snapshot`/`status.changed` feed the battery pill, Wi-Fi/Bluetooth
  rows and HUD values.
- **M6** — dynamic `overlay.host` transition in the §7.9.3 order, asserted by
  `tests/test_coop.c`.

If the SDK's C client is missing from the submodule, `NEXTUI_SDK=ON` fails with
a clear configure message instead of compiling a stub.

## License

The wrapper and the files added by this fork are MIT (`LICENSE`). The engine is
GPL-3.0; the vendored NextUI overlay is PolyForm Noncommercial 1.0.0;
Apostrophe is MIT. See [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).
