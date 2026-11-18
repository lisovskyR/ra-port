# ra-port

**Native macOS, Linux, Android, and iOS source port of Command & Conquer: Red Alert.**

[![macOS](https://img.shields.io/badge/macOS-native-111111?logo=apple&logoColor=white)](#quick-start)
[![Linux](https://img.shields.io/badge/Linux-Ubuntu-e95420?logo=ubuntu&logoColor=white)](#linux-desktop)
[![Android](https://img.shields.io/badge/Android-debug%20APK-3ddc84?logo=android&logoColor=white)](#android-debug-apk)
[![iOS](https://img.shields.io/badge/iOS-debug%20app-111111?logo=apple&logoColor=white)](#ios-debug-app)
[![Build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-064f8c)](#build-from-source)
[![Runtime](https://img.shields.io/badge/runtime-SDL2-cc3333)](#current-status)
[![Source-only](https://img.shields.io/badge/source--only-no%20game%20data-lightgrey)](#game-data)
[![License](https://img.shields.io/badge/license-GPLv3%20with%20additional%20terms-blue)](#license-and-notice)

`ra-port` lets you play Red Alert (1996) on modern platforms. It currently runs as native macOS and Linux executables, a local Android debug APK, and an iOS debug app, with SDL2 providing the platform layer.

![Red Alert running natively in a macOS window](docs/images/ra-port-macos-window.png)

The repository contains only source code and build tooling. No game assets are included in the repository. To play, provide legally obtained Red Alert assets from your own discs, mounted images, or local backups.

## Why This Exists

Red Alert was released for a very different desktop world. This project keeps the original code recognizable while supporting macOS, Linux, Android, and iOS.

This is an unofficial source port based on the source code Electronic Arts released under GPLv3 with additional terms: <https://github.com/electronicarts/CnC_Red_Alert>.

## Current Status

| Status | Feature | Notes |
| --- | --- | --- |
| :white_check_mark: | macOS on Apple Silicon | Builds and runs with CMake/Ninja. |
| :white_check_mark: | Linux on Ubuntu | Builds and runs as a native SDL2 desktop executable. |
| :white_check_mark: | Android debug APK | Builds a local landscape APK for arm64-v8a devices and emulators. |
| :white_check_mark: | iOS debug app | Builds a landscape simulator/device app with CMake/Xcode. |
| :white_check_mark: | Campaign | Allied and Soviet campaigns are fully working. |
| :white_check_mark: | Skirmish | Local skirmish is fully working. |
| :white_check_mark: | Videos | Videos are playing with sound. |
| :white_check_mark: | Controls and audio | macOS keyboard/mouse plus Android and iOS touch/audio work. |
| :x: | Online/network multiplayer | Not wired up yet. |
| :x: | Launcher/setup tools | Not ported. |
| :x: | Expansion packs | Not a focus yet. |
| :x: | `.app` bundle | Not packaged yet; the build creates a normal macOS executable. |
| :x: | Android release build | Only local debug APKs are supported right now. |
| :x: | iOS release build | Only local debug simulator/device builds are supported right now. |

## Quick Start

Install the macOS build tools:

```sh
brew install cmake ninja pkg-config sdl2
xcode-select --install
```

Build the port:

```sh
cmake -S . -B build -G Ninja
cmake --build build --target redalert_mac -j 8
```

On Ubuntu, install the Linux build tools and build the port:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libsdl2-dev
cmake -S . -B build-linux -G Ninja
cmake --build build-linux --target redalert_linux -j 8
```

Prepare local game data:

```sh
scripts/prepare_assets_from_local.sh \
  --allies /path/to/allies-disc \
  --soviet /path/to/soviet-disc
```

Run:

```sh
scripts/run_mac_dev.sh --no-build
```

On Ubuntu, run:

```sh
scripts/run_linux_dev.sh --no-build
```

To build and run the Android debug APK, install the Android prerequisites listed below, keep the same prepared local game data under `assets/redalert`, then run:

```sh
scripts/build_android_debug.sh
scripts/run_android_debug.sh --no-build
```

To build and run the iOS simulator debug app, install full Xcode, keep the same prepared local game data under `assets/redalert`, then run:

```sh
scripts/build_ios_debug.sh
scripts/run_ios_simulator.sh --no-build
```

## Game Data

The repository contains only source code and build tooling. It does not contain game data, movies, music, disc images, archives, installers, generated palettes, or packaged executables.

The asset preparation script copies from local paths that you provide:

- `assets/redalert/allies`
- `assets/redalert/soviet`

Those directories are ignored by git. They should contain original disc-root style files such as `INSTALL/REDALERT.INI` and the base-game `.MIX` files.

The Android and iOS debug builds use the same ignored `assets/redalert` tree. Gradle copies those local files into generated Android debug assets, and the iOS CMake target copies them into the debug app bundle. They are not checked in and they are not used for release packaging.

### Language packs

The engine opens `WOLAPI.MIX` before every other mixfile when it is present, even though online play is not built in. Version 3.03 language packs ship their translated text, fonts, title screens, and mission briefings in that file, so it replaces the English versions.

To play in a pack's language, copy its `WOLAPI.MIX` into both `assets/redalert/allies/` and `assets/redalert/soviet/`. The engine only searches the current disc's folder at startup. Remove the file from both folders to go back to English.

The Russian 3.03 translation by dantt3, based on the 2005 Siberian Studio localization, works this way. It is the `RA1303Rus` patch on Playground.ru. Its `WOLAPI.MIX` translates the menus, in-game text, and mission briefings. Voices and movies stay in English. The patch's `ra1_303_rus.exe` patches the Windows game executable and is not needed.

### Voice packs

The engine also loads `SC*.MIX` files from either disc's data folder, and searches them before the stock mixfiles. So add-ons that ship replacement speech, unit voices, or movies in such files override the English versions. One copy in `assets/redalert/allies/` is enough. Small ones are loaded into memory at startup. Larger ones, such as movie packs, are read from disk as needed.

Mechanics VoiceOver's Russian dub was released in December 2021, in the Old-Games.ru thread "Релиз русской озвучки Command and Conquer: Red Alert". It comes in two forms:

- **For the original game:** `SC--EVAR.MIX` for EVA, `SC--UNTR.MIX` for unit voices, and `SC--VIDR.MIX` for the dubbed cutscenes. Copy them in as they are. Leave out its `EXPAND.MIX` and `EXPAND2.MIX`, which replace expansion files the base-game discs don't include. When this was written, its Yandex Disk download failed with a server error.
- **For the Remastered Collection:** an installer with no password. Its voice files and its cutscenes' dubbed audio can be converted for this port. The cutscene video is Bink 2, which can't be converted, so the original videos are kept and only their audio is replaced.

To convert the Remastered release, extract its installer with `innoextract`, then run the converter on the mod's audio folder:

```sh
python3 scripts/make_voice_mix.py "<extracted>/Data/Audio/EN-US" assets/redalert/allies
```

It writes `SC--EVAR.MIX` and `SC--UNTR.MIX` in the game's own 16-bit IMA ADPCM format. EVA lines are kept under the 50,000-byte limit of the game's speech buffers by lowering their sample rate when needed. It uses macOS's `afconvert` to resample.

For the cutscenes, run the movie converter on the mod's movie folder and both discs' `MAIN.MIX`:

```sh
python3 scripts/make_movie_mix.py --track 2 "<extracted>/Data/ART/MOVIES/RA" assets/redalert/allies \
    assets/redalert/allies/MAIN.MIX assets/redalert/soviet/MAIN.MIX
```

The Bink files carry several audio tracks, and `--track` picks the dub, counting from 0. In this dub's files the Russian is track 2. Add `--dry-run` to see the matches without writing anything. The script works like this:

- It finds each cutscene's original movie by comparing soundtracks, because the names inside `MAIN.MIX` are encrypted.
- It measures the timing offset from the music and effects the two versions share, so speech stays in sync.
- It writes the dubbed audio in place of the original's, at the original's loudness.
- Only audio bytes change, so the original video and frame timing are untouched.
- Any cutscene it can't match with confidence is left in English.

It writes `SC--VIDR.MIX` and needs `ffmpeg` and a C compiler.

## Build From Source

Configure and build on macOS:

```sh
cmake -S . -B build -G Ninja
cmake --build build --target redalert_mac -j 8
```

If you do not have Ninja installed, omit `-G Ninja` and CMake will choose the default generator.

The build currently creates a raw macOS executable:

```sh
build/redalert_mac
```

It is not packaged as a `.app` bundle yet.

## Linux Desktop

Install Ubuntu build dependencies:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libsdl2-dev
```

Configure and build:

```sh
cmake -S . -B build-linux -G Ninja
cmake --build build-linux --target redalert_linux -j 8
```

Run from the repository root:

```sh
scripts/run_linux_dev.sh
```

For headless smoke validation, install Xvfb and ImageMagick, then capture the title/menu:

```sh
sudo apt-get install -y xvfb imagemagick
scripts/smoke_linux_menu.sh --seconds 20
```

## Run

The normal development run command builds if needed, verifies local assets, codesigns the executable, and launches from the repository root:

```sh
scripts/run_mac_dev.sh
```

Useful variants:

```sh
scripts/run_mac_dev.sh --no-build
scripts/run_mac_dev.sh --prepare-only
```

The Linux run helper follows the same flow without codesigning:

```sh
scripts/run_linux_dev.sh
scripts/run_linux_dev.sh --no-build
scripts/run_linux_dev.sh --prepare-only
```

You can also run the built executable directly after codesigning:

```sh
codesign --force --sign - build/redalert_mac
./build/redalert_mac
```

Runtime files such as `SAVEGAME.*`, `OPTIONS.INI`, `ASSERT.TXT`, screenshots, logs, and generated palette caches are ignored by git.

## Android Debug APK

The Android target is for local development and testing only. It builds an arm64-v8a debug APK, locks the activity to landscape, uses touch-native input, and extracts the bundled debug assets into app storage on first launch.

Install Android tooling:

- JDK 17
- Gradle compatible with Android Gradle Plugin 9.2.0
- Android SDK Platform 36
- Android SDK Build Tools
- Android NDK `28.2.13676358`
- Android CMake package
- Android platform-tools for `adb`

Android Studio is the easiest way to install the SDK, NDK, CMake, emulator, and platform-tools. On Homebrew-based macOS setups, the helper scripts auto-detect common `openjdk@17` and Android SDK locations when `JAVA_HOME`, `ANDROID_HOME`, or `ANDROID_SDK_ROOT` are not already set.

Build the debug APK:

```sh
scripts/build_android_debug.sh
```

The first build downloads SDL2 sources into ignored local storage under `android/third_party/`. The APK is written to:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

Install and launch on a connected device or emulator:

```sh
scripts/run_android_debug.sh --no-build
```

If an emulator is low on space and in-place install fails, uninstall the old debug app first:

```sh
scripts/run_android_debug.sh --no-build --fresh-install
```

`--fresh-install` removes existing app data, including extracted assets and saves.

Build, install, launch, and tail Android logs:

```sh
scripts/run_android_debug.sh --logcat
```

Useful direct commands:

```sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.raport.redalert/.RedAlertActivity
```

The debug APK intentionally includes your local ignored game data so the app can run on the device without external storage setup. Do not distribute that APK.

## iOS Debug App

The iOS target is for local development and testing only. It builds a landscape app for the iOS simulator by default, or for a signed arm64 device build when you provide an Apple development team.

Install iOS tooling:

- Full Xcode, not only Command Line Tools
- CMake
- Local Red Alert assets prepared under `assets/redalert`

Build the simulator app:

```sh
scripts/build_ios_debug.sh
```

The first build downloads SDL2 sources into ignored local storage under `ios/third_party/`. The app is written under:

```text
ios/build-simulator/Debug-iphonesimulator/redalert_ios.app
```

Install and launch on the booted iOS simulator:

```sh
scripts/run_ios_simulator.sh --no-build
```

Build a signed device app for iPhone or iPad:

```sh
RA_IOS_DEVELOPMENT_TEAM=TEAMID scripts/build_ios_debug.sh --device
```

Device builds use automatic signing and may register the device and create a development profile. Set `RA_IOS_BUNDLE_ID` to a bundle identifier your team can use if `com.raport.redalert` is not available to you. Install the result with:

```sh
xcrun devicectl device install app --device DEVICE_ID ios/build-device/Debug-iphoneos/redalert_ios.app
```

Devices on iOS 27 need Xcode 27. Select it with `DEVELOPER_DIR` when several Xcode versions are installed:

```sh
DEVELOPER_DIR=/Applications/Xcode-27.1.0.app/Contents/Developer RA_IOS_DEVELOPMENT_TEAM=TEAMID RA_IOS_BUNDLE_ID=com.example.redalert scripts/build_ios_debug.sh --device
```

Apps built with the iOS 27 SDK must use the UIScene lifecycle, so the app ships a scene delegate and attaches SDL's window to that scene. The minimum deployment target is iOS 15, the lowest that Xcode 27 accepts.

The debug iOS app intentionally includes your local ignored game data. Do not distribute that app bundle. The game runs from a copy of that data in app-writable storage, so saves and options can be written inside the iOS sandbox. Each launch brings the copy up to date with the bundle:

- Files added to `assets/redalert` before a rebuild, such as a language pack, are copied in.
- Changed files are replaced, and files that an earlier build copied but the bundle no longer has are removed.
- The player's `REDALERT.INI` settings are never overwritten.

### App icon

The repository ships an original icon design in `ios/Assets.xcassets`, drawn by `ios/icon/make_app_icon.swift`. It contains no game art.

You can use the original 1996 artwork instead. Either script writes a 1024x1024 PNG:

- `ios/icon/make_exe_icon.py` takes the Red Alert 95 program icon from the game's `RA95.EXE` and enlarges its 32x32 pixels with hard edges. It only parses the file and never runs it.
- `ios/icon/make_disc_art_icon.swift` crops the Allied disc's installer screen, with the Command & Conquer Red Alert logo along the top.

```sh
python3 ios/icon/make_exe_icon.py assets/redalert/allies/INSTALL/RA95.EXE assets/AppIcon-1024.png
```

```sh
swift ios/icon/make_disc_art_icon.swift /path/to/allied-disc/SETUP95/SETUP.BMP assets/AppIcon-1024.png
```

The build uses `assets/AppIcon-1024.png` whenever that file exists. `assets/` is ignored by git, so EA's artwork stays out of the repository. To use any other 1024x1024 PNG, set `RA_IOS_APP_ICON=/path/to/icon.png`. Delete the file, or leave the variable unset, to go back to the original design.

### Screen layout and iPhone Duo

The game is drawn for 640x400. On iOS it is scaled at the display's native pixel density. Wide iPhones show the 640x400 frame centered, with bars that already clear the Dynamic Island.

On displays squarer than 640x400, such as both iPhone Duo displays and iPads, the game page grows in whole map-cell rows (448 lines on the Duo, 472 on 4:3 tablets):

- Missions use the whole page and fill the screen edge to edge, so the tactical map shows extra rows instead of letterbox bars. The stretch needed to fill is under 2 percent on the Duo. The strip below the sidebar art is black, and blank metal tab plates fill the top bar between the Options tab and the credits counter.
- Menus, movies, briefings, and score screens are still drawn for 640x400. They use a centered 640x400 view of the page and are fitted with bars that keep clear of the Dynamic Island. This includes the briefing opened from the in-game Options menu.
- On phones, only landscape-right is allowed while missions fill the screen. That puts the Dynamic Island over the left edge of the map instead of the build sidebar.

When the device is folded, unfolded, or the safe area changes, the frame is re-laid out and the last frame is redrawn immediately, even while a menu is idle. Touch input uses the same layout as rendering.

Launch with `RA_IOS_LAYOUT_DEBUG=1` to log the window size, safe-area insets, orientation, the shown part of the page, its screen rectangle, and each touch in game coordinates.

## Fullscreen

Start fullscreen:

```sh
RA_FULLSCREEN=1 scripts/run_mac_dev.sh
```

Toggle fullscreen while running:

```text
Command+Return on macOS
Alt+Return on Linux
```

## Tests

Run the source-level tests and script checks:

```sh
tests/run_script_tests.sh
```

Validate a fresh checkout with a full build first:

```sh
cmake -S . -B build -G Ninja
cmake --build build --target redalert_mac -j 8
tests/run_script_tests.sh
```

On Linux:

```sh
cmake -S . -B build-linux -G Ninja
cmake --build build-linux --target redalert_linux -j 8
tests/run_script_tests.sh
```

## Project Layout

| Path | Purpose |
| --- | --- |
| `CODE/` | Main Red Alert game code |
| `PORT/MAC/` | Shared desktop runtime, compatibility shims, SDL2 integration |
| `PORT/ANDROID/` | Android entrypoint and platform-specific resource setup |
| `PORT/IOS/` | iOS entrypoint and writable sandbox resource setup |
| `android/` | Gradle Android app that builds the debug APK |
| `ios/` | CMake/Xcode iOS app target |
| `WIN32LIB/`, `WINVQ/` | Legacy support libraries used by the port |
| `scripts/` | Asset preparation, run helpers, smoke capture, Linux include overlay generation |
| `tests/` | Focused source-level and shim tests |
| `docs/images/` | README images only, not game data |

## Contributing

The port is intentionally conservative: keep original source layout and behavior recognizable, and prefer small platform-specific support files over broad rewrites. Good next areas are macOS `.app` packaging, Linux packaging, Android/iOS release packaging, physical device validation, Intel macOS validation, save/load hardening, expansion support, CI coverage, and mobile input/UI polish.

Network and online multiplayer are out of scope for the current milestone.

## License And Notice

The source code is distributed under GPLv3 with additional terms. See `LICENSE.md`.

This is an unofficial modified source port. It is not affiliated with, endorsed by, sponsored by, or supported by Electronic Arts or any other rights holder. See `NOTICE.md`.
