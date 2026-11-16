# NetStorm data formats

Reverse-engineered from the community build 10.82 V13 (`netstorm.game` built
2026-04-13). Everything is little-endian. Nothing here needs the original
source code; it all comes from the files plus decompiling `netstorm.game`.

## `netstorm.tarc` — script archive (TAFF)

Contains all the game's design data as **plain-text scripts**:

| Extension | Count | What it is |
|-----------|-------|------------|
| `.type`    | 124 | Unit/building/effect definitions: stats, flags, animation frames |
| `.fort`    | 32  | Mission / fort layouts |
| `.english` | 90  | Mission text, tutorials, help, UI strings |

Header (44 bytes):

```
char  magic[16]     "TAFF v0.2\x1a", zero padded
u32   ?             2
u32   count
u32   ?             2
u32   offsetTable   count x u32 (offsets into the name block)
u32   namesOff
u32   namesSize     { u32 dataOff; u32 size; char name[]; '\0' } x count
u32   dataBase      entry data is at dataBase + dataOff
```

Every entry is XOR-encrypted with the repeating 13-byte key **`mydoghasfleas`**,
position counted from the start of the entry (`BaseFile.cpp`, function at
`0x417ba0`). `d/setup.cfg` uses the same cipher.

## `.type` scripts

```
typename windArcher constructor
typeflags createsisland emplacement shadow saveFrame;
{
    description="Crossbow";
    maxHitPoints = 480;
    range = 16;
    cost = 600;
    foot_x = 3;  foot_y = 3;
    orientations = 20;
}
L00 : default : "warch1.gif" #00 : "warch1a.gif" #71;
```

- Cluster IDs are letters (`A`..`Z`, `AA`..) + a 2-digit variation.
- Each frame line has one or two image references; the second is the shadow.
- 49 distinct property keys across all types. `nstool <dir> types` prints a summary.

## `d/_shapes.shp` — sprite database

The original memory-maps this file (`winUtil.cpp`, `0x530ac0`) and uses it in
place as `masterShapeDatabasePtr` (`RiftType.cpp`).

```
At file start, 119 "sets" back to back:
  char magic[4] = "1.10"
  u32  frameCount
  { u32 offset; u32 0; } frames[frameCount]     offset relative to the set start
```

Each frame offset points at a **frame body**. The 36 bytes before it are a header:

```
f32  extents[6]        iso footprint / placement extents (not decoded yet)
i16  width, height     logical size
i16  hotX, hotY        anchor
i16  ?, ?
```

Frame body (layout from the hand-written blitter at `0x5338e2`):

```
u8   ?[8]
i32  x0, y0, x1, y1    bounding box of drawn pixels, relative to the anchor
then (y1-y0+1) RLE rows, each a code stream ending in 0:
  0            end of row
  1, n         skip n transparent pixels
  even 2n, c   n pixels of colour c
  odd  2n+1    n literal pixels follow
```

Pixels are 8-bit palette indices. The blitter can pass them through a 256-byte
remap table first (`0x51eb30`). `d/!color.dat` holds 9 such tables, and they
are **lighting/effect tables, not team colours**: brighten, darken, black
silhouette, red damage flash, identity (table 4), etc.

All 4,124 frames decode cleanly (largest 510 x 511).

### Mapping sets to types

The engine has a fixed table of 119 type names (pointer table at
`0x60dae0`: `dude, sunArcher, sunAviary, ...`). **Sprite set N belongs to
entry N.** Verified for 115/119 sets by frame count (the other 4 have no
matching script or have a couple of extra frames).

Within a set, frame k is the image for the k-th frame line of the `.type`
file. Types flagged `shadow`/`flyershadow` have a second block of the same
length holding each line's shadow. Shadow frames use only palette index 150
and are drawn as translucent black.

Lines labelled `baseframe` are the static body of a building; other lines in
the same type are **overlays** drawn on top of it (factory windows lighting
up, an archer's crossbow at each of its orientations).

## Islands

Islands sit on a rectangular grid of 16x11-pixel cells. Terrain is
auto-tiled from type `isle` (set 27) by looking at the 8 neighbours:

| Cluster | Meaning |
|---------|---------|
| AA | filled (6 variants per element) |
| BF / CG / DH / EI | left / top / right / bottom edge |
| FL / GM / HN / IO | outside corner top-left / top-right / bottom-right / bottom-left |
| AB / AC / AD / AE | inside corner, missing SW / NW / NE / SE diagonal |

Edge pieces have 2 variants per element; element order in the file is sun,
thunder, wind, rain. Tiles, buildings and fringe are anchored at the
bottom-right pixel of their cell/footprint; walkers anchor at their feet.
`fringe` (set 32) and `islandStalag` (set 25) hang under bottom-facing edges.

Ownership: an owned island's orange rim highlight (palette indices only used
by edge tiles) is recoloured to the player's colour. There is no separate
outline. Grass sets: wind = brown, rain = snow/ice, thunder = grey, the green
set is the common default.

## `.fort` missions

Written by the engine's generic serializer (`template.cpp`, `DataManager.cpp`).
All 32 shipped missions decode completely (`nstool <dir> forts`).

```
u8   'F' (version 70)
u8   flags (254 = extended)
then sections, each:  u16 length (including the 2 length bytes), payload
```

| # | Section | Contents |
|---|---------|----------|
| 0 | name | 4 bytes, u16 length, name ("Player" / "Unnamed") |
| 1 | State | 28 bytes |
| 4 | Chaff | the neutral world: 256 chunks = 16x16 chunks of 16x16 cells |
| 6 | Territory | 20 x 6-byte records: u8 ?, u8 flags (9 = used, 3 = empty), u8 u8 (seed?), u16 0 |
| 7 | TypeNames | u8 n, n x u32 name hash (each byte added at shift (i&3)*8) |
| 9 | Technology | |
| 10 | Money | f32 |
| 11 | Deck | |
| 15–34 | Terr00–Terr19 | each player territory's objects |

Chunk-list sections (Chaff, TerrNN): `u8 version`, then for each chunk in
raster order a marker byte. `99` ('c') = `u16 count` + objects; anything else
= empty chunk. Terr sections list only that territory's chunks; the
chunk-to-world map is built at runtime (the server places forts), so it is not
in the file.

Object record:

```
u8  pos      x << 4 | y within the chunk; the object's anchor = bottom-right
             cell of its footprint
u8  type     saved type id (index into TypeNames). >= 246: player land marker
             (player = 255 - type) followed by one value byte. 0: skip 1 byte.
then, depending on the type's flags (f1 = +0xe8, f2 = +0xec):
  f1 & 0x20     saveFrame        u8
  f1 & 0x1000   saveQA           u8
  f1 & 0x2000   saveQB           u16
  f2 & 0x4      bridge           u8 piece
  f2 & 0x4000   factory (v>=2)   u8
  f2 & 0x5d77cf00 (v>0)          u8 owner
  f1 & 0x20000  container        u8 n, then n x { u8 type, [QA], [QB], [u8 flags unless bomb] }
```

Implicit flags (RiftType.cpp `0x4c6347`), from the `group` property and
others: battery → f2|0x800, archer/cannon → f2|0x8000000, blocker →
f2|0x4000000; vortex/factory/walker/balloon → container; buried → saveQA.
Group enum: archer 0, cannon 1, blocker 2, aviary 3, flyer 4, battery 5,
fence 6, walker 7, balloon 8, misc 9, anti-air 10, none 11.

Land is mostly generated, not stored: objects flagged `createsisland`
(geysers, archers, cannons, blockers, batteries...) grow their own small
island; `island` objects are 3x3 pre-drawn islands. Player home islands
appear to be generated from the territory record (terrain builder, not yet
ported).

## Palettes

- In-game palette: 8-bit bitmap resource `RT_BITMAP #139` in `netstorm.game`
  (confirmed against in-game screenshots).
- `GamePalSpec = "{DataDir}\{local.1}.COL"`, chosen per fort via `fortPal`.
- `.COL` files: `u32 size (0x308)`, `u32 magic (0xB123)`, 256 x RGB. The ones
  shipped in `d/` belong to the help-screen GIFs and title screens.
- Optional "ascendancy" palettes `windy/rainy/thundery/sunny.col` (not shipped).

## Networking (from imports/strings, not decoded yet)

UDP + TCP (WinSock), legacy IPX/SPX. Community servers on port 6800
(`d/!rootservers.dat`). The port will use its own protocol; see ROADMAP.
