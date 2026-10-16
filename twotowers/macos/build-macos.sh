#!/bin/sh
# Build The Two Towers Recompiled for macOS (x86-64: Intel Macs, and Apple
# silicon through Rosetta 2), from the generated sources (run generate.py first).
#
#   brew install cmake ninja sdl2          (an x86-64 Homebrew on Apple silicon:
#                                           arch -x86_64 /usr/local/bin/brew ...)
#   sh twotowers/macos/build-macos.sh [out-folder]
#
# The folder it makes holds twotowers_recomp, twotowers-setup, TwoTowers.command
# and lib/libSDL2-2.0.0.dylib (copied from Homebrew when found).
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$here/build-macos/TwoTowers-Recompiled}
sdl=$( (arch -x86_64 /usr/local/bin/brew --prefix sdl2 || brew --prefix sdl2) 2>/dev/null || true)
arch -x86_64 cmake -S "$here/twotowers" -B "$here/build-macos/b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 ${sdl:+-DSDL2_DIR=$sdl/lib/cmake/SDL2}
arch -x86_64 ninja -C "$here/build-macos/b"
mkdir -p "$out/lib"
cp "$here/build-macos/b/twotowers_recomp" "$here/build-macos/b/twotowers-setup" "$out/"
cp "$here/twotowers/macos/TwoTowers.command" "$out/"
chmod +x "$out/TwoTowers.command"
[ -n "$sdl" ] && [ -e "$sdl/lib/libSDL2-2.0.0.dylib" ] && cp "$sdl/lib/libSDL2-2.0.0.dylib" "$out/lib/"
echo "Built: $out"
