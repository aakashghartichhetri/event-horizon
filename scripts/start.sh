#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
exec ./build/event-horizon-server \
  --host "${EH_HOST:-127.0.0.1}" \
  --port "${EH_PORT:-8080}" \
  --workers "${EH_WORKERS:-4}" \
  --data-dir data --web-dir web
