#!/usr/bin/env bash
# Builds dist/Catacomb-<version>.pkg: a universal (Apple Silicon + Intel) Audio Unit
# installer for testers.
#
#   packaging/build-installer.sh
#
# Signing (optional). Without these the installer is unsigned: testers approve it once in
# System Settings → Privacy & Security ("Open Anyway"). With an Apple Developer account:
#   CATACOMB_APP_SIGN="Developer ID Application: Your Name (TEAMID)"
#   CATACOMB_PKG_SIGN="Developer ID Installer: Your Name (TEAMID)"
#   CATACOMB_NOTARY_PROFILE=<profile saved with `xcrun notarytool store-credentials`>
# …and it signs the component, signs the package, notarizes and staples it.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$(pwd)"

export PATH="$(python3 -m site --user-base 2>/dev/null)/bin:$PATH"
command -v cmake >/dev/null || { echo "Need CMake: python3 -m pip install --user cmake ninja"; exit 1; }
if ! echo '#include <algorithm>' | clang++ -x c++ -fsyntax-only - 2>/dev/null; then
  export CXXFLAGS="-isystem $(xcrun --show-sdk-path)/usr/include/c++/v1 ${CXXFLAGS:-}"
fi
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
juce=()
[[ -z "${JUCE_DIR:-}" && -d "$HOME/Documents/MeatThumb Synth/JUCE" ]] && JUCE_DIR="$HOME/Documents/MeatThumb Synth/JUCE"
[[ -n "${JUCE_DIR:-}" ]] && juce=(-DJUCE_DIR="$JUCE_DIR")

version=$(sed -n 's/^project(Catacomb VERSION \([0-9.]*\).*/\1/p' plugin/CMakeLists.txt)
echo "== Catacomb $version"

# ---- Build: universal, macOS 11+ ---------------------------------------------------------
echo "== Building (universal)"
cmake ${generator[@]+"${generator[@]}"} -S plugin -B plugin/build-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 ${juce[@]+"${juce[@]}"} >/dev/null
cmake --build plugin/build-release --config Release --target Catacomb_AU
component="plugin/build-release/Catacomb_artefacts/Release/AU/Catacomb.component"
lipo -info "$component/Contents/MacOS/Catacomb"

# ---- Stage and sign the component --------------------------------------------------------
# Stage outside the project: if it lives in an iCloud-synced folder (e.g. Desktop), macOS tags
# files there with attributes we don't want in the package.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/payload" "$stage/scripts"
cp -R "$component" "$stage/payload/"
xattr -cr "$stage/payload/Catacomb.component"
if [[ -n "${CATACOMB_APP_SIGN:-}" ]]; then
  codesign --force --deep --options runtime --timestamp --sign "$CATACOMB_APP_SIGN" "$stage/payload/Catacomb.component"
else
  codesign --force --deep --sign - "$stage/payload/Catacomb.component"
fi
codesign --verify --deep --strict "$stage/payload/Catacomb.component"

# After installing: make macOS rescan Audio Units (Logic sees it after a relaunch).
cat > "$stage/scripts/postinstall" <<'POST'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
POST
chmod +x "$stage/scripts/postinstall"

# ---- Package -----------------------------------------------------------------------------
mkdir -p dist
COPYFILE_DISABLE=1 pkgbuild --root "$stage/payload" --install-location /Library/Audio/Plug-Ins/Components \
  --scripts "$stage/scripts" --identifier com.catacomb.synth.au --version "$version" \
  "$stage/Catacomb-AU.pkg" >/dev/null

sed "s/@VERSION@/$version/g" packaging/distribution.xml > "$stage/distribution.xml"
mkdir -p "$stage/resources"
for f in packaging/resources/*; do sed "s/@VERSION@/$version/g" "$f" > "$stage/resources/$(basename "$f")"; done

out="dist/Catacomb-$version.pkg"
sign=()
[[ -n "${CATACOMB_PKG_SIGN:-}" ]] && sign=(--sign "$CATACOMB_PKG_SIGN" --timestamp)
productbuild --distribution "$stage/distribution.xml" --resources "$stage/resources" \
  --package-path "$stage" ${sign[@]+"${sign[@]}"} "$out" >/dev/null

if [[ -n "${CATACOMB_PKG_SIGN:-}" && -n "${CATACOMB_NOTARY_PROFILE:-}" ]]; then
  echo "== Notarizing"
  xcrun notarytool submit "$out" --keychain-profile "$CATACOMB_NOTARY_PROFILE" --wait
  xcrun stapler staple "$out"
fi

echo "== Built $root/$out ($(du -h "$out" | cut -f1))"
[[ -z "${CATACOMB_PKG_SIGN:-}" ]] && echo "   (unsigned: testers approve it in System Settings → Privacy & Security → Open Anyway)"
