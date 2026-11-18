# Cross-compilation toolchain for NextUI on tg5040 (TrimUI Brick Pro,
# Allwinner A133P, aarch64, glibc 2.33).
#
# The supported environment is the NextUI toolchain container
# (ghcr.io/loveretro/tg5040-toolchain), which exports CROSS_TRIPLE/SYSROOT and
# ships the vendor sysroot (SDL2 2.26.1, ALSA, OpenSSL, zlib).
#
# Usage (inside the container):
#   cmake -B build-nextui -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-tg5040.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Override for a different image layout:
#   -DTG5040_CROSS_ROOT=/opt/aarch64-nextui-linux-gnu -DTG5040_SYSROOT=...

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TG5040_CROSS_TRIPLE "aarch64-nextui-linux-gnu" CACHE STRING "Target triple")
if(DEFINED ENV{CROSS_TRIPLE})
    set(TG5040_CROSS_TRIPLE "$ENV{CROSS_TRIPLE}" CACHE STRING "Target triple" FORCE)
endif()

set(TG5040_CROSS_ROOT "/opt/aarch64-nextui-linux-gnu" CACHE PATH "Cross toolchain root")
if(DEFINED ENV{CROSS_ROOT})
    set(TG5040_CROSS_ROOT "$ENV{CROSS_ROOT}" CACHE PATH "Cross toolchain root" FORCE)
endif()

set(TG5040_SYSROOT "${TG5040_CROSS_ROOT}/${TG5040_CROSS_TRIPLE}/libc" CACHE PATH "Vendor sysroot")
if(DEFINED ENV{SYSROOT})
    set(TG5040_SYSROOT "$ENV{SYSROOT}" CACHE PATH "Vendor sysroot" FORCE)
endif()

set(CMAKE_C_COMPILER "${TG5040_CROSS_ROOT}/bin/${TG5040_CROSS_TRIPLE}-gcc")
set(CMAKE_AR "${TG5040_CROSS_ROOT}/bin/${TG5040_CROSS_TRIPLE}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${TG5040_CROSS_ROOT}/bin/${TG5040_CROSS_TRIPLE}-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${TG5040_CROSS_ROOT}/bin/${TG5040_CROSS_TRIPLE}-strip" CACHE FILEPATH "")

set(CMAKE_SYSROOT "${TG5040_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${TG5040_SYSROOT}/usr" "${TG5040_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A133P: four Cortex-A53 cores. Keep one conservative baseline for the whole
# wrapper and the vendored overlay renderer.
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a53 -mtune=cortex-a53 -I${TG5040_CROSS_ROOT}/include")

# pkg-config must look inside the sysroot; the image sets these too, but doing
# it here keeps the file usable from a plain shell.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${TG5040_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${TG5040_SYSROOT}/usr/lib/pkgconfig:${TG5040_SYSROOT}/usr/share/pkgconfig")
