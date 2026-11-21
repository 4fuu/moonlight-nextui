#!/bin/sh
# Generates no-B-frame Annex-B test streams with access unit delimiters
# (one AUD per frame), similar to what Sunshine sends to the device.
#
#   engine/cedar-hwdec/scripts/make-test-streams.sh [SECONDS] [SIZE]
#
# Ported from isalin/moonlight-tsp scripts/make-test-streams.sh; the output
# goes to media/ next to the tool and H.265 is skipped when ffmpeg lacks
# libx265.
set -eu
here=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
out="$here/media"
secs=${1:-20}
size=${2:-1280x720}
mkdir -p "$out"
# The top-left 64x64 block is flat luma 16 + 8 * (frame % 27), so a tool
# can read the frame number back from a decoded picture (tsp-videotest -M).
src="testsrc=size=$size:rate=60,format=yuv420p[bg];\
color=c=black:size=64x64:rate=60,format=yuv420p,\
geq=lum='16+8*mod(N\,27)':cb=128:cr=128[mark];\
[bg][mark]overlay=0:0:shortest=1,format=yuv420p"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$src" -t "$secs" \
    -c:v libx264 -preset veryfast -tune zerolatency -profile:v high \
    -bf 0 -g 600 -maxrate 10M -bufsize 2M \
    -color_primaries smpte170m -color_trc smpte170m -colorspace smpte170m \
    -bsf:v h264_metadata=aud=insert -f h264 "$out/test720p60.264"
if ffmpeg -hide_banner -encoders 2>/dev/null | grep -q ' libx265'; then
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$src" -t "$secs" \
        -c:v libx265 -preset veryfast -tune zerolatency \
        -x265-params "bframes=0:keyint=600:log-level=error" -crf 20 \
        -color_primaries smpte170m -color_trc smpte170m -colorspace smpte170m \
        -bsf:v hevc_metadata=aud=insert -f hevc "$out/test720p60.265"
fi
ls -l "$out"
