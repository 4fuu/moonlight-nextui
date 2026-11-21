# moonlight-nextui

Moonlight game streaming for NextUI on the TrimUI Brick / tg5040.

This is a self-contained fork of
[richieszemeredi/nextui-moonlight-pak](https://github.com/richieszemeredi/nextui-moonlight-pak):
the Apostrophe wrapper is rebuilt in this repository, the moonlight-embedded
2.7.0 engine is rebuilt from source in CI (with Cedar hardware decoding on the
VPU) together with its library closure, and the NextUI
cooperation layer (HOME overlay, HUD, sleep/wake, battery) is being added on
top of [nextui-app-sdk](https://github.com/4fuu/nextui-app-sdk).

- Port notes, build, controls and limits: [NEXTUI.md](NEXTUI.md)
- Third-party licenses and provenance: [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md)
- Device checklist: [docs/NEXTUI-DEVICE-TEST.md](docs/NEXTUI-DEVICE-TEST.md)
- Integration plan: <https://ampcode.com/artifacts/moonlight-sdk-e0e0c7bc-58f6-46d7-9bcd-6e31a08e7a96>

## Quick build

CI is the authoritative aarch64 build. `.github/workflows/engine.yml` builds
the engine from source for tg5040 (moonlight-embedded 2.7.0 plus the patches
in `engine/patches/`), and `.github/workflows/build.yml` produces
`Moonlight.pakz`. On a Docker host:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest bash -lc '
    apt-get update && apt-get install -y --no-install-recommends cmake zip file python3
    ./scripts/nextui/build.sh tg5040
    ./scripts/nextui/make-pak.sh
  '
```

Host build and tests:

```sh
./scripts/nextui/build.sh host
ctest --test-dir build-host --output-on-failure
```

## Status

Baseline streaming works and the SDK cooperation layer is implemented.

v0.4.0 adds hardware H.264/HEVC decoding on the Cedar VPU, on by default
([docs/cedar-hwdec.md](docs/cedar-hwdec.md)), and replaces the bundled binary
with an engine rebuilt from source in CI plus a one-frame present buffer.
Hardware decode was device-verified: correct picture at 1024x768, about 3 ms
per frame, streams at roughly a third of the software decoder's CPU, no
dropped frames and no ION leak; a firmware without the vendor libraries falls
back to software by itself, and `export MOONLIGHT_HWDEC=0` forces that.

HOME/HUD use NextUI's system panel by default (v0.3.4, stable after the
v0.3.0 device freeze); the in-process full-fidelity panel is opt-in with
diagnostics. Sleep inhibit during a stream and SDK battery/status remain
active. Device acceptance of the cooperation layer is pending. See
[NEXTUI.md](NEXTUI.md#status).

## License

Wrapper: MIT. Engine: GPL-3.0. Vendored NextUI overlay: PolyForm Noncommercial
1.0.0. Apostrophe: MIT. See [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).
