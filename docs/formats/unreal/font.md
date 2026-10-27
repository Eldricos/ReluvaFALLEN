# Unreal Engine 1: fonts

Status: **implemented** (`src/engines/unreal/font.cpp`), drawn by the HUD
([unreal_vm.md](../../engines/unreal_vm.md)). **Checked on The Fallen**
(October 2026): every one of Engine.u's fonts reads to its end.

A Font object is pages of characters, each page a texture with the
characters drawn on it:

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) (none on The Fallen's) |
| compact | how many pages |
| per page: compact | its texture, an object reference (The Fallen's are inside the font: `Hud_Font_Med_Light_Blue_Import.Texture7`) |
| per page: compact | how many characters |
| per character: 4 × i32 | its rectangle on the texture: start U, start V, width, height in texels |
| i32 | characters a page holds (256) |

A character's page is its code over that number, its rectangle the
remainder's. The Fallen's fonts are one page of 256, most of them
unused (zero-sized). The textures are 8-bit with a palette, the letters
drawn on colour 0, which is see-through though the textures aren't
marked masked. Their colours are drawn in: the HUD's orange, gold,
purple and blue fonts are separate fonts.
