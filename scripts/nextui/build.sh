#!/bin/sh
# Build the Moonlight wrapper.
#
#   scripts/nextui/build.sh            host build (SDL2 from the system)
#   scripts/nextui/build.sh tg5040     cross build inside the NextUI toolchain
#
# The tg5040 build is the authoritative aarch64 build. It expects the
# ghcr.io/loveretro/tg5040-toolchain container (CROSS_TRIPLE/SYSROOT exported);
# see NEXTUI.md and .github/workflows/build.yml.

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
TARGET=${1:-host}

if [ "$TARGET" = "tg5040" ]; then
    BUILD=${BUILD_DIR:-$ROOT/build-nextui}
    : "${SYSROOT:?run inside the tg5040 toolchain image (SYSROOT is not set)}"
    : "${CROSS_TRIPLE:?run inside the tg5040 toolchain image (CROSS_TRIPLE is not set)}"
    cmake -S "$ROOT" -B "$BUILD" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-tg5040.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DMOONLIGHT_PLATFORM=tg5040
elif [ "$TARGET" = "host" ]; then
    BUILD=${BUILD_DIR:-$ROOT/build-host}
    cmake -S "$ROOT" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DNEXTUI_BUILD_TESTS=ON
else
    echo "usage: $0 [host|tg5040]" >&2
    exit 2
fi

cmake --build "$BUILD" --parallel "${JOBS:-4}"
echo "build output: $BUILD"
