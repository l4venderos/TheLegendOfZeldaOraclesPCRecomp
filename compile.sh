#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/source"
GAME_DATA="$ROOT/GameData"
DIST="$ROOT/dist"
TOOLS_BUILD="$ROOT/build-tools-linux"
GAME_BUILD="$ROOT/build-game-linux"
GENERATED="$ROOT/build_generated"
APPDIR="$ROOT/Oracles.AppDir"

CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
CXX="${CXX:-c++}"
LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy}"
APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"

command -v "$CMAKE" >/dev/null 2>&1 || { echo "[ERROR] CMake was not found." >&2; exit 1; }
command -v "$CXX" >/dev/null 2>&1 || { echo "[ERROR] A C++20 compiler was not found." >&2; exit 1; }
command -v "$CTEST" >/dev/null 2>&1 || { echo "[ERROR] CTest was not found." >&2; exit 1; }

if [[ ! -f "$GAME_DATA/OracleOfAges.gbc" || ! -f "$GAME_DATA/OracleOfSeasons.gbc" ]]; then
  echo "[ERROR] Put your own OracleOfAges.gbc and OracleOfSeasons.gbc files in GameData/." >&2
  exit 1
fi
if ! command -v "$LINUXDEPLOY" >/dev/null 2>&1; then
  echo "[ERROR] linuxdeploy not found. Download the official x86_64 AppImage and set LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage." >&2
  echo "       https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" >&2
  exit 1
fi
if ! command -v "$APPIMAGETOOL" >/dev/null 2>&1; then
  echo "[ERROR] appimagetool not found. Download the official x86_64 AppImage and set APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage." >&2
  echo "       https://github.com/AppImage/appimagetool/releases/continuous/appimagetool-x86_64.AppImage" >&2
  exit 1
fi

mkdir -p "$DIST" "$GENERATED" "$DIST/GameData"
echo "[1/6] Configure portable compiler and tests..."
"$CMAKE" -S "$SRC" -B "$TOOLS_BUILD" -DCMAKE_BUILD_TYPE=Release

echo "[2/6] Build compiler and regression tests..."
"$CMAKE" --build "$TOOLS_BUILD" --target oracles-recompiler oracles-test-binaries --parallel
"$CTEST" --test-dir "$TOOLS_BUILD" --output-on-failure

echo "[3/6] Generate native code from the supplied cartridges..."
"$TOOLS_BUILD/oracles-recompiler" --game ages --input "$GAME_DATA/OracleOfAges.gbc" --output "$GENERATED/ages_native.cpp" --manifest "$DIST/GameData/OracleOfAges.orcmeta"
"$TOOLS_BUILD/oracles-recompiler" --game seasons --input "$GAME_DATA/OracleOfSeasons.gbc" --output "$GENERATED/seasons_native.cpp" --manifest "$DIST/GameData/OracleOfSeasons.orcmeta"

echo "[4/6] Build the portable Oracles application..."
"$CMAKE" -S "$SRC" -B "$GAME_BUILD" -DCMAKE_BUILD_TYPE=Release -DORACLES_BUILD_GAME=ON -DAGES_NATIVE="$GENERATED/ages_native.cpp" -DSEASONS_NATIVE="$GENERATED/seasons_native.cpp"
"$CMAKE" --build "$GAME_BUILD" --target Oracles --parallel
test -x "$DIST/Oracles"

echo "[5/6] Prepare AppDir (commercial ROMs are not bundled)..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/doc/oracles-pc-recomp"
cp "$DIST/Oracles" "$APPDIR/usr/bin/Oracles"
cp "$ROOT/README.md" "$APPDIR/usr/share/doc/oracles-pc-recomp/README.md"
cp "$ROOT/LICENSE" "$APPDIR/usr/share/doc/oracles-pc-recomp/LICENSE"

echo "[6/6] Bundle dependencies and generate the AppImage..."
ARCH=x86_64 "$LINUXDEPLOY" --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/Oracles" \
  --desktop-file "$ROOT/packaging/oracles.desktop" \
  --icon-file "$ROOT/packaging/oracles.svg"
ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$DIST/Oracles-PC-Recomp-v0.2.4-x86_64.AppImage"
chmod +x "$DIST/Oracles-PC-Recomp-v0.2.4-x86_64.AppImage"

echo
echo "Build complete:"
echo "  Linux executable: $DIST/Oracles"
echo "  AppImage:         $DIST/Oracles-PC-Recomp-v0.2.4-x86_64.AppImage"
echo "  Runtime ROMs:     $GAME_DATA/OracleOfAges.gbc and OracleOfSeasons.gbc"
echo "Run the AppImage from a directory containing GameData/, or pass --data-dir."
