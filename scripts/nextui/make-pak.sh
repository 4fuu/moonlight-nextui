#!/bin/sh
# Build Moonlight.pakz from a cross-compiled wrapper and the bundled spruce
# engine. Run after scripts/nextui/build.sh tg5040 (or the CMake steps in
# NEXTUI.md / CI).
#
# The archive shape is the NextUI app-installer shape: launch.sh is at the
# archive root, not inside a Moonlight.pak/ directory.

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build-nextui}
DIST="$ROOT/dist"
PAK="$DIST/Moonlight.pak"
TEMPLATE="$ROOT/Tools/tg5040/Moonlight.pak"
APP="$BUILD/moonlight-pak"

if [ ! -x "$APP" ]; then
    echo "make-pak: $APP is missing; run scripts/nextui/build.sh tg5040 first" >&2
    exit 1
fi
if [ ! -x "$ROOT/engine/bin/moonlight" ]; then
    echo "make-pak: engine/bin/moonlight is missing" >&2
    exit 1
fi

rm -rf "$PAK"
mkdir -p "$PAK/bin/libs" "$PAK/shim" "$PAK/res" "$PAK/licenses"

cp "$TEMPLATE/launch.sh" "$TEMPLATE/pak.json" "$PAK/"
chmod 755 "$PAK/launch.sh"

install -m 755 "$APP" "$PAK/moonlight-pak"
install -m 755 "$ROOT/shim/moonlight" "$PAK/shim/moonlight"

# Bundled spruce 2.7.0 moonlight-embedded engine and its shared-library
# closure. The engine is never rebuilt here; GPL source is referenced in
# docs/THIRD-PARTY.md and licenses/.
install -m 755 "$ROOT/engine/bin/moonlight" "$PAK/bin/moonlight"
for lib in "$ROOT"/engine/bin/libs/*.so*; do
    cp -L "$lib" "$PAK/bin/libs/"
done
chmod 755 "$PAK"/bin/libs/*.so*

# The overlay capability is a guarded placeholder, not a static declaration.
# Phase A must define dynamic ownership before this is enabled: the menu can
# host HOME in-process, but during a stream the engine owns the display and
# the wrapper must answer handled_by_system. A static "overlay" in pak.json
# would claim HOME for the whole app lifetime and leave streaming with no
# owner.
if [ "${MOONLIGHT_OVERLAY_CAPABILITY:-0}" = "1" ]; then
    echo "make-pak: adding overlay capability (phase A dynamic ownership required)"
    python3 - "$PAK/pak.json" <<'PY'
import json, sys
path = sys.argv[1]
with open(path) as fh:
    manifest = json.load(fh)
manifest["capabilities"] = ["overlay"]
with open(path, "w") as fh:
    json.dump(manifest, fh, indent=2)
    fh.write("\n")
PY
fi

# CA fallback; launch.sh prefers /mnt/SDCARD/.system/res/ca-bundle.crt.
CA_SRC=${CA_BUNDLE_SRC:-/etc/ssl/certs/ca-certificates.crt}
if [ -r "$CA_SRC" ]; then
    cp "$CA_SRC" "$PAK/res/ca-bundle.crt"
else
    echo "make-pak: warning: no CA bundle at $CA_SRC; HTTPS pairing needs the system bundle" >&2
fi

cp "$ROOT/LICENSE" "$PAK/licenses/LICENSE"
cp "$ROOT/third_party/apostrophe/LICENSE" "$PAK/licenses/Apostrophe-MIT.txt"
cp "$ROOT/third_party/nextui-overlay/LICENSE" "$PAK/licenses/nextui-overlay-PolyForm-Noncommercial.txt"
cp "$ROOT/docs/licenses/moonlight-embedded-GPL-3.0.txt" "$PAK/licenses/moonlight-embedded-GPL-3.0.txt"
cp "$ROOT/docs/THIRD-PARTY.md" "$PAK/licenses/THIRD-PARTY.md"

# Sanity checks.
if [ -n "${CROSS_TRIPLE:-}" ]; then
    if ! "${CROSS_TRIPLE}-readelf" -h "$PAK/moonlight-pak" | grep -q 'AArch64'; then
        echo "make-pak: moonlight-pak is not an AArch64 ELF" >&2
        exit 1
    fi
    if ! "${CROSS_TRIPLE}-readelf" -h "$PAK/bin/moonlight" | grep -q 'AArch64'; then
        echo "make-pak: bin/moonlight is not an AArch64 ELF" >&2
        exit 1
    fi
fi
test -x "$PAK/launch.sh"
test -x "$PAK/shim/moonlight"
python3 -m json.tool "$PAK/pak.json" >/dev/null

rm -f "$DIST/Moonlight.pakz"
( cd "$PAK" && zip -qrX "$DIST/Moonlight.pakz" . -x '.*' )

echo "==> pak contents:"
find "$PAK" -maxdepth 2 -type f | sort
echo "==> archive:"
ls -lh "$DIST/Moonlight.pakz"
