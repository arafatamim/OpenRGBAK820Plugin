# OpenRGB AK820 Max Plus Plugin

An [OpenRGB](https://openrgb.org) 1.0 plugin for the **Ajazz AK820 Max Plus** keyboard.

- **Direct mode:** per-key colour control with a brightness slider, so OpenRGB effects and the Effects plugin work on it.
- **Built-in effects:** all 16 of the keyboard's own effects, with speed, brightness and colour settings.
- **Battery:** the charge level shows on the plugin's own tab.

| Connection | USB ID | Status |
|---|---|---|
| 2.4 GHz dongle | `1A2C:8FFF` | Tested |
| Wired | `1A2C:A036` | Untested |
| Bluetooth | — | Not supported |

## Install

Download from [Releases](../../releases).

### Windows

Use either file:

- **`OpenRGBAK820Plugin.msi`:** run it. It installs the plugin for your user only, so it doesn't need admin rights. It installs to `%APPDATA%\OpenRGB\plugins` unless you choose another folder in the wizard. To remove it, use Apps & features.
- **`OpenRGBAK820Plugin.dll`:** copy it to `%APPDATA%\OpenRGB\plugins\` yourself.

Then restart OpenRGB. The keyboard appears as **Ajazz AK820 Max Plus**.

### Linux

1. Copy `libOpenRGBAK820Plugin.so` to `~/.config/OpenRGB/plugins/`.
2. Install the udev rule so OpenRGB can open the keyboard without root:
   ```
   sudo cp 60-openrgb-ak820.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
3. Replug the dongle and restart OpenRGB.

The `.so` is built against Qt 6.8 on Debian trixie, so it needs a **Qt 6** build of OpenRGB using Qt 6.8 or newer, such as the Qt 6 AppImage. It won't load in Qt 5 builds. The Linux build loads in Qt but hasn't been tested with the keyboard yet.

The plugin needs OpenRGB **1.0** (x64). It won't load in 0.9.

## Notes

- The plugin opens the keyboard when it first sends lighting. If the keyboard was asleep or unplugged, it reconnects on the next update.
- If you change the effect with the keyboard's Fn keys while Direct mode is on, OpenRGB doesn't notice. To get Direct mode back, pick any effect in OpenRGB and then select Direct again.
- The built-in effects only support 7 fixed colours (red, green, blue, yellow, magenta, cyan and white). Any other colour you pick is matched to the nearest of those.
- Over the 2.4 GHz dongle, packets are spaced 5 ms apart. If some rows don't update, raise `gap` in `AK820Plugin.cpp`.

## Build

### Windows

You need the Visual Studio 2022 build tools, CMake 3.20 or newer, and Qt 6.8 for `msvc2022_64`. You can install Qt with:

```
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64
```

```
cmake -B build -A x64 -DCMAKE_PREFIX_PATH=<qt>/6.8.3/msvc2022_64
cmake --build build --config Release
```

CMake fetches the OpenRGB `release_1.0` headers automatically. The plugin links against the `hidapi-hotplug.dll` that ships with OpenRGB.

To build the installer (WiX 5):

```
dotnet tool install -g wix --version 5.0.2
wix extension add -g WixToolset.UI.wixext/5.0.2
wix build installer/AK820Plugin.wxs -arch x64 -ext WixToolset.UI.wixext -d Version=0.2.0 -o build/OpenRGBAK820Plugin.msi
```

### Linux

You need CMake, g++, pkg-config, git, Qt 6 and hidapi. On Debian or Ubuntu that's `cmake g++ pkg-config git qt6-base-dev libhidapi-dev`. To match the release build's Qt 6.8, build in a `debian:trixie` container:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/libOpenRGBAK820Plugin.so`. It links against the system `hidapi-hidraw`, the same backend OpenRGB uses.

## License

GPL-2.0-or-later, the same license as OpenRGB.

This is an unofficial plugin. It isn't affiliated with Ajazz or the OpenRGB project.
