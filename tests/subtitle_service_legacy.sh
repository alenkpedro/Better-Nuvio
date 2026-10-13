#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture=$(mktemp -d tools/service-build/node_modules/.bn-fixtures.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
cat > "$fixture/sub.srt" <<'SRT'
1
00:00:02,100 --> 00:00:04,700
FIRST

2
00:00:06,000 --> 00:00:08,000
SECOND

SRT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=320x180:rate=24' -i "$fixture/sub.srt" -t 12 -c:v mpeg4 -q:v 5 -c:s srt "$fixture/srt.mkv"
ffmpeg -hide_banner -loglevel error -y -i "$fixture/srt.mkv" -c:v copy -c:s ass "$fixture/ass.mkv"
maps=(-map 0:v)
for i in {1..43};do maps+=(-map 0:s:0);done
ffmpeg -hide_banner -loglevel error -y -i "$fixture/srt.mkv" "${maps[@]}" -c copy "$fixture/many.mkv"
# Stage exactly as arm.sh does, then load the service through package.json.
cp -R "${NUVIO_SERVICE_PACKAGE:-plugin-service}" "$fixture/service"
if [ -z "${NUVIO_SERVICE_PACKAGE:-}" ];then
  node tools/service-build.cjs "$fixture/service/runtime/service.cjs"
fi
if [ -n "${NUVIO_SERVICE_NODE_IMAGE:-}" ];then
  docker run --rm --platform linux/amd64 -v "$PWD:/work:ro" -w /work "$NUVIO_SERVICE_NODE_IMAGE" \
    "${NUVIO_SERVICE_NODE:-node}" tests/subtitle_service_legacy.cjs "$fixture" "$fixture/service"
else
  "${NUVIO_SERVICE_NODE:-node}" tests/subtitle_service_legacy.cjs "$fixture" "$fixture/service"
fi
