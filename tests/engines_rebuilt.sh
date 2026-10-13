#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-O1 -g -Wall -Wextra -Isrc)
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" src/progresso.c src/syncprog.c src/continuar_motor.c src/legenda.c src/assrender.c src/media_clock.c src/js.c src/jsw.c tests/engines_rebuilt.c -o /tmp/bn-engines-rebuilt
/tmp/bn-engines-rebuilt
