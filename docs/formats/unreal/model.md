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

In every one of The Fallen's maps each zone's connectivity is only
itself and its visibility (and each leaf's visible zones) is every bit
set: they say nothing. What a view can see is worked out each frame
instead, from the zone portals, as UE1's renderer did:

- The camera's zone: down the BSP from the root to a leaf, the zone on
  the side of the last node it's on. No leaf there (inside solid, as the
  free camera is at 0, 0, 0) and every zone counts as seen.
- A portal is a polygon of a surface flagged 0x4000000 whose node has
  different zones on its two sides. From the camera's zone, each portal
  on screen leads into the zone beyond it, seen through the portal's
  rectangle on screen (its part in front of the eye, projected) within
  the rectangle the zone it's seen from was; the zone beyond is seen
  through that, and goes on to its own portals. A zone reached again
  through more of the screen goes round again with the larger rectangle.
- Within 32 units of a portal's box, walking through it, the portal is
  edge-on and covers nothing, while the zone beyond fills half the view:
  there, it's seen through the whole of the rectangle.
- A solid surface seen through (translucent, masked, modulated or
  invisible) is a window: if going back through it from its centre
  comes out of the solid within 512 units, in another zone, that's a
  portal too. Further than that and what's beyond isn't drawn: one of
  M01_KiraL1A's floors is mirrored and translucent over more than a
  thousand units of solid, and the rooms below showed through it.

A polygon is drawn when the zone in front of its node is seen (and the
one behind it, for two-sided, non-solid and semisolid surfaces, or when
the front's is zone 0). Only the level's own polygons are left out:
movers and actors are drawn wherever they are. Compared with drawing
everything, the views from walks through six maps were the same but for
single pixels along seams and that floor, with fewer draws: about a
quarter of them on M06_WorfL1B, half on M03_KiraL2 and the Promenade,
most on CM06_SW_Capture (Z in the viewer draws them all).

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

Poly flags Reluva uses: 0x1 invisible, 0x2 masked, 0x4 translucent, 0x8
not solid, 0x20 semisolid,
0x40 modulated, 0x80 fake backdrop (sky shows through), 0x100
two-sided, 0x800 no smoothing, 0x400000 unlit, 0x4000000 zone portal.

The reader checks every index against its array; when a game's layout
differs (another version, a licensee's change) that check is what fails,
and `reluva-tool ue-props <map> <model>` shows the bytes to compare.
