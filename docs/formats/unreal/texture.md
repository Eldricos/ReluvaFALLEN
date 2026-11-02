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
in The Fallen's textures, checked where noted against Fire.dll and
Engine.dll.

The fire, water and ice textures step as the engine's `UTexture::Tick`
paces them: with `MaxFrameRate` 0, once every frame they're drawn (so
faster on a faster display); otherwise each frame's time adds up, and
once it's past 1 / `MaxFrameRate` the texture steps, the sum starting
again from 0, unless it's past 1 / `MinFrameRate`, when only that much
is taken off so a slow display catches up. Both rates are kept to
0.01-100, and a texture steps at most once a frame. An ice texture's
sliding goes by the clock; only its drawing is paced.

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
step the sparks add their heat (a spark sets its pixel to at least its
heat), then the heat spreads and cools (Fire.dll's order):

- Spreading: each pixel becomes a quarter of four pixels' sum, plus 1,
  less (255 - `RenderHeat`) / 16, kept to 0-255. With `bRising` the four
  are below it: the one under and the one to its right, the one two
  under and the one to its left, so flames climb; without, itself, the
  one to its right, and the one under and to its left. The picture is
  counted as one row after another, wrapping at the end: the rows past
  the bottom are the top ones, and a pixel's left neighbour at the left
  edge is the end of the row above. A low `RenderHeat` leaves only the
  sparks' own pixels, a high one long trails.
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

As Fire.dll does them: its routines were run in an x86 emulator on test
fields and on `FX.Lripple04_a`, and Reluva's output is the same byte for
byte (the field, the lighting table and every pixel, after 1 to 300
steps). WaterTexture itself is abstract; the game has wave and wet ones.

The height field is a byte a cell, 128 flat, half the texture wide and
as tall, and starts flat. Its rows are staggered: row r's cell c sits
over pixel column 2c + 1 + (r & 1). Each step updates every other row,
the odd ones and the even ones in turn: a cell becomes half the sum of
its four diagonal neighbours (in the rows above and below, the cells
either side of its column) less its own value, through a table that
rounds down, and up by one below 128, so ripples die away by rounding
alone; values stay 0-255 and the edges wrap.

The rows not updated that step are drawn. Pixel row y shows field row
y + 1; on the others, the rows either side of it added. Over a cell the
slope is the difference between the cells either side (made even);
between two cells it's the mean of theirs. The slope + 512 (0-1023)
looks up the texture's table:

- Wave: a lit bumpy surface. The slope s = −slope · `WaveAmp` / (255 ·
  196) tilts the normal to θ = π/2 + (π/2) · s / (1 + |s|); the light
  comes from `BumpMapLight` · π/255 and gives (256 − `PhongRange` / 2)
  · cos(θ − light); where 2θ − light − `BumpMapAngle` · π/255 is within
  `PhongSize` / 512 of 0, a highlight adds up to 2 · `PhongRange`,
  falling off linearly. Rounded towards 0, kept to 0-255, a palette
  index. (The names read as if `BumpMapAngle` were the light's angle; in
  the code it's the second angle in the highlight.) `FX.Lripple04_a`'s
  flat water is index 93.
- Wet: (slope + 1) · `WaveAmp` / 512, rounded towards 0 and kept to
  -128..127, is how far along its row each pixel takes its
  `SourceTexture`'s, wrapping.

The drops are the `Drops` properties (`ADrop`: type, depth, X, Y, A-D),
the first `NumDrops` of them, applied in order before each step. A drop
sets a cell in both rows of the pair Y (rows 2Y and 2Y + 1), column X;
the moving ones wrap within the field. For several the byte the scripts
call depth is a phase, and D a height or a speed. Sin is a table of a
turn in 256 steps, 0-255.

| Type | Each step |
|---|---|
| FixedDepth | the cell at D |
| PhaseSpot | depth += D; the cell at Sin(depth) |
| ShallowSpot | as PhaseSpot at Sin / 2 + 64 |
| HalfAmpl | as PhaseSpot, but no lower than 128 |
| RandomMover | the cell 185 in the first row and 71 in the second, then X and Y each move by a random −3..3 |
| FixedRandomSpot | the cell random |
| WhirlyThing, BigWhirly | a 16-bit phase in B:A moves by D:C; the cell (X, Y) + (Sin, Cos of B's angle) / 16 (/ 8 for BigWhirly) at depth: round a circle of 8 (16) cells |
| Horizontal, Vertical, DiagonalLine1 and 2 | D / 2 + 1 cells from (X, Y) at depth: right, down, down-left, down-right |
| Horizontal, Vertical, DiagonalOsc1 and 2 | the same lines, depth += C a step and the cells at Sin(depth) |
| RainDrops | one step in 16, a random cell in the D × D square from (X, Y): depth in the first row, 255 − depth in the second |
| AreaClamp | the D / 2 square from (X, Y) at depth |
| LeakyTap | A += D; when it wraps past 255, the cell as RainDrops does |
| DrippyTap | as LeakyTap, A set to a random value each drip, so the drips come unevenly |

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

`TimeMethod` (frame-rate synced or real time) isn't used: it slides in
real time, drawn as the frame rates above pace it. With no ice textures
in The Fallen, this is Reluva's reading of the property names, tried
only on the tests' made-up texture, and not checked against Fire.dll.

### Scripted (ScriptedTexture)

Drawn by UnrealScript while the game runs: its `NotifyActor`'s
`RenderTexture` event calls `DrawTile`, `DrawText` and the like on it
(Engine.u). Until Reluva runs UnrealScript it shows its `SourceTexture`.
