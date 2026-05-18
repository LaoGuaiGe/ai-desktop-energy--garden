# Garden UI Sim / Focus Page Sim

PC LVGL simulator for previewing the 1280x452 UI without flashing ESP32-P4.

There are two CMake targets:

- `garden-sim`: legacy standalone garden demo.
- `garden-focus-sim`: current focus page simulator. It directly compiles `../lvgl9.5-demo/main/garden_focus.c`, so UI tweaks are made against the same source that will be used on ESP32-P4.
- `garden-pages-sim`: current multipage simulator. It directly compiles the ESP32-P4 page/navigation sources from `../lvgl9.5-demo/main/`.

The simulator reuses LVGL from `../lvgl9.5-demo/managed_components/lvgl__lvgl`; no extra LVGL clone is needed if the ESP-IDF project has already fetched managed components.

## Build Focus Page Simulator

Use MSYS2 UCRT64 on Windows:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2
cd /z/ai-desktop-energy--garden/garden-ui-sim
cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=$(which mingw32-make)
cmake --build build --target garden-focus-sim -j4
./build/garden-focus-sim.exe
```

## Build Multipage Simulator

```bash
cmake --build build --target garden-pages-sim -j4
./build/garden-pages-sim.exe
```

This target uses the same page order as the firmware:

```text
0 focus
1 garden/home
2 ai
3 device
4 album
```

Swipe horizontally with the mouse to test navigation.

Controls:

| Input | Effect |
| --- | --- |
| Mouse click | LVGL touch/click |
| Space | Start / pause focus timer |
| Home | Go to garden/home page in `garden-pages-sim` |
| ESC | Quit |

## Build Legacy Garden Simulator

```bash
cmake --build build --target garden-sim -j4
./build/garden-sim.exe
```

## Porting Rule

For focus page UI, edit `../lvgl9.5-demo/main/garden_focus.c` first and validate it with `garden-focus-sim`. The same file is compiled by the ESP32-P4 firmware, so only platform setup remains device-specific.
