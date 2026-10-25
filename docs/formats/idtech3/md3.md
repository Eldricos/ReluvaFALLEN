# id Tech 3: models (`.md3`) and skins (`.skin`)

Status: **implemented** (`src/engines/idtech3/md3.cpp`, drawn by
`model_cache.cpp`). **Checked on all 1156 retail Elite Force models**
(`reluva-tool model-check`, October 2026): all read, and every one of
their 3862 surfaces finds its shader or image (782 through skins) except
12 that name none: models whose shader the game code picks
(`models/misc/telep.md3`) and player heads whose skin the game chooses
(`models/players/munrocrew/head.md3`). Placed models sit where the
original puts them.

Shader names are looked up without their extension: many models name
`models/x/y.tga` where a script defines `models/x/y` (the Borg map objects
do), see [shader.md](shader.md).

Sources: the GPL Quake III Arena source (`code/qcommon/qfiles.h`, the
renderer's `tr_model.c`, `tr_surface.c`), read for the layout, not copied.

All values little-endian; integers 32-bit, floats IEEE single.

## Header (108 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IDP3` |
| 4 | i32 | version, 15 |
| 8 | char[64] | name |
| 72 | i32 | flags |
| 76 | i32 × 4 | frame, tag (per frame), surface and skin counts |
| 92 | i32 × 4 | offsets of the frames, tags, first surface, and the end |

**Frames** (56 bytes each): f32[3] bounds minimum, f32[3] maximum, f32[3]
local origin, f32 radius, char[16] name.

**Tags** (112 bytes each, frame by frame): char[64] name, f32[3] origin,
f32[3][3] axes. A tag is where another model attaches: a player's torso
sits on its legs' `tag_torso`, the head on the torso's `tag_head`, a weapon
on `tag_weapon`. What's attached is placed by the tag's axes (as columns)
and origin, in the tagged model's space.

## Surfaces

Each surface starts with a 108-byte header, offsets relative to it:

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `IDP3` |
| 4 | char[64] | name (what `.skin` files refer to) |
| 68 | i32 | flags |
| 72 | i32 × 4 | frame count (the model's), shader, vertex and triangle counts |
| 88 | i32 × 5 | offsets of the triangles, shaders, texture coordinates, vertices, and the next surface |

- **Shaders:** char[64] name, i32 index (the original's own use).
- **Triangles:** i32[3], indices into one frame's vertices. Wound like the
  maps' polygons (clockwise seen from the front).
- **Texture coordinates:** f32[2] per vertex, the same in every frame.
- **Vertices:** frame by frame, 8 bytes each: i16[3] position in 1/64 units,
  and the normal as two angles in a byte each. The high byte is the
  latitude around Z and the low byte the longitude from +Z, both
  × 2π/256: normal = (cos lat · sin lng, sin lat · sin lng, cos lng).

Files ending `_1.md3` and `_2.md3` are lower-detail copies.

## Drawing

Models have no lightmaps or vertex colours. A surface's shader (or, with
no script, the image of that name) is lit by a light: an ambient and a
directed colour from a direction. In a map that's the light grid where the
model stands (see [bsp.md](bsp.md)); `rgbGen lightingDiffuse`, `vertex` and
`exactVertex` all mean that light. Without a map, a fixed light.

Players are three models in one folder: `lower.md3`, `upper.md3`,
`head.md3`, assembled by their tags, with `<part>_<skin>.skin` choosing
their shaders.

## Player animations (`animation.cfg`)

A player's folder holds an `animation.cfg` naming the runs of frames its
parts play. How the file is read was taken from the GPL ioquake3 game code
(`cg_players.c`); Reluva's reader is `src/engines/idtech3/player_animation.cpp`.

Lines of words (`sex f`, `soundpath alexa`) are settings; every other line
is four numbers and a comment naming the animation:

    first-frame  frames  looping-frames  frames-per-second  // NAME

- Names starting `BOTH_` play on legs and torso together, `TORSO_` on the
  upper body only, `LEGS_` on the lower body only. Elite Force's file lists
  them in that order, as Quake III's does.
- The frame numbers count through both models as if one followed the
  other, but the legs model has no torso frames: a legs animation's first
  frame is that much earlier in the legs model, by the first legs
  animation's first frame minus the first torso animation's (43 for
  Elite Force's players: legs from 138 in the file, 95 in `lower.mdr`).
- After playing through, an animation repeats its last *looping-frames*
  frames, or with 0 stays on its last frame.
- Legs and torso each play their own animation at their own speed; the
  torso sits on the legs' `tag_torso` of the legs' current frame, the head
  on the torso's `tag_head`. Drawing the halves on unrelated frames is
  what tears them apart at the waist.

## Skins

Text, one surface per line: `surfacename,shader`. Lines for tags
(`tag_head,`) and empty shaders are ignored; surface names are matched
ignoring case.

## In maps

Items, weapons and characters get their models from the game code, not
the map, so the viewer shows only what entities name themselves in
`model` (not brush models, `*n`) or `model2`, placed by `origin` and
`angles` ("pitch yaw roll", or `angle` for the yaw). Static props are
usually built into the map itself as triangle soups.
