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

# moonlight-common 8af4562 sends an ENABLE_HAPTICS packet (magic 0x0D) when
# the input stream starts. Vibepollo 2.18.19 logs it as an unknown input
# magic; this handheld has no haptics, so skip the packet. The patch is
# applied to the pak copy only: engine/bin/libs stays byte-identical to the
# spruce package and SHA256SUMS keeps matching it.
python3 "$ROOT/scripts/nextui/patch-engine.py" \
    "$PAK/bin/libs/libmoonlight-common.so.4"

# pak.json declares "capabilities": ["overlay"]. That is the permission, not a
# lifetime claim: the wrapper sends overlay.host:false while the engine owns
# the display and restores it in the menu (contract 7.9). Keep the declaration
# in the manifest file so NextUI's parser and the tests see the same value.
if ! python3 -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("capabilities")==["overlay"] else 1)' "$PAK/pak.json"; then
    echo "make-pak: pak.json must declare capabilities [\"overlay\"]" >&2
    exit 1
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

# The engine is copied unmodified, so its whole shared-library closure has to
# be inside the pak or come from the device. v0.3.2 shipped without the
# PulseAudio/audio tail (libpulsecommon and friends) and failed to start on
# device; this check is the regression guard for that.
READELF="${CROSS_TRIPLE:+${CROSS_TRIPLE}-readelf}" \
    python3 "$ROOT/scripts/nextui/check-libs.py" "$PAK"

test -x "$PAK/launch.sh"
test -x "$PAK/shim/moonlight"
python3 -m json.tool "$PAK/pak.json" >/dev/null

rm -f "$DIST/Moonlight.pakz"
( cd "$PAK" && zip -qrX "$DIST/Moonlight.pakz" . -x '.*' )

echo "==> pak contents:"
find "$PAK" -maxdepth 2 -type f | sort
echo "==> archive:"
ls -lh "$DIST/Moonlight.pakz"
