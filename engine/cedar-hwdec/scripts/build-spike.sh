#!/bin/sh
# Cross-builds the Cedar H.264 decode spike (tsp-videotest) for tg5040.
#
#   engine/cedar-hwdec/scripts/build-spike.sh
#
# The tool only dlopen()s the device's vendor decoder libraries, so any
# aarch64 Linux cross gcc works. The script tries, in order:
#
#   1. $CROSS_CC, if set;
#   2. the NextUI toolchain container's compiler, if CROSS_ROOT/SYSROOT are
#      exported (the CI container) or its extracted tree is at
#      $TOOLCHAIN_ROOT (see fetch-toolchain.sh);
#   3. Debian's gcc-aarch64-linux-gnu.
#
# Output: build-spike/tsp-videotest and build-spike/spike-<arch>-<gcc>.tar.gz,
# the bundle to copy to the device (tool + media + README).

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build-spike}
CFLAGS=${CFLAGS:--O2 -g}
HARDEN="-fstack-protector-strong -D_FORTIFY_SOURCE=2"
SRCS="$ROOT/tools/tsp-videotest.c $ROOT/src/cedar/cedar.c $ROOT/src/cedar/cedar_sync.c \
      $ROOT/src/disp.c $ROOT/src/g2d_rot.c $ROOT/src/ion.c"
# The vendor SBM parser must bind to our SemTimedWait (src/cedar_sync.c).
# -rdynamic: the tg5040 toolchain's ld.bfd (2.31) lacks
# --export-dynamic-symbol, which is what moonlight-tsp uses upstream.
LDFLAGS="-Wl,--export-dynamic -Wl,-z,relro,-z,now -ldl -lpthread"

pick_cc() {
    if [ -n "${CROSS_CC:-}" ]; then
        printf '%s\n' "$CROSS_CC"
        return
    fi
    triple=${CROSS_TRIPLE:-aarch64-nextui-linux-gnu}
    for dir in "${CROSS_ROOT:-}" "${CROSS_ROOT:-}/bin" \
               "${TOOLCHAIN_ROOT:-}/opt/$triple/bin" \
               "${TOOLCHAIN_ROOT:-}/opt/x86_64-$triple/bin"; do
        [ -n "$dir" ] || continue
        if [ -x "$dir/$triple-gcc" ]; then
            printf '%s\n' "$dir/$triple-gcc"
            return
        fi
    done
    for name in aarch64-nextui-linux-gnu-gcc aarch64-linux-gnu-gcc; do
        if command -v "$name" >/dev/null 2>&1; then
            printf '%s\n' "$name"
            return
        fi
    done
    echo "build-spike: no aarch64 cross compiler found (set CROSS_CC)" >&2
    exit 1
}

CC=$(pick_cc)
SYSROOT_ARG=
if [ -n "${SYSROOT:-}" ] && [ -d "$SYSROOT" ]; then
    SYSROOT_ARG="--sysroot=$SYSROOT"
fi

mkdir -p "$OUT"
echo "build-spike: CC=$CC ${SYSROOT_ARG:-}"
# shellcheck disable=SC2086 # intentional word splitting of the flag lists
$CC $SYSROOT_ARG -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
    $HARDEN -D_GNU_SOURCE -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/src/cedar"  \
    $CFLAGS -o "$OUT/tsp-videotest" $SRCS $LDFLAGS

"${READELF:-readelf}" -h "$OUT/tsp-videotest" | grep -q 'AArch64'
"${READELF:-readelf}" -d "$OUT/tsp-videotest" | grep -q 'libdl'
strip --strip-unneeded "$OUT/tsp-videotest" 2>/dev/null ||
    "${STRIP:-strip}" --strip-unneeded "$OUT/tsp-videotest" 2>/dev/null || true

# One tarball with everything the device run needs.
STAGE=$OUT/bundle
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$OUT/tsp-videotest" "$STAGE/"
cp "$ROOT/tools/spike/README-device.txt" "$STAGE/README.txt"
cp "$ROOT/media/"*.264 "$STAGE/" 2>/dev/null || true
if [ "${SPIKE_HEVC:-0}" = 1 ]; then
    cp "$ROOT/media/"*.265 "$STAGE/" 2>/dev/null || true
fi
tar -C "$STAGE" --sort=name --mtime='@0' --owner=0 --group=0 \
    --numeric-owner -cf - . | gzip -n -9 > "$OUT/spike-bundle.tar.gz"
rm -rf "$STAGE"

sha256sum "$OUT/tsp-videotest" "$OUT/spike-bundle.tar.gz"
echo "build-spike: done, copy $OUT/spike-bundle.tar.gz to the device"
