# Unreal Engine 1: movers and the sky zone

Status: **implemented** (`src/engines/unreal/actors.cpp`, drawn by the
Maps page). **Checked on The Fallen** (`reluva-tool ue-check`, October
2026): 2103 movers over the 74 maps, all reading (one, M07_SiskoL3A's
Mover128, has a brush whose own BSP doesn't hold together; its Polys are
fine, so that BSP is skipped); 58 maps have a sky zone. In the Maps page
the skies look right in every map, and movers sit, turn and move as in
the original.

Sources: public community documentation of the UE1 formats and of
UnrealEd's brush, mover and zone properties (as for
[package.md](package.md)). The rotation formula is the usual one for
Unreal's rotators (pitch, yaw, roll, 65536 to a turn).

## Classes

An actor's class is found by following class references: the export's
class (often imported from Engine.u or the game's own packages), then each
class's super reference in the package defining it, until the name
matches (`isA()`). So The Fallen's own mover classes count as movers
whatever they're called.

## Movers

Movers (doors, lifts, anything that moves) are brushes kept out of the
level's BSP: each is drawn from its own polygons, wherever it is. From
its properties:

| Property | Meaning |
|---|---|
| Brush | its Model, whose Polys object holds the polygons |
| Location, Rotation | where it is saved (at keyframe KeyNum) |
| PrePivot | the brush point placed at Location |
| MainScale, PostScale | Scale structs: a vector (then a sheer rate and axis, unused here) |
| KeyPos[0-7], KeyRot[0-7] | keyframes, relative to the base position and rotation |
| KeyNum, NumKeys | the keyframe it's saved at; how many there are (default 2) |
| MoveTime | seconds from one keyframe to the next (default 1) |

A brush point v is placed at
`Location + PostScale * (R(Rotation) * (MainScale * (v - PrePivot)))`,
scales applied per axis. The base is Location less KeyPos[KeyNum] (and
the same for rotation); keyframe k is at base + KeyPos[k].

R's columns, for pitch p, yaw y, roll r: forward
`(cos p cos y, cos p sin y, sin p)`, right
`(sin r sin p cos y - cos r sin y, sin r sin p sin y + cos r cos y, -sin r cos p)`,
up `(-(cos r sin p cos y + sin r sin y), cos y sin r - cos r sin p sin y, cos r cos p)`.

A brush's Model has the same layout as a level's ([model.md](model.md)),
its BSP usually empty; its Polys reference is read from there. The BSP
isn't used for movers, so one that doesn't hold together is dropped
rather than refusing the mover.

### Polys

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) |
| i32 × 2 | polygon count, and the count allocated |
| polygon each | below |

| Type | Meaning |
|---|---|
| compact | vertex count |
| f32 × 3 × 4 | base point, normal, texture U and V axes |
| f32 × 3 each | the vertices |
| u32 | poly flags ([model.md](model.md)) |
| compact × 2 | actor, texture: object references |
| compact | item name (name index) |
| compact × 2 | link, brush polygon |
| i16 × 2 | pan U, V |

Texture coordinates as for the level's surfaces:
`((v - base) . U + panU, (v - base) . V + panV)` in texels.

### Drawing

The Maps page draws movers where they're saved; O moves every mover to
its next keyframe over its MoveTime (straight lines, as the default
glide; the game's script picks the real moves). Their light is worked out
at each vertex from the level's lights without shadows, where they start
(the original lights movers as they move; not done yet).

## The sky zone

A level's sky is a small room elsewhere in the map holding a SkyZoneInfo
actor. Where a zone's walls have the fake backdrop flag (0x80), the
original shows that room as seen from the SkyZoneInfo, turned the way the
player looks. The Maps page does the same: the level drawn from the sky
zone actor's Location with the camera's direction, then the view drawn
over it from scratch, its backdrop surfaces writing only depth (so what's
behind them stays hidden and the sky shows). K turns it off (backdrops
then drawn a plain colour). With several sky zones the first is used;
the original picks per zone.

## Class defaults

A placed actor stores only the properties that differ from its class's
defaults, and a class only those that differ from its parent's. So an
actor's mesh, draw type and scale usually come from its class. The
defaults are the last thing in a class's data, after its script and
just after its config name (and before that, the class it's within).

Reading a class properly means parsing its script (part of running
UnrealScript, not done yet). Meanwhile `findTrailingDefaults()` finds
them by where they end: the earliest start within the last 64 KB of the
class's data from which tagged properties read cleanly to exactly the
end, each a plausible size for its type, with a compact name index and a
compact object reference just before. Later starts that also read are
tails of the same list, so the earliest is kept; without the config name
before it, a list's start isn't taken. `ue-check` counts the classes
whose defaults were found and weren't. An actor's properties are then
its own over its class's defaults over each parent's
(`ClassDefaults::actorProperties()`), each property's object references
read in the package it came from.
