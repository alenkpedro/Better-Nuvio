#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture=$(mktemp -d /tmp/bn-subtitle-playback.XXXXXX)
server=""
cleanup(){ if [ -n "$server" ];then kill "$server" 2>/dev/null || true;wait "$server" 2>/dev/null || true;fi;rm -rf "$fixture"; }
trap cleanup EXIT
cat > "$fixture/sub.srt" <<'SRT'
1
00:00:02,100 --> 00:00:04,700
FIRST

2
00:00:06,000 --> 00:00:08,000
SECOND

SRT
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=320x180:rate=24' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -i "$fixture/sub.srt" -t 12 -c:v mpeg4 -q:v 5 -c:a aac -c:s srt "$fixture/srt.mkv"
ffmpeg -hide_banner -loglevel error -y -i "$fixture/srt.mkv" -c:v copy -c:a copy -c:s ass "$fixture/ass.mkv"
NUVIO_PLUGIN_PORT=28442 NUVIO_PLUGIN_FETCH_PORT=28443 node tests/subtitle_http_fixture.cjs "$fixture" > "$fixture/service.log" 2>&1 &
server=$!
for i in {1..50};do if curl -fsS http://127.0.0.1:28442/health 2>/dev/null >/dev/null;then break;fi;sleep .1;done
cc -DNV_MAC_VIDEO -DNV_ASS_LIBASS -O1 -g -Isrc tests/subtitle_playback.c src/video_mac.c src/subtitle_engine.c src/legenda.c src/assrender.c src/js.c src/jsw.c src/rede.c src/redeurl.c -o /tmp/bn-subtitle-playback \
  $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample libass) \
  -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib -lSDL2 -framework OpenGL -lz -Wno-macro-redefined -Wno-deprecated-declarations
for format in srt ass;do
 SDL_AUDIODRIVER=dummy NUVIO_SUBTITLE_SERVICE_URL=http://127.0.0.1:28442/subtitles/window /tmp/bn-subtitle-playback "http://127.0.0.1:28444/$format.mkv" "$format"
done
