#!/bin/sh
# Prepare a scratch SD root with the vendored NextUI resources and run the
# full-fidelity overlay render smoke test.
#
#   scripts/nextui/test-overlay.sh <overlay-smoke binary> <sd root> [png out]
#
# The binary is compiled with SDCARD_PATH pointing at <sd root> (see
# CMakeLists.txt), so this never touches /mnt/SDCARD on the host.

set -eu

BIN=${1:?usage: test-overlay.sh <overlay-smoke> <sd-root> [png-out]}
SD=${2:?usage: test-overlay.sh <overlay-smoke> <sd-root> [png-out]}
OUT=${3:-$SD/overlay-smoke.png}

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
RES="$ROOT/third_party/nextui-overlay/res"

rm -rf "$SD"
mkdir -p "$SD/.system/res" "$SD/.userdata/shared"

cp "$RES/assets@3x.png" "$RES/inputs@3x.png" "$RES/font2.ttf" "$SD/.system/res/"
printf 'font=0\nlang=en\n' >"$SD/.userdata/shared/minuisettings.txt"

DEVICE=brick \
SHARED_USERDATA_PATH="$SD/.userdata/shared" \
SDL_VIDEODRIVER=dummy \
    "$BIN" "$OUT"

echo "overlay smoke output: $OUT"
