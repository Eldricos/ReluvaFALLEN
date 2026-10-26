# Unreal Engine 1: static lighting (lightmaps)

Status: **implemented** (`src/engines/unreal/lighting.cpp`, drawn by the
Maps page). **Data checked on The Fallen** (`reluva-tool ue-check`,
October 2026): every map's lighting reads, with no bytes left over after
the model's last field; 345952 lightmaps over the 74 maps, every
light actor's properties read. How lit levels look is still to be
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

For each sample, its position on the surface's plane, then:

    light = zone ambient + sum over the lights whose bit is set of
            colour * (1 - distance / radius) * facing * cone

- **Colour** (`lightColor()`): `LightHue` round the colour wheel (0 red,
  43 yellow, 85 green, 128 cyan, 171 blue, 213 magenta), mixed with white
  by `LightSaturation` (255 white, 0 the pure hue), times
  `LightBrightness / 128`. Defaults as `Engine.Light`'s: brightness 64,
  hue 0, saturation 255.
- **Radius**: `25 * (LightRadius + 1)` world units; default radius 64.
- **Facing**: the cosine between the surface's normal and the direction to
  the light (none for `LightEffect` 13, non-incidence).
- **Cone** (`LightEffect` 8 static spot, 12 spotlight): the light's
  `Rotation` gives its direction, `LightCone` (0-255) its half-angle as
  0-90 degrees, fading to the edge.
- **Zone ambient**: the surface's zone (the front zone of its first node)
  gives its ZoneInfo's `AmbientBrightness`, `AmbientHue`,
  `AmbientSaturation`, as a colour the same way. Surfaces without a
  lightmap get only this; unlit surfaces (poly flag 0x400000) are drawn
  as their texture.
- Light effects that change over time (flicker, pulse, strobe...) are drawn
  as their steady light for now.
- **At most 1** (`kMaxLight`): the texture's own colours. In the
  original, a lamp right against a wall (TrainingRoom, the two over the
  far door) shows its texture's detail, and the wall round it keeps its
  colour; summed as above the light there is well over 1, and drawn so it
  washed out to white. So the original doesn't brighten surfaces past
  their texture (as UE1's renderers do with 1x blending), and Reluva caps
  it the same.

The 128 brightness unit, the linear falloff and the cone's angle are the
first things to tune against the original.

## Compared with the original (TrainingRoom, October 2026)

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

## Drawing

Lightmaps are packed into 1024x1024 pages, each with a one-texel border
copying its edges so filtering doesn't bleed. They hold the light halved,
and are drawn over the surface's texture doubled (destination times
source times two); with the light capped at 1 that's the texture's own
colours at most, but the headroom is there for a game that brightens
past them. In the Maps page `L` turns them
off (shading by facing instead) and `[`/`]` dim or brighten them, for
comparing with the original.
