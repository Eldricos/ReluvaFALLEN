# Unreal Engine 1: textures and palettes

Status: **implemented** (`src/engines/unreal/texture.cpp`) for P8, DXT1,
RGB8 and RGBA8. **Checked on The Fallen** (`reluva-tool ue-check`,
October 2026): all 3801 palettes read and 4029 of 4211 textures decode.
The other 182 were procedural textures (FireTexture, WetTexture...: force
fields, cloaks, turret beams, explosion waves) that store no pixels at
all; they now get a stand-in (below). Sources as in [package.md](package.md).

## Palette

Properties, then a compact colour count (256) and that many colours of
four bytes: R, G, B, A. The alpha isn't used for P8 textures.

## Texture

Properties (see [package.md](package.md)), then the mipmaps.

Properties used: `Format` (Byte: 0 P8, 1 RGBA7, 2 RGB16, 3 DXT1, 4 RGB8,
5 RGBA8; P8 when absent), `Palette` (Object), `USize`/`VSize` (Int),
`bMasked` (Bool: colour 0 is see-through), `PolyFlags` (Int: how
surfaces with it draw, see [model.md](model.md)). A surface flagged
Masked sees through colour 0 whether or not its texture is marked so:
CM_SpaceStation's windows are FLCs without `bMasked` on masked surfaces,
lit hull showing between the windows in a recording of the original.

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
IceTexture, ScriptedTexture, and The Fallen's FliTexture) are laid out
the same. In The Fallen they store a top mipmap of their size with no
pixels: the engine makes them while the game runs. Fire, water, wave,
wet, ice and FLC textures play (below, `procedural.cpp`), in the Maps
page and the Textures page (Space pauses); as a still picture (the
meshes page, `decodeTexture()`) fire, water and FLC ones show their
first picture, and wet, ice and scripted ones their `SourceTexture`
scaled to their size (or an ice texture's `GlassTexture` without one);
any other, a gradient through its palette. The Textures page says which
it is. `ue-check` runs each animated texture for ten seconds: on The
Fallen all 519 play (337 FLC, 108 fire, 70 wet, 4 wave).

The Fallen has no IceTexture or ScriptedTexture objects: of its 180
packages, only Fire.u and Engine.u name them, defining the classes.

## Animated textures

The class names and their settings' names come from Fire.u (the enums
`ESpark` and `WDrop`, the structs `Spark` and `ADrop`). What each
setting does is Reluva's own reading of those names and of the values
in The Fallen's textures, to be compared with the original running. The
fire and water simulations step 30 times a second.

### FLC (FliTexture)

The Fallen's own class. After the mipmaps: float length, float rate,
u16 frame count, u16 width, u16 height, a u16 not understood (2 to 173),
u32 bytes of frames, then the frames of an FLC animation (Autodesk
Animator's format, without its 128-byte file header). Each frame is a
u32 size, 0xF1FA, a u16 chunk count, 8 reserved bytes, then its chunks
(u32 size, u16 type); 14 of the 337 textures pad a frame by 2 bytes past
its chunks, so frames are stepped by their size. Chunk types seen: 4
(palette, 8-bit), 7 (changed lines in 16-bit words), 12 (changed lines
in bytes), 15 (whole picture, run-length coded), 16 (whole picture as
is), 18 (a thumbnail, skipped); 11 (6-bit palette) and 13 (black) are
read too. Every texture has one frame more than its count: the "ring"
frame, which turns the last frame back into the first, so after it play
carries on from the second. Its `Flic` (a C3DFLIC object) is empty.

Timing: length / frames is 1/30 s in every texture, so a frame shows
for that divided by the rate (`Rate`, 0.014 to 1): the red blinker
`FlcAnim.Redblink01` (rate 0.1) a third of a second a frame. That the
rate divides is a guess from its saved `FrameTime` (0.058, more than
1/30). Pixels go through the FLC's own palette chunks, starting from
the texture's palette; masked textures, and any on a masked surface, see
through colour 0.

Playing is script's to ask for: `Mode` (0 stopped, 1 once, 2 looping),
with `FrameNum` and `FrameTime` where it is. Reluva plays a FliTexture
by those once the HUD draws it (see [unreal_vm.md](../../engines/unreal_vm.md));
going back to an earlier frame decodes again from the first. A HUD tile
that sees through colour 0 does so in every frame.

### Fire (FireTexture)

A heat value (0-255) per pixel, shown through the palette. After the
mipmaps: a compact count, then 8 bytes a spark: type (`ESpark`), heat,
X, Y (pixels) and four bytes A-D whose use depends on the type. Each
step the heat spreads and cools, then the sparks add theirs (a spark
sets its pixel to at least its heat):

- Spreading: with `bRising` each pixel takes the heat of the three below
  it (the middle twice), so flames climb; without, it blends with its
  four neighbours. Either way times `RenderHeat` / 256, so a low
  `RenderHeat` leaves only the sparks' own pixels and a high one long
  trails. Rows below the bottom are cold; the sides wrap.
- Sparks: Burn a random heat at its spot; Sparkle flashes near it (A
  how often, B how far); Pulse and Signal rise and fall or switch on and
  off (D the speed, A the phase); Blaze, Cone, OzHasSpoken,
  BlazeLeft/Right, Emit, Fountain and Sprinkler send out moving sparks
  that cool as they go (Emit and Fountain along A, B as signed speeds in
  32nds of a pixel a step, Fountain falling back); Cylinder, Lissajous,
  Jugglers and Wheel move round paths (B the size, C the speed); the
  organic ones (Flocks, Eels, Organic, WanderOrganic, Gametes) wander;
  the clouds and Stars dot heat at random near the spot; the lightnings
  draw jagged lines (Line and Ramp to the point A, B; Sphere outwards).
  At most `SparksLimit` moving sparks at once.
- `DrawMode` (`dmode` in Fire.u: DRAW_Normal, DRAW_Lathe, DRAW_Lathe_2
  to DRAW_Lathe_4): a lathe turns the sparks round the texture's
  middle, each spark's heat put again at its copies turned 1/2, 1/3 or
  1/4 of the way round for Lathe_2 to Lathe_4, and right round, a pixel
  apart, for DRAW_Lathe. Only the Cardassian phaser's `pulsecap` (in
  CARD.u and DS9.u) uses one, Lathe_4: its sparks, a glow round the
  middle already, become four-fold.

The 108 fire textures hold 26,859 sparks; Sparkle (9,403), Jugglers,
Burn, Emit, Signal and Blaze are most of them. Many textures are
pictures painted in sparks (a force field's 1,024 Sparkles).

### Water, wave and wet (WaterTexture and its subclasses)

A height field at half the texture's size (the drops' X and Y are in
its cells: a 64x64 texture's run to about 32), rippling: each step a
cell becomes half its four neighbours' sum less its value the step
before, damped by 0.96; the edges wrap. The drops are the `Drops`
properties (`ADrop`: type, depth, X, Y, A-D), the first `NumDrops` of
them (the rest are left over from editing). Each step a drop sets its
cell: FixedDepth to its depth; PhaseSpot rising and falling by its depth
(D the speed: the texture's `FX_Frequency` when it was placed);
ShallowSpot by half; HalfAmpl between 0 and its depth; RandomMover and
the whirlies moving (round a circle of C / 16 cells, B a signed speed,
A the phase); the lines a row of cells of C / 2 rising and falling; the
oscillators a spot swinging along a line; RainDrops at random cells;
AreaClamp holding cells flat; the taps now and then.

A wet texture shows its `SourceTexture` with each pixel moved by the
slope there (times `WaveAmp` / 128, up to 16 pixels); water and wave
textures show the slope through their palette (128 flat). A wave
texture is lit instead, as a bumpy surface (its slope, without
`WaveAmp`'s scaling, which is the wet textures' distortion): from the
direction `BumpMapAngle` (256 a turn), slopes facing the light brighter
and those facing away darker by `BumpMapLight` / 128; past a slope of
(255 - `PhongSize`) / 4 towards it, a highlight adds `PhongRange` / 64
times as much again. The four wave textures (`FX.Lripple04_a` and the
disruptor's ring, hit and MyTex4) have palettes dark at 128 and bright
above, so it's their crests that show.

### Ice (IceTexture)

A `SourceTexture` seen through a `GlassTexture`, one sliding over the
other (both scaled to the texture's size). Each pixel shows the source
shifted by the slope of the glass's brightness there (a quarter of the
difference either side, up to 16 pixels), like light bent by uneven
glass. The glass slides, or with `MoveIce` the source does, as
`PanningStyle` says (`PanningType` in Fire.u):

| Style | Movement |
|---|---|
| 0 Linear | `HorizPanSpeed`, `VertPanSpeed`: 128 still, each step either side a quarter of a pixel a second |
| 1 Circular | round a circle of `Amplitude` / 4 pixels, `Frequency` / 64 turns a second |
| 2 Gestation | to and fro on both axes, at slightly different rates |
| 3 WavyX, 4 WavyY | linear, with a wobble across |

`TimeMethod` (frame-rate synced or real time) isn't used: it always runs
in real time, 30 steps a second. With no ice textures in The Fallen,
this is Reluva's reading of the property names, tried only on the
tests' made-up texture.

### Scripted (ScriptedTexture)

Drawn by UnrealScript while the game runs: its `NotifyActor`'s
`RenderTexture` event calls `DrawTile`, `DrawText` and the like on it
(Engine.u). Until Reluva runs UnrealScript it shows its `SourceTexture`.
