# Unreal Engine 1: collision against a level

How Reluva tells solid from open space in a level, and sweeps points and
boxes through it (`engines/unreal/collision.h`). It reads only what the
level's model already holds ([model.md](model.md)): its BSP nodes, its
leaf hulls and the "root outside" flag. The boxes follow Engine.dll's
`UModel::PointCheck` and `LineCheck`, read for what they do and written
here anew.

## Solid and open

A level is carved out of solid space by subtractive brushes, with
additive ones put back into it. Its BSP divides space by the brushes'
polygon planes. Walking from the root to a point:

- The walk starts with the model's **root outside** (its second-to-last
  i32): whether the space before any plane counts as open. It's 0 (solid)
  in every map The Fallen has.
- At a node, the point is in front (distance ≥ 0) or behind. A node from
  a solid (CSG) brush decides the space: in front is open, behind is
  solid. A node with no vertices, or with flag **0x01** (from a
  non-solid brush: semisolid or non-solid, drawn but carving nothing) or
  **0x20**, leaves it as it was, as the engine's checks have it. As a rule, with *outside* carried down the tree:
  front: `outside || csg`; back: `outside && !csg`.
- Where the walk leaves the tree (a child index ≤ 0), *outside* says
  whether the point is open.

Checked on The Fallen: 540 of its 543 navigation points (path nodes,
player starts and the like, which UnrealEd puts in open space) come out
open, and a line down from each finds a floor.

## Boxes

Actors collide as boxes: half sizes radius × radius × height (UE1's
cylinders, as boxes for the world). Reach along a normal is
`|nx|·ex + |ny|·ey + |nz|·ez`.

- **The walk.** Down the BSP with the box made 1.1 times bigger: it's on
  a node's side if its centre is within that reach of it, and may be on
  both. A box that reaches a solid leaf (the outside rule above) has only
  found a hull to test.
- **The hull.** Each node with a `collisionBound` has a convex hull in
  the model's leaf hulls, an array of i32 starting there:
  - node indices, each one's plane a face of the hull, facing out (bit
    0x40000000 set: the plane flipped), up to −1;
  - then six floats stored as their bits: the hull's bounding box, min
    then max.
  
  A solid leaf counts only if the box, at its true size, overlaps its
  node's hull. A node with no hull has nothing to hit.
- **What the hull is tested against.** Each plane in turn, and the box
  is clear if any one has its centre at least its reach in front:
  - the hull's planes;
  - the bounding box's six faces, 0.1 out;
  - bevels: for two of its planes that face opposite ways along an axis
    (X, Y or Z) but agree across it ((axis × n1) · (axis × n2) > 0.001),
    the plane through the line where they meet, square to that axis,
    facing as the first.

  These bevels and box faces are what keep a box from stopping early at
  an outside corner, where planes only widened would cross in front of
  it. Before this, M07_SiskoL1A's player start (by a ramp) fit nowhere,
  and a box stuck in such a sliver on M04_SiskoL1 kept landing in place.

## Sweeps

`trace(start, end, extent)` sweeps a box (or a line: extent 0) and
returns the first time it enters solid space.
- **The walk.** The tree is walked with the part of the move,
  `[f0, f1]` (fractions 0–1), on each side of each node's widened plane
  (times 1.1 for a box). Along the move the distance changes linearly.
- **A line** stops at the first solid leaf it reaches. The plane where
  that leaf's part began is the surface hit, with its normal facing back
  along the move. A line that starts in a solid leaf says so
  (`startSolid`).
- **A box** clips the whole move against the hull of each solid leaf's
  node. For each plane, the box's entry time is the latest time it
  crosses a plane moving in (one it starts within its reach of counts as
  0). Its exit time is the earliest time it crosses one moving out. If
  it runs alongside a plane, outside it, the hull isn't hit. It's a hit
  if it enters (after −1) before it leaves, and the plane it entered
  last is the one hit. A hull it starts inside without crossing into
  isn't hit, so a box overlapping the world can move out.
- **Stopping short.** A sweep that hits stops 0.1 units from the plane
  it hit, measured along the plane's normal, not along the move. Coming
  in at a grazing angle, backing off 0.1 units along the move would
  hardly move it away from the plane.

On The Fallen, a box the player's size (17 × 17 × 39) traced down from a
navigation point stops 39 units above where the line down does, and
along X and Y 17 units before it.

## How movement uses it

`ScriptLevel` ([unreal_vm.md](../../engines/unreal_vm.md)) sweeps each
move through the world first, then actors' cylinders and movers. Movers
are collided with the same way as the world, through their brush's own
BSP, with the move first turned into the brush's coordinates (a mover
without one is a box). An actor that does start in solid (spawned into a wall, or pushed there
by a script) may move if the move ends in open space. Otherwise it is
first nudged to the nearest open spot (up first, then sideways, in 2-unit
steps up to 32), as spawning does. If no open spot is near, it stays put.
Spawning first tries straight up as far as the actor's own height: some
player starts sit closer to the floor than the player's half height
(M06_SiskoL1B's and M06_WorfL1B's, 16 units over it against 50), and
with only the 32-unit search the player was left at the world's origin.
