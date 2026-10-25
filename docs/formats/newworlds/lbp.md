# Star Trek: New Worlds: textures (`.lbp`, `.mp0`–`.mp4`, `.eb0`, `.eb1`, `.fbp`)

Status: **understood**: formats 1 and 6, with or without a colour key, which
covers every texture in the retail archives the survey found.
Decoder: `src/engines/newworlds/texture.cpp`; the New Worlds engine currently
shows every texture in an install. `reluva-tool lbp-info <file> <prefix>`
decodes one to BMP files.
Stored in `.cat` archives with chunk tag `0x16`.

All values little-endian.

```
u32 format           1 = one layer (opaque), 6 = two layers (with alpha)
u32 palette count    256
palette              count × 4 bytes: blue, green, red, 0 (like a Windows RGBQUAD)
u32 colour-key flag  0 = no key, 1 = a key index follows
[u32 key index]      only if the flag is 1: pixels with this index are transparent
u32 width
u32 height
layer 1              width × height bytes: palette indices
layer 2              format 6 only: width × height bytes of alpha. In model
                     textures such as k_bop_g it is a light map (255 where the
                     lights are), not transparency
```

### Formats

`reluva-tool tex-survey` over all 23 retail archives (2,108 textures):

| Format | Key flag | Count | Used for |
|---|---|---|---|
| 1 | 0 | 1,559 | most textures: terrain and building `.lbp`, every mipmap level `.mp0`–`.mp4`, environment maps `.eb0/.eb1`, fonts `.fbp` |
| 6 | 0 | 479 | textures with transparency: effects, trees, bushes, some unit skins |
| 1 or 6 | 1 | ~70 | colour-keyed: interface images (`lbp/gui/...`), sprites under `res/kurtscou/`, a few star and planet textures |

Mipmap sets (`.mp0`, `.mp1`… in a `0x29` container) halve in size each level,
each with its own palette: e.g. `lbp/txr/map/aln/brid01` is 256 × 256,
128 × 128, 64 × 64 (66580, 17428, 5140 bytes).

### Colour-keyed textures

When the flag is 1, a u32 key index comes before the width and height. Found
by working back from the survey, which had read the key and width as width and
height: the "data / (w × h)" ratios it printed equal (pixels + 4) / (key ×
width) for every group, e.g.

| Texture | Ratio | Key | Width | Height |
|---|---|---|---|---|
| `lbp/gui/fed/basebut1.lbp` | 0.126 | 255 | 32 | 32 |
| `lbp/gui/point.lbp` | 0.064 | 254 | 16 | 16 |
| `lbp/gui/smlogos.lbp` | 0.502 | 255 | 32 | 128 |
| `lbp/txr/solsys/stars1.mp0` | 1.008 | 254 | 128 | 256 |
| `res/kurtscou/kscout1l.lbp` | 12.012 | 5 | 64 | 60 |
| `lbp/txr/solsys/tabras01.mp0` (format 6) | 2.016 | 254 | 256 | 256 |

For format 6 with a key, keyed pixels are transparent; the decoder keeps the
alpha layer separately (`Texture::alpha`), since in model textures it is a
light map rather than transparency.

**The key index is the transparent colour.** `reluva-tool tex-keys` over all 69
colour-keyed textures in the retail archives:

- Interface images (`lbp/gui/...`): key 255 (191 in `pbbutt.lbp`), always pure
  magenta `#ff00ff`. In cursors (`attack.lbp`, `move.lbp`, …) three corners are
  the key and the top-left is the pointer tip.
- Sprites in `res/kurtscou/` (`kscout*`, `ktank*`): the key is a black index, and
  all four corners use it in every one.
- Star fields and planets (`lbp/txr/solsys/stars*`, `tabras*`): key 254, black,
  so space is transparent.
- Images that fill their whole area (sliders, effects, padded buttons) don't have
  key-coloured corners, so only 32 of 69 have the key in all four corners.

One exception: `res/kurtscou/ksmile_m.lbp` has key 255 = black (51 pixels) but
a background of index 2 = `#fe02fe`, nearly but not exactly the `#ff00ff` used
everywhere else. It sits in a developer folder with other test sprites, so it's
probably an unused asset; the decoder doesn't special-case it.

### Padding

Some interface images (`basebut1.lbp`, `smlogos.lbp`, `kln/ops2crnr.lbp`) have
solid bars along the right and bottom edges, presumably padding to a
power-of-two size, as the 3D hardware of the time required. Which part the game
actually draws is presumably defined elsewhere (e.g. the interface layout).

### Other

`misc.cat` stores one string table (`str/eng/frend.stb`) with the texture tag.

Evidence (from `rommisc.cat`, decoded with `lbp-info`):

| Texture | Header | Data bytes |
|---|---|---|
| `lbp/txr/rom/rom_hs.lbp` | 0, 64, 64 | 8192 = 2 × 64 × 64 |
| `lbp/txr/rom/rflare.lbp` | 0, 128, 128 | 32768 = 2 × 128 × 128 |
| `lbp/txr/rom/rimpact.lbp` | 0, 256, 256 | 131072 = 2 × 256 × 256 |
| `lbp/txr/rom/rb_muzzl.mp0` | 0, 64, 64 | 8192 = 2 × 64 × 64 |

Decoding `rom_hs` and `rb_muzzl` as 64 wide gave recognisable images (with the
second layer showing as a second image underneath), while 64-wide decodes of
the larger textures looked stretched, as expected if their real widths are 128
and 256. Every texture size seen in the archives fits 8 + 1024 + 12 + 2 × w × h
for power-of-two sizes.

Confirmed by viewing the decoded images:

- Decoded at the header size, `rom_hs`, `rflare` and `rb_muzzl` look right.
- `rimpact.lbp` (256 × 256) is a 4 × 4 grid of 64 × 64 frames, presumably an
  animated impact effect stored as a sprite sheet.
- Layer 2 looks like a transparency mask of the image.
- With the palette read as blue, green, red, `explode.lbp` is orange and
  yellow fire; read as red, green, blue it isn't.

## Open questions

- Where the used area of padded interface images is defined.
- Whether other format values exist, e.g. in `.fbp` font textures or `.eb0`
  environment maps.
- How sprite sheets are divided into frames (fixed grid, or described elsewhere).

## Texture sets

Container tag `0x29` holds an `.mmb` (tag `0x2a`) followed by one or more
texture levels (`.mp0`, `.mp1`… or `.eb0`, `.eb1`). `rb_muzzl.mmb` is
`01 01 00 00` followed by the float 0.25; the second byte may be the number of
levels (`rb_muzzl` has only `.mp0`).
