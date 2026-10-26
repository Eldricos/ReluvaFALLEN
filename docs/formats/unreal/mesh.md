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

**Which way up.** The block's float copy of the vertices is turned from
the packed ones: in KiraCellLight packed (313, 1016, 26) is float
(9.62, 1.62, -31.19), i.e. after Scale x stays, y takes z and z takes
-y (a quarter turn about X). Each mesh's turn is found from its own
float copy: of the 24 axis-aligned turns, the one taking its first frame
(by Scale and Origin) closest onto the `(FrameVerts, FrameVerts, 128)`
float array, if within 5% of its size on average. On KiraCellLight it
finds exactly the turn above.

Comparing in CM05_Kira (the viewers' **M** key steps through them): as
stored, the chairs lie on their backs; turned onto the float copy, they
and the people are upside down or flat; turned the **opposite** way (the
transpose), all stand as in the game. So that's what's drawn. Both are
compared about their middles, since the float copy can also be moved.

`ue-check` on The Fallen (October 2026) tallied the turns found: 159
meshes the KiraCellLight one, 8 none, a few each of five others, and 174
where none fitted, even about the middles. Those have no float copy: their
`(FrameVerts, FrameVerts, 128)` array holds one float per vertex, all 1.0
(weights, say), not three. Drawn as stored they lie on their backs as the
chairs did, so they're given the usual turn (KiraCellLight's) and the
same opposite-way drawing.

**Keyframed meshes.** Many animated meshes keep fewer `(FrameVerts,
FrameVerts, 1)` vertex arrays than they have frames: ARD_AmmoCrate 2 for
100 frames (its whole export is 8550 bytes, too small for 100), and
ARD_TortureMachine about 250 for 400, each followed by a `(9, 9, 64)`
array and, every few dozen, a `(k, k, 5)` header whose k is near an
animation's frame count. They look like keyframes with timing still to be
worked out; for now the first frame found is used for every frame
(`Mesh::keyFrames` says how many were found) so the mesh is at least drawn.

**`ue-check` on The Fallen** after this layout was added (October 2026):
306 of 455 LodMeshes and 4 of 9 Meshes read. JadziaDax failed only for an
animation (aaThumbTest2) running past its frames; such animations are now
left out rather than the mesh. The rest are still to be looked at with
`ue-probe`, which lists the block's array headers and every place the
standard tail could start.

A Mesh (not LodMesh) in this layout has no triangles in their standard
array either; they're presumably the block's triangle and corner arrays
(with texture coordinates somewhere in it), not read yet. The Fallen has
9 such meshes (Female, Male...).
