# Star Trek: New Worlds: playing on the map

Reluva's first playable layer: the missions page's play mode (**F3**), with
the game's camera and controls, the colony's starting units to select and
move, buildings to place and build, the colony's economy, and the
mission's script running on what happens. It is built from what recordings of play show
([play-tutorials.md](play-tutorials.md) for the controls,
[play.md](play.md) for the rest). Figures the recordings can't give are
named placeholders, listed below, to be replaced with readings from the
running game ([vm-checklist.md](vm-checklist.md)).

## Controls

As the first tutorial teaches them:

| Input | What it does |
|---|---|
| Left click on a unit | selects it |
| Left drag | selects the side's units inside the box |
| Left click on the ground | sends the selected units there |
| Double click on the ground (nothing selected) | locks the camera on the spot; numpad 4/6 orbit it |
| Right click | drops the selection (or the camera's lock) |
| Ctrl + left / right click | zooms in on the pointed spot / out |
| Arrow keys | move the camera forward, back, left, right |
| Numpad 4 / 6 | turn |
| Numpad 8 / 2 | zoom in towards the pointer / out ("targeted zoom") |
| Numpad 7 / 1 | raise / lower the camera |
| Numpad 9 / 3 | tilt |
| F2 | the sky view, much higher (F2 again: the bird's-eye view) |
| F1 | the first-person view, low over the ground (F1 again: back) |
| Home | back to the colony hub |
| Shift + V | to the selected vehicles (or the first one) |
| Ctrl + 1-9 | makes the selection a battle group |
| 1-9 / Shift + 1-9 | selects the group / centres on it |
| B | the build menu in the NEW WORLDS panel (B again closes it) |
| 1-9, 0 (menu open) | picks a building; its hologram follows the pointer |
| Left click (placing) | builds where the hologram is green |
| Right click or Escape (placing) | puts the hologram away |
| Tab | the full-screen Tricorder, units marked |
| I | the interface on/off |
| F3 or Escape | stops playing (Escape first closes the menu or hologram) |

The numpad keys work with NumLock on or off. The game's build menu is a
row of buttons (play-tutorials.md); here it is a numbered list. Not yet
modelled: the playbook (a double click with units selected), selecting
buildings, units defending themselves, units building (here a building
goes up by itself once placed).

## What's modelled

- **Which way models face:** −y is a model's front ([mdl.md](mdl.md#moving-parts)),
  so a unit is drawn turned to put −y along its heading.
- **Starting units:** four of the side's ground scouts (`ffs`, `gscout`,
  `rfgs`) in a row south of the hub. Every recorded colony starts with four
  vehicles of one kind selected (F1, R1); which kind isn't known.
- **Where units can go:** a tile is open unless its movement class, the
  tile flags' bits 16-17 ([gmf.md](gmf.md#tiles-and-ground-models)), is 0:
  holes, `water_01` and the rock formations, which nothing in the
  recordings crosses. Between tiles, the ground may not climb more than
  750 units in a quarter tile, so the walls between levels block and ramps
  don't. A unit finds its way round with A* over the 8192-unit tiles,
  straightening the path where a line is clear. Units sent to one spot
  spread over a small grid round it.
- **Selection:** the NEW WORLDS panel names one selected unit and its state
  (READY, EN ROUTE) as the game's does, or lists a MULTISELECTION; selected
  units carry the recordings' row of small orange marks above them.

## The colony

- **The start:** the hub alone where the recordings put it
  ([colony.md](colony.md)), the stock every recording starts with
  (Romulans a little less; [play.md](play.md)), a crew of 20 and a
  capacity of 50.
- **The build menu** offers the side's buildings that have a model
  ([colony.md](colony.md)); one is dim until what it needs stands (a
  construction yard first, a resource processor before mines), bright
  when the stock can pay for it. While placing, the panel shows its cost,
  in red what the stock lacks.
- **Where buildings go:** on the tile grid, over as many tiles as the model
  needs, on free, flat ground near the hub's height, and not on a deposit
  block; a mining station's middle must be on one. The hologram is the
  model tinted green or red. Whether the game forbids building on
  deposits isn't known (Reluva's colony layout keeps them free for mines).
- **Construction:** the cost comes off the stock when work starts (an
  order the stock can't pay waits, and the colony is told "short
  resources" once a minute). It is drawn as the recordings show it: an
  apron (the model pressed flat) for the first 15 % of the time, then the
  building rising out of it, with two of the side's construction bees
  (`fconbee`, `conbee`, `rcb`) at its corners playing their `manipulate`
  animation. How many bees the game sends, and whether they come from the
  construction yard, isn't known. When it's done, the script hears `built
  <type>` (Engineering's message, the game's spoken announcement) and the
  colony's capacity grows.
- **Power:** the hub and each generator supply 60, other buildings use
  5-15. Short of power, construction, mining and refining slow, and the
  colony is told once a minute. The title shows supply / demand.
- **Mining:** a mining station digs its block's ore (the block's map
  value × 4) for the resource processor, which refines it into the
  material of the same place in the list (ore kind 1 into dilithium,
  2 into polycomp, and so on: which ore makes what isn't known). A block
  dug dry tells the script `mine-exhausted` at its middle.
- **Crew:** grows by one every 30 s up to the capacity; the NEW WORLDS
  panel shows it as COLONY SIZE.
- **The script:** the mission's script ([mission-scripts.md](mission-scripts.md))
  starts with play: `start` fires, its timers run, and the economy's events
  reach it; its messages show in COMMUNICATIONS and its objectives in
  COMMAND.

## Placeholders

| What | Value | From |
|---|---|---|
| Ground scout speed | 4000 units a second (a block in 4 s) | by eye; to be timed |
| Animation speed | 10 frames a second | Reluva's (arf.md) |
| Units' drawn size | 4 × their models | the tutorial: a scout is a quarter of the vehicle yard's width beside it, against a sixteenth for the models |
| Bird's-eye view | 3.5 blocks away, 40° down | by eye |
| Sky view | 14 blocks, 65° | by eye |
| First-person view | 0.6 block, 6° | by eye |
| Spacing of a group's spots | 2500 units | Reluva's |
| Build time | 40 s at full power (a quarter speed at the least) | F1's vehicle yard: placed 6:40, done 7:22 |
| Costs | the training missions' build menu; accommodation's taken as the sickbay's | play-tutorials.md |
| Power | hub and generator +60; buildings −5 to −15 | Reluva's, so the hub alone runs a few buildings |
| Capacity added | +5 a building, +10 transporter pad, +15 accommodation | F1's capacity rose 50, 55, 65, 80, 90 |
| Mining | 1.5 ore a second; a block holds its map value × 4 | C5's mines met its targets in about ten minutes |
| Refining | 3 ore a second, one material per ore, kind k into material k | Reluva's |
| Crew growth | one every 30 s | F1: 20 to 40 in 18 minutes |
| Warnings | once a minute while they hold | K2's power messages a minute apart |

The code: `src/engines/newworlds/world.h` (units, passability, paths,
groups), `economy.h` (stock, construction, power, mining, crew),
`colony.h` (`ColonyLayout::spotAt`, where a building fits), the map view's game camera and picking (`viewers.h`,
`MapViewer::gameKey`), and the missions page's play mode.
