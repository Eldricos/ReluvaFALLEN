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
A texture with neither (no pixels, no `bHasComp`) is reported as such.
The Fallen has none (`ue-check`, November 2026): every texture of the
class Texture stores its pixels, none needs its compressed copy, and
the only ones with no pixels are the 519 procedural ones (below).

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

Timing, as Engine.dll's `UFliTexture::Tick` does it: `FrameTime` is
the time from the start (0 to `Length`), moved on by each tick times
`Rate` while `Mode` is once or looping (a tick longer than `Length`
moves it not at all); past `Length` looping goes round (`FrameTime`
modulo `Length`) and once stops (`Mode` 0) back at the start, or, with
`bFinishAtEnd`, on its last frame (the engine steps one frame short of
the ring frame and leaves `FrameNum` 0; Reluva leaves it on the last
frame's number). `FrameNum` is 1 + 30 x `FrameTime` in whole frames: a
frame shows 1/30 s at `Rate` 1 (length / frames is 1/30 s in every
texture), so the red blinker `FlcAnim.Redblink01` (rate 0.1) a third of
a second a frame. Pixels go through the FLC's own palette chunks, starting from
the texture's palette; masked textures, and any on a masked surface, see
through colour 0.

Playing is script's to ask for: `Mode` (0 stopped, 1 once, 2 looping),
with `FrameNum` and `FrameTime` where it is. Reluva plays a FliTexture
by those once the HUD draws it (see [unreal_vm.md](../../engines/unreal_vm.md));
going back to an earlier frame decodes again from the first. A HUD tile
that sees through colour 0 does so in every frame.

### Fire (FireTexture)

As Fire.dll does it. Its routines were run in an x86 emulator against a
model of them, byte for byte (the heat, the sparks and the random index
after every step, every spark type), and Reluva's code against that
model on the game's own textures (fx.fire01's 803 sparks and the
1,500 moving ones they make, over 60 steps).

A heat value (0-255) per pixel, shown through the palette. After the
mipmaps: a compact count, then 8 bytes a spark: type (`ESpark`), Heat,
X, Y (pixels) and four bytes A-D whose use depends on the type. A
texture starts cold. Each step (none if it's under 8 pixels either way):

1. Each spark in the list in turn draws itself. A spark's pixel is set
   to its value outright (not added to, nor the larger kept).
2. The heat spreads and cools.
3. The Stars look again.

**Spreading:** each pixel becomes a quarter of four pixels' sum, plus 1,
less (255 − `RenderHeat`) / 16, kept to 0-255.
- With `bRising` the four are below it: the one under and the one to
  its right, the one two under and the one to its left, so flames climb.
- Without, it's itself, the one to its right, and the one under and to
  its left.
- The picture is counted as one row after another, wrapping at the end:
  the rows past the bottom are the top ones, and a pixel's left
  neighbour at the left edge is the end of the row above.
- A low `RenderHeat` leaves only the sparks' own pixels, a high one long
  trails.

**Random numbers:** one stream for every fire and water texture: a table
of 512 bytes read and stirred four bytes at a time. Fire.dll fills it
from `rand()` when it loads; Reluva does the same with MSVC's `rand()`
from seed 1, which is an assumption.

**Moving sparks:** emitters add them to the same list, after the placed
ones, while the list holds fewer than `SparksLimit`, placed sparks
included.
- A new spark is drawn and moved on the step it's made.
- One that dies is replaced by the last in the list, which then waits
  until the next step.
- A moving spark steps one pixel along each axis with a chance of
  |v| / 128 a step (v a signed byte; −128 always moves).
- On loading, sparks off the texture are dropped. `SparksLimit` is kept
  to 4-8192 only if the texture holds more sparks than it.

**What each spark type does each step** (all byte arithmetic wraps;
"random" is a byte from the stream; Sin is a turn in 256 steps, 0-255):

| Type | Each step |
|---|---|
| Burn | its pixel a random heat (not its Heat) |
| Sparkle | Heat at (X + A·random/256, Y + B·random/256): a box A × B to the right of and below it |
| Pulse | Heat at its pixel, then Heat += D (a sawtooth) |
| Signal | Heat at its pixel if Heat > C; then Heat += D, and once it wraps a random Heat |
| Cylinder, Cylinder3D | Sin(A+64) + Heat at X + Sin(A)·B/256; A += D (3D only on the near half) |
| Jugglers | the same up and down |
| Lissajous | Sin(A+64)+32 at (X + Sin(A)·Heat/256, Y + Sin(B)·Heat/256); A += C, B += D |
| Blaze | half the time a spark that cools by 5 a step, moving at random A, B |
| OzHasSpoken | half the time a spark rising (B −127), drifting −63..64, cooling by 2 |
| Cone | a quarter of the time a spark drifting −63..64 then falling (B += 3), for 49 steps |
| BlazeRight, BlazeLeft | the same, flung right or left, for C steps |
| Emit, Fountain | a quarter of the time a spark moving along A, B, cooling by D; a Fountain's falls back (every other step B += 3) and goes out at 50 |
| Organic, WanderOrganic | sparks that rise 2 pixels a step from 252 down to 192, drifting; WanderOrganic's anywhere in 32 pixels, the spark itself wandering 7 pixels at random |
| RandomCloud | the same, warming from 4 to 248 |
| CustomCloud, LocalCloud | sparks warming from D by 4 to 248, moving along A, B; Custom's spot wanders |
| Eels, Gametes | one step in 13 a spark living C steps at Heat, moving at random (a Gamete's along a swaying angle) |
| Flocks, Wheel | a spark each step, living C steps, heading B (A advancing by D/16) |
| Sprinkler | a spark each step swept by Sin(A), A += D |
| Stars | B at its pixel; after the spread, B becomes the pixel's heat, and if that's under 38, A is put there |
| Line, RampLightning | while C counts down, a jagged flash from the spot along A, B (bit 0: the negative way), Heat to Heat (Ramp: to Heat/8); then, when a random ≥ D, C anew (0, 1, 4 or 5) |
| SphereLightning | when a random ≥ D, a flash a random way, C long, Heat to Heat/4 |

`DrawMode` (`dmode`: DRAW_Normal, DRAW_Lathe, DRAW_Lathe_2 to _4) is
only the editor's: placing a spark with a lathe adds its copies to the
list (turned by 128, 85 or 64 in A). The game draws nothing more for it.

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

As Fire.dll's `UIceTexture` makes it (`MoveIcePosition`, `RenderIce`,
`BlitTexIce`, `BlitIceTex`): each pixel (x, y) shows the `SourceTexture`
along its row by the `GlassTexture`'s palette index there, wrapping:
without `MoveIce` the glass shifted by the position (U, V), source[y][x +
glass[y + V][x + U]]; with it the source shifted, source[y + V][x + U +
glass[y][x]]. The position:

- a clock (`MasterCount`) moving 120 a second;
- a pan (`UDisplace`, `VDisplace`): U less 2 x (`HorizPanSpeed` - 128)
  pixels a second, V plus 2 x (`VertPanSpeed` - 128);
- by `PanningStyle` (`PanningType` in Fire.u), with a = `Amplitude` + 1
  and f = (`Frequency` + 1) x clock x 0.0012 radians, added in whole
  pixels to the pan:

| Style | U | V |
|---|---|---|
| 0 Linear | | |
| 1 Circular | a sin f | a cos f |
| 2 Gestation | a sin f | a cos (f x 0.0011 / 0.0012) |
| 3 WavyX | a sin f / 2 | |
| 4 WavyY | | a cos f / 2 |

`TimeMethod` 0 (frame-rate synced) moves it a 120th of a second each
time the texture is due (`UTexture::Tick`'s pacing, so with
`MaxFrameRate` 0 a 120th each frame drawn); 1 (real time) by each tick's
time. It's drawn again only when the position, rounded to whole pixels,
changes. With no ice textures in The Fallen, tried only on the tests'
made-up texture.

### Scripted (ScriptedTexture)

Drawn by UnrealScript while the game runs: its `NotifyActor`'s
`RenderTexture` event calls `DrawTile`, `DrawText` and the like on it
(Engine.u). Until Reluva runs UnrealScript it shows its `SourceTexture`.
