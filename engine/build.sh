#!/bin/bash
set -e -o pipefail

MOONLIGHT_COMMIT="${MOONLIGHT_COMMIT:-775444287305849ebdf4736c75298ad0713e2d5d}"
MOONLIGHT_COMMON_COMMIT="b126e481a195fdc7152d211def17190e3434bcce"
ENET_COMMIT="dea6fb5414b180908b58c0293c831105b5d124dd"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.3.1}"
CURL_VERSION="${CURL_VERSION:-8.7.1}"
FFMPEG_VERSION="${FFMPEG_VERSION:-4.4.5}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
CROSS=aarch64-linux-gnu

export STRIP=${CROSS}-strip
export PKG_CONFIG_PATH=/usr/lib/${CROSS}/pkgconfig
export PKG_CONFIG_LIBDIR=/usr/lib/${CROSS}/pkgconfig

export OPTFLAGS="-O3 -ffunction-sections -fdata-sections -flto=auto"
export LDFLAGS="-Wl,--gc-sections -flto=auto"

# ccache setup
export CCACHE_DIR="${CCACHE_DIR:-/ccache}"
export PATH="/usr/lib/ccache:$PATH"
ln -sf /usr/bin/ccache /usr/local/bin/${CROSS}-gcc
ln -sf /usr/bin/ccache /usr/local/bin/${CROSS}-g++
ccache --max-size=500M
ccache --zero-stats

# Local install prefix for all from-source builds
PREFIX=/build/local
mkdir -p "$PREFIX"

# ============================================================
# Build OpenSSL 3.x from source
# (Don't set CC/CXX — OpenSSL's --cross-compile-prefix handles it)
# ============================================================
echo "=== Building OpenSSL ${OPENSSL_VERSION} ==="
wget -q --tries=3 --timeout=30 "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
tar xf "openssl-${OPENSSL_VERSION}.tar.gz"
cd "openssl-${OPENSSL_VERSION}"
./Configure linux-aarch64 \
    --cross-compile-prefix=${CROSS}- \
    --prefix="$PREFIX" \
    --libdir=lib \
    shared \
    no-tests \
    -O3
make -j$(nproc)
make install_sw
cd /build

# Set CC/CXX/AR for remaining builds
export CC=${CROSS}-gcc
export CXX=${CROSS}-g++
export AR=${CROSS}-ar

# ============================================================
# Build curl from source (against our OpenSSL)
# ============================================================
echo "=== Building curl ${CURL_VERSION} ==="
wget -q --tries=3 --timeout=30 "https://curl.se/download/curl-${CURL_VERSION}.tar.xz"
tar xf "curl-${CURL_VERSION}.tar.xz"
cd "curl-${CURL_VERSION}"
./configure \
    --host=${CROSS} \
    --prefix="$PREFIX" \
    --with-openssl="$PREFIX" \
    --without-libpsl \
    --disable-manual \
    --disable-ldap \
    --enable-shared \
    --disable-static \
    CFLAGS="$OPTFLAGS" \
    LDFLAGS="$LDFLAGS -Wl,-rpath-link,$PREFIX/lib"
make -j$(nproc)
make install
cd /build

# ============================================================
# Build minimal FFmpeg from source
# Only the decoders/demuxers moonlight actually needs
# ============================================================
echo "=== Building FFmpeg ${FFMPEG_VERSION} ==="
wget -q --tries=3 --timeout=30 "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
tar xf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
cd "ffmpeg-${FFMPEG_VERSION}"
./configure \
    --prefix="$PREFIX" \
    --cross-prefix=${CROSS}- \
    --arch=aarch64 \
    --target-os=linux \
    --enable-cross-compile \
    --enable-shared \
    --disable-static \
    --disable-programs \
    --disable-doc \
    --disable-everything \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=opus \
    --enable-decoder=aac \
    --enable-decoder=pcm_s16le \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-parser=opus \
    --enable-parser=aac \
    --enable-demuxer=h264 \
    --enable-demuxer=hevc \
    --enable-demuxer=rtp \
    --enable-demuxer=rtsp \
    --enable-protocol=rtp \
    --enable-protocol=udp \
    --enable-protocol=tcp \
    --enable-swresample \
    --enable-avcodec \
    --enable-avutil \
    --extra-cflags="$OPTFLAGS" \
    --extra-ldflags="$LDFLAGS"
make -j$(nproc)
make install
cd /build

# ============================================================
# Build moonlight-embedded
# ============================================================
echo "=== Building moonlight-embedded ==="
git clone https://github.com/moonlight-stream/moonlight-embedded.git moonlight
cd moonlight
git checkout "$MOONLIGHT_COMMIT"
git submodule update --init --recursive

# Pin the whole network stack: the v2.7.1 experiment is about the common-c/ENet
# wakeup and control-receive fixes, so fail if the submodules drift.
test "$(git -C third_party/moonlight-common-c rev-parse HEAD)" = "$MOONLIGHT_COMMON_COMMIT" || {
    echo "ERROR: moonlight-common-c is not $MOONLIGHT_COMMON_COMMIT" >&2
    exit 1
}
test "$(git -C third_party/moonlight-common-c/enet rev-parse HEAD)" = "$ENET_COMMIT" || {
    echo "ERROR: ENet is not $ENET_COMMIT" >&2
    exit 1
}

# Apply patches
for patch in /patches/*.py; do
    [ -f "$patch" ] && python3 "$patch" && echo "Applied: $(basename $patch)"
done
for patch in /patches/*.patch; do
    [ -f "$patch" ] && git apply "$patch" && echo "Applied: $(basename $patch)"
done

mkdir -p build && cd build

# Point cmake at our from-source FFmpeg + OpenSSL + curl
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=${CROSS}-gcc \
    -DCMAKE_CXX_COMPILER=${CROSS}-g++ \
    -DCMAKE_FIND_ROOT_PATH="/usr/${CROSS};${PREFIX};/usr" \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_LIBRARY_PATH="/usr/lib/${CROSS};${PREFIX}/lib" \
    -DCMAKE_INCLUDE_PATH="/usr/include;${PREFIX}/include" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$OPTFLAGS -I${PREFIX}/include" \
    -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS -L${PREFIX}/lib -L/usr/lib/${CROSS} -Wl,-rpath-link,${PREFIX}/lib -Wl,-rpath-link,/usr/lib/${CROSS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS -L${PREFIX}/lib -L/usr/lib/${CROSS} -Wl,-rpath-link,${PREFIX}/lib -Wl,-rpath-link,/usr/lib/${CROSS}" \
    -DENABLE_X11=OFF
make -j$(nproc)
cd /build

# ============================================================
# Collect output
# ============================================================
echo "=== Collecting output ==="
mkdir -p "$OUTPUT_DIR/libs"

# moonlight binary
cp moonlight/build/moonlight "$OUTPUT_DIR/"
${STRIP} -s "$OUTPUT_DIR/moonlight"

# Libraries built by moonlight-embedded
for lib in moonlight/build/libgamestream/libmoonlight-common.so* moonlight/build/libgamestream/libgamestream.so*; do
    [ -f "$lib" ] && cp "$lib" "$OUTPUT_DIR/libs/"
done

# Libraries we built from source (OpenSSL, curl, FFmpeg)
cp "$PREFIX"/lib/libssl.so.3 "$OUTPUT_DIR/libs/"
cp "$PREFIX"/lib/libcrypto.so.3 "$OUTPUT_DIR/libs/"
cp "$PREFIX"/lib/libcurl.so.4 "$OUTPUT_DIR/libs/"
cp "$PREFIX"/lib/libavcodec.so* "$OUTPUT_DIR/libs/"
cp "$PREFIX"/lib/libavutil.so* "$OUTPUT_DIR/libs/"
cp "$PREFIX"/lib/libswresample.so* "$OUTPUT_DIR/libs/"

# Recursively collect remaining shared library dependencies
# Skip libs that devices provide (glibc, SDL2, ALSA, udev, etc.)
SKIP_LIBS="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libgcc_s|libstdc\+\+|libSDL2|libasound|libudev|libdrm|libwayland|libEGL|libGLES|libMali|libz\.so"

collect_deps() {
    local binary="$1"
    ${CROSS}-readelf -d "$binary" 2>/dev/null | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | while read -r lib; do
        [ -f "$OUTPUT_DIR/libs/$lib" ] && continue
        echo "$lib" | grep -qE "$SKIP_LIBS" && continue

        local src
        src=$(find "$PREFIX/lib" "/usr/lib/${CROSS}" "/lib/${CROSS}" \
                     "/usr/lib/${CROSS}/pulseaudio" -maxdepth 1 \
                     -name "$lib" 2>/dev/null | head -1)
        if [ -n "$src" ]; then
            cp -L "$src" "$OUTPUT_DIR/libs/$lib"
            echo "  Collected: $lib"
            collect_deps "$OUTPUT_DIR/libs/$lib"
        else
            echo "ERROR: $lib not found; the engine closure would be incomplete" >&2
            exit 1
        fi
    done
}

echo "Collecting transitive dependencies..."
collect_deps "$OUTPUT_DIR/moonlight"
for lib in "$OUTPUT_DIR"/libs/*.so*; do
    collect_deps "$lib"
done

# Keep one file per SONAME. The build tree contains linker symlinks and
# fully-versioned files; the pak only needs the runtime names the loader looks
# up (libavcodec.so.58, libgamestream.so.4, ...).
for so in "$OUTPUT_DIR"/libs/*.so*; do
    [ -f "$so" ] || continue
    base=$(basename "$so")
    soname=$(${CROSS}-readelf -d "$so" 2>/dev/null |
             sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
    if [ -n "$soname" ] && [ "$base" != "$soname" ]; then
        rm -f "$so"
    elif [ -z "$soname" ]; then
        case "$base" in
            *.so) rm -f "$so" ;;
        esac
    fi
done

# Strip all collected libs
for so in "$OUTPUT_DIR"/libs/*.so*; do
    ${STRIP} -s "$so" 2>/dev/null || true
done

echo "=== ccache stats ==="
ccache --show-stats

echo "=== Build complete ==="
ls -la "$OUTPUT_DIR/"
ls -la "$OUTPUT_DIR/libs/"
