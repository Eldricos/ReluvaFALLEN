# id Tech 3: compiled maps (`maps/*.bsp`)

Status: **all lumps read and cross-checked** (`src/engines/idtech3/bsp.cpp`);
the light grid is kept raw. **Verified on all 67 retail Elite Force maps**
(`reluva-tool bsp-check`, October 2026): every one is version 46 with the
record sizes below, and 27 of them need the corrections described at the
end.

Sources: the GPL Quake III Arena source (`code/qcommon/qfiles.h`, the map
compiler and renderer) and the community's "Unofficial Quake 3 Map Specs".

All values are little-endian. Integers are 32-bit signed, floats IEEE
single precision.

## Header

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IBSP` |
| 4 | i32 | version, 46 |
| 8 | 17 × (i32 offset, i32 length) | the lumps, in the order below |

## Lumps

| # | Lump | Record size | Record |
|---|---|---|---|
| 0 | entities | text | `{ "key" "value" ... }` blocks, see [text.md](text.md); usually NUL-terminated |
| 1 | shaders | 72 | char[64] name, i32 surface flags, i32 content flags |
| 2 | planes | 16 | f32[3] normal, f32 distance |
| 3 | nodes | 36 | i32 plane, i32[2] children (negative = −(leaf+1)), i32[3] mins, i32[3] maxs |
| 4 | leafs | 48 | i32 cluster (−1 = outside), i32 area, i32[3] mins, i32[3] maxs, i32 first/count leaf surface, i32 first/count leaf brush |
| 5 | leaf surfaces | 4 | i32 surface |
| 6 | leaf brushes | 4 | i32 brush |
| 7 | models | 40 | f32[3] mins, f32[3] maxs, i32 first/count surface, i32 first/count brush. Model 0 is the world; entities use the others as `"model" "*n"` |
| 8 | brushes | 12 | i32 first side, i32 side count, i32 shader |
| 9 | brush sides | 8 | i32 plane, i32 shader |
| 10 | vertices | 44 | f32[3] position, f32[2] texture st, f32[2] lightmap st, f32[3] normal, u8[4] RGBA colour |
| 11 | indices | 4 | i32, relative to the surface's first vertex |
| 12 | fogs | 72 | char[64] shader, i32 brush, i32 visible side (−1 = none) |
| 13 | surfaces | 104 | see below |
| 14 | lightmaps | 49152 | 128×128 RGB |
| 15 | light grid | 8 | u8[3] ambient, u8[3] directed colour, u8 longitude, u8 latitude of the direction towards the light; see below |
| 16 | visibility | — | i32 cluster count, i32 bytes per cluster, then one bit per cluster pair: bit `to` of row `from` set when `to` can be seen from `from` |

### Surfaces

| Offset | Type | Meaning |
|---|---|---|
| 0 | i32 | shader |
| 4 | i32 | fog (−1 = none) |
| 8 | i32 | type: 1 polygon, 2 patch, 3 triangle soup (models), 4 flare |
| 12 | i32 × 2 | first vertex, vertex count |
| 20 | i32 × 2 | first index, index count |
| 28 | i32 | lightmap (negative = none) |
| 32 | i32 × 2 | position in the lightmap |
| 40 | i32 × 2 | size in the lightmap |
| 48 | f32[3] | lightmap origin in the world |
| 60 | f32[3][3] | lightmap s and t vectors and the surface normal; for patches, [0] and [1] are the patch's bounds instead |
| 96 | i32 × 2 | patch width and height in control points |

Patches are grids of quadratic Bézier patches: width × height control
points (both odd), each 3×3 block one patch, sharing edges. The engine
tessellates them; the file holds only the control points.

## The light grid

Light sampled on a regular grid, for lighting models. Cells are 64 × 64 ×
128 units unless the worldspawn entity's `gridsize` key says otherwise.
Grid points lie on multiples of the cell size inside the world model's
bounds: from ceil(mins / size) × size to floor(maxs / size) × size on each
axis, X fastest, then Y, then Z. If the lump's size doesn't match, the
original ignores the grid.

Colours are stored at half brightness like lightmaps (doubled, keeping the
hue). The direction is two angles × 2π/256: longitude (byte 6) from +Z,
latitude (byte 7) around it, giving (cos lat · sin lng, sin lat · sin lng,
cos lng).

To light a point the original blends the eight surrounding grid points by
distance, skipping any that are all black (inside walls) and rescaling
the rest, normalizes the blended direction, then adds 32 to the ambient
light (capped at 255). `src/engines/idtech3/light_grid.cpp`; read from the
GPL renderer's `tr_bsp.c` and `tr_light.c`.

## Collision

How Reluva moves things through a map (`collision.cpp`; the behaviour is
Quake III's, the code Reluva's own). Brushes are convex volumes, each the
intersection of the half-spaces behind its sides' planes, with the
contents of its shader (`solid`, `playerclip`, `water`...); the compiler
adds bevel planes so boxes slide along edges properly. A trace sweeps an
axis-aligned box from a start to an end: the box is made symmetric about
its centre, and each plane is pushed out by how far the box reaches along
its normal (the sum of `|normal[i]| * halfSize[i]`), which turns the box
into a point. Through the BSP tree, the swept box goes down whichever
sides of each node's plane it touches, to the leaves, and is clipped
against the leaves' brushes (each brush once per trace) whose contents
share a bit with the mask: the line is inside a brush between the last
plane it enters and the first it leaves. The move stops 1/8 unit short
of what it hits, so the next move doesn't start inside it. A trace that
starts inside a brush reports `startSolid`, and `allSolid` when it ends
inside too. Brush models (`*1`, `*2`... for doors and lifts) have no tree
of their own: their brushes are tested directly, in the model's own
coordinates.

Curved surfaces (patches) collide too, when their shader's contents
share a bit with the mask. They are found through the leaves' surface
lists, or a brush model's surfaces. Each patch is cut into triangles
once, when the map loads. The more its control points bend, the finer it
is cut: twice the square root of its largest bend in units, 2 to 16 cuts
per 3×3 block, so the triangles stay within about a tenth of a unit of
the curve. Each triangle collides like a thin brush, bounded by its own
plane facing both ways (half a unit out on each side), its three edges
(a tenth of a unit wide, which covers the thin wedge left between two
triangles on the inside of a bend, while the ridge it leaves on the
outside is about a hundredth of a unit) and its bounding box. A move starting inside one isn't held there, so nothing sticks to
the patch it stands on. As a big curve has thousands of triangles, they
are kept in runs of 16 (neighbours, as cut) under one box, and runs of
16 runs under another, so a trace skips most of them at once. Reluva's
own solution, not the original's code.

## What the reader checks

Every lump lies inside the file and holds whole records; every index into
another lump is in range (node planes and children, leaf ranges, model and
brush ranges, surface shaders, vertices and indices); a patch's size
matches its vertex count; vis data covers every cluster. Errors name the
record, the field and its value, e.g. `BSP surface 0: index 2 is vertex 3
(of 3)`. The rest of the engine relies on this and doesn't check again.

### Tolerated, as the original engine does

Some Elite Force maps hold values out of range that the original engine
accepts. Reluva corrects them the same way and counts them (`Bsp::fixes`,
shown in the map viewer and by `reluva-tool bsp-check`):

| Value | Original behaviour | Reluva sets |
|---|---|---|
| surface lightmap ≥ lightmap count | lit by its vertex colours | lightmap −1 |
| surface fog that doesn't exist | not checked | fog −1 |
| flare (or bad surface) with vertices or indices | never used | no vertices or indices |
| unknown surface type | (ioquake3 refuses the map) | type 0, never drawn |

The original's behaviour was read from the GPL Quake III Arena renderer
(`tr_bsp.c`, `tr_shader.c`). The last row is more lenient than Quake III,
since Elite Force's engine may differ; maps that need it show up in
`bsp-check`'s counts.
