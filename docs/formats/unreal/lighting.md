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

## The sample grid (guesses, checked by fit)

Where the grid lies on the surface isn't certain from the documentation,
so Reluva tries the readings that make sense and keeps the one that
fits the polygon, counting each (`ue-check`'s `grids:` line, the Maps
page's info):

- **Axes**: the surface's texture axes as stored (`raw`), or the same made
  unit length (`unit`, world units); whichever gives a polygon spanning
  about `uClamp - 1` by `vClamp - 1` sample steps of `uScale` x `vScale`.
- **Start**: along those axes from the surface's base point, the
  lightmap's `pan` taken as offsets (`pan`: X along U, Y along V), or as a
  point (`pan-point`), or failing both the polygon's own lowest corner
  (`bounds`). A reading fits when every polygon point lands inside the
  grid, give or take a sample.
- Grids no reading fits within two samples are counted `off fit`.

On The Fallen: the pan as offsets fits 345700 of the 345952 grids (pan
as a point 3, bounds 249, none off fit), so that's the layout; the rest
are likely degenerate surfaces. For the axes the first run counted
322682 raw and 23270 unit, but a surface whose texture axes are unit
length already counted as raw; `ue-check` now counts those apart
("unit already"), to show which holds where they differ.

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

The 128 brightness unit, the linear falloff and the cone's angle are the
first things to tune against the original.

## Drawing

Lightmaps are packed into 1024x1024 pages, each with a one-texel border
copying its edges so filtering doesn't bleed. They hold the light halved,
and are drawn over the surface's texture doubled (destination times
source times two), so light reaches twice the texture's own colours as
the original's overbright lighting does. In the Maps page `L` turns them
off (shading by facing instead) and `[`/`]` dim or brighten them, for
comparing with the original.
