#!/usr/bin/env bash
# Host-level checks against the installed AU (run plugin/build.sh install first).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
extra=()
if ! echo '#include <algorithm>' | clang++ -x c++ -fsyntax-only - 2>/dev/null; then
  extra=(-isystem "$(xcrun --show-sdk-path)/usr/include/c++/v1")
fi
clang++ "${extra[@]}" -std=c++17 -O2 -Wall -o build/auhost auhost.cpp -framework AudioToolbox -framework CoreFoundation
./build/auhost
