#!/bin/bash
# Makes the application icons from jara.svg: jara.icns for macOS, jara.ico for Windows and PNGs (png/) for the window
# icon elsewhere. The macOS icon keeps the margin and shadow of the macOS icon grid, the others are cropped to the tile.
# Needs rsvg-convert and ImageMagick (magick), and iconutil for the .icns (macOS only).
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Full-bleed variant: no drop shadow, view box on the tile
sed -e '/Drop shadow/,/filter="url(#shadow)"\/>/d' \
    -e 's/viewBox="0 0 1024 1024"/viewBox="100 100 824 824"/' jara.svg > "$tmp/tile.svg"

mkdir -p png
for size in 16 24 32 48 64 128 256 512 1024; do
    rsvg-convert -w $size -h $size "$tmp/tile.svg" -o png/jara_$size.png
done
magick png/jara_{16,24,32,48,64,128,256}.png jara.ico

if command -v iconutil > /dev/null; then
    mkdir "$tmp/jara.iconset"
    for size in 16 32 128 256 512; do
        rsvg-convert -w $size -h $size jara.svg -o "$tmp/jara.iconset/icon_${size}x${size}.png"
        rsvg-convert -w $((2 * size)) -h $((2 * size)) jara.svg -o "$tmp/jara.iconset/icon_${size}x${size}@2x.png"
    done
    iconutil -c icns "$tmp/jara.iconset" -o jara.icns
else
    echo "iconutil not found, jara.icns was not updated"
fi
