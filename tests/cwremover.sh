#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tests/engines_rebuilt.sh
bash tests/continuar_pipeline.sh
