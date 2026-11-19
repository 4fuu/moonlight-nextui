#!/bin/sh
# Run the cooperation transition test against the SDK's fake devserv.
#
#   scripts/nextui/test-coop.sh <test-coop> <fake-devserv> <sd-root> <log> [mode]

set -eu

TEST=${1:?usage: test-coop.sh <test-coop> <fake-devserv> <sd-root> <log> [mode]}
FAKE=${2:?usage: test-coop.sh <test-coop> <fake-devserv> <sd-root> <log> [mode]}
SD=${3:?usage: test-coop.sh <test-coop> <fake-devserv> <sd-root> <log> [mode]}
LOG=${4:?usage: test-coop.sh <test-coop> <fake-devserv> <sd-root> <log> [mode]}
MODE=${5:-inprocess}

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
RES="$ROOT/third_party/nextui-overlay/res"
SOCK="$SD/devserv.sock"

rm -rf "$SD"
mkdir -p "$SD/.system/res" "$SD/.userdata/shared"
cp "$RES/assets@3x.png" "$RES/inputs@3x.png" "$RES/font2.ttf" "$SD/.system/res/"
printf 'font=0\nlang=en\n' >"$SD/.userdata/shared/minuisettings.txt"

rm -f "$LOG" "$SOCK"

"$FAKE" --socket "$SOCK" --log "$LOG" &
FAKE_PID=$!
trap 'kill "$FAKE_PID" 2>/dev/null || true' EXIT INT TERM

# Wait for the socket to appear.
i=0
while [ ! -S "$SOCK" ] && [ "$i" -lt 50 ]; do
    sleep 0.05
    i=$((i + 1))
done
[ -S "$SOCK" ] || { echo "test-coop: fake devserv did not start" >&2; exit 1; }

if [ "$MODE" = "inprocess" ]; then
    MODE_ENV=1
else
    MODE_ENV=0
fi

DEVICE=brick \
SHARED_USERDATA_PATH="$SD/.userdata/shared" \
NEXTUI_DEVSERV_SOCKET="$SOCK" \
MOONLIGHT_INPROCESS_OVERLAY="$MODE_ENV" \
SDL_VIDEODRIVER=dummy \
    "$TEST" "$LOG" "$MODE"
