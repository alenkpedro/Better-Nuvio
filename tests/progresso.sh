#!/bin/bash
# Compatibility entry point for the rebuilt repository and RPC contract.
set -euo pipefail
cd "$(dirname "$0")/.."
exec bash tests/engines_rebuilt.sh
