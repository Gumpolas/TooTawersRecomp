#!/bin/sh
# Build the Linux AppImage from the Linux package.
#
#   make_appimage.sh <linux package folder> <libSDL2-2.0.so.0> <AppImage type 2 runtime> <output .AppImage>
#
# The runtime is the AppImage project's (github.com/AppImage/type2-runtime,
# runtime-x86_64). The image is made by pysquashfs.py (Python only), so no
# mksquashfs or appimagetool is needed.
set -e
PKG=$1 SDL=$2 RT=$3 OUT=$4
[ -d "$PKG" ] && [ -f "$SDL" ] && [ -f "$RT" ] && [ -n "$OUT" ] || { sed -n '2,9p' "$0"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
APPDIR=$(mktemp -d)/TwoTowers.AppDir
mkdir -p "$APPDIR/usr/share/twotowers" "$APPDIR/usr/lib"
cp -Rp "$PKG/." "$APPDIR/usr/share/twotowers/"
cp "$SDL" "$APPDIR/usr/lib/libSDL2-2.0.so.0"
cp "$HERE/AppRun" "$APPDIR/AppRun"
cp "$HERE/twotowers-recompiled.desktop" "$APPDIR/"
cp "$PKG/twotowers.png" "$APPDIR/twotowers.png"
ln -s twotowers.png "$APPDIR/.DirIcon"
# (1.9.9) every file readable by everyone, the programs runnable: files copied from a Windows disk came as 0700 and,
# owned by root inside the image, could not be read by the player -- AppRun's copy failed and the game never started
# (the AppImage catalog's test: "cp: cannot open ... Permission denied")
find "$APPDIR" -type d -exec chmod 755 {} +
find "$APPDIR" -type f -exec chmod 644 {} +
for x in AppRun usr/lib/libSDL2-2.0.so.0 usr/share/twotowers/twotowers_recomp usr/share/twotowers/twotowers-setup usr/share/twotowers/TwoTowers.sh; do
    [ -f "$APPDIR/$x" ] && chmod 755 "$APPDIR/$x"
done
python3 "$HERE/pysquashfs.py" "$APPDIR" "$OUT.sqfs"
cat "$RT" "$OUT.sqfs" > "$OUT"
rm -f "$OUT.sqfs"
chmod 755 "$OUT"
rm -rf "$(dirname "$APPDIR")"
ls -la "$OUT"
