# Star Trek: New Worlds: `.fif` fonts

Status: **fully understood**. Reader: `src/engines/newworlds/fif.cpp`.
Worked out from the files themselves, October 2026: the six fonts in
`fedgui.cat` and `glblgui.cat`. `reluva-tool fif-info <file.fif>` prints a
glyph table; `reluva-tool fif-info <archive.cat>` lists an archive's fonts.

A font is two files under `fnt/`, packed together in a pair of containers
(see [cat.md](cat.md)):

```
0x28 container (one font)
  0x27 container
    0x26 name.fif   the glyph table
    0x16 name.fbp   the glyphs' image, an ordinary texture (lbp.md)
```

## `.fif`

All values little-endian.

```
char[8]  "FONTPROP"
u16      first code (32 in every font)
u16      glyph count
u32      size: always the line height − 2 (7, 9, 16); its use isn't known
u32      line height in pixels (9, 11, 18)
u32      0 (0x00010101 in fe5pt, probably leftover memory)
glyph count × 16 bytes, for codes first, first + 1, …:
    u32 x, u32 y, u32 width, u32 height   the glyph's box in the image
```

So a file is exactly 24 + 16 × count bytes. Codes are Latin-1 (ISO 8859-1):
`final12` holds 32-239, so its last glyph is `ï`. A glyph of height 0 is
missing. Every glyph has the line's full height, laid out in rows of the
image one line high; the width includes the gap to the next glyph, so text
is drawn by placing glyphs side by side. The image is one pixel wider and
taller than the glyphs need.

## `.fbp`

A format 1 texture (8-bit palette, no alpha layer) with the glyphs in white,
anti-aliased edges in greys, on black. Reluva uses each pixel's brightness
as its coverage and blends the text colour by it; the game probably does
something similar, since nothing else in the files marks the background.

## The fonts

| Font | Archive | Codes | Drawn | Height | Image | Looks |
|---|---|---|---|---|---|---|
| `final12` | `glblgui` | 32-239 | 174 | 18 | 304×109 | the large title font, upper and lower case and accents |
| `fe8pt` | `fedgui` | 32-255 | 189 | 11 | 193×78 | Federation interface text |
| `fe5pt` | `fedgui` | 32-255 | 190 | 9 | 161×64 | small, capitals only (lower-case codes hold capitals) |
| `fedengft` | `glblgui` | 32-255 | 180 | 9 | 161×64 | a rougher `fe5pt`: ten symbols missing (`©` among them), a solid block among the fractions, and squashed 4-pixel accented letters from `à` |
| `feengft2` | `glblgui` | 32-255 | 190 | 9 | 161×64 | like `fe5pt`, laid out differently in its image |
| `fepnttle` | `fedgui` | 32-142 | 89 | 11 | 224×45 | wide capitals for titles; nothing past `}` |

`fedgui.cat` stores `fe5pt` five times, each copy identical (like the
sounds, see [wav.md](wav.md)). The other sides' GUI archives (`kligui`,
`romgui`, `forgui`) presumably hold their own fonts.

## In Reluva

The data viewer's fonts page (**Tab**) shows each font's characters and some
sample text: **Left/Right** picks a font, **+/-** change the size and **B**
outlines each glyph's box. On the strings page, **F** switches between
Reluva's built-in font and the game's fonts.

## Open questions

- What the size field is for (a baseline would be 15 in `final12`, not 16).
- Which interface uses which font.
