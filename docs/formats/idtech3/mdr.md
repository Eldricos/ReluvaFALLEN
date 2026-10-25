# MDR models (Elite Force's player bodies)

Raven's skeletal model format, used by Star Trek: Voyager – Elite Force
for its players' bodies (`models/players2/<name>/lower.mdr` and
`upper.mdr`; heads stay `.md3`). The layout, the packed bones and how a
vertex is placed were read from the GPL ioquake3 renderer, which loads
these files (`qfiles.h`, `tr_model.c`, `tr_animation.c`); Reluva's reader,
`src/engines/idtech3/mdr.cpp`, is new code from this description.

Everything is little-endian. Offsets are from the start of the file
unless said otherwise.

## Header (104 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"RDM5"` |
| 4 | 4 | version: 2 |
| 8 | 64 | name |
| 72 | 4 | frames |
| 76 | 4 | bones (at most 128) |
| 80 | 4 | where the frames start; **negative** when the bones are packed (the frames start at minus this) |
| 84 | 4 | levels of detail |
| 88 | 4 | where the first starts |
| 92 | 4 | tags |
| 96 | 4 | where they start |
| 100 | 4 | the end of the file |

## Frames

Each frame: bounds (two vectors), a local origin, a radius (40 bytes);
then, when the bones aren't packed, a 16-byte name and each bone as a 3 x 4
matrix of floats (48 bytes): rows of a rotation, the fourth column the
bone's position. Packed bones are 24 bytes each: twelve 16-bit numbers,
the first three the position's x, y and z (minus 32768, in 64ths of a
unit), the other nine the rotation's rows (minus 32768, in 32766ths).

## Levels of detail

Each (12 bytes): how many surfaces, where the first starts and where the
next level of detail starts, both from the level's start. Reluva uses the
first, the most detailed.

## Surfaces (168 bytes, then their data)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | ident |
| 4 | 64 | name (matched against skins in lower case) |
| 68 | 64 | shader |
| 132 | 4 | (in-game use) |
| 136 | 4 | (back to the header) |
| 140 | 4 | vertices |
| 144 | 4 | where they start |
| 148 | 4 | triangles |
| 152 | 4 | where they start |
| 156 | 8 | bone references (unused) |
| 164 | 4 | where the next surface starts |

Offsets here are from the surface's start. A vertex is its normal, its
texture coordinates, how many weights it has (24 bytes), then its weights,
20 bytes each: a bone, a weight, an offset (a vector). A triangle is three
vertex numbers.

## Posing

A vertex's position in a frame is the sum, over its weights, of the
weight times the bone's matrix applied to the offset (rotation, then the
position added); its normal the sum of the weights times the rotation
applied to the vertex's normal. Between two frames the original blends
the bones' matrices; Reluva poses every frame when loading (as an `.md3`
holds them) and blends the posed vertices on the GPU, which differs only
in how a blend between two quite different frames bends.

## Tags (36 bytes each)

A bone and a 32-byte name. In a frame, the tag's axes are the bone
matrix's first three columns (x, y, z) and its position the fourth.
