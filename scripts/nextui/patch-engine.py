#!/usr/bin/env python3
"""Apply the moonlight-common compatibility patch to the packaged engine.

moonlight-common-c sends an ENABLE_HAPTICS input packet (magic 0x0000000D,
sent once when the input stream starts) to any host that reports app
version >= 7.1.0. Vibepollo 2.18.19 does not recognise that magic in its
input validator and logs:

    Ignoring unknown input packet magic 0x0000000D

The packet is also the legacy (pre-Gen5) multi-controller magic, which makes
the warning easy to misread. Disable the haptics notification for this build:
the handheld has no haptics, and the packet has no other purpose.

The engine is a prebuilt GPL binary; the source at moonlight-embedded 274d3db
(third_party/moonlight-common-c 8af4562) has sendEnableHaptics() inlined into
startInputStream(). At file offset 0x1b5f0 the function allocates and fills
the NV_HAPTICS_PACKET. Replacing the first instruction with an unconditional
branch to the function's return path skips that block:

    startInputStream+0x110:  b  1b598  (startInputStream+0xb8, function return)

Run on the copy in the pak, never on engine/bin/libs, so the vendored binary
stays pristine and SHA256SUMS keeps matching upstream.

Usage: patch-engine.py <libmoonlight-common.so.4> [--check]
"""

import hashlib
import sys

OFFSET = 0x1B5F0
ORIGINAL = bytes.fromhex("b60100d0e1c30091c0423291 11b3ff97".replace(" ", ""))
# b 0x1b598 from 0x1b5f0
PATCHED = bytes.fromhex("eaffff17") + ORIGINAL[4:]

EXPECTED_SHA256 = "daffca86d7e7a1c8ee45a2f73cd782b37e49c511b9c9230cb33aa888efed831c"


def main():
    args = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    if not args:
        print("usage: patch-engine.py [--check] <libmoonlight-common.so.4>",
              file=sys.stderr)
        return 2

    path = args[0]
    check_only = "--check" in sys.argv[1:]

    with open(path, "rb") as handle:
        data = bytearray(handle.read())

    if len(data) < OFFSET + len(ORIGINAL):
        print("patch-engine: %s is too small" % path, file=sys.stderr)
        return 2

    window = bytes(data[OFFSET:OFFSET + len(ORIGINAL)])
    if window == PATCHED:
        print("patch-engine: already patched (%s)" % path)
        return 0
    if window != ORIGINAL:
        print("patch-engine: unexpected bytes at 0x%x in %s" % (OFFSET, path),
              file=sys.stderr)
        print("  found:   %s" % window.hex(), file=sys.stderr)
        print("  expected %s (sha256 %s)" % (ORIGINAL.hex(), EXPECTED_SHA256),
              file=sys.stderr)
        return 1

    if check_only:
        print("patch-engine: FAIL: not patched (%s)" % path, file=sys.stderr)
        return 1

    data[OFFSET:OFFSET + len(PATCHED)] = PATCHED
    with open(path, "wb") as handle:
        handle.write(data)

    digest = hashlib.sha256(data).hexdigest()
    print("patch-engine: skipped the haptics-enable packet in %s" % path)
    print("patch-engine: patched sha256 %s" % digest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
