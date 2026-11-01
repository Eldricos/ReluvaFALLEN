# Unreal Engine 1: static lighting (lightmaps)

Status: **implemented** (`src/engines/unreal/lighting.cpp`, drawn by the
Maps page). **Data checked on The Fallen** (`reluva-tool ue-check`,
October 2026): every map's lighting reads, with no bytes left over after
the model's last field; 345952 lightmaps over the 74 maps, every
light actor's properties read; 1377 lights pulse, flicker or blink
(below). How lit levels look is still to be
compared with the original running.

Sources: public community documentation of the UE1 formats and of
UnrealEd's light properties (as for [package.md](package.md)); the
layout of the data after a model's vertices is in [model.md](model.md).
The way light is worked out from it below is Reluva's own model of what
the engine does, to be tuned against the original's screenshots.

## What's stored

UE1 levels don't keep lit lightmaps. A surface with a lightmap
(`BspSurf::lightMap`, an index into the model's light map indices) has a
grid of `uClamp` x `vClamp` sample points over it, and for each light
that reaches it one bit per sample: whether that light can see the
sample. The lights are the model's `lights` list from the index's
`lightActors` up to the first 0; light `k` of the list has its bits at
`dataOffset + k * rowBytes * vClamp` in the model's light bits, rows
padded to whole bytes (`rowBytes = (uClamp + 7) / 8`), the sample at
column `i` in bit `i & 7` (lowest first) of byte `i / 8`.

The light's colour, brightness, radius and cone come from the light
actor's properties, worked out when the level loads (the original does it
then too, which is why lights' brightness can change in game).

## The sample grid

Sample (i, j) of a surface's grid is at texture coordinates
`(pan.X + i * uScale, pan.Y + j * vScale)`, measured from the surface's
base point along its texture axes as stored (`dot(P - base, textureU)`,
without the surface's own pan). Worked out on The Fallen by trying the
readings that made sense and counting which covered each polygon
(`ue-check`, October 2026): where the texture axes aren't unit length
already, the stored axes fitted 208723 grids and the same made unit
length 474; the pan as offsets fitted 345700 of the 345952 grids, as a
point 3. So that's the layout. The 249 grids it doesn't place (likely
degenerate surfaces) start at the polygon's lowest corner instead,
counted by `ue-check` as "placed by bounds".

The lights list holds each lightmap's lights in turn, so it repeats a
light for every lightmap it reaches: up to 416997 entries (M01_KiraL1A)
for a few hundred actors. Each actor is read once.

## Working out the light

As the original's renderer does it (Render.dll's light manager and
lighting functions, and Engine.dll's `FGetHSV`, read for what they do and
written anew). For each sample, its position on the surface's plane,
then:

    light = zone ambient + sum over the lights whose bit is set of
            min(1, colour * falloff * facing * cone)

all at most 1, and the surface drawn as texture x light. The original's
lightmaps are 7 bits, 127 the texture as it is, drawn doubled (OpenGLDrv
blends them `GL_DST_COLOR`, `GL_SRC_COLOR`); Reluva's lightmaps are in
those units, 1 the texture as it is.

- **The engine's colour** (`engineHsv()`, `FGetHSV`): the hue runs red
  to green to blue and back to red in three straight thirds (0-85 red to
  green, 86-170 green to blue, 171-255 blue to red), its channels
  summing to 1 (yellow is 0.5, 0.5, 0); mixed with white by the
  saturation (255 white, 0 the pure hue); times a curve of the value v:
  with b = 1.4 v / 255, 0.7 b / (sqrt(b) + 0.01), at most 1 (0.82 at
  255, 0.58 at 128, 0.41 at 64: roughly 0.83 sqrt(v / 255)).
- **A light's colour** (`lightColor()`): its `LightHue` and
  `LightSaturation` at value 255, times `LightBrightness / 255`, its
  `LightType`'s level now, and the level's `Brightness` (`LevelInfo`;
  M05_KiraL1 has 0.75, most 1). Each light's palette is 256 levels times
  that, each channel at most 127: so in Reluva's units twice it, and one
  light gives a sample at most 1. Defaults as `Engine.Light`'s:
  brightness 64, hue 0, saturation 255. (Reluva used to scale saturated
  lights up so their channels summed as white's, fitted to a recording;
  the original doesn't, and the brightness differences that fit chased
  came from the display, below.)
- **Radius**: `25 * (LightRadius + 1)` world units; default radius 64.
- **Falloff and facing**, by f = distance / radius: 1 - 3 f^2 + 2 f^3
  (`lightFalloff()`), times the cosine between the surface's normal and
  the direction to the light. (The original keeps a table of 4096 steps
  of f^2 holding (1 - 3 f^2 + 2 f^3) / f, and multiplies by the light's
  distance from the surface's plane over its radius: together the same.)
- **Non-incidence** (`LightEffect` 13): 1.02 - f (at most 1), whatever
  way the surface faces.
- **Cylinder** (`LightEffect` 17, 4,301 of The Fallen's lights, the most
  used effect): f is the distance across only, so the light reaches its
  radius at any height; 1 - f^2, whatever way the surface faces. A large
  one (radius 1000 and more, the sky lights of the outdoor maps) ignores
  its shadow bits (below, compared with the recording: Reluva's fitted
  rule; the original's cylinder lighting does read the shadow bits).
- **Cone** (`LightEffect` 8 static spot, 12 spotlight): the light's
  `Rotation` gives its direction; with s = 1 - `LightCone` / 256 the
  cosine of the cone's edge, ((cos - s) / (1 - s))^2 inside it, nothing
  outside.
- **Zone ambient**: the surface's zone (the front zone of its first node)
  gives its ZoneInfo's `AmbientHue`, `AmbientSaturation` and
  `AmbientBrightness` through the engine's colour, a quarter of the
  lightmap's range (64 of the 7 bits' 127: about half of it in Reluva's
  units). Surfaces without a lightmap get only this; unlit surfaces (poly
  flag 0x400000) are drawn as their texture.
- A light's properties are its own over its class's defaults
  ([actors.md](actors.md#class-defaults)). On The Fallen that changes
  only the 90 `Spotlight` actors (now cones, `LightEffect` 12 from their
  class) and the 30 `DS9_LavaZone` zones (now an orange glow of
  brightness 255).

## How the frame is shown

The original's Direct3D renderer sets the display's gamma ramp to
x^(1 / (2.5 x Brightness)), `Brightness` the player's setting
(`[WinDrv.WindowsClient]`, 0.5 by default: x^0.8). Its screenshots
(`ReadPixels`) instead raise what's drawn to 1 / (1.5 x Brightness)
(x^1.33 at 0.5). Reluva shows the frame as the display does, x^0.8
(`-` and `=` change it).

That explains both comparisons below: the TrainingRoom screenshot was
about 0.85 x Reluva^1.31 (the screenshot's curve on lights a little
brighter than the original's), and the recording, a capture of the
display, was about 1.07 x Reluva^0.83. Against the recording's cut scenes
(22 frames matched by shape from CM05_Kira, CM01a_SW, Cm11B_SWK and
others), Reluva is now within 10-20% of it at every brightness (its 25th,
50th, 75th and 95th percentiles 1.21, 1.11, 1.10 and 1.17 times the
recording's), where before its dark tones were 3 to 9 times darker.

## Actors' lighting

Meshes are lit at each vertex, as the original's renderer does:

- **Its lights**: those whose radius reaches the actor (special lit ones
  only for a special lit actor), most important first, importance
  (1 - distance / radius) x `LightBrightness`; down to an eighth of the
  first's; at most three that stay put (`bStatic`), and lights that move
  only while it has fewer than three. A light that stays put must see
  the actor's `Location`, no wall between.
- **At each vertex**, for each light, with d the vector to it and N the
  vertex's normal: (max(0, (cos + 1)^2 - 1.5) + specular) x (1 - |d| /
  radius) x the light's colour (undoubled: half of a lightmap's), cos
  the cosine between d and N. Light from beside and a little behind
  still reaches it (the term is 2.5 facing the light, 0 from about 77
  degrees). Specular: the light mirrored in the vertex's plane; where
  the camera's ray to the vertex runs along the mirrored light's
  direction from it, 6 x (`Specular` x that cosine)^2 (`Specular` is 1
  on every actor).
- **Summed**: the zone's ambient light (its ambient through the
  engine's colour, not halved) plus `AmbientGlow` / 255 (255 pulses,
  0.25 + 0.2 sin 8t), plus 1.4 x `ScaleGlow` x the lights, at most 1.
  `bUnlit`: no lights, 0.7 x `ScaleGlow` all round.

Reluva does this in the renderers (`gfx::DrawCall::vertexLights`). Movers
are still lit as one directed light and ambient.

## Lights whose brightness changes

`LightType` (Engine.Actor's `ELightType`: 0 none, 1 steady, 2 pulse,
3 blink, 4 flicker, 5 strobe, 6 backdrop, 7 subtle pulse, 8 and 9 the
texture palette ones) says how a light's brightness changes over time.
On The Fallen 1377 of the 29202 lights change: 567 pulse, 598 flicker,
208 subtle pulse, 3 blink, 1 strobe. A cycle is `LightPeriod` 35ths of a
second (default 32), starting `LightPhase` 256ths of the way in.
`lightLevel()` gives the share of the brightness at a time, Reluva's own
curves (to be compared with the original running):

| Type | Brightness through a cycle |
|---|---|
| Pulse | 0.6 + 0.39 sin |
| Subtle pulse | 0.9 + 0.09 sin |
| Blink | on the first half, off the second |
| Strobe | on the first tenth |
| Flicker | random, a new value 20 times a second: 0.4-1, out now and then |

Light effects that change over time (`LightEffect`), on The Fallen 255
watery shimmers, 224 fire wavers, 45 torch wavers, 30 slow and fast
waves, and a few shocks, discos and rotors. In UE1 these move across
the surfaces; here too, as patterns worked out texel by texel from each
texel's place against the light (`lightPattern`; Reluva's reading of the
names, the original's code isn't known):

| Effect | Across the surfaces |
|---|---|
| Torch waver, fire waver | brighter and darker by direction from the flame (0.85-1, 0.7-1), the directions' shares changing at random 6 and 10 times a second |
| Watery shimmer | two ripples crossing over the surfaces, drifting (0.6-1) |
| Slow wave, fast wave | rings rolling out from the light, half and two a second, half its radius apart (lightmap texels are tens of units: closer rings fell between them) (0.7-1) |
| Rotor | four beams turning round it once a period |
| Disco | spots on a grid of directions, turning the same way |
| Shock | the whole light dropping to 0.2 at random, as before |

What moves (meshes, movers) is lit by each such light as a whole, by the
same effect's curve over time: torch waver 0.85-1 and fire waver 0.7-1
(smooth random, 6 and 10 changes a second), shimmer two sines round
0.8, the waves a sine 0.7-1, disco and rotor a sine over their period.

Lights the scripts may change (any class but plain `Light`, like
`TriggerLight`, or one with a Tag a trigger can name) are kept apart
the same way, starting as they are (one that starts off, at nothing:
kept at the brightness it was placed with, so it lights once its scripts
turn it on; one put out at the start, a `CTV_TriggerLight` not initially
on, was dropped and never lit),
and while the game runs follow their live actor: its `LightType` and
`LightBrightness` against the brightness it was placed with (a
TriggerLight fading, a light switched off or destroyed). With them and
the effects, 3,436 of the lights change (1,377 before), over 34,029
lightmaps; the most texels kept apart in one map is about 291,000.

The light a sample gets is linear in each light's brightness, so a
lightmap reached by changing lights is kept as its light without them
plus each one's at full brightness (`AnimatedLightmap`), and redrawn as
`base + sum(level * share)` (capped as above) whenever one of them
changes by a step of the 255; a light with a pattern keeps each texel's
place against it too, and its share is times the pattern there now, so
its lightmaps are drawn again every frame, while their zone is seen. The Maps page uploads only those
lightmaps' rectangles (`Renderer3D::updateTextures()`), all in one copy
each frame. On The Fallen a map has up to 49 such lights over at most
about 89000 texels (291,000 with the scripted lights and effects).

**What moves** is lit as the lights are now (some all round, the rest
from their average direction, the two together at most the texture's
own colour, as the lightmaps are; capped each alone they reached twice
it, and faces under bright lights were pale against the recording): meshes and movers by the
level's lights at their current brightness (a pulsing light pulses on
them), from where they are each frame (a door sliding into the dark goes
dark), and by the dynamic lights. While the game plays, a light only
reaches a character if the level's walls aren't between them (the yellow
lamps of the rooms round M03_SiskoL1A's red room lit Sisko white).

**Dynamic lights**: while playing, the actors with a light that aren't
`bStatic` and aren't the level's own lights (a spark burst's flash, an
explosion's, a muzzle's, a flashlight). As UE1, they light the world
without shadows: each frame the nearest 16 to the eye go to the
renderer, and each lit surface of the level and of the movers is drawn
again, its texture times their light added over it (UE1's texture x
(lightmap + dynamic light); the renderer's `dynamicLightsTextured`).
Static lights left out of the level's lightmaps (built before they were
placed) light nothing, as in UE1. On M01_SiskoL1A a spark burst lights
the wall round it for a tenth of a second.

The 128 brightness unit, the linear falloff and the cone's angle are the
first things to tune against the original.

## Compared with the original (TrainingRoom, October 2026)

(History: the curve fitted here was the screenshot's, not the display's;
see "How the frame is shown". Reluva no longer uses it.)

A screenshot of the original and one of the Maps page from about the same
place, sampled over matching areas (0-255, averaged):

| Area | Reluva | Original |
|---|---|---|
| Light strip (unlit surface) | 133 182 190 | 99 124 148 |
| Ceiling light panel (unlit) | 126 149 106 | 84 107 78 |
| Upper cabinets | 52 72 82 | 27 33 41 |
| Wall beside the door | 38 51 53 | 22 27 30 |
| Floor | 25 35 42 | 13 16 20 |
| Ceiling arch | 25 33 35 | 7 8 11 |

Even the unlit surfaces, which lighting doesn't touch, were brighter, and
the darker the area the larger the gap. One curve fits every row:
original = 0.85 x Reluva^1.31 (colours 0-1). So most of the difference
was in how the finished frame is shown (the original's gamma or
brightness setting), not in the lighting. The Maps page now multiplies
colours by 0.88 and raises the frame to the power 1.3 (`-`/`=` change the
power), which gives that curve. With it matched, the lighting itself
(falloff, brightness unit) is compared again. Rough: the two cameras
weren't in exactly the same place. The original's brightness setting for
that shot was `Brightness=0.500000` (`[WinDrv.WindowsClient]` in its
`DS9.ini`), the middle of its slider, so the 0.88 / 1.3 curve is what
that setting gives. How other settings change it isn't known yet: a
screenshot of the same place at another Brightness (say 1.0) would show
it, and then the curve can follow the player's setting.

## Compared with a recording of the original (October 2026)

(History, in part: the fits below for light colours, characters' light,
the falloff and the display curve have been replaced by what the
original's code does, in the sections above. They're kept for what the
recording showed.)

A full play-through recorded from the original (5 h 40 m, 960x720)
against the same views here, matched shot for shot in cut scenes:

- The red emergency lights of M03_SiskoL1A flood the room red, Sisko
  too; here they were a dim red and Sisko white: the colour (above), the
  lights that start off, and lights through walls on characters.
- Characters (meshes) take one ambient and one directed light: the
  lights round them summed, the ambient share from how far apart their
  directions are. CM10_KiraL1A's two guards stand under two lamps over a
  door; with half of all light ambient they were lit as brightly in
  front as on top (their 75th percentile 0.085 against the recording's
  0.032, through its display curve); with light from one side kept
  directed, 0.027. Characters lit from all round (Kira in M05_KiraL1,
  Worf on the Defiant) barely change.
- The lights actors carry (not the level's: a pickup, a flare) take their
  colour without the scaling for saturated colours above. CM10_SWK_L3's
  red orb (pure red, brightness 255, radius 275) with it lit the floor
  round its pedestal at 0.127 red against the recording's 0.025 (through
  its display curve) and the pedestal a saturated red; without, 0.045
  and pink, as the recording. Facing (incidence) isn't it: the
  recording lights the pedestal's and the cap's sides, which face away.
- Lights with `bSpecialLit` don't light characters: CM11_LabRitual's
  eight blue halos (radius 3225, high over the room) lit Obanak blue and
  white, where the recording has his face orange from the altar's fire
  (two pulsing orange lights beside it). On surfaces they light only
  those flagged SpecialLit, which the editor's lists already reflect.
- Bright areas (the opening's lab, Kira's monastery) come within 10-20%;
  dark ones fall short by more the darker they are, as a brighter
  display setting would (the recording's player had one: its HUD, drawn
  the same in both, is lighter too). So the falloff stays linear (1 -
  (d/r)^2 brightened some dark areas to it and others past it) and the
  light at most 1 (2 burnt out faces and changed little else).
- The HUD's panels are paler in the recording, (93,86,180) against
  (63,51,150): not translucency (their insides are black in both) but
  the display curve, undone on ours (87,74,169).
- The recording's display is brighter all through, most in the dark
  tones (Ops, the lab, Kira's courtyard, the mine: its 25th, 50th, 75th
  and 95th percentiles above ours alike, dark tones 3 to 9 times). Undo
  our curve (0.88 x^1.3, fitted at Brightness 0.5) and the recording is
  about 1.07 x^0.83 of the light: a higher Brightness setting. Not a
  rendering difference; Reluva could follow the game's Brightness
  setting once its curve at other values is known.
- Hass'terral's snowfield (M10_SiskoL1A) was far too dark, about 13
  times beyond the display difference. It's lit by 24 cylinder lights
  (radius 6400, brightness 32, pale blue) high above. The ground's
  lightmap (201x156 samples, 64 units each) has their shadow bits set for
  only 8.5% of light and sample pairs: the editor traced them from where
  the lights are (the bits agree 94% with a clear line to the light, so
  they're read right), leaving most of the field in cliffs' and
  buildings' shadow. In the recording the whole field is evenly lit, no
  shadow of a cliff anywhere in its six minutes. So large cylinder lights
  (radius 1000 and more: the outdoor maps' sky lights; indoor cylinder
  lights are 50 to 625) light without their shadow bits
  (`unshadowed()`). The ground now comes within 15% of the recording
  (through its display curve). Fitted: the original's code isn't known.
  Still darker than the recording: the cliffs beside the path, which
  these lights don't reach (dropping the incidence for them overshot the
  ground by 70% and barely lit the cliffs).
- The credits' starfield and nebulae (CM_SpaceStation) are the sky
  zone's own: a sphere of unlit `ds9skybox.stars.starbg1a` (radius 1024,
  2 units a texel) and two masked quads of `ds9Nebula1`/`ds9Nebula2`.
  Their texels are dim (most stars 20 to 66, the nebulae about
  (16,10,16)), so on ours they all but vanish; through the recording's
  display curve (above) both show as in the recording, the stars with
  about half its bright pixels (theirs larger, brighter blobs: a lower
  resolution scaled up, where ours filters one-texel stars down).
- Not found: what lights the credits station (CM_SpaceStation). Half
  its hull surfaces have lightmaps whose list of lights is empty (or
  none at all): the level's last lighting build reached none of their
  samples, and lists are read right (the lightmaps point only into that
  build's lists, the older ones left unreferenced). In the recording the
  outward faces of its rings, behind the windows, are evenly bright
  while the undersides and the hub, with empty lists alike, are dark;
  neither "no lights means unlit" nor "an empty list means unlit"
  matches (each lights faces the recording leaves dark). The same
  station in CM_Worf_Endgame is lit by its lights and comes within
  about 20% of the recording (bluer there).

## Characters' shadows

The Fallen's characters cast shadows of their own shape: actors with
`bProjectsShadows` (pawns, weapons and pickups by default) from lights
with `bActorShadows` (every `Light` by default), when the renderer says it
draws them (`GetRendererFlags` bit 2; the game's blob shadows,
`CTV_Shadow`, then hide). How the original does it, read from its
renderer (Render.dll's `PrepareProjectedShadow`, `DrawProjectedShadow`
and its light manager's choice of light), and redone in Reluva's own code:

- **The floor.** A line straight down from the actor's `Location`, four
  times its `CollisionHeight` long; no shadow unless it hits something
  whose normal points up at least 0.7, on the side facing the camera.
  A held `Weapon` (`bHeldItem`, with an `Owner`) casts with its holder's
  light and place, faded by its own `ScaleGlow`. An actor whose
  `MultiTextureOne` overlay is blended (`bMultiTextureOn`,
  `MultiTextureOneStyle` `STY_Blended`) fades its shadow by
  `MultiTextureOneScaleGlow` instead (a cloak fading in).
- **Distance.** A factor of 900 / the floor point's depth in the view,
  at most 3.5; under 0.05 there's none.
- **The light.** Of the lights reaching the actor with `bActorShadows`
  (special lit ones only for a special lit actor), the one giving most at
  the floor point: `LightBrightness`/255 (times its `LightType`'s level
  now) x (1 - distance / radius) / (1 + the zone's
  `AmbientBrightness`/255). The light it had is kept unless the new one
  gives 0.02 more. The original's list of lights reaching an actor tests
  only their radius, not walls between.
- **Strength** (`ShadowBrightness`, the light in `ShadowLightActor`).
  Its target is that amount times the distance factor, at most 1. It
  moves towards the target by 1.3 x the seconds since the actor was last
  drawn (at most 1): up to it while the light stays the same; when the
  light changes, down to nothing, and the new light takes over once it's
  under 0.05. It jumps straight to the target if the actor wasn't drawn
  for 3 seconds, or either light isn't `bStatic`. (A player pawn standing
  still and not `Frozen` doesn't change it.) Under 0.05: no shadow.
- **Drawing.** The light is moved up along the floor's normal so it's at
  least max(2 x the larger of `CollisionHeight` and `CollisionRadius`,
  half its distance across) above the actor, which keeps shadows from
  low lights short. The mesh, as posed, is flattened from it onto the
  floor plane, raised 0.1 + 0.005 x depth off it, and multiplies what's
  there: each vertex by 1 - min(I / |its distance from the light - the
  actor's distance from the light|, 0.8), where I = min(strength x 50,
  35). The shadow is darkest (to 0.2) where it falls close to the actor
  and fades along its length.

Reluva leaves out the player-pawn rule. It draws overlaps once (the
renderer's StencilMode::Once) and has the per-vertex darkness in the
renderers (`gfx::ColorSource::Shadow`). Against a recording of the
original: Kira's shadow on M05_KiraL1's bridge floor falls the same way;
the original's reaches further, maybe from another light it kept. On the
Promenade shrine's platform, the light it chooses is now one beyond a
wall that the earlier reading left out.

## Coronas

A light with `bCorona` (in 33 of The Fallen's maps, and some
projectiles) draws a corona: its `Skin` (else its `Texture`, the lens
flares of `ds9genFX.LensFlares`, if it's drawn as a sprite: a mesh's
`Texture` is its editor icon, and the red orb, a pickup with `bCorona`,
glowed as the Inventory icon's crate on green; nor one of the editor's own
icons, Engine's `S_` textures: M05_KiraL1 has a ZoneInfo and a plain light
with `bCorona` and no flare, which glowed as a bulb and a question mark) as a glow where it is, added over
everything, the same size on the screen however far away: the texture's
width times `DrawScale` times 8 on a 960-wide frame. In a recording of
the original the credits station's flares (`coronaora02` at 0.025,
`coronaora01` at 0.05, both 128 wide) were about 15 and 22 pixels across,
the glow's visible part about 60% of its quad. The light's own
brightness doesn't matter: those have `LightBrightness` 0 and glow in
their texture's orange. Shown only while the level's walls aren't between
the camera and the light (a light inside its lamp's fixture counts as
seen) and its zone is seen, fading in and out over a fifth of a second.
Reluva's reading: the original's code isn't known (`MapViewer::addCoronas`).

## Translucent lit surfaces

A translucent surface the lights reach (not `Unlit`: The Fallen's title
logos, flagged translucent and special-lit, each lit by its own
`TriggerLight` that a dispatcher turns on and off) is drawn in one pass,
so it can't take its lightmap texel by texel: it's tinted by its light,
the average over its polygons' corners and middles of its lightmaps
(following its changing lights) and the dynamic lights, times two, at
most its texture's colour (fitted to the recording; the plain average
was 2.5 times too dark). Drawn at full brightness before, the logos all
showed at once, the Collective logo still there over The Fallen's; now
each fades in and out with its light, the gaps between them as in the
original.

## The screen flash

The player's `FlashScale` and `FlashFog`, which `PlayerPawn.ViewFlash`
works out each tick from the head's zone (`ViewFlash`, `ViewFog`) and
the flashes of being hurt or picking things up, tint the scene: every
pixel times the scale, plus the fog, under the HUD. Not applied before:
underwater (The Fallen's `CTV_H20Zone`, ViewFog 0.13 0.20 0.17) the view
was the land's colours where the recording is blue-green throughout.

## Distance fog

The Fallen's ZoneInfo adds `DistanceFogging` and `DistanceFogColor` (the
FarClipping group, with `bForceClearScreen`). Eighteen maps set them,
mostly on their LevelInfo: Jerrado (3500 units, a near-black red-brown
17,4,0), Hass'Terral (5000, a dark blue), mission 11 (3500, 24,12,4).
As the original does it (Render.dll's `SetDistanceClipping` and
`SetDistanceFogging`, D3DDrv's `SetFogDistance`):

- The camera's zone's own settings: its ZoneInfo's, or for a zone
  without one the LevelInfo's. A ZoneInfo without them has no fog
  (Reluva used to fall back to the LevelInfo's).
- Fog grows linearly with depth in the view, from none at the camera to
  all `DistanceFogColor` at `DistanceFogging` (Direct3D's linear table
  fog, start 0, end that distance). A pass over the level's opaque and
  masked surfaces and the meshes blends the colour over by depth
  (`drawFogOver`).
- Nothing beyond `DistanceFogging` is drawn: the far clipping plane is
  put there, unless the camera is in a sky zone. The sky, drawn from the
  sky zone, isn't fogged or clipped.

How far it reaches in the recording isn't measured: its views of those
maps didn't match ours closely enough.

## Drawing

The level's surfaces are one-sided, as UE1's are, unless flagged
`TwoSided` (0x100): a camera inside solid sees through the wall it's in.
M03_SiskoL1A's opening rides down inside the rock beside the lift shaft
and, in the original, looks in at the far wall's lamp and braces; drawn
two-sided, all it saw was the back of the near wall.

A mover's polygons (its brush's, drawn in their own coordinates) wind
the other way round to the level's, so they're culled the other way:
from inside M04_WorfL1a's turbolift (CTV_ElevatorMover2, whose brush has
144 untextured outer faces), culled as the level's, all that showed was
those faces from behind, white lit green.

Lightmaps are packed into 1024x1024 pages, each with a one-texel border
copying its edges so filtering doesn't bleed. They hold the light halved,
and are drawn over the surface's texture doubled (destination times
source times two); with the light capped at 1 that's the texture's own
colours at most, but the headroom is there for a game that brightens
past them. In the Maps page `L` turns them
off (shading by facing instead) and `[`/`]` dim or brighten them, for
comparing with the original.
