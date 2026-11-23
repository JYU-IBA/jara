# Building

## Requirements

- A C++20 compiler (Clang, GCC or MSVC)
- [CMake](https://cmake.org/) 4.2 or newer
- [Qt](https://www.qt.io/) 6.9 or newer (Widgets)
- [JIBAL](https://github.com/JYU-IBA/jibal), installed so that CMake finds it (`find_package(Jibal)`), with its
  data files
- [GSL](https://www.gnu.org/software/gsl/)
- zlib
- git (optional, for the version information)

JaRA is developed and tested on macOS. It should build on Linux and Windows too, but is not regularly tested there.

JIBAL is built and installed from its sources as described in its README, on every platform. It needs GSL, so
install the other dependencies first.

### macOS

The dependencies other than JIBAL are available from Homebrew:

```sh
brew install cmake qt gsl
```

### Ubuntu

The compiler, CMake, GSL, zlib and Qt come from the Ubuntu packages:

```sh
sudo apt install build-essential git cmake qt6-base-dev libgsl-dev zlib1g-dev
```

Only Qt Widgets is needed, which is in `qt6-base-dev`. Qt applications on X11 also need `libxcb-cursor0` to start; if
JaRA exits with an error about the "xcb" platform plugin, install it.

JIBAL is installed to `/usr/local` by default. Run `sudo ldconfig` after installing it, so that its shared library is
found when JaRA runs.

## Compiling

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The program is `build/jara.app` on macOS and `build/jara` elsewhere. A project file can be given on the command
line: `jara project.json`.

If CMake does not find Qt or JIBAL, add their installation prefixes to `CMAKE_PREFIX_PATH`, e.g.
`-DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/usr/local"`.

## Version information

The version is taken from the latest `vX.Y.Z` tag, or from `version.txt` when there is no git. Every build records
the git state, so the About dialog tells which commit the program was built from and whether it had uncommitted
changes.

## Installing and the JIBAL data

`cmake --install build` installs JaRA (`--prefix` to choose where). With `-DJARA_BUNDLE_JIBAL_DATA=ON` the JIBAL
masses, abundances and stopping files and a `jibal.conf` are installed with it, and JaRA uses them instead of the
user's own JIBAL configuration. The masses and abundances come from the JIBAL installation, the stopping files are
downloaded once and checked against a known SHA-256. JaRA looks for the `jibal.conf` relative to the program:

- macOS: `jara.app/Contents/Resources/jibal.conf`
- Windows: `jibal.conf` next to `jara.exe`
- Linux: `../share/jara/jibal.conf` from the program, e.g. `/usr/local/share/jara/jibal.conf`

The data files are in `jibal_data` next to it. Without it, JIBAL's own configuration is used. The stopping files in
the data are listed in [Settings](settings.md#stopping-data-in-the-packages).

`jara --check-installation` checks that JIBAL and its data are found, without opening a window. It prints the JIBAL
configuration file and data files used and the stopping of 2 MeV ⁴He in Si, and exits with status 0 when the stopping
could be calculated.

## macOS disk image

`release_scripts/deploy_mac.sh` builds JaRA in Release mode in `build_deploy/` and makes `JaRA <version> macOS
<arch>.dmg`. The app in it has Qt, GSL and JIBAL inside, and the JIBAL data files installed with
`JARA_BUNDLE_JIBAL_DATA`. macdeployqt6 (from Qt) and jibaltool must be in `PATH`. The app is signed ad hoc, or with the
identity in `JARA_SIGN_IDENTITY` if it is set. Only with a "Developer ID Application" identity is the hardened runtime
(needed for notarization) used, because with a self-signed certificate it would not load the bundled libraries. The
script ends by running the packaged app with `--check-installation`. The disk image only runs on the architecture and
macOS version that Qt and the other libraries were built for.

## Packages and releases on GitHub

The Build workflow (`.github/workflows/build.yml`) runs for pushes to main, for version tags and by hand. It builds
JIBAL from its main branch and makes these packages:

- a Linux AppImage, made with linuxdeploy on Ubuntu 24.04 with Qt from the Qt installer;
- a Windows zip and MSI installer (CPack with WiX), with GSL and zlib from vcpkg;
- a macOS disk image from `deploy_mac.sh`, signed ad hoc.

Each package is checked with `--check-installation` and uploaded as an artifact of the run. Another job builds JaRA
with the Ubuntu 26.04 packages as described above, to check these instructions. The commit of JIBAL used is in
`build-info-*.txt` of the artifacts.

To make a release:

1. Run `release_scripts/bump_version.sh` and push the commit and the tag.
2. Check the packages of the Build run of the tag.
3. Run the Release workflow (`.github/workflows/release.yml`) by hand with the tag. It makes a draft release with the
   packages of that run and their SHA-256 sums.
4. Optionally replace the ad hoc signed disk image with one signed locally (`gh release upload <tag> <dmg> --clobber`,
   and update `SHA256SUMS.txt`).
5. Publish the draft.
