# Unreal Engine 1: meshes (Mesh, LodMesh)

Status: **implemented** for the layout below (`src/engines/unreal/mesh.cpp`,
drawn by `mesh_render.cpp` in the Maps and Meshes pages), tested on
meshes built by the unit tests. **The Fallen's meshes differ**: on its
data (`ue-check`, October 2026) none of its 455 LodMeshes and 9 Meshes
read. Some fail with plausible frame counts read near the end
(ARDSecurityCameraBaseMesh: 45 frames of 46 vertices) but an empty vertex
array near the start, so the change is likely early in the layout.
`reluva-tool ue-probe <game folder> <mesh>` maps a mesh's data (where
its lazy arrays sit, its first and last bytes) to work it out.

### What The Fallen changes (worked out so far)

From `ue-probe` dumps of ARD.u's ARDSecurityCameraBaseMesh and
ARD_KiraCellLight_On (LodMeshes) and Shared.u's Female (a Mesh),
version 73, October 2026:

- **As documented**: the bounding box and sphere; the lazy vertex and
  triangle arrays; the animation sequences (Female's 274, names, frames
  and 30 fps rates all read); connects, the second box and sphere, vertex
  links and textures. In a LodMesh everything from the faces to the end
  too: KiraCellLight's faces, 24 collapse-wedge entries, 24 wedges (each
  of vertices 0-11 twice), 3 materials (one unlit, 0x00400000), no special
  faces, 12 model and 0 special vertices, the level-of-detail settings,
  no remap entries, 12 old frame vertices, ending exactly at the end.
- **Empty**: the vertex and triangle lazy arrays, in all three. The
  vertices are kept elsewhere.
- **Inserted** after the textures, where the per-frame bounding boxes
  were: what looks like the model as imported. KiraCellLight's begins:
  i32 1; a string with a 16-bit length ("covered lightbusted on"); i32 12
  (its vertex count); f32 0.2456; i32 2 and i32 2 (its materials); i32
  64; per material its image file and group (`CoveredLightBustedOn_01.pcx`,
  `NOGROUP`, 16-bit lengths) and i32 triangle count (8, 6); i32 3, 3, 10;
  some floats; i32 14 twice (its triangles); then 20-byte entries
  starting (2, 1, 0), (5, 4, 3), (8, 7, 6), which look like triangles.
  ARDSecurityCameraBaseMesh's is the same shape (no name, 30, one
  material of 46 triangles). The rest of this block, presumably holding
  the vertex positions, is still to be read from a whole dump
  (`ue-probe ... -all`).

Sources: public community documentation of the UE1 mesh formats (as for
[package.md](package.md)). Mesh importer settings (Scale, Origin,
RotOrigin) as UnrealEd's `#exec MESHMAP`/`MESH` documentation describes
them.

Meshes are vertex-animated: every frame stores every vertex. A lazy array
(below) has, from version 63, the file offset just past it first.

## Mesh

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) |
| 25 + 16 bytes | bounding box and sphere (12 bytes before version 62) |
| lazy array | vertices, all frames' (frame f's vertex v at f * FrameVerts + v) |
| lazy array of 20 bytes | triangles (empty in a LodMesh) |
| array | animation sequences |
| lazy array of 8 bytes | connects (each vertex's triangles) |
| 25 + 16 bytes | bounding box and sphere again |
| lazy array of i32 | vertex links |
| array of compact | textures: object references, by texture index |
| array of 25 bytes | each frame's bounding box |
| array of 16 bytes | each frame's bounding sphere |
| i32 × 2 | FrameVerts, AnimFrames |
| u32 × 2 | AND and OR of the triangles' flags |
| f32 × 3 | Scale |
| f32 × 3 | Origin |
| i32 × 3 | RotOrigin (pitch, yaw, roll) |
| i32 × 2 | current polygon and vertex (editor) |
| f32, or array of f32 | texture level of detail: version 65 one, from 66 an array |

**Vertices** are 4 bytes: X in bits 0-10, Y in 11-21, Z in 22-31, each
signed. Some UE1 games store 8 (16 bits each, then 2 unused); the size is
told by the lazy array's end offset, and `ue-check` counts any 8-byte
meshes.

**Triangle**: u16 × 3 vertices; 3 × (u8 U, u8 V) texture coordinates
(0-255 across the texture); u32 flags (poly flags: masked, translucent,
modulated, unlit, invisible); i32 texture index.

**Animation sequence**: compact name, compact group (names); i32 first
frame; i32 frame count; an array of notifications (f32 time, compact
function name); f32 rate (frames per second).

A vertex is placed in the mesh's own space as
`R(RotOrigin) * (Scale * (v - Origin))`, scales per axis.

## LodMesh

A Mesh (above, triangles empty), then:

| Type | Meaning |
|---|---|
| array of u16 | collapse point thus |
| array of u16 | face level |
| array of 8 bytes | faces: u16 × 3 wedges, u16 material |
| array of u16 | collapse wedge thus |
| array of 4 bytes | wedges: u16 vertex, u8 U, u8 V |
| array of 8 bytes | materials: u32 flags, i32 texture index |
| array of 8 bytes | special faces (the weapon triangle) |
| i32 × 2 | model vertices, special vertices |
| f32 × 3, i32, f32 × 2 | level-of-detail settings |
| array of u16 | remap anim verts |
| i32 | old frame verts |

A face's corners are wedges (a vertex and texture coordinates); a frame's
special vertices come before the model's, so a wedge's vertex is
`special vertices + wedge vertex` in the frame (an assumption; The Fallen
will tell, as most meshes have none).

## Checks

The reader refuses a mesh whose vertex count isn't FrameVerts x
AnimFrames, whose triangles name vertices past a frame, or whose
animations run past the frames, which is what a layout off for a game's
version would give.

## Drawing

Each mesh is uploaded once: every (vertex, texture coordinates) pair per
frame, with smooth normals (turned to point outward, as the triangles'
winding isn't certain). A frame, or a blend between two, is drawn by
offsetting into them. An actor's skins: MultiSkins[i] for texture index
i, else Skin, else the mesh's own texture. Its light: the level's lights
reaching where it stands, part all round, part from their average
direction (no shadows yet).

Where a placed actor puts its mesh: `Location + PrePivot`, turned by
Rotation, scaled by DrawScale. Its animation: AnimSequence, from
AnimFrame (0-1 through it); still unless N is pressed in the Maps page.
