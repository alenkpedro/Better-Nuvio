#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture=$(mktemp -d /tmp/bn-watch-playback.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'color=size=160x90:rate=12' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 120 -c:v mpeg4 -q:v 8 -c:a aac "$fixture/watch.mkv"
sources=()
for source in src/*.c;do [ "$source" != src/main.c ] && sources+=("$source");done
cc "${sources[@]}" tests/watch_playback.c -DNV_MAC_VIDEO -DNV_ASS_LIBASS -Isrc -o /tmp/bn-watch-playback -O1 -g \
 $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample libass) \
 -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib -lSDL2 -lSDL2_image -lSDL2_ttf -lz -framework OpenGL -Wno-deprecated-declarations -Wno-macro-redefined
mkdir "$fixture/data"
SDL_AUDIODRIVER=dummy NUVIO_DADOS="$fixture/data" /tmp/bn-watch-playback "$fixture/watch.mkv"
