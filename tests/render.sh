#!/usr/bin/env bash
# Render the example patches in tests/render.cpp to renders/*.wav (optimised build).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/build
extra=()
if ! echo '#include <algorithm>' | clang++ -x c++ -fsyntax-only - 2>/dev/null; then
  extra=(-isystem "$(xcrun --show-sdk-path)/usr/include/c++/v1")
fi
clang++ "${extra[@]}" -std=c++17 -O3 -Wall -Wextra -o tests/build/render tests/render.cpp engine/*.cpp
tests/build/render
