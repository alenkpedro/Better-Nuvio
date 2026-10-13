#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture=$(mktemp -d /tmp/bn-seekr-strip.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=320x180:rate=1:duration=10' -vf 'tile=10x1' -frames:v 1 "$fixture/sheet.jpg"
sources=()
for source in src/*.c;do
  case "$source" in src/main.c|src/seekr.c) continue;; esac
  sources+=("$source")
done
cc "${sources[@]}" tests/seekr_strip_shot.c -Isrc -o /tmp/bn-seekr-strip-shot -O1 -g \
  -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib \
  -lSDL2 -lSDL2_image -lSDL2_ttf -lz -framework OpenGL -Wno-deprecated-declarations -Wno-macro-redefined
NUVIO_DADOS="$fixture" /tmp/bn-seekr-strip-shot "$fixture/sheet.jpg" "${1:-/tmp/bn-seekr-strip}"
