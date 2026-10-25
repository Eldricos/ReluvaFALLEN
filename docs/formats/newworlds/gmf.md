# Star Trek: New Worlds: `.gmf` maps

Status: **first look**. Stored in the planet archives (`alnwrld`, `alpwrld`,
`deswrld`, `icewrld`, `redwrld`, `vlcwrld`) with chunk tag `0x11`, 3 to 7 per
archive, mostly 380–470 KB (`kurt64.gmf` in `icewrld` is 166 KB). Each
archive also holds that planet's ground tiles (`geo/map/alp/alpin_01.geo`
…, 93 in `alpwrld`) and terrain textures (`lbp/txr/map/alp/green01`,
`cliff01`, `brid01`, …). Worked out from the start of `coverd2.gmf`, October
2026. `reluva-tool gmf-info <file>` walks the layout below.

All values little-endian.

```
0x00   u16 count of type-21 records   376 in coverd2, 12 in kurt64
0x02   u16 count of type-20 records   376, 12
0x04   u16 count of type-19 records   749, 6
0x06   u16 grid width, u16 height     64 x 64, 32 x 32
0x0a   the records, in that order: u16 type, u16 length, u16 ? (0 so far),
       then length bytes
...    data of unknown layout up to the end of the file
```

| Type | Length | Notes |
|---|---|---|
| 21 | 148–155 | one per tile (see below): small items `00 00 00 k 00 80 00`, many `02 ff ff ff ff ff` |
| 20 | 338–362 | one per tile: u32 its own number (0, 1, …), u32 0, u32 ?, u32 `0x18000`, u16 ?, then 8 × 8 byte grids (5, plus 0–3 8-byte items); values such as 2, 8, `0b`, `0d`, `0e`, perhaps terrain textures per cell |
| 19 | 22 | a 2 × 2 **block** of tiles: 6 bytes (0), then four u32 tile numbers, e.g. (0, 1, 3, 2), (4, 5, 6, 7), (8, 5, 6, 7) |

So the type-21 and type-20 counts are the number of distinct **tiles** (each
with two records), type 19 builds 2 × 2 **blocks** from them, and the header's
grid is in blocks: `kurt64` is 32 × 32 blocks = 64 × 64 tiles, as its name
says. Ground-tile models (`geo/map/...`) also have an 8 × 8 face grid.

After the records (checked on `kurt64`, from the file itself):

```
u32 N                         37092 in kurt64, 39607 in coverd2
N × u32                       lists, each ended by ff ff ff ff; entries are
                              24-bit values with bit 23 set, e.g. 00a0b277
grid w × h × 10 bytes         one entry per grid cell, row by row:
                                u16 block number (0-5 in kurt64)
                                u16 bits 12-15: resource deposit kind (1-6,
                                0 none); bits 0-11: 50-300 (see below)
                                u16 bits 12-15 repeat the kind; rest 0
                                u32 where the cell's list starts in the N u32s
1024 bytes                    a colour table: u32 entries, bytes B, G, R, 0,
                              ending with a zero entry (see Terrain codes)
```

The lists are stored column by column (cell (0, 1) starts at 47, cell (1, 0)
at 1380). `kurt64`'s grid is a field of block 1 with scattered variants
(blocks 2-5) and a patch of block 0, as a test map would be.

### Map positions in the game

The game gives map positions (the Tricorder's "TERRAIN AT <a:b>", and
messages such as a mine running out "at a, b") as **eighths of a block,
row first**: the first number divided by 8 is the grid row (as the file
stores rows), the second divided by 8 the column; so they run 0-511 on a
64 × 64 block map. Found from the mines reported exhausted in nine
recorded missions (F1, K1, K2, J1, J2, J3, J4, K7; see the play-*.md
pages): read this way all 15 lie on or beside a deposit block (all but one on one,
mean distance 0.1 blocks, against about 10 for random points), and every
mine position is 8n + 4, a block's centre. Read as 16 per block, or column
first, they fall nowhere near deposits. Which compass direction row 0 faces
isn't known.

### Resource deposits

Bits 12-15 of a grid cell's second value mark a resource deposit, kinds 1
to 6, and the third value's bits 12-15 always repeat it (every `icewrld`
map). They're the six raw ores the colony mines: the Tricorder names the
ore under the cursor ("<ore> AT <a:b>") when the cursor is over its relief
inset (over the main view it reads TERRAIN), and every such reading in the
Romulan recordings lies on a block of one kind:

| Kind | Ore | Readings |
|---|---|---|
| 1 | dilithium | R1, R12, J9R ([play-r12.md](play-r12.md), [play-j9r.md](play-j9r.md)) |
| 2 | talgonite | ten in J3R ([play-j3r.md](play-j3r.md)) |
| 3 | silicon | six in J4R ([play-j4r.md](play-j4r.md)) |
| 4 | kelbonite | R1, J4R, R9 |
| 5 | magnesite | two in C11, the column cut off by the box but only kind 5 fits ([play-c11.md](play-c11.md)) |
| 6 | dolamide | R9 ([play-r9.md](play-r9.md)) |

The tutorials name the six raw ores ([play-tutorials.md](play-tutorials.md)).
The colony's six materials (Dilithium, Polycomp, Thermoconc, Transalumin,
Nitrium, Duranium; [play-f1.md](play-f1.md)) are what a resource processor
makes of them; which ore makes which isn't known. The deposits are hand-painted patches: `j1` (a
multiplayer arena) is covered in large ones of every kind, `f1` (the first
Federation mission) has one cluster of all six plus two small patches, `r12`
only kind 1, `k12` kinds 1-3, and `kurt64` none.

The low 12 bits run 50-300 in steps of 12.5. Inside deposits they vary
(perhaps the amount); elsewhere they are mostly one value per map (50 in
`f1`, 250 in most others) with other values tracing the terrain. Unknown.

### Placed objects

The lists are the objects placed on the map (trees, rocks and the like):
each grid cell's list holds the objects in that block. Each entry is a u32:

| Bits | Meaning |
|---|---|
| 0-3 | x within the block, on a 16 x 16 grid (1024 units a square), from the west |
| 4-7 | y, from the south |
| 8-13 | unknown; evenly spread over 0-63 (a finer position?) |
| 14-15 | unknown variant: 1-3 for types 0 and 2, 0 or 3 for type 1 |
| 16-18 | type |
| 19-22 | evenly spread over 0-15: presumably a turn in sixteenths |
| 23 | always set |

The position was checked by continuity: trees grow in groves, and only this
reading (x low, y high, in map orientation) carries the groves across block
edges (correlation of neighbouring squares 0.21 across an edge, against
0.01-0.03 for the other readings, 0.32 inside a block). Trees are planted on
alternate squares, so they don't overlap (a tree is 2200 units across).
Oddly, no object in any `icewrld` map has x < 8 and y >= 8: one quarter of
every block is always empty, even in `kurt64`'s plain field, so those bits
may mean something else.

Types in `icewrld` (`f1`): 1 x25472 (trees, by their planting and size), 0
x9411 (scattered evenly: probably `rock`), 2 x1083 (probably `bush`), 4 x3
(probably `frn_spec`). Only the trees are sure. `tree_s` is a flat soft
shadow, probably drawn under trees. The scenery textures' alpha layers are
their outlines (transparency), not light maps.

## Tiles and ground models

Checked on all 31 maps of the six planet archives (`icewrld`, `deswrld`,
`alpwrld`, `alnwrld`, `redwrld`, `vlcwrld`):

- A tile's type-20 header's third value is the number of its ground model.
  The numbers don't follow the archive's order (which differs per planet:
  `deswrld` stores its bridges first) but one list of fixed places, the same
  on every planet, a missing model leaving its place empty (`<base>`: `ice`,
  `alpin`, `des`, `vol`, `red`, `alien`):

  | Numbers | Models |
  |---|---|
  | 0-15 | `<base>_01`-`16` |
  | 16 | `hole_01` (a plateau at the top level) |
  | 17, 18, 19, 20 | `<base>_21` (17 isn't used by any map), `<base>_22`, `<base>_20`, `<base>_18` |
  | 21-26 | `crack_03`, `04`, `05`, `06`, (none: there's no `crack_07`), `crack_08`: `crack_0N` at 18 + N |
  | 27-33 | `brid_01`, `brid_02`, `ramp`, `concrete`, `hole_03`, `hole_02`, `concact` |
  | 34 | `<base>_19` |
  | 35-38 | `<base>_23`-`26` |
  | 39, 40 | `<base>_17`; 40 is flat ground whose model isn't known |
  | 41-43 | `ball_01`-`03` (`alnwrld`) |
  | 44-47 | `water_01`-`04` (`alpwrld`); `alien_27`-`29` at 45-47 (`alnwrld`, order not sure) |
  | 48, 49 | `mount_01`, `mount_02` (`alpwrld`) |
  | 50, 51 | `arche_01`, `needle_0` (`deswrld`) |
  | 52, 53 | `icecave1`, `icecave2` (`icewrld`); `lavafall` at 52 (`vlcwrld`) |
  | 54 | `finger_1` (`redwrld`) |
  | after the places | the `lod/` models, in archive order (no map uses them) |

  Found by fitting every map: each tile's terrain layers give its heights
  (see [Terrain codes](#terrain-codes)), and neighbouring tiles' edges must
  meet (heights at 5 points within 80 units). `reluva-tool gmf-fit
  <planet.cat>` checks a numbering both ways, `-m` for a summary, `-o file`
  to try another, `-h` per number by heights. With this table 98-99.6 % of
  tile edges meet on every planet (the earlier table, with `alpwrld`'s own
  variant: 89-94 %) and 87 % of cells match the layers (65-74 %); the rest is
  mostly the layers' coarse steps on slopes. The shaped pieces are sure
  (base 0-16, 18, cracks, bridges, ramp, holes, the planets' own models);
  the flat ones (`<base>_17`-`20`, `_23`-`26`, `concrete`, `hole_02`) look
  alike to both checks, so which flat place each takes is the earlier
  table's guess where it still fits. Still unplaced: 40 (flat), 44 on
  `alnwrld`, 48-49 on `alnwrld` (top-level plateaus), 52-53 on `alpwrld`
  (a slope like `alpin_01` and a top-level plateau), 54 on `icewrld` (a
  middle-level plateau), each used by a few tiles.
- **Rotation:** bits 14-15 of the fourth value turn the model anticlockwise
  by that many quarter turns about the tile's centre. Bits 16-17 are one
  value per model number, the same on all 31 maps (`reluva-tool gmf-dump`
  and a count): 2 for the shaped pieces 0-5, 9, 11, 12, 15, `crack_03`,
  `05`, `06` and `brid_02`; 1 for the flat ones (6-8, 10, 13, 14, 18-20,
  34-40), `crack_04`, `ramp`, `hole_02`, `concact`, water, balls, `needle_0`
  and `icecave2`; 0 for `hole_01`, `concrete`, `hole_03`, `water_01`,
  `mount_01`-`02`, `arche_01`, `icecave1`/`lavafall` and `finger_1`; 3 for
  `crack_08` and `brid_01`. The 0s are the places nothing crosses (holes,
  rock formations), so perhaps a movement class (the Tricorder reads
  IMPASSABLE over some ground); not confirmed. They aren't a mirror or a
  height offset (tested). Each model appears in groups of 4
  records per rotation (`f1`: most models 16 times), which differ in their
  terrain codes. A few `ramp` tiles with bits 16-17 = 1 would fit better
  turned a further half turn; not enough to be sure.
- **Layout:** a block's four tiles go round the square in record order, at
  (0, 0), (1, 0), (1, 1), (0, 1) tiles from its corner. With rotation, 88.5 %
  of neighbouring tile edges in `f1` meet exactly (heights at 5 points within
  80 units); the next best layout scores 72 % and no rotation 46 %.
- Ground models span -4096..4096 in x and y (a tile is 8192 units, a block
  16384). Heights come in levels built into the models: 0, 3264 and 7360
  (`ice_08` is a plateau at 3264, `ice_09` at 7360; cliffs and ramps join
  them). The "flat `ice_19` tiles at 0 beside `ice_09` plateaus" that
  were most of the mismatches with the earlier table are number 18, which
  is `ice_22`, a corner slope down from the plateau.
- **Bit 24** of the fourth value is set only on `hole_02` tiles (in every
  `icewrld` map). `hole_02` is a bowl whose texture (`hole02`) is transparent
  round the hole, so ground must show through there: presumably the bit
  says so. Drawn that way, with the lowland ice texture underneath, the
  tiles look like the frozen-water pits in screenshots of `f1`: blue ice at
  the bottom of smooth, steep sides.
- In each type-21 record, the first four items count 0-3 starting at
  (rotation + 2) mod 4: the tile's sides, numbered in its own frame.

## Terrain codes

A type-20 record's five 8 x 8 grids are **not** textures, as first thought,
but a **height map**: how many of the five layers a cell has (non-zero
codes) is the ground's level there, 1 at height 0, 3 at 3264, 5 at 7360, 2
and 4 on slopes (about 1 + height / 1840). That holds for the model the
tile's number gives with the table above, at the tile's rotation, for 87 %
of cells on every planet; the layers' slopes are coarser than the models'
(a slope tile can match its mirror image as well), so slopes are told
apart by their edges instead. (Under the earlier, wrong numbering `hole_03`,
`ramp` and `concrete` tiles seemed to share grids.)
Each cell holds a code from 1 to 18, or 0 for none. Codes 1, 2, 4, 7, 10,
13, 15 and 18 appear only in a grid's first row and column, the others only
inside it, and they come in pairs (border 15 with inner 9, 2 with 11, 13
with 8, 4 with 12, 18 with 6, 1 with 17, 10 with 5).

The codes index the colour table at the end of the file (the same 18 colours
in every `icewrld` map; entry 0 is magenta). Coloured that way, plateaus are
light blue and lowland dark blue, and the first row and column draw a grid:
a minimap or terrain-type map. Cells are in map orientation (row 0 at the
south, column 0 at the west, after the tile's rotation). Higher layers cover
parts of a tile in staircase shapes; the 8 bytes after the grids are bit
masks of rows (`ff ff fc f0 f0 e0 c0 c0`) matching those shapes: the
levels, for the game's own use (movement, placing buildings).

### Textures

Neither the map nor the ground models name their textures. Each face numbers
a texture among its model's own (0-6, as for other models; see
[geo.md](geo.md#textures)). Findings so far:

- The 26 numbered tiles have the same geometry on every planet (`ice_01`,
  `alpin_01` and `des_01` are identical), and mostly the same texture
  numbering; where it differs (`ice_01`'s slope is texture 0, `alpin_01`'s
  texture 1), that fits textures numbered in name order, as for other
  models, with different names per planet.
- The planets' textures are named by role: `alpwrld` and `deswrld` have
  `low01`-`07`, `med01`-`02`, `top01`, `cliff01`-`05`, `ramp01`, and four
  plain variants (`green01`-`04`, `sand01`-`04`); `icewrld` has `ice01`-`08`,
  `blue01`-`04`, `snow01`-`02`, `rock01`-`03`, `cliff01`-`06`. Some carry a
  tile's number: `low07` is plain flat ground, like tile 07.
- The pictures are drawn for the tiles' texture coordinates: `alpin_01`'s
  low edge, slope and top faces fall on `cliff01`'s grass strip, rock and
  snow; `alpin_10`'s top and slope on `med02`'s snow and rock.
- Matching textures to faces by colour (across seams, or by the colour
  expected for snow, rock and grass) gave no consistent answer.

So the exact assignment is probably in the game itself. The viewer gives
each texture number a texture by the shape of its faces (high, middle or
low flat ground, or slope: `top`/`snow`, `med`/`rock`, `low`/`ice` or a
plain variant, `cliff`), preferring one numbered like the tile; holes,
cracks, ramps, bridges, water and concrete use their own
(`reluva-tool ground-textures <planet.cat>` lists the choices). Each
planet's `concrete` texture (`lbp/txr/map/<planet>/concrete`; the ice
world's is in `glblgame.cat`, not `icewrld.cat`) is square panels, blue-grey
on ice and tan on desert: the colonies' pads in the recordings.
`glblgame.cat`'s `conc_rng` model and texture (16 frames of a ring of
flames) are something else, an effect.
`cliff07`, which no model's name matches, may be the texture for the walls
between levels.

A ground model's own 8 x 8 grid (after its faces) lists, per cell, the faces
over it; cells overlap, so it's a lookup for heights or collisions, not
textures.

## Open questions

- Which textures the ground models' faces use (the viewer's are chosen by
  role).
- What the terrain codes' values mean, beyond the layers giving heights.
- The few unplaced numbers (see the table) and the flat models' exact places.
- How the game builds and textures the walls between levels.
- What bits 16-17 of the tile flags (one value per model) are for; the rest of the type-21 records; the grid
  entries' second value's low 12 bits; the placed objects' bits
  8-15 and the missing quarter.
- A full magnesite reading (kind 5); which ores make which of the six materials.

## Not in the map

Every part of the file is now accounted for: the tile, link and block
records, the placed objects, the grid (blocks and deposits) and the colour
table. The only parts not understood are per tile model (the type-21 links)
or per cell (the low bits above), so units, buildings and starting colonies
can't be stored here: they presumably come from the missions, which
[`level.dat`](level.md) names by a "parser short name" (`F1`, the map's
name) but whose scripts aren't in any archive (probably inside `STNW.exe`). The tiles numbered
`concrete` (30) aren't colony pads either: on every map they're single tiles or pairs, 24-105 per
map, and 68 % of them sit where another map has one too (cracks: about 50 %, ramps 22 %), so they
come with the maps' shared terrain. The pads under starting colonies come with the colony (see
[colony.md](colony.md#concrete)).

`reluva-tool gmf-dump <planet.cat> <dir>` writes each map's tiles (position, model number and
name, flags), grid and placed objects as CSV files, for this kind of study.
