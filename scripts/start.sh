#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# On macOS Homebrew may install Boost outside CMake's default search paths.
if command -v brew >/dev/null 2>&1; then
  BOOST_PREFIX="$(brew --prefix boost 2>/dev/null || true)"
  if [[ -n "$BOOST_PREFIX" && -d "$BOOST_PREFIX" ]]; then
    export CMAKE_PREFIX_PATH="$BOOST_PREFIX${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
  fi
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SUPPRESS_REGENERATION=ON
cmake --build build -j 2
exec ./build/event-horizon-server \
  --host "${EH_HOST:-127.0.0.1}" \
  --port "${EH_PORT:-8080}" \
  --workers "${EH_WORKERS:-4}" \
  --data-dir data --web-dir web
