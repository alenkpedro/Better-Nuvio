#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
cc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wl,-dead_strip -Isrc \
 -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -Wno-macro-redefined \
 tests/subtitle_catalog.c src/js.c src/linguas.c -o /tmp/bn-subtitle-catalog
/tmp/bn-subtitle-catalog
