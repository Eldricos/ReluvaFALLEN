# Unreal Engine 1: collision against a level

How Reluva tells solid from open space in a level, and sweeps points and
boxes through it (`engines/unreal/collision.h`). It reads only what the
level's model already holds ([model.md](model.md)): its BSP nodes and
the "root outside" flag. Clean room like the rest of the engine: this is
Reluva's reading of the data, checked against The Fallen's maps, not
Epic's code.

## Solid and open

A level is carved out of solid space by subtractive brushes, with
additive ones put back into it. Its BSP divides space by the brushes'
polygon planes. Walking from the root to a point:

- The walk starts with the model's **root outside** (its second-to-last
  i32): whether the space before any plane counts as open. It's 0 (solid)
  in every map The Fallen has.
- At a node, the point is in front (distance ≥ 0) or behind. A node from
  a solid (CSG) brush decides the space: in front is open, behind is
  solid. A node with flag **0x01** comes from a non-solid brush
  (semisolid or non-solid, which are drawn but carve nothing), and leaves
  it as it was. As a rule, with *outside* carried down the tree:
  front: `outside || csg`; back: `outside && !csg`.
- Where the walk leaves the tree (a child index ≤ 0), *outside* says
  whether the point is open.

Checked on The Fallen: 540 of its 543 navigation points (path nodes,
player starts and the like, which UnrealEd puts in open space) come out
open, and a line down from each finds a floor.

## Boxes

Actors collide as boxes: half sizes radius × radius × height (UE1's
cylinders, as boxes for the world). A box is on a node's side if any of
it is: its centre within the box's **reach** along the node's normal,
`|nx|·ex + |ny|·ey + |nz|·ez`, of that side. A box can be on both sides,
and then both are walked. It overlaps solid if any leaf it reaches is.

The planes are only widened, without the extra "bevel" planes UE1 adds
around sharp outside corners (the model's leaf hulls, which Reluva reads
but doesn't use). So near a corner that sticks out, a box is stopped a
little early, and widened planes cross to make thin slivers of solid
just outside the corner. Two things keep these slivers from trapping an
actor:

- **Touching isn't overlapping.** A box only counts as across a plane
  once it is 0.03 units past its widened position. A box resting against
  a wall then slides along it without float rounding putting it in.
- **Stopping short of the plane.** A sweep that hits stops 0.1 units
  from the plane it hit, measured along the plane's normal, not along
  the move. Coming in at a grazing angle, backing off 0.1 units along the
  move would hardly move it away from the plane.

## Sweeps

`trace(start, end, extent)` sweeps a box (or a line: extent 0) and
returns the first time it enters solid space. It walks the tree with the
part of the move, `[f0, f1]` (fractions 0–1), that's on each side of each
node's widened plane. Along the move the distance changes linearly, so
each side's part is where `d0 + f·(d1 − d0)` stays on that side. The
first solid leaf reached gives the time. The plane where that leaf's part
began is the surface hit, with its normal facing back along the move. A
sweep that starts in a solid leaf says so (`startSolid`) and goes
nowhere.

On The Fallen, a box the player's size (17 × 17 × 39) traced down from a
navigation point stops 39 units above where the line down does, and
along X and Y 17 units before it. That's what widening by reach should
give.

## How movement uses it

`ScriptLevel` ([unreal_vm.md](../../engines/unreal_vm.md)) sweeps each
move through the world first, then actors' cylinders and movers. Movers
are collided with the same way as the world, through their brush's own
BSP, with the move first turned into the brush's coordinates (a mover
without one is a box). An actor that does start in solid (spawned into a wall, or pushed there
by a script) may move if the move ends in open space. Otherwise it is
first nudged to the nearest open spot (up first, then sideways, in 2-unit
steps up to 32), as spawning does. If no open spot is near, it stays put.
