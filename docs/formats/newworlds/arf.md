# Star Trek: New Worlds: `.arf` animation frames

Status: **known**. Chunk tag `0x25`, stored with the models (27 in
`kliactor.cat`: `geo/kln/conbee00.arf`-`conbee15.arf`, the construction
vehicle's arm; `geo/fed/fcmstand.arf` and `fcmwalk0.arf`-`fcmwalk9.arf`, a
crew member standing and walking). Worked out October 2026; the model viewer
plays them (K).

Each file is one frame of an animation: where every animated part is.

```
u32 count            the model's animated parts (conbee 3, crew 13)
count × 58 bytes, in the order of the .mdl's animated list:
    9 × f32          rotation, row by row (a proper rotation in every file)
    f32              scale (1 in every file)
    u16 0x1c, u32 12, 3 × f32 position
```

The same 58-byte record holds a `.geo`'s attachments and a `.mdl`'s
reference pose (see [mdl.md](mdl.md)). Positions and rotations are in the
model's own space, not relative to a parent part: in `fcmstand` the
positions are the joints (neck (0, 2, 52), hips (±6, 0, −10), knees
(±8, 0, −40), ankles (±8, 1, −71), shoulders (±20, 1, 45), elbows
(±23, 1, 18)).

## Posing a model

A model's geometry is stored in its reference pose, which its `.mdl` gives
(one record per animated part after the states). A part moves with the
animated part of the same name (geometry part's node number → the `.mdl`
parts list's name → the animated list's number), each vertex as

    v' = frame position + frame rotation × reference rotation^T × (v − reference position)

That makes the crew walk with their limbs joined, and the construction
vehicle swing its arm through 180° and back over its 16 frames.

## The sets in every actor archive

Checked on all four actor archives (October 2026):

| Archive | Sets | Frames | Model (its `.mdl` states) |
|---|---|---|---|
| `fedactor` | `fed/fconb` | 17 | `fconbee`, the construction vehicle (`reference`, `manipulate`) |
| `kliactor` | `kln/conbee` | 16 | `conbee` |
| `romactor` | `rom/rcb` | 17 | `rcb` |
| all three | `fed/fcmstand`, `fed/fcmwalk` | 1, 10 | every side's `crewman` (`reference`, `stand`, `walk`) |
| `foractor` | none | | |

So a set plays the state it's named after (`fcmstand`: stand,
`fcmwalk`: walk), or the model's only other state (the construction
vehicles': manipulate), and the crewmen of all sides share one set of
animations, stored under `fed/` in each archive (identical copies). The
Federation's set is named `fconb` for the model `fconbee`. The animated
list's numbers are each part's position in the frames: `conbee` lists
`arm=0 boom=2 grabber=1`, `fconbee` and `rcb` `arm=0 boom=1 grabber=2`, and
both play correctly that way.

In play, two construction bees of the side play their `manipulate` set
on each construction pad while the building goes up (game.md#the-colony).

## Open questions

- At what speed the frames play (the viewer plays 10 a second), and whether
  `walk` loops.
