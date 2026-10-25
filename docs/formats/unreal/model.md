# Unreal Engine 1: models (BSP)

Status: **implemented** (`src/engines/unreal/model.cpp`). Geometry
**checked on The Fallen** (October 2026): all 74 maps read and draw. The
lighting data after the vertices is tested on models built by the unit
tests, **not yet on The Fallen's**. Sources as in [package.md](package.md).

A level's geometry is a Model object in the map package: of its models,
Reluva takes the one with the most BSP nodes (each brush also has a Model,
with polygons but no nodes). Coordinates are left-handed: X forward, Y
right, Z up; Reluva negates Y for its right-handed renderer.

## Layout

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) |
| f32 × 6, u8 | bounding box: min, max, whether valid |
| f32 × 3 (+ f32) | bounding sphere: centre, and from version 62 the radius |
| compact + f32 × 3 each | vectors (normals, texture axes) |
| compact + f32 × 3 each | points |
| compact + node each | BSP nodes, below |
| compact + surface each | surfaces, below |
| compact + vertex each | vertices: compact point index, compact side index |
| i32 | shared sides |
| i32 | zone count |
| zone each | zones, below |
| compact | the Polys object (the brushes' polygons the BSP was built from) |
| compact + index each | light map indices, below |
| compact + bytes | light bits: the shadow bitmaps ([lighting.md](lighting.md)) |
| compact + 25 bytes each | bounds (boxes) |
| compact + i32 each | leaf hulls |
| compact + leaf each | leaves: compact zone, compact permeating lights, compact volumetric lights, u64 visible zones |
| compact + compact each | lights: object references to light actors, each lightmap's list ending in 0 |
| i32 | root outside |
| i32 | linked |

Brushes' own models stop after the vertices. Everything from the zones
on is read separately from the geometry: if it doesn't read (or leaves
bytes over, which `ue-check` reports) the level still draws, without
lightmaps.

### Zone

| Type | Meaning |
|---|---|
| compact | the ZoneInfo actor, an object reference (zone 0: the LevelInfo) |
| u64 | connectivity: zones this one touches |
| u64 | visibility: zones it can see |

### Light map index

| Type | Meaning |
|---|---|
| i32 | offset of its bitmaps in the light bits |
| f32 × 3 | pan: where the sample grid starts |
| compact × 2 | samples across (uClamp) and down (vClamp) |
| f32 × 2 | U and V scale: texture units between samples |
| i32 | its first light in the lights list (-1: none) |

How the pan and scales place the grid is worked out in
[lighting.md](lighting.md).

### Node

| Type | Meaning |
|---|---|
| f32 × 4 | plane: normal, distance |
| u64 | zone mask |
| u8 | node flags |
| compact × 7 | first vertex (into the vertices), surface, back, front, coplanar node, collision bound, render bound |
| u8 × 2 | zones on each side |
| u8 | vertex count: a node with 3 or more is a convex polygon |
| i32 × 2 | leaves on each side |

### Surface

| Type | Meaning |
|---|---|
| compact | texture, an object reference |
| u32 | poly flags |
| compact × 4 | base point (into points), normal, texture U and V axes (into vectors) |
| compact × 2 | lightmap, brush polygon |
| i16 × 2 | pan U, V |
| compact | actor (the brush it came from), an object reference |

A polygon point P's texture coordinates, in texels, are
`(P - base) · U + panU` and `(P - base) · V + panV`, divided by the
texture's size.

Poly flags Reluva uses: 0x1 invisible, 0x2 masked, 0x4 translucent,
0x40 modulated, 0x80 fake backdrop (sky shows through), 0x800 no
smoothing, 0x400000 unlit, 0x4000000 zone portal.

The reader checks every index against its array; when a game's layout
differs (another version, a licensee's change) that check is what fails,
and `reluva-tool ue-props <map> <model>` shows the bytes to compare.
