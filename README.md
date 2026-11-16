# NetStorm Port

A from-scratch modern engine for **NetStorm: Islands at War** (Titanic
Entertainment / Activision, 1997): native high resolutions with free zoom,
upscaled art, and proper LAN + internet play. Windows, Linux and Steam Deck.

**You need your own copy of NetStorm.** This repo contains no game files; the
engine reads everything from your install folder at runtime.

## Status

Milestone 0 (data foundation) is done: every script and every sprite in the
game loads natively. See [ROADMAP.md](ROADMAP.md) and [docs/FORMATS.md](docs/FORMATS.md).

## Build

Needs CMake 3.16+, a C++20 compiler and SDL2.

```sh
# Linux / Steam Deck
sudo apt install libsdl2-dev cmake g++     # or your distro's equivalent
cmake -S . -B build && cmake --build build -j

# Windows (vcpkg)
vcpkg install sdl2:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Run

```sh
./build/netstorm --data "/path/to/Netstorm"            # world view (Tab: sprite viewer)
./build/netstorm --data "/path/to/Netstorm" --mission rainvsrain
./build/netstorm --data "/path/to/Netstorm" --fullscreen
./build/nstool "/path/to/Netstorm" info
./build/nstool "/path/to/Netstorm" types               # unit/building stats
./build/nstool "/path/to/Netstorm" dump-sprites out/   # all frames as PNG (for upscaling)
./build/nstool "/path/to/Netstorm" extract-scripts out/
./build/nstool "/path/to/Netstorm" forts                 # decode every mission
```

World controls: left click drop bridge piece, right click / R rotate, B let the AI
place one, drag or WASD pan, wheel zoom, M / [ ] cycle missions, N test layout, G grid.

Sprite viewer controls: ←/→ change sprite set, PgUp/PgDn jump 10, mouse wheel zoom,
drag to pan, F sharp/smooth filtering, T cycle team colours, Space animate,
F11 fullscreen.

## Layout

```
src/core     file IO, PNG writer
src/data     archive, sprite DB, palettes, .type parser   (no SDL)
src/render   SDL2 renderer, camera, sprite texture cache
src/app      viewer, entry point, nstool
tools/re     Python reverse-engineering helpers (angr/capstone)
docs/        format notes
```
