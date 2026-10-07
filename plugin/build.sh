#!/usr/bin/env bash
# Build the Catacomb AU. `./build.sh install` also installs it for
# Logic/GarageBand and validates it with auval.
set -euo pipefail
cd "$(dirname "$0")"

# cmake/ninja from `python3 -m pip install --user cmake ninja` live here.
export PATH="$(python3 -m site --user-base 2>/dev/null)/bin:$PATH"
command -v cmake >/dev/null || { echo "Need CMake: python3 -m pip install --user cmake ninja (or brew install cmake)"; exit 1; }

# Some Command Line Tools installs leave libc++'s headers off clang's search path.
if ! echo '#include <algorithm>' | clang++ -x c++ -fsyntax-only - 2>/dev/null; then
  export CXXFLAGS="-isystem $(xcrun --show-sdk-path)/usr/include/c++/v1 ${CXXFLAGS:-}"
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
# JUCE: $JUCE_DIR, else the checkout next to Meat Thumb, else fetched by CMake.
juce=()
[[ -z "${JUCE_DIR:-}" && -d "$HOME/Documents/MeatThumb Synth/JUCE" ]] && JUCE_DIR="$HOME/Documents/MeatThumb Synth/JUCE"
[[ -n "${JUCE_DIR:-}" ]] && juce=(-DJUCE_DIR="$JUCE_DIR")

cmake "${generator[@]}" -B build -DCMAKE_BUILD_TYPE=Release "${juce[@]}"
cmake --build build --config Release

component="build/Catacomb_artefacts/Release/AU/Catacomb.component"
echo "Built: $component"

if [[ "${1:-}" == "install" ]]; then
  dest="$HOME/Library/Audio/Plug-Ins/Components"
  mkdir -p "$dest"
  rm -rf "$dest/Catacomb.component"
  cp -R "$component" "$dest/"
  xattr -cr "$dest/Catacomb.component"
  codesign --force --deep --sign - "$dest/Catacomb.component"
  # Make macOS rescan Audio Units, then validate the way Logic does.
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  auval -v aumu Cat1 Ctcb
  echo "Installed to $dest — in Logic: Instrument slot → AU Instruments → Catacomb → Catacomb"
fi
