#!/usr/bin/env bash
# Builds the Windows release zip from Linux with MinGW-w64 (no Windows machine needed):
#
#   sudo apt install cmake ninja-build g++-mingw-w64-x86-64-posix libz-mingw-w64-dev zip
#   tools/build-windows-zip.sh            # -> dist/CanvasEditor-<version>-windows-x64.zip
#
# The executables are linked statically, so the zip runs on a clean Windows 10/11 install.
# With wine installed (apt install wine64) the unit tests also run on the Windows build.
# Set BUILD_DIR / DIST_DIR to change where the build and the zip go.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-windows}"
dist="${DIST_DIR:-$root/dist}"

version="$(sed -n 's/^project(CanvasEditor VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
[ -n "$version" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
name="CanvasEditor-$version-windows-x64"

generator=()
command -v ninja > /dev/null && generator=(-G Ninja)
cmake -S "$root" -B "$build" "${generator[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-w64-x86_64.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel

# run the tests on the Windows build when wine is available
wine=""
for candidate in wine64 wine /usr/lib/wine/wine64; do
	if command -v "$candidate" > /dev/null; then wine="$candidate"; break; fi
done
if [ -n "$wine" ]; then
	echo "running canvas_tests.exe with $wine"
	WINEDEBUG=-all "$wine" "$build/canvas_tests.exe"
else
	echo "wine not found: skipping the Windows unit tests"
fi

stage="$dist/$name"
rm -rf "$stage" "$dist/$name.zip"
mkdir -p "$stage"
for exe in CanvasEditor.exe canvas-cli.exe; do
	[ -f "$build/$exe" ] || { echo "$exe was not built" >&2; exit 1; }
	cp "$build/$exe" "$stage/"
done
x86_64-w64-mingw32-strip "$stage"/*.exe 2> /dev/null || true
# from the sources, so files left in the build folder by older versions never ship
cp -r "$root/MikuMikuWorld/res" "$stage/res"
cp -r "$root/Licenses" "$stage/Licenses"
cp "$root/LICENSE" "$root/README.md" "$stage/"

(cd "$dist" && zip -qr "$name.zip" "$name")
echo "wrote $dist/$name.zip"
