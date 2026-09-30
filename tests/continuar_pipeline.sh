#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-O1 -g -ffunction-sections -fdata-sections -Wno-deprecated-declarations -Wno-macro-redefined -Isrc -I/opt/homebrew/include -I/opt/homebrew/include/SDL2)
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" tests/continuar_pipeline.c src/catalogo.c src/progresso.c src/continuar_motor.c src/cwordem.c src/js.c src/jsw.c src/colecoes.c src/catordem.c -Wl,-dead_strip -o /tmp/bn-continuar-pipeline
/tmp/bn-continuar-pipeline
