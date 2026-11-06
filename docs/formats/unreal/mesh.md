# Unreal Engine 1: meshes (Mesh, LodMesh)

Status: **implemented** for the layout below (`src/engines/unreal/mesh.cpp`,
drawn by `mesh_render.cpp` in the Maps and Meshes pages), tested on
meshes built by the unit tests. **The Fallen's meshes are laid out
differently** (below): read for LodMeshes and its classic Meshes,
checked on one of its meshes whole. `reluva-tool ue-probe <game folder>
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

Turned so, they stand up but face their right (in CM05_Kira and the
rest): imported meshes are also given three quarter turns of yaw (a
quarter turn back), to face ahead along X. The viewers' **Y** key steps
through the four.

**Keyframed meshes.** Many animated meshes keep fewer `(FrameVerts,
FrameVerts, 1)` vertex arrays than they have frames: ARD_AmmoCrate 2 for
100 frames, ARD_TortureMachine about 250 for 400. Each array is followed
by a `(9, 9, 64)` array, and every few dozen comes a `(k, k, 5)` header
whose k is near an animation's frame count. They're the clips' vertex
keys, timed in seconds (see "Clips by name, and vertex keys" below), and
every frame is filled from them. Checked: `ue-probe` gives
ARD_TortureMachine's 400 frames filled from its clips, 399 of them
unlike the first. ARD_AmmoCrate's 100 frames are its one still pose, as
no script animates it. `ue-check` counts no mesh left showing its first
frame for all of them (`Mesh::keyFrames`).

**`ue-check` on The Fallen** after this layout was added (October 2026):
306 of 455 LodMeshes and 4 of 9 Meshes read (all now: see Classic Meshes
below). JadziaDax failed only for an
animation (aaThumbTest2) running past its frames; such animations are now
left out rather than the mesh. The rest are still to be looked at with
`ue-probe`, which lists the block's array headers and every place the
standard tail could start.

**Classic Meshes** (not LodMesh): The Fallen has 9.
- **Six have geometry:** `TransporterTubeMesh`, `EMPulseRifleImpactMesh`,
  `JEM_Grenade` and the three `SR3_Corpse*`. Each keeps its triangles in
  the standard `Tris` lazy array, 20 bytes each: three u16 vertex
  numbers, three (u, v) byte pairs, u32 polygon flags and an i32
  texture. The connections and vertex links are in their standard lazy
  arrays too. The vertex array is empty: the vertices are in the
  imported-model block, as in the LodMeshes. The
  reader reads them so: the grenade's 14 vertices and 24 triangles; the
  tube's 70 and 120; the corpses' 187 vertices, 5 frames and 370
  triangles, two of them posed from 28 bones.
- **Female, Male and MaleTwo have none:** no vertices, no triangles,
  only a block of bones and animation clips (4, 274 and 111 clips).
  They're the shared animation sets the characters' meshes borrow from
  (`shared.Male`, above). `CTV_Player.AltMeshes` lists them, and
  `C3DInitAltMeshes` hands them to the C3D code. They're read as sets,
  not drawn, as the game doesn't draw them.

`ue-check` (November 2026): 455 of 455 LodMeshes and the 6 Meshes with
geometry read, none failed.

**Meshes built on a skeleton** (BajFanatic, GrigSentryDrone, Obsidian_Snow,
the corpses and the other characters that keep no packed vertices). Worked
out on the game's own files (October 2026); every one of the 402 skeletons
reads whole, and all 21 meshes with no packed vertices animate.

The block holds the bones as a tree, written depth first, before it the
mesh's name and textures' file names, after it its body parts' names
(`(3, 3, 10)` and "FullBody", "LowerTorso", "UpperTorso"). A bone:

| Field | |
|---|---|
| u16 length and the bone's name | `root`, `Bip01_Pelvis`... (some empty, some ending in a tab) |
| `(n, n, 8)` and n i32 | (0, 1) seen; not used |
| `(k, k, 128)` and k float xyz | the bone's vertices, in its own space (none for keyframed meshes' bones, whose packed frames have them) |
| `(k, k, 128)` and k float | their weights (a vertex shared by bones has some under 1) |
| `(k, k, 128)` and k i32 | which of the frame's vertices each is |
| 12 floats | a 4x3 matrix ("offset"; not used: see below) |
| i32 | 7 |
| 12 floats | the bone's placement in its parent's space: rows X, Y, Z, then the origin |
| i32 | 7 for root, 4 for most, 0 for some |
| `(c, c, f)` | c clips, one per animation |
| per clip: `(n, n, f)` and n x (time, x, y, z) floats | position keys: times in seconds, an offset to the placement's origin, in 3ds Max's axes (below) |
| per clip: `(m, m, f)` and m x (float time, 6 bytes) | rotation keys: a quaternion, below |
| per clip: 8 bytes | (0, 0), (0, 1), (1, 1) seen |
| `(h, h, 4)` | h children, whose records follow |
| after the children: 12 floats, i32 7, 10 floats, two `(n, n, f)` and n i32 | a matrix (identity), what may be joint limits (+-30 degrees), links to other bones (usually none) |

A rotation key's 6 bytes are a quaternion: x, y and z packed as 11, 11
and 10 bits (0 to the top of each standing for -1 to 1, as packed
vertices are), then w - 1 as a signed 16-bit fraction of 32767
(x²+y²+z²+w² comes to 1 on every key). w - 1 runs from -2 to 0, more
than 16 bits hold: for a negative w it wraps round, so a positive value
v stands for v - 65536 (w = 1 + (v - 65536) / 32767). Read as plain
signed, those keys came out with w near 1.5 (x, y and z leaving 0.46 for
it) and the bone flipped half a turn between them and the keys round
them: knees in the runs, FireRunR's right elbow.

**Posing.** A bone's place at time t of a clip: turned by its rotation
key's quaternion taken as rows of a matrix (i.e. by its conjugate, as row
vectors turn), then by its placement's turn, at its placement's origin
plus its position key; then its parent's, up to the root.

The position keys are in 3ds Max's axes, not the skeleton's: Z up, the
figure facing -Y with its right at -X. The skeleton's are Y up, the
figure facing -Z (the guns point that way) with its right at +X (the
right clavicle is at X +4), so a key (x, y, z) is the offset (-x, z, y).
The rotation keys are in the skeleton's axes already. Mostly it's the
pelvis that has these keys, so standing poses hardly show it. Taken as
they were, a crouch's drop of 27 units went backwards (the figure folded
its legs in the air, its hips at standing height), and a run's bob went
forwards and back. Taken in Max's axes, the crouch is down at the floor,
and the gun tips come within 5.8 units of the precomputed places on
average, against 12.4 (below: the crouched shot's from about 40 to 5). A vertex is the
weighted sum, over its bones, of its position there (`p * bone * parent *
...`, row vectors), in the same units as packed vertices after Scale and
Origin (KiraCellLight's one bone, `root`, gives exactly its packed
vertices so). Checked on BajFanatic by where the bones put the vertices
they share, which should agree: with the placements alone (and the
"offset" applied) 5.4 units apart on average, without the offset 1.5,
with clip 0's first keys 0.03, so the vertices are bound in that pose
(BajFanatic's is a T-pose) and the placements are just a pose.

**Clips and frames.** Clip i is the mesh's i-th animation in frame order;
its length in frames is its last key's time at 30 frames a second (one
frame if all its keys are at 0). They add up to the mesh's frames exactly:
GrigSentryDrone's 30 clips its 30 animations' 1626 frames, BajFanatic's 4
its BaseReference, Bomb, SuicideLunge and SuicideRun (50 frames; the other
314 animations it lists run past those and are left out). So every frame
is posed from the keys, interpolated (slerp for rotations), and drawn as
ordinary vertex animation.


**Shared animation sets.** The humanoids (Leeta, Nog, Rom, the Klingons,
BajFanatic, Obsidian_Snow...) keep a frame or a few of their own and list
some 270-320 animations past them (BaseRelax, BaseReady, CrchWalkF...).
Those are in three Mesh exports in Shared.u with a skeleton (the same
Biped bone names) and clips but no vertices: Male (274 clips, 10256
frames end to end), MaleTwo (111, 2490) and Female (4, 69). A character's
list is one of theirs (Leeta's is Female's, the Klingons' Male's,
BajFanatic's MaleTwo's), and an animation in it is a clip of a set whose
frames it is exactly: the Male-list ones on Male's timeline (BaseRelax at
409), MaleTwo's own on MaleTwo's (its BaseRelax at 324). The viewer gives
a character the clips its placed actors play, posing the character's own
bones with the set's keys for the bones of the same name (rendered in
checking: Leeta and BajFanatic standing in BaseRelax, a Klingon and
BajFanatic in CrchWalkF). On The Fallen's levels: 277 placed characters'
animations come from the sets, 61 are their own, and 103 (the Grigari
drones' "BaseReady", from their class) aren't in their mesh's list at all;
the viewer plays their idle. Nearly all placed characters ask for a
one-frame pose (BaseRelax, BaseReady), which the game's scripts follow
with an idle loop, so the viewer plays Idle1 (IdleReady1 for a ready pose)
in its place; every humanoid list has both.

**Clips by name, and vertex keys.** Before the bones the block names the
mesh's own animations, in clip order: `(c, c, 4)` and c x (u16 length and
name, f32 seconds, i32 2, u16 length and group). After them another
`(c, c, 4)` holds a run of vertex keys per clip, most empty: `(k, k, 5)`
and k x (f32 time, `(n, n, 1)` and n packed vertices, `(m, m, 64)` and m x
12 bytes, a 24-byte bounding box). A clip with keys is vertex animation
(ARD_AmmoCrate's 2 keys over 100 frames, the creatures, Quark's Blink,
Eyebrows and Phonemes); the others are the bones' clip of the same index.
Each of the mesh's animations is the clip of its name, its frames at
their times (30 a second), between keys blended. The per-frame vertex
arrays found earlier are these keys, which is why "keyframed" meshes had
fewer than their frames, and why taking them in order as frames went
wrong where a clip's keys aren't one a frame (Baj_ImpulseShip's 2-key
one-frame Hover shifted everything after it). Checked: the 243 meshes
that keep one array of every frame match the clips to within a packed
unit; the 180 that keep only keys or bones are now filled every frame
from them; the 38 glass and debris fragments in Shared.u have an i32 0
before their standard fields and otherwise read as standard meshes.

**The face table.** Straight after the vertex keys' runs comes the
engine's own face table (Engine.dll's `SANS`): `(c, c, 4)`, one entry for
each of the mesh's clips by index, each an i32 attachment point, `(n, n,
64)` and n i32 vertices (the packed vertices' numbering), then `(k, k, 5)`
keys, each an f32 time and `(n, n, 64)` and n x 3 f32: where each listed
vertex goes, in the attachment point's frame, in the skeleton's units.
Clips that aren't the face's have empty entries; the shared sets have
none. 50 meshes have keys there: every character with Blink, Eyebrows
and Phonemes (Mouth on Morn and ObanakWraith) in PLYR.u and PAWNAI.u, and
FED.u's wounded ensign. Their point is `FaceAttach` (`FaceAttch` on Morn
and an ensign): on a triangle, kind 7 (its own turn and offset not used),
so the frame is the triangle's on the posed mesh: X from its first
corner to its third, Y the normal (first edge across the second), Z = X x
Y, from the first corner. They're the clips' face keys again, cut to the
vertices that move and kept in the head's frame. Checked: Kira's (point
7, triangle 588; Blink 8 vertices and 2 keys, Eyebrows 12 and 3, Phonemes
42 and 8) put at their first key on her posed face, the listed vertices
are where her bones put them, to within 0.001 units. After the table
come `(1)` and `(n, n, 64)` vertices (the ones whose posed places the
engine keeps), then the bones.

**Shared sets name their clips too** (Female: BaseReference, FireCrch,
RunF, WalkF; MaleTwo its 111; Male its 274), so a borrowed sequence must
also be the clip of its name. Female's own clips are the start of her
timeline (Leeta's RunF at frame 15), the rest of a Female list points
into Male's. With the face-keyframed humanoids counted (Quark, Garak,
the ensigns, Kira, Dax, Worf, Odo: bones for the body, vertex keys for
Blink, Eyebrows and Phonemes), all 17440 sequences the characters list
past their own frames are a set's clip, and the levels' placed
characters play 814 borrowed animations and 85 of their own. Rendered in
checking: Kira at a low console (SitOpConLo), Worf in WoundedIdle, Dax
running (runf), an ensign sneaking (SneakF). The Meshes page steps
through a character's shared animations too, borrowing each when picked.

**Attachment points.** After the block's groups (`FullBody`,
`LowerTorso`, `UpperTorso`: `(g, g, 10)` and names, 16-bit lengths) come
two empty `(0, 0, 10)` arrays, then `(n, n, 5)` attachment points. Each
point is:
- a name: `RightHand`, `Face`, `GunTip`, `FirePoint`, `KickPoint`...;
- an i32 type, 1 or 2;
- an i32 triangle;
- 12 floats: the point's turn, rows X Y Z, then its offset;
- an i32 kind, 4 or 7.

What the type means:
- **Type 1** is on a triangle and moves with it. On Kira, 5 of 8
  numbers are past her 388 vertices but within her 772 triangles. The
  prison rat's `BitePoint` (no turn, no offset) is a triangle at the end
  of its body on its middle line. The phaser rifle's `FirePoint` (offset
  half a unit) is a triangle at the very end of its barrel.
- **Type 2** is from the mesh's own origin, in the skeleton's units
  (Scale applied, the import turn not), as the precomputed places below
  are. The breather's `Face` is at its origin. The phaser rifle lies
  along its Z, the muzzle at -17, the stock at +16: its `RightHand` grip
  is 3 units below at +10 (the pistol grip at the back) and its
  `LeftHand` beside it at -2.8, forward of the middle. Read in the float
  copy's space instead (the rifle has none: it's the usual turn guessed),
  the right hand is 10 units below, outside the rifle. Over all 67
  type 2 points, 29 are nearer their mesh's surface read so, 8 read the
  float copy's way. Engine.dll settles it: a type 2 point is the stored
  matrix as it is, in the same space as the skeleton's posed vertices
  (`C3DGetWldToC3DTM` maps the world into that space; the mesh's Scale,
  Origin and RotOrigin aren't read there). Reluva places it as it
  places those vertices, so a point and its mesh turn together whatever
  turn is guessed for the mesh.

Then come `(s, s, 5)`, the shared animation sets the mesh's animations
come from (`shared.MaleTwo`, `shared.Male`), and `(m, m, 4)` places
worked out in advance. Each place is a name, 12 floats and an i32, from
the origin: the Jem'Hadar's gun tip in each firing pose,
`FireGunTipLoc`, `FireCrchGunTipLoc`, `FireShldrGunTipLoc`... A crouched
shot's tip is lowest and a shot from the shoulder highest.

They're in the skeleton's units (Scale applied, from the origin, the
import turn not applied), not the float copy's. Checked: JemHaddar has a
GunTip of its own (type 1, on its gun). Posed through each firing
animation, the tip comes within 2 to 5.4 units of six of its seven places
(the seventh, `FireRunRGunTipLoc`, is now within 2.1 too). Read in the float copy's
space, it is 18 to 100 units off every one.

Checked:
- On The Fallen, all 419 meshes with the block read: 429 attachment
  points in 135 meshes, every triangle within its mesh, and 74
  precomputed places in 14.
- **A type 1 point, as Engine.dll places it** (`PointMatrix` and
  `TriFrame`, read November 2026):
  - The frame: from the triangle's corner 0, X toward corner 2, Y its
    normal (corner 1 less corner 0, crossed with corner 2 less corner 0),
    Z = X x Y. The corners are as posed, in the skeleton's space. No
    bone is involved.
  - The point: its offset along that frame's axes from corner 0, and
    its turn's rows taken in the frame.
  - The i32 after the 12 floats (4 or 7) is the stored matrix's flags.
    7 is the identity: no turn, no offset, whatever floats are stored.
- **A type 2 point** is the stored matrix as it is, from the
  skeleton's origin; the triangle isn't used. Any other type is the
  identity.
- **Checked against the precomputed places:** `reluva-tool ue-points`
  does this, and `ue-check` prints its summary. 65 of the 74 places name
  a type 1 point and an animation (`<Animation><Point>Loc`); two more
  name a `Fire` their meshes haven't. Each animation is posed through
  its frames in 60 steps.
  - The point comes 1.9 units from its place on average, every one of
    the 65 within 5.
  - Every one's turn is the place's exactly (rows' dot products summing
    to 3.00).
  - Reluva's earlier reading (the triangle's middle, in its bone's axes)
    was 3.6 units off on average, 47 within 5, and only the barrel's
    line matched.
  - `FireRunRGunTipLoc` was 10 to 17 units under its place until the
    rotation keys' w was read with its wrap (below): its right elbow
    flipped half a turn between keys.
  - A pose with no skeleton (an animation of vertex keys) uses the same
    frame.

On Kira, standing:
- **Hands and feet:** her `RightHand` point is on her right at hip
  height, and her `RightFoot` point 40 units down (her middle is 47
  above the floor).
- **Face:** her `Face` point is 4 units in front of her head.
