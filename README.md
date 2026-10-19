# Plasma TV

A fullscreen, remote-controllable TV client for [TVHeadend](https://tvheadend.org)
and M3U playlists with an XMLTV guide (e.g. [Dispatcharr](https://github.com/Dispatcharr/Dispatcharr)),
built with Qt 6, Qt Quick, Kirigami and
[Bigscreen UI](https://invent.kde.org/devinlin/bigscreen-ui). It follows Plasma
theming and uses Breeze Dark by default.

## Features

* Channel list with logos
* TV guide
* TVHeadend or M3U + XMLTV as the source
* Navigation with TV remote buttons

## Screenshots

**Live TV**: channel logos, the programme on air with its progress and the
time left, plus details and "up next" for the highlighted channel. The channel
that is playing shows through behind the menus.

![Live TV page with the channel list and programme details](img/live-tv.png)

**Watching**: the info bar shows the channel, the current programme with its
progress and what's on next. Up/down zaps, OK opens the channel list.

![Watch mode with the info bar over live video](img/watch.png)

**Settings**: server address, credentials (the password is stored in the
wallet), streaming profile, days of guide data and color scheme, next to the
connection status.

![Settings page with server, playback and appearance options](img/settings.png)

## Build

Supported on **Ubuntu** (26.04 and later) and **Arch Linux**, on both x86_64
and ARM64 (aarch64, including Arch Linux ARM). The code has no
architecture-specific parts, so build natively on the target machine.

### Arch Linux / Arch Linux ARM

Build and install a proper package with the bundled PKGBUILDs. Bigscreen UI
isn't in the Arch repositories yet, so build and install it once first:

```sh
sudo pacman -Syu --needed base-devel git   # toolchain (gcc, binutils, make)
cd packaging/arch/bigscreen-ui
makepkg -si        # Bigscreen UI
cd ..
makepkg -si        # installs the dependencies, builds and installs the package
```

The PKGBUILD builds the committed state of this git repository. For a
development build, install the dependencies and use CMake directly:

```sh
sudo pacman -S --needed base-devel cmake git qt6-base qt6-declarative qt6-multimedia \
    qt6-multimedia-ffmpeg qt6-svg qtkeychain-qt6 kirigami qqc2-desktop-style breeze breeze-icons
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

If CMake reports that the C++ compiler "is not able to compile a simple test
program", the toolchain itself is broken, usually because of a partial upgrade
(`gcc`/`glibc`/`binutils` out of sync). Run `sudo pacman -Syu`, delete the
build directory and configure again.

### Ubuntu

```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
    qt6-multimedia-dev qtkeychain-qt6-dev libqt6sql6-sqlite \
    qml6-module-qtmultimedia qml6-module-qtquick-controls \
    qml6-module-org-kde-kirigami qml6-module-org-kde-desktop kde-style-breeze qt6-svg-plugins
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
sudo cmake --install build   # optional: installs to /usr/local with a launcher entry
```

### Running

```sh
./build/plasma-tv            # fullscreen
./build/plasma-tv --windowed # for development (F11 toggles)
```

Kirigami, [Bigscreen UI](https://invent.kde.org/devinlin/bigscreen-ui), the
Plasma Qt Quick Controls style and Breeze (style and color schemes) are used at
runtime only, so no KF6 `-dev` packages are needed. Bigscreen UI isn't packaged
by distributions yet: on Arch, use `packaging/arch/bigscreen-ui`; elsewhere,
build and install it from source (it needs Qt 6.11 or later).

## M3U + XMLTV

Under *Settings → Source* choose **M3U + XMLTV** and enter the playlist
address, e.g. `http://<host>:9191/output/m3u` for Dispatcharr. The guide address
can stay empty if the playlist names it in its header. Recordings and streaming
profiles need TVHeadend.

## Remote control

| Key (remote / keyboard)                | Action                                      |
|----------------------------------------|---------------------------------------------|
| Arrows                                 | Navigate                                     |
| OK / Enter                             | Select, watch, open programme details; pause/resume while watching |
| Play/Pause / P                         | Pause/resume while watching                 |
| Back / Esc / Backspace                 | Go back (from the top bar: return to video); while watching: quit menu |
| Menu / Settings / F2                   | Settings                                    |
| Guide / G / F3                         | TV guide                                    |
| 0-9                                    | Jump to channel number                      |
| Ch+/Ch- / PgUp/PgDn                    | Zap (watching) or page through lists        |
| ⏭ / ⏮ (or F / R) in the guide        | +/- 24 hours                                |
| Info / I                               | Info bar while watching                     |
| Audio / A, Subtitle / S                | Cycle audio track / subtitles               |
| Stop / X                               | Stop playback                               |

Remotes using HDMI-CEC (kernel CEC input) or LIRC/ir-keytable mapped to these
key codes work out of the box.
