#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture=$(mktemp /tmp/bn-audio-playback.XXXXXX.mkv)
trap 'rm -f "$fixture"' EXIT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=320x180:rate=24' \
 -f lavfi -i 'sine=frequency=440:sample_rate=48000' -f lavfi -i 'sine=frequency=880:sample_rate=48000' \
 -map 0:v -map 1:a -map 2:a -t 12 -c:v mpeg4 -q:v 5 -c:a aac \
 -metadata:s:a:0 language=eng -metadata:s:a:0 title='English 440Hz' \
 -metadata:s:a:1 language=por -metadata:s:a:1 title='Português 880Hz' "$fixture"
cc -DNV_MAC_VIDEO -DSDL_QueueAudio=audioCapturar -O1 -g -Isrc tests/audio_playback.c src/video_mac.c -o /tmp/bn-audio-playback \
 $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample) \
 -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib -lSDL2 -framework OpenGL -Wno-macro-redefined -Wno-deprecated-declarations
SDL_AUDIODRIVER=dummy /tmp/bn-audio-playback "$fixture"
