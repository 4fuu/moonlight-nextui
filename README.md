# moonlight-nextui

Moonlight game streaming for NextUI on the TrimUI Brick / tg5040.

This is a self-contained fork of
[richieszemeredi/nextui-moonlight-pak](https://github.com/richieszemeredi/nextui-moonlight-pak):
the Apostrophe wrapper is rebuilt in this repository, the spruce 2.7.0
moonlight-embedded engine and its library closure are bundled, and the NextUI
cooperation layer (HOME overlay, HUD, sleep/wake, battery) is being added on
top of [nextui-app-sdk](https://github.com/4fuu/nextui-app-sdk).

- Port notes, build, controls and limits: [NEXTUI.md](NEXTUI.md)
- Third-party licenses and provenance: [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md)
- Device checklist: [docs/NEXTUI-DEVICE-TEST.md](docs/NEXTUI-DEVICE-TEST.md)
- Integration plan: <https://ampcode.com/artifacts/moonlight-sdk-e0e0c7bc-58f6-46d7-9bcd-6e31a08e7a96>

## Quick build

CI (`.github/workflows/build.yml`) is the authoritative aarch64 build and
produces `Moonlight.pakz`. On a Docker host:

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

Baseline streaming works; the SDK cooperation layer (M1-M6) waits for the
phase-A contract and phase-B system side. The full-fidelity NextUI overlay
renderer is vendored and built, but not attached to the menu yet. See
[NEXTUI.md](NEXTUI.md#status).

## License

Wrapper: MIT. Engine: GPL-3.0. Vendored NextUI overlay: PolyForm Noncommercial
1.0.0. Apostrophe: MIT. See [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).
