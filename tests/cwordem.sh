#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
cc -O1 -Isrc src/cwordem.c tests/cwordem.c -o /tmp/bn-cwordem-policy
/tmp/bn-cwordem-policy
bash tests/continuar_pipeline.sh
