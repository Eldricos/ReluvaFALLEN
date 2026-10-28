# Cinepak video ('cvid')

Status: **implemented** (`src/common/video/cinepak.cpp`): key and inter
frames, V1 and V4 codebooks, full and partial updates, grey and colour.
**Checked on The Fallen** (October 2026): every frame of
`movies/intro.avi` decodes.

Written from the public description of the format. Everything is
big-endian.

## Frames

A frame starts with a 10-byte header:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | flags: bit 0 set means each strip keeps its own codebooks |
| 1 | 3 | the frame's length |
| 4 | 2 | width |
| 6 | 2 | height |
| 8 | 2 | how many strips |

The strips follow, each under the one before. Each starts with 12 bytes:
an id (16 bits), its size counting these 12 bytes (16 bits), then its
top, left, bottom and right (16 bits each); the strip's height is bottom
minus top, and it's placed under the strip before whatever its top says.
Unless the frame's flags' bit 0 is set, a strip starts with the
codebooks of the strip above it in the same frame; otherwise with its
own from the frame before.

## Chunks

A strip's body is chunks: an id and a size (16 bits each, the size
counting the 4-byte header).

| Id | Meaning |
|---|---|
| `0x2000` | the V4 codebook, whole |
| `0x2200` | the V1 codebook, whole |
| `+0x100` | (`0x2100`, `0x2300`) a partial update of it |
| `+0x400` | (`0x2400`, `0x2600`, ...) its entries are grey |
| `0x3000` | a key frame's vectors |
| `0x3100` | an inter frame's vectors |
| `0x3200` | vectors all V1 |

A codebook has up to 256 entries. Each is four luma bytes (a 2x2 patch:
top left, top right, bottom left, bottom right) then, unless grey, U and
V as signed bytes. The colour of each pixel:

    R = Y + 2V
    G = Y - U/2 - V
    B = Y + 2U

clamped to 0..255. In a partial update, 32 bits of flags (the highest
first) come before each 32 entries; only an entry whose bit is set is
read, the others kept.

## Vectors

The picture is coded in 4x4 blocks, left to right then top to bottom
within the strip. Flags are read 32 bits at a time, the highest bit
first, a new word when the last runs out.

- **Key frame (`0x3000`)**: for each block a flag: set, a V4 block (four
  bytes, four V4 entries, one for each 2x2 quarter: top left, top right,
  bottom left, bottom right); clear, a V1 block (one byte, one V1 entry
  whose 2x2 patch is doubled to cover the block).
- **Inter frame (`0x3100`)**: for each block a flag: clear, the block is
  left as it was; set, a second flag picks V4 or V1 as above.
- **V1 only (`0x3200`)**: one byte for each block, a V1 entry.
