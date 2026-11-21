# Pinned copy of the tg5040 cmake toolchain as installed by the NextUI
# toolchain container (LoveRetro/tg5040-toolchain, COPY toolchain-aarch64.cmake
# ${CROSS_ROOT}/Toolchain.cmake; sha256 954091be9ae9859c8c02ebb6f26c33055c60f91f4eb6400cfa7fe64f6c1dea7b).
# engine.yml copies this file over the image's copy so a rebuild does not
# silently pick up a changed toolchain file.
#
# Expects the image environment: CROSS_TRIPLE, CROSS_ROOT, SYSROOT, CC, CXX.
# CMAKE_FIND_ROOT_PATH starts with CMAKE_PREFIX_PATH, so the from-source
# OpenSSL/curl/FFmpeg win over any sysroot copy of the same library.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(cross_triple $ENV{CROSS_TRIPLE})
set(cross_root $ENV{CROSS_ROOT})
set(sys_root $ENV{SYSROOT})

set(CMAKE_C_COMPILER $ENV{CC})
set(CMAKE_CXX_COMPILER $ENV{CXX})

set(CMAKE_CXX_FLAGS "-I ${cross_root}/include/")

list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} ${cross_root} ${cross_root}/${cross_triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_SYSROOT ${sys_root})

#set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/qemu-aarch64)