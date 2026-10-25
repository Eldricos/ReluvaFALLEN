# Unreal Engine 1: textures and palettes

Status: **implemented** (`src/engines/unreal/texture.cpp`) for P8, DXT1,
RGB8 and RGBA8. On The Fallen every palette reads; some textures (cloak
and turret effects, `JemCLoakFx04`, `LTurretLaser01`) have a first
mipmap with no pixels, which led to the compressed list below being read.
Sources as in [package.md](package.md).

## Palette

Properties, then a compact colour count (256) and that many colours of
four bytes: R, G, B, A. The alpha isn't used for P8 textures.

## Texture

Properties (see [package.md](package.md)), then the mipmaps.

Properties used: `Format` (Byte: 0 P8, 1 RGBA7, 2 RGB16, 3 DXT1, 4 RGB8,
5 RGBA8; P8 when absent), `Palette` (Object), `USize`/`VSize` (Int),
`bMasked` (Bool: colour 0 is see-through), `PolyFlags` (Int: how
surfaces with it draw, see [model.md](model.md)).

Mipmaps: a compact count, then each, largest first:

| Type | Meaning |
|---|---|
| i32 | from version 63: the file offset just past the pixel data (for skipping it) |
| compact | size of the pixel data |
| bytes | the pixels: P8 one palette index each; DXT1 8-byte 4x4 blocks; RGB8 3 bytes and RGBA8 4 bytes, both stored blue first (an assumption to check) |
| i32 × 2 | width, height |
| u8 × 2 | log2 of width and height |

When the `bHasComp` property is set, a second list of mipmaps in the same
layout follows the first, in the format `CompFormat` gives (DXT1 when
absent); Reluva uses it when the first list's top mipmap has no pixels.
A texture with neither (no pixels, no `bHasComp`) is reported as such:
which of The Fallen's are like that, and why, is still to be checked.

Procedural textures (FireTexture, WaterTexture, WetTexture, WaveTexture,
IceTexture, ScriptedTexture) are laid out the same and decode to their
stored first frame; animating them isn't done yet.
