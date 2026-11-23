# Building

## Requirements

- A C++20 compiler (Clang, GCC or MSVC)
- [CMake](https://cmake.org/) 4.3 or newer
- [Qt](https://www.qt.io/) 6.12 or newer (Widgets)
- [JIBAL](https://github.com/JYU-IBA/jibal), installed so that CMake finds it (`find_package(Jibal)`), with its
  data files
- [GSL](https://www.gnu.org/software/gsl/)
- zlib
- git (optional, for the version information)

On macOS the dependencies other than JIBAL are available from Homebrew:

```sh
brew install cmake qt gsl
```

JIBAL is built and installed from its sources as described in its README.

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

## macOS disk image

`release_scripts/deploy_mac.sh` builds JaRA in Release mode in `build_deploy/` and makes `JaRA <version> macOS
<arch>.dmg`. The app in it has Qt, GSL and JIBAL inside, and the JIBAL masses, abundances and stopping files in
`Contents/Resources` with a `jibal.conf` that JaRA uses instead of the user's own JIBAL configuration. The stopping
files are downloaded once and checked against a known SHA-256. macdeployqt6 (from Qt) and jibaltool must be in `PATH`.
The app is signed ad hoc, or with the identity in `JARA_SIGN_IDENTITY` if it is set. Only with a "Developer ID
Application" identity is the hardened runtime (needed for notarization) used, because with a self-signed certificate
it would not load the bundled libraries. The disk image only runs on the
architecture and macOS version that Qt and the other libraries were built for.
