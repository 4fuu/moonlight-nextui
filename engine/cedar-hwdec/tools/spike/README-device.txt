tsp-videotest: Cedar hardware H.264 decode spike for tg5040 (Brick Pro / A133P)
=================================================================================

Ported from isalin/moonlight-tsp (GPL-3.0-or-later), tools/tsp-videotest.c.
The tool only dlopen()s the firmware's own libraries (/usr/lib/libvdecoder.so,
libvideoengine.so, libawh264.so, ...); it needs no install and writes nothing
outside /tmp. It never touches /dev/disp unless you pass -d.

Copy this whole folder to the device, e.g.:

    # on the host
    scp -r . root@<device>:/tmp/tsp-spike/
    # on the device
    cd /tmp/tsp-spike

Run (decode only, no display, 20 s of 720p60 H.264):

    ./tsp-videotest -r 60 -t 20 test720p60.264

What a good run prints, and what to send back
----------------------------------------------
1. "plugins: h264=registered h265=registered|missing ..."
   -> does THIS firmware have libawh265.so (HEVC)? This answers the open
      question from the device library scan.
2. "decoder: open in N ms (...)"
   -> VE init works and how long InitializeVideoDecoder takes.
3. the first "picture:" line (printed for picture 1)
   -> the decoder's actual output: fmt= (5 = NV21, 6 = NV12), width/height,
      stride, crop offsets, phyY/phyC, bufFd, full_range/matrix.
4. "summary: wall=... submitted=... pictures=... fps=... errors=..."
   -> pictures == submitted, errors 0 means the ABI is right on this device.
5. "cpu: ... = X% of one core; rss ..."
   -> CPU cost of decode alone.
6. "submit->picture: n=... mean=... p50=... p95=... p99=... max=... ms"
   -> per-frame decode latency (tsp's Smart Pro figure: 3.3 ms at 720p).
7. "decode call: ..." and "submit->decode call: ..."
   -> hardware decode time vs. parser wake-up time.
8. "semwait: interposed calls=... blocked=..."
   -> our blocking SemTimedWait replaced the vendor polling one. "vendor" or
      "NOT interposed" changes how decode latency should be read.

HEVC: the same command with test720p60.265 works only if line 1 says
h265=registered.

Display sanity check (optional; takes over the screen for a few seconds).
This uses tsp's disp2 layer, NOT the Moonlight SDL path; it proves the
decoded pixels reach the panel on this firmware:

    ./tsp-videotest -d -t 5 test720p60.264
    ./tsp-videotest -X -F -C 0 -L 0      # always run after -d

After both, check for leftovers:

    cat /sys/kernel/debug/ion/heaps/sys_user 2>/dev/null | head
    grep -i dmabuf /sys/class/disp/disp/attr/sys 2>/dev/null

A CPU reference: the same clip decoded in software with the bundled engine's
FFmpeg is not measured here; the coordinator compares against the stream
stats of the Moonlight pak instead.

Exit codes: 0 success, 1 setup/decoder failure, 3 watchdog (decode wedged,
or a second signal).
