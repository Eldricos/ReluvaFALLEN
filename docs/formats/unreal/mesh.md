# Unreal Engine 1: meshes (Mesh, LodMesh)

Status: **implemented** for the layout below (`src/engines/unreal/mesh.cpp`,
drawn by `mesh_render.cpp` in the Maps and Meshes pages), tested on
meshes built by the unit tests. **The Fallen's meshes are laid out
differently** (below): read for LodMeshes, checked on one of its meshes
whole; its classic Meshes not yet. `reluva-tool ue-probe <game folder>
<mesh>` maps a mesh's data (where its lazy arrays sit, every byte with
-all).

### The Fallen's layout: an imported-model block

Worked out from `ue-probe` dumps of ARD.u's ARDSecurityCameraBaseMesh and
ARD_KiraCellLight_On (LodMeshes; the latter dumped whole) and Shared.u's
Female (a Mesh), version 73, October 2026.

Everything documented above is there, but:

- **The vertex and triangle lazy arrays are empty.**
- **Between the textures and the per-frame bounding boxes** sits a block
  that looks like the model as imported into the editor. Its arrays are
  `(i32 count, i32 count again, i32 flags)` then the elements. In
  KiraCellLight (2121 bytes):
  - a header: i32 1; a string with a 16-bit length (its name, "covered
    lightbusted on"); i32 12 (its vertices); f32; i32 2 and 2 (its
    materials); i32 64; per material its image file and group
    (`CoveredLightBustedOn_01.pcx`, `NOGROUP`, 16-bit lengths) and i32
    triangle count (8, 6);
  - triangles, `(14, 14, 256)`: per triangle i32 x 3 corners and an i32
    material;
  - corners, `(42, 42, 64)`, pairs of i32;
  - **the vertices, `(12, 12, 1)`: 12 packed vertices** as the standard
    array's, then 8 zero bytes, i32 64, a bounding box (6 floats) and a
    float; the same again;
  - the vertices as floats, already placed by Scale and RotOrigin,
    `(12, 12, 128)`; per-vertex weights (1.0) and indices (0-11), the
    same way; names ("root", "FullBody"), more bounds.
- After the block **the standard fields resume** (per-frame bounding
  boxes onwards) and run to exactly the end.

So Reluva reads it as: the standard fields up to the textures; then, if
the standard layout doesn't hold (no vertices in their array), the start
of the standard tail is found from the end back as the place it reads
from to exactly the end, agreeing with itself (frame counts above 0, a
sane Scale and Origin, for a LodMesh FrameVerts = model + special
vertices and some faces); the vertices are the block's `(N, N, 1)` array
of N = FrameVerts x AnimFrames packed vertices, or one such array per
frame (which animated meshes are expected to use, still to be seen on
the data). On KiraCellLight this gives its 12 vertices, its 14 triangles
(8 with texture 0, 6 with texture 1, as the block's header counts) and
Scale (0.0307, 0.0307, 0.0614), with no bytes over.

A Mesh (not LodMesh) in this layout has no triangles in their standard
array either; they're presumably the block's triangle and corner arrays
(with texture coordinates somewhere in it), not read yet. The Fallen has
9 such meshes (Female, Male...).
