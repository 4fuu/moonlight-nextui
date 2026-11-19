#!/usr/bin/env python3
"""Check that a Moonlight.pak ships a complete engine dependency closure.

The engine is copied into the pak unmodified, so every library it loads at
runtime must be either inside pak/bin/libs or provided by the device system.
This check walks NEEDED from bin/moonlight and every bundled library and fails
on anything unresolved.

Device libraries are an explicit contract (BASE_ALLOW), not "whatever the
build sysroot happens to contain": the toolchain sysroot includes build-only
packages, and using it as a provider would mask exactly the regression this
check exists for (libpulsecommon was present in a build environment but absent
from the pak). When --sysroot is given, the allowlisted sonames that are
actually referenced are also confirmed to exist there.

Usage:
  check-libs.py <pak-dir> [--sysroot DIR] [--readelf CMD]

Exit status: 0 when the closure is complete, 1 when anything is unresolved,
2 for a usage/IO error.
"""

import argparse
import os
import shutil
import subprocess
import sys

# Sonames the device system provides. Keep this list small and deliberate.
BASE_ALLOW = {
    # glibc / toolchain runtime
    "ld-linux-aarch64.so.1",
    "libc.so.6",
    "libm.so.6",
    "libdl.so.2",
    "libpthread.so.0",
    "librt.so.1",
    "libresolv.so.2",
    "libutil.so.1",
    "libanl.so.1",
    "libcrypt.so.1",
    "libgcc_s.so.1",
    "libstdc++.so.6",
    # NextUI system libraries used by the engine and the wrapper
    "libz.so.1",
    "libudev.so.1",
    "libasound.so.2",
    "libSDL2-2.0.so.0",
    "libSDL2_image-2.0.so.0",
    "libSDL2_ttf-2.0.so.0",
    "libdrm.so.2",
    "libEGL.so.1",
    "libGLESv2.so.2",
    "libwayland-client.so.0",
    "libwayland-server.so.0",
    "libwayland-egl.so.1",
    "libMali.so.1",
}


def read_needed(readelf, path):
    try:
        out = subprocess.run(
            [readelf, "-d", path],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
    except OSError as error:
        raise SystemExit("check-libs: cannot run %s: %s" % (readelf, error))

    needed = []
    for line in out.splitlines():
        if "(NEEDED)" in line and "[" in line and "]" in line:
            needed.append(line.split("[", 1)[1].split("]", 1)[0])
    return needed


def is_elf(path):
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def sysroot_sonames(root):
    names = set()
    for base in ("lib", "usr/lib", "lib64", "usr/lib64"):
        start = os.path.join(root, base)
        if not os.path.isdir(start):
            continue
        for dirpath, _dirnames, filenames in os.walk(start):
            for name in filenames:
                if ".so" in name:
                    names.add(name)
    return names


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("pak", help="extracted pak directory (contains bin/)")
    parser.add_argument("--sysroot", default=None,
                        help="aarch64 device/toolchain sysroot for a sanity check")
    parser.add_argument("--readelf", default=None,
                        help="readelf command (default: $READELF or readelf)")
    args = parser.parse_args()

    pak = os.path.abspath(args.pak)
    bindir = os.path.join(pak, "bin")
    libdir = os.path.join(bindir, "libs")
    binary = os.path.join(bindir, "moonlight")

    if not os.path.isfile(binary):
        raise SystemExit("check-libs: missing %s" % binary)
    if not os.path.isdir(libdir):
        raise SystemExit("check-libs: missing %s" % libdir)

    readelf = args.readelf or os.environ.get("READELF") or "readelf"
    if not shutil.which(readelf):
        raise SystemExit("check-libs: readelf not found (%s)" % readelf)

    bundled = {}
    for name in sorted(os.listdir(libdir)):
        path = os.path.join(libdir, name)
        if not os.path.isfile(path):
            continue
        if not is_elf(path):
            raise SystemExit("check-libs: %s is not an ELF file" % path)
        bundled[name] = path

    targets = [("bin/moonlight", binary)]
    targets += [("bin/libs/" + name, bundled[name]) for name in bundled]

    unresolved = {}
    referenced = set()
    for label, path in targets:
        for needed in read_needed(readelf, path):
            referenced.add(needed)
            if needed in bundled or needed in BASE_ALLOW:
                continue
            unresolved.setdefault(needed, []).append(label)

    print("check-libs: %d bundled libraries, %d references"
          % (len(bundled), len(referenced)))

    if args.sysroot:
        providers = sysroot_sonames(os.path.abspath(args.sysroot))
        missing = sorted((referenced & BASE_ALLOW) - providers)
        if missing:
            print("check-libs: warning: device libs not found in sysroot: %s"
                  % ", ".join(missing))

    if unresolved:
        print("check-libs: FAIL: unresolved dependencies:", file=sys.stderr)
        for needed in sorted(unresolved):
            print("  %s (needed by %s)"
                  % (needed, ", ".join(unresolved[needed])), file=sys.stderr)
        print("check-libs: add the missing sonames to engine/bin/libs "
              "(complete closure) or to BASE_ALLOW if the device provides them",
              file=sys.stderr)
        return 1

    print("check-libs: ok: every NEEDED is bundled or a device library")
    return 0


if __name__ == "__main__":
    sys.exit(main())
