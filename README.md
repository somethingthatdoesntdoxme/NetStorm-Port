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
./build/netstorm --data "/path/to/Netstorm"            # world view (F2: sprite viewer)
./build/netstorm --data "/path/to/Netstorm" --mission rainvsrain
./build/netstorm --data "/path/to/Netstorm" --fullscreen
./build/nstool "/path/to/Netstorm" info
./build/nstool "/path/to/Netstorm" types               # unit/building stats
./build/nstool "/path/to/Netstorm" dump-sprites out/   # all frames as PNG (for upscaling)
./build/nstool "/path/to/Netstorm" extract-scripts out/
./build/nstool "/path/to/Netstorm" forts                 # decode every mission
```

World controls: click your priest to walk/build (temples, workshops, altars), click a golem then a
downed enemy priest to capture him, click a workshop to produce units, left click drop bridge piece, right click / R rotate, B let the AI
place one, drag or WASD pan, wheel zoom, M / [ ] cycle missions, N test layout, G grid.

The left sidebar holds Storm Power (with income per minute), the golem button, the next bridge
pieces (click the big one to rotate), units ready to drop, the build grid for whatever priest or
workshop is selected, and a minimap (click or drag to move the view). Hover anything for a
tooltip. The strip beside MAP is the balance of power (hover for percentages). Keys: 1-6 pick
a ready unit, Space find your priest, H jump home, Tab next golem. Your priest always has a
gold marker over him, a card in the sidebar (click it to jump to him), a ring on the minimap,
and an arrow at the screen edge when he's out of view.

Controllers (Steam Deck, Xbox, PlayStation) work everywhere, menus included:

| Input | Action | Input | Action |
|---|---|---|---|
| Left stick | cursor | A | click |
| Right stick / D-pad | pan | B | back / cancel |
| LT / RT | zoom out / in | X | rotate bridge piece |
| Hold RB + stick | radial menu of the selection's actions | Y | find your priest |
| LB | next golem | Start | menu |
| Select | AI lays one bridge piece | | |

On the Deck, use the "Gamepad" controller layout for the non-Steam shortcut. Games open on your temple at roughly the original's scale. The sidebar art is drawn in
code; unit pictures come from your install.

Sprite viewer controls: ←/→ change sprite set, PgUp/PgDn jump 10, mouse wheel zoom,
drag to pan, F sharp/smooth filtering, T cycle team colours, Space animate,
F11 fullscreen.

## Tests

```sh
cmake -S . -B build -DNETSTORM_DATA=/path/to/Netstorm && cmake --build build && (cd build && ctest)
```

## Layout

```
src/core     file IO, PNG writer
src/data     archive, sprite DB, palettes, .type parser   (no SDL)
src/render   SDL2 renderer, camera, sprite texture cache
src/app      viewer, entry point, nstool
tools/re     Python reverse-engineering helpers (angr/capstone)
docs/        format notes
```

## Skirmish and LAN / internet battles

- **Skirmish** (title screen): set up to 8 players - you plus AIs - with a fury (wind / rain / thunder) and team each,
  map size, starting Storm Power and number of neutral islands. Maps are generated from the setup.
- **LAN / Internet**: one player chooses *Host a battle*, opens seats by clicking slots to *Human*, and presses
  *Start* once everyone's in. Others pick the battle from the list (found automatically on the same network) or type
  the host's address. Internet play: the host forwards **TCP port 27100**. LAN discovery uses UDP port 27101.
- Battles are deterministic lockstep: every machine simulates the same game and only orders cross the network, so
  Windows, Linux and Steam Deck players can play together. Desyncs are detected and shown; a player who drops out
  surrenders and the battle carries on.
- Test it yourself: `tools/test/lan.py --data <install>` plays real host/client processes against each other.

### Internet play without port forwarding (relay)

Everyone connects *out* to a small relay server, so nobody needs to touch their router:

1. Run `netstorm-relay` (built alongside the game, no graphics, tiny) on any machine everyone can reach - a cheap VPS
   is ideal; a home machine works if that one person forwards **TCP 27102** once.
2. Host: *LAN / Internet* -> type the relay's address in **RELAY** -> **HOST ROOM**. The lobby shows a 5-letter room code.
3. Friends: type the same relay address and the **ROOM CODE** -> **JOIN ROOM**.

### Chat and reconnecting

- Press **Enter** to chat, in the lobby and in battle.
- If someone's connection drops, their army stands idle and their seat is kept for 3 minutes; their game reconnects
  by itself (directly or through the relay), replays the battle from the start to catch up, and carries on.
  If they don't make it back in time, their army surrenders.
