# Installing

Packages of each release are on the [releases page](https://github.com/JYU-IBA/jara/releases) on GitHub. Each package
contains JIBAL and its data files (masses, abundances, stopping), so nothing else needs to be installed. The changes
of each version are in the [changelog](../CHANGELOG.md) and in the notes of the release. `SHA256SUMS.txt` of the
release has the checksums of the packages.

To build JaRA yourself, see [Building](building.md).

## macOS

Download `JaRA <version> macOS arm64.dmg`. It is for Macs with Apple silicon (M1 and later). Open the disk image and
drag JaRA to Applications.

The app is not notarized by Apple: it is signed ad hoc or with a self-signed certificate, not with an Apple Developer
ID. macOS therefore refuses to open it the first time, saying that it cannot check JaRA for malicious software or
that Apple could not verify it. To open it anyway:

1. Open JaRA from Applications once and close the warning with **Done** (do not move it to the Bin).
2. Open **System Settings → Privacy & Security**. Near the bottom, under Security, is a note that JaRA was blocked.
   Click **Open Anyway** and confirm with your password or Touch ID.
3. Open JaRA again and click **Open** in the warning.

After this it opens normally. The same is needed again for each new version.

Alternatively, remove the quarantine attribute that the browser put on the downloaded app, in Terminal:

```sh
xattr -dr com.apple.quarantine /Applications/JaRA.app
```

On macOS 14 and earlier, Control-clicking the app and choosing **Open** also works. On macOS 15 and later it no longer
does, and System Settings must be used.

## Windows

Download the MSI installer, `JaRA <version> Windows x64.msi`, or `JaRA <version> Windows x64.zip` to run JaRA
without installing. The packages are not signed, so Windows SmartScreen may warn that it prevented an unrecognized
app from starting. Click **More info**, then **Run anyway**. From the zip, extract all of its files and run
`jara.exe` in them.

## Linux

Download `JaRA <version> Linux x86_64.AppImage`, make it executable and run it:

```sh
chmod +x "JaRA <version> Linux x86_64.AppImage"
./"JaRA <version> Linux x86_64.AppImage"
```

The AppImage needs FUSE 2 (`libfuse2` or `libfuse2t64` on Ubuntu). Without FUSE, run it with
`--appimage-extract-and-run`.

## Checking the installation

`jara --check-installation` (or the packaged program with this option) loads the JIBAL data and reports whether
everything was found, without opening a window.
