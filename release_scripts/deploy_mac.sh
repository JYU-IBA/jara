#!/bin/bash
# Builds JaRA in Release mode and makes a DMG of it with Qt, GSL, JIBAL and the JIBAL data files (masses, abundances,
# stopping) in the app bundle, so that it runs without Homebrew or JIBAL installed.
# Usage: deploy_mac.sh. Set JARA_SIGN_IDENTITY to sign with a Developer ID, otherwise the app is signed ad hoc.
# JIBAL must be installed (CMake finds it and jibaltool runs), Qt's macdeployqt6 must be in PATH.
set -e
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
builddir="build_deploy"
jibal_data_url="https://users.jyu.fi/~jaakjuli/jibal/data/data.tar.gz"
jibal_data_sha256="799a74a37cfd3d003a8b4914e6ff2c554c1b78eacbba84e3a6a414f982ce0bce"

for tool in cmake ninja macdeployqt6 qtpaths6 jibaltool hdiutil codesign otool curl; do
    if ! command -v "$tool" > /dev/null; then
        echo "$tool not found!"
        exit 1
    fi
done
jibal_datadir="$(jibaltool config 2>&1 | grep "^datadir = " | sed "s/^datadir = //")"
jibal_libdir="$(dirname "$(dirname "$(command -v jibaltool)")")/lib"
if [ ! -f "$jibal_datadir/masses.dat" ] || [ ! -f "$jibal_datadir/abundances.dat" ]; then
    echo "JIBAL masses.dat and abundances.dat are not in \"$jibal_datadir\"!"
    exit 1
fi
echo "JIBAL data (masses, abundances) from $jibal_datadir, library from $jibal_libdir"

version=$(tr -d '[:space:]' < version.txt)
arch=$(uname -m)
output="JaRA $version macOS $arch.dmg"

# A build of its own, so that a debug build or one with uncommitted changes in another directory is never shipped
rm -rf "$builddir/jara.app"
cmake -S . -B "$builddir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$builddir"

jibal_data_file="$builddir/jibal_data.tar.gz"
if [ ! -f "$jibal_data_file" ]; then
    echo "Downloading JIBAL data files"
    curl -fL "$jibal_data_url" -o "$jibal_data_file"
fi
if ! echo "$jibal_data_sha256  $jibal_data_file" | shasum -a 256 --check --status; then
    echo "$jibal_data_file does not pass the SHA-256 check!"
    shasum -a 256 "$jibal_data_file"
    exit 1
fi
jibal_data_dir="$builddir/jibal_data"
rm -rf "$jibal_data_dir"
mkdir -p "$jibal_data_dir"
tar zxf "$jibal_data_file" -C "$jibal_data_dir"

staging="$builddir/dmg"
app="$staging/JaRA.app"
resources="$app/Contents/Resources"
rm -rf "$staging"
mkdir -p "$staging"
cp -R "$builddir/jara.app" "$app"
# Paths in jibal.conf are relative to the directory it is in. JaRA looks for it in Resources (src/elements.cpp).
cat > "$resources/jibal.conf" << EOF
datadir = .
masses_file = masses.dat
abundances_file = abundances.dat
files_file = files.txt
assignments_file = assignments.txt
EOF
cp "$jibal_datadir/masses.dat" "$jibal_datadir/abundances.dat" "$resources"
cp "$jibal_data_dir"/*.ele "$jibal_data_dir"/*.stg "$jibal_data_dir/files.txt" "$jibal_data_dir/assignments.txt" "$resources"

# macdeployqt would copy every plugin of the Qt installation it finds, which with Homebrew includes the virtual keyboard
# (and with it QtQuick and QML) and plugins of Qt modules that are not deployed (PDF, SVG). Only these are needed.
qt_plugindir="$(qtpaths6 --plugin-dir)"
plugins=(platforms/libqcocoa.dylib styles/libqmacstyle.dylib)
for plugin in "$qt_plugindir"/imageformats/*.dylib; do
    case "$(basename "$plugin")" in
        libqpdf.dylib|libqsvg.dylib) ;;
        *) plugins+=("imageformats/$(basename "$plugin")") ;;
    esac
done
# Without qt.conf the plugins of an installed Qt would be loaded, if there is one
printf "[Paths]\nPlugins = PlugIns\n" > "$resources/qt.conf"
executables=()
for plugin in "${plugins[@]}"; do
    mkdir -p "$app/Contents/PlugIns/$(dirname "$plugin")"
    cp "$qt_plugindir/$plugin" "$app/Contents/PlugIns/$plugin"
    executables+=("-executable=$app/Contents/PlugIns/$plugin")
done

echo "Running macdeployqt6"
macdeployqt6 "$app" -no-plugins "${executables[@]}" -libpath="$jibal_libdir" -no-strip

# The signing is done here and not by macdeployqt -codesign, which leaves some of the frameworks it copies with their
# original (ad hoc, no team) signature. With the hardened runtime dyld then refuses to load them ("different Team IDs").
# macdeployqt changes the binaries, and Apple Silicon does not run them unsigned, so they are signed ad hoc otherwise.
sign_identity="${JARA_SIGN_IDENTITY:--}"
sign_options=(--force --sign "$sign_identity")
if [ "$sign_identity" != "-" ]; then
    echo "Signing with $sign_identity"
    sign_options+=(--timestamp)
fi
# The hardened runtime (needed for notarization) loads only libraries of the same team as the app. A self-signed
# certificate has no team, and then nothing in Frameworks is loaded, so it is used only with a Developer ID.
case "$sign_identity" in
    "Developer ID Application"*) sign_options+=(--options runtime) ;;
esac
# Inside out: libraries and plugins, then frameworks, then the app
find "$app/Contents" -type f -name "*.dylib" -print0 | xargs -0 codesign "${sign_options[@]}"
find "$app/Contents/Frameworks" -maxdepth 1 -name "*.framework" -print0 | xargs -0 codesign "${sign_options[@]}"
codesign "${sign_options[@]}" "$app"

# Every binary must have the team of the app, or the hardened runtime does not load it
team_id() {
    codesign -dv "$1" 2>&1 | grep "^TeamIdentifier=" | sed "s/^TeamIdentifier=//"
}
app_team=$(team_id "$app")
wrong_team=$(find "$app/Contents" -type f \( -perm -u+x -o -name "*.dylib" \) | while read -r binary; do
    if file -b "$binary" | grep -q "^Mach-O" && [ "$(team_id "$binary")" != "$app_team" ]; then
        echo "$binary: $(team_id "$binary")"
    fi
done)
if [ -n "$wrong_team" ]; then
    echo "Binaries not signed by the team of the app ($app_team)!"
    echo "$wrong_team"
    exit 1
fi

# Everything the app loads must come from the bundle or from the system
not_bundled() {
    local binary library name
    find "$app/Contents" -type f \( -perm -u+x -o -name "*.dylib" \) | while read -r binary; do
        # A library lists its own install name first, which does not matter for loading it
        otool -L "$binary" | tail -n +2 | awk '{print $1}' | grep -vxF "$(otool -D "$binary" | tail -n +2)" \
            | while read -r library; do
            case "$library" in
                @rpath/*|@executable_path/*)
                    name="${library#@rpath/}"
                    name="${name#@executable_path/../Frameworks/}"
                    if [ ! -e "$app/Contents/Frameworks/$name" ]; then
                        echo "$binary: $library"
                    fi
                    ;;
                /opt/homebrew/*|/usr/local/*|"$jibal_libdir"/*)
                    echo "$binary: $library"
                    ;;
            esac
        done
    done
}
missing=$(not_bundled)
if [ -n "$missing" ]; then
    echo "Libraries outside the bundle are still used!"
    echo "$missing"
    exit 1
fi
codesign --verify --deep --strict "$app"

ln -s /Applications "$staging/Applications"
rm -f "$output"
hdiutil create -volname "JaRA $version" -srcfolder "$staging" -format UDZO -ov "$output"
echo "Made $output"
