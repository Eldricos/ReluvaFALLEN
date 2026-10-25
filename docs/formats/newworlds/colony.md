# Star Trek: New Worlds: starting colonies

The maps don't hold the colonies a mission starts with ([gmf.md](gmf.md)):
the game places them from its mission scripts inside `STNW.exe`. The
recordings of play ([play.md](play.md)) show them only in passing. This
page collects what they show, and how Reluva's missions page lays out an
approximate colony from it (`src/engines/newworlds/colony.h`).

## What the recordings show

- **F1** starts with the colony hub (`fch`) on a large concrete pad, the
  camera over it, the Tricorder reading 407:209, and four vehicles.
- **K1** starts with the colony hub (`kch`) on a concrete pad and three
  other buildings, which the recording doesn't name.
- The Romulan recordings start on a concrete pad with the hub (`rch`), and
  in later missions more: R9 the hub, a low building and four cloaked
  tanks; R14 hub, construction yard, science station, vehicle yard,
  resource processor and two more; C11 a colony already built.
- **J4K** starts beside an old Klingon colony of four buildings to
  recapture. **J8K** starts with about a dozen buildings, and **C10**
  with its colony already built (52/50).
- Every other mission's start isn't seen closely enough to say. The
  colony size at the start varies (F1 20/50, F2 24/90, K2 35/90).

None of the recordings shows which way a colony faces.

## Concrete

The starting colonies stand on concrete (F1, K1, K2's notes), the planet's
`concrete` ground texture of square panels ([gmf.md](gmf.md#textures)).
From F1's recording, the only one rechecked for this:

- **It's there from the start.** At 4:10 the hub stands on a large pad,
  its edges straight along tile edges, with whole tiles of ice cut out of
  it: a patchwork of tiles, concrete or not.
- **It isn't paved under new buildings.** The vehicle yard (placed 6:40)
  goes down on concrete already there. The accommodation block's outline
  goes down on the ice beyond the pad at 19:08, and the block is finished
  there at 20:54 on bare ice, the pad's edge beside it.
- The sickbay (selected 26:40) stands on a block-sized square of concrete
  jutting from the pad, so the pad follows the starting buildings in
  places. The natives' structures stand on bare ice.
- **It isn't in the map.** The map has ordinary ground tiles under the
  pad. Its `concrete` tiles (model 30) are single tiles or pairs on every
  map, mostly at positions other maps share, never near a colony in a
  group ([gmf.md](gmf.md#not-in-the-map)).

So the pad comes with the starting colony, from the mission (in
`STNW.exe`), as its buildings do. Its exact extent per mission isn't known.

## Where the colonies are

Only F1's position is read directly (the start's Tricorder readout). For
the rest, the first mines the colony reported exhausted stand in: mines
lie beside the colony. Where two did, the middle of them is used.

| Mission | Side | Position | From |
|---|---|---|---|
| F1 | Federation | 407:209 | the start's Tricorder readout |
| J2 | Federation | 220:412 | a mine (the other, 196:436, lies on no deposit and was probably misread) |
| F12 | Federation | 212:196 | a mine (reported late, at 31:28) |
| J9 | Federation | 312:392 | between mines 316:404 and 308:380 |
| C9 | Federation | 260:300 | a mine |
| K1 | Klingon | 244:92 | a mine (reported late, at 35:02) |
| K2 | Klingon | 300:284 | between mines 324:260 and 276:308 |
| J1 | Klingon | 52:148 | a mine |
| J2 | Klingon | 92:404 | a mine |
| J3 | Klingon | 204:172 | a mine |
| K7 | Klingon | 160:440 | between mines 172:444 and 148:436 |
| J4 | Klingon | 340:180 | the first of several mines |
| K12 | Klingon | 84:252 | a mine |
| K14 | Klingon | 100:292 | a mine (on K12's map) |
| J9 | Klingon | 308:368 | between mines 316:380 and 300:356 |
| C10 | Klingon | 292:364 | the first mine (the second, 276:444, is ten blocks away) |
| R2 | Romulan | 260:356 | between mines 220:380 and 300:332 |
| J1 | Romulan | 111:442 | the start's Tricorder readout |
| J2 | Romulan | 166:184 | the readout the player kept returning to |
| J3 | Romulan | 103:311 | between mines 124:300, 76:284 and 108:348 |
| J4 | Romulan | 276:380 | a mine |
| R9 | Romulan | 308:428 | the first of five mines |
| J6 | Romulan | 92:196 | a mine |
| J7 | Romulan | 200:324 | between mines 188:324 and 212:324 |
| R12 | Romulan | 260:264 | between the first mines, 244:252 and 276:276 |
| R14 | Romulan | 260:272 | between mines 244:260 and 276:284 (on R12's map) |

The other missions (F2, F3, F8, F14, J1, J3, J4, J6 to J8 as the
Federation, K8, J6 to J8 as the Klingons, and R1, R8, J8, J9 and C11 as
the Romulans) have no position yet.

Every mine position the recordings give is a block's middle (8 × n + 4 in
both numbers: 404:220, 244:92, 300:284), so mining stations stand one to
a block, on its deposit.

## Buildings and models

The scripts' building names, and the models the recordings matched to
them ([play-f1.md](play-f1.md), [play-k1.md](play-k1.md); the Romulan
column from the Romulan recordings, see [Romulan matches](#romulan-matches)
below). Those marked *name* aren't matched in a recording yet but are named
so by the game's own metamodels (below); the turrets marked *guess* by their
models' names alone.

| Building | Federation | Klingon | Romulan |
|---|---|---|---|
| Colony hub | `fch` | `kch` | `rch` |
| Colony hub upgrade | `fch_b` | `kch_b` | `rch_b` (the tower; `rch_c` the next stage) |
| Vehicle yard | `fvpy_a` | `kvy` | `rvy` (upgrade `rvy_b`) |
| Construction yard | `fcy` | `kcy` | `rcy_a` (upgrade `rcy_b`) |
| Resource processor | `frps_a` | `krps` | `rrpp_a` (upgrades `rrpp_b`) |
| Power generator | `fpgs` *name* | `kpgs` | `rps1` |
| Advanced generator | `fpgs2` *name* | `kpgs2` | `rps2` |
| Mining station | `fms` *name* | `kms` | `rms` |
| Accommodation (the Klingons' Arsenal) | `accom` | `kab` | `rab` *name* |
| Sickbay (the Romulans' utility hospital) | `fsb` | `ksb_a` | `ruh_a` (upgrade `ruh_b`, likely) |
| Science station | `fss_a` *name* | `kss_a` | `rsb_a` |
| Science station upgrade | `fss_b` *name* | `kss_b` *name* | `rsb_b` |
| Security center | `fsc` | `ksc` *name* | `risb` *name* |
| Hydroponics / livestock / clone farm | `fhf` | `klf` | `rcbf` *name* |
| Storage facility (the Romulans' material storage) | | `ksf` | `rsf` |
| Landing pad | `landpad` *name* | | |
| Transporter pad | | | `rbcs` |
| Photon turret | `fpdt` *guess* | | |
| Disruptor turret | | `ksg` | |
| Static disruptor | | `kdws` *guess* | `rdds` |
| Photon defense (not a script name) | | | `rpds` |
| Shield generator (not a script name) | | | `rcss` |

### The game's names for the buildings

`misc.cat`'s metamodels (`mdl/fed/`, `mdl/kln/`, `mdl/rom/`) name each
model, in the game's own words for internal use (`reluva-tool mdl-info`):

- **Federation:** `fch` hub (`_b`, `_c` its upgrades), `fcy` construction
  yard (upgrades `_b`, `_c`), `fvpy_a`-`_c` vehicle yard, `frps_a`
  resource processing station (`_b`, `_c`), `fpgs`/`fpgs2` power
  generator and advanced, `fms`/`fms2` mining station and advanced, `fsb`
  sickbay (`_b`), `fss_a` science station (`_b`), `accom` accommodation
  block, `fhf` hydroponics farm, `landpad` landing pad, `fpdt` defence
  weapon, `fpds` defence artillery, `fmsg` mobile shield.
- **Klingon:** `kch` colony hub (`_b`, `_c`), `kcy` construction yard,
  `kvy` vehicle yard, `krps` storage processor (the panel's resource
  processor; `krps_i` its interior), `kpgs`/`kpgs2`, `kms`/`kms2`, `ksb_a`
  sickbay (`_b`, `_c`), `kss_a` science block (`_b`-`_d`), `kab`
  accommodation block (the panel's Arsenal), `ksc` security centre, `klf`
  livestock farm, `kief` import/export, `kdwsp` defence artillery, `kmdb`
  disruptor battery. `ksf`, `ksg` and `kdws` have no metamodel.
- **Romulan:** `rch` hub (`_b`, `_c`; `rch_i` interior), `rcy_a`
  construction yard (`_b`), `rvy` vehicle yard (`_b`), `rrpp_a` storage
  processor (`_b`), `rps1`/`rps2` power generator and advanced,
  `rms`/`rms2` mining station and advanced, `ruh_a` sickbay (`_b`),
  `rsb_a` science block (`_b`), `rab` accommodation block, `risb` security
  centre, `rsf` storage facility, `rcbf` clone farm, `rbcs` border control
  station, `rdds` defence weapon, `rpds` defence artillery. The `_i`
  models are interiors.

The names the panel shows can differ from these (the Klingons' Arsenal).

### Romulan matches

The Romulan GUI archive (`romgui.cat`, `lbp/gui/rom/ptrt/`) holds the
selection panel's portraits, large and small, as pictures named after the
building or craft (`build/`, `craft/`, and `upgde/` for upgrades). They are
pre-rendered views of the models, so a portrait seen in a recording names
the portrait file, and the file's picture shows which model it is. Most
portrait names are the model names or close to them; the ones that are not:
`rchub` the hub, `rms1` the mining station, `rrpp`/`rsb`/`ruh` the bases
drawn with their upgrade bays as see-through outlines, `rlp` the transporter
pad (`rbcs`), `rsg` the shield generator (`rcss`), `rddt` the photon defense
(`rpds`), `rdsb` the static disruptor (`rdds`), `rf` the clone farm
(`rcbf`), `r_hcp` the Hubrin containment pillar; crafts `rshut` the shuttle
(`rs`), `rconb`/`rcrgb` the construction and cargo bees, `rptl` the photon
artillery (`rpa`), `rcapc` the cloaked assault vehicle, `rt` and `rct` the
tank uncloaked and cloaked, `rctp` the unarmed APC (`rapcnt`). These were
checked against the recordings (October 2026; frames in the scratchpad
only):

| Panel name | Model | Evidence | How sure |
|---|---|---|---|
| Colony hub | `rch` | the four-legged arch in every Romulan start; portrait | sure |
| Colony hub upgrade | `rch_b` | a tall column standing inside the arch (J9R 13:08); the metamodel calls it the first upgrade; the panel then offers "improved hub" | fairly |
| (improved hub) | `rch_c` | the spiked grey plate at the arch's foot, seen only after the first upgrade (J9R 13:08, not at 4:06); the second upgrade by its metamodel | fairly |
| Construction yard / upgrade | `rcy_a` / `rcy_b` | the flat cross with raised green-lit corners; the ramp-shaped block joined to its side (R1 12:45, R2) | sure / likely |
| Vehicle yard / upgrade | `rvy` / `rvy_b` | octagonal ring; the upgrade is the pointed grey-faced wedge joined to it, selected on its own (R1 12:45) | sure / fairly |
| Science station / upgrade | `rsb_a` / `rsb_b` | the low six-bayed dome (J9R 7:08); upgrades are ribbed slanted slabs standing in its bays (R2 14:51, J9R 13:05, J7R 23:15) | sure / fairly |
| Resource processor / upgrade | `rrpp_a` / `rrpp_b` | portrait is `rrpp_a` (three fins over a grey front); its five upgrades (the panel lists one plant per material) have one model | sure / likely |
| Power generator, adv. | `rps1`, `rps2` | tower on a flat base (R12 40:44, selected) and tower on splayed legs; portraits | sure |
| Mining station | `rms` | flat X; portrait `rms1` | sure |
| Utility hospital (sickbay) / upgrade | `ruh_a` / `ruh_b` | the low block with pyramid ends and green panels, selected (R2 15:01); its portrait shows a tall slab in its bay, the shape of `ruh_b` | sure / likely |
| Material storage | `rsf` | the slatted block beside the hub in every start (R1, R2, J8R, J9R, C11), selected in C11 at 0:40; portrait `rsf` | sure |
| Transporter pad | `rbcs` | the rounded block with a raised lip, a transporter glow beside it while selected (R2 16:30); portrait `rlp` | fairly |
| Static disruptor | `rdds` | portrait `rdsb` (grey finned column on a dark base); small dark posts round the colony | fairly |
| Photon defense | `rpds` | portrait `rddt`, the tall pale slab (R2, J6R) | fairly |
| Shield generator | `rcss` | portrait `rsg`, a four-legged pyramid with a green-lit base (R9 48:43); small green-based pyramids on the J7R pad | fairly |
| Security centre | `risb` | not selected; a tall ribbed tower with a grey middle strip stands on several pads (R2, J9R), probably the "tall finned tower" of J7R | tentative |

The upgrades are separate models that stand in the base's bays (the base
metamodels list `BAY_0`-`BAY_5` places), which is why an upgrade can be
selected on its own. Reluva still draws an upgrade on its base's spot.

Units, by portrait: ground scout `rfgs` (the surveillance squad shows the
same portrait, so probably the same craft), science vehicle `rsv`, APC
`rapc`, cloaked assault vehicle the see-through `rapc` shape (no model of
its own found; tentative), photon artillery `rpa` (a hover craft, not a
building: six of them at J6R 33:23, pale with green lights), cloaked tank
`rct` (the forked hull; the disruptor tank shows the same hull uncloaked).
J4R's six starting units have small forked icons, so probably tanks
(tentative).

Most Klingon buildings have a texture number the name rule can't fill
([geo.md](geo.md#textures)), so a face of each (often its top) shows
untextured.

## Reluva's layout

The missions page (**C** shows or hides the colony, **V** looks at it) lays
out a colony like this. It is Reluva's own approximation, not the game's
layout:

- Buildings stand on the map's tile grid (half a block, 4 map positions),
  each over as many tiles as its model needs with a small gap, centred on
  its model's middle (some models' origin is at a corner: `rch`, `rms`).
- A tile is free if nothing stands on it, its ground is flat, near the
  hub's height and on the map, and its block has no deposit.
- The hub goes within six tiles of the site, where the most free tiles
  are around it, less a little for each tile it moves.
- Each building the mission's script builds (its `built` events, as **X**
  steps through them) goes on the free place nearest the hub; a mining
  station goes on the nearest deposit instead, and an upgrade on the
  building it upgrades.
- The starting colony stands on a pad: the level, deposit-free tiles
  within two tiles of the hub, drawn with the planet's concrete texture.
  Buildings the script adds get none (as in F1's recording). On rolling
  ground (K1's site) there may be no level tiles, so no pad.
- Trees and rocks under a building or on the pad aren't drawn.

What would settle the real layout and pads: a starting colony seen from
above in the running game, for any mission, with the Tricorder's readout
at the pad's corners.
