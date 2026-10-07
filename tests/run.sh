#!/usr/bin/env bash
# Build and run the engine's unit tests (no JUCE needed).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
# Some Command Line Tools installs leave libc++'s headers off clang's search path.
extra=()
if ! echo '#include <algorithm>' | clang++ -x c++ -fsyntax-only - 2>/dev/null; then
  extra=(-isystem "$(xcrun --show-sdk-path)/usr/include/c++/v1")
fi
clang++ "${extra[@]}" -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -o build/tests test_*.cpp ../engine/*.cpp
./build/tests
