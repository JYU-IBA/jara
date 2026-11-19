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
