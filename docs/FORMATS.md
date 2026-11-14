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

Pixels are 8-bit palette indices. Team colours go through a 256-byte remap
table first (`0x51eb30`); `d/!color.dat` holds 9 such tables.

All 4,124 frames decode cleanly (largest 510 x 511).

### Mapping sets to types — TODO

Sets correspond to `.type` definitions (frame count = frame lines, doubled for
types with the `shadow`/`flyershadow` flag), but the set order isn't
alphabetical. Next step: recover the registration order from `sub_4c7530`
(called from the shape loader at `0x4c9b40`).

## Palettes

- In-game palette: 8-bit bitmap resource `RT_BITMAP #139` in `netstorm.game`
  (provisional — matches grass/skin tones; verify against a real screenshot).
- `GamePalSpec = "{DataDir}\{local.1}.COL"`, chosen per fort via `fortPal`.
- `.COL` files: `u32 size (0x308)`, `u32 magic (0xB123)`, 256 x RGB. The ones
  shipped in `d/` belong to the help-screen GIFs and title screens.
- Optional "ascendancy" palettes `windy/rainy/thundery/sunny.col` (not shipped).

## Networking (from imports/strings, not decoded yet)

UDP + TCP (WinSock), legacy IPX/SPX. Community servers on port 6800
(`d/!rootservers.dat`). The port will use its own protocol; see ROADMAP.
