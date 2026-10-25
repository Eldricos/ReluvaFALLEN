# Unreal Engine 1: models (BSP)

Status: **read up to the vertices** (`src/engines/unreal/model.cpp`),
which is what drawing a level's geometry needs; tested on models built by
the unit tests, **not yet on The Fallen's maps**. Sources as in
[package.md](package.md).

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
| ... | zones, the Polys object, lightmaps, light bits, bounds, leaf hulls, leaves, lights, ... (not read yet) |

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
