#!/bin/bash
# Builds JaRA in Release mode and makes a DMG of it with Qt, GSL, JIBAL and the JIBAL data files (masses, abundances,
# stopping) in the app bundle, so that it runs without Homebrew or JIBAL installed.
# Usage: deploy_mac.sh. Set JARA_SIGN_IDENTITY to sign with a Developer ID, otherwise the app is signed ad hoc.
# JIBAL must be installed (CMake finds it and jibaltool runs), Qt's macdeployqt6 must be in PATH.
set -e
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
builddir="build_deploy"

for tool in cmake ninja macdeployqt6 qtpaths6 jibaltool hdiutil codesign otool install_name_tool; do
    if ! command -v "$tool" > /dev/null; then
        echo "$tool not found!"
        exit 1
    fi
done
jibal_libdir="$(dirname "$(dirname "$(command -v jibaltool)")")/lib"
echo "JIBAL library from $jibal_libdir"

version=$(tr -d '[:space:]' < version.txt)
arch=$(uname -m)
output="JaRA $version macOS $arch.dmg"

# A build of its own, so that a debug build or one with uncommitted changes in another directory is never shipped.
# The JIBAL configuration and data files (masses, abundances, stopping) are installed in the bundle's Resources, see
# JARA_BUNDLE_JIBAL_DATA in CMakeLists.txt.
cmake -S . -B "$builddir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DJARA_BUNDLE_JIBAL_DATA=ON
cmake --build "$builddir"

staging="$builddir/dmg"
app="$staging/JaRA.app"
resources="$app/Contents/Resources"
rm -rf "$staging"
cmake --install "$builddir" --prefix "$staging"
mv "$staging/jara.app" "$app"

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

# The build and install rpaths point to Homebrew and the JIBAL installation, which would be searched before the
# bundle. Only rpaths relative to the bundle are kept.
for rpath in $(otool -l "$app/Contents/MacOS/jara" | awk '$1 == "cmd" && $2 == "LC_RPATH" {getline; getline; print $2}'); do
    case "$rpath" in
        @*) ;;
        *) install_name_tool -delete_rpath "$rpath" "$app/Contents/MacOS/jara" ;;
    esac
done
if ! otool -l "$app/Contents/MacOS/jara" | grep -q "path @executable_path/../Frameworks "; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$app/Contents/MacOS/jara"
fi

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
"$app/Contents/MacOS/jara" --check-installation

ln -s /Applications "$staging/Applications"
rm -f "$output"
hdiutil create -volname "JaRA $version" -srcfolder "$staging" -format UDZO -ov "$output"
echo "Made $output"
