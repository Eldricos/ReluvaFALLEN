# Star Trek: New Worlds: watched in play, mission J1 (Romulan side)

Joint mission J1 ("Into the Arena", `icewrld`'s `j1` map, the snow world
Mazikaan) played as the Romulans, in a 36-minute recording (2160.8 s) with
commentary (the Drive file "Romulan Mission 3"). Times are
the recording's; messages are paraphrased. For the other two sides see
[play-j1f.md](play-j1f.md) and [play-j1k.md](play-j1k.md).

The mission is identified three ways: the loading screen reads "JOINT LEVEL
01" (not "ROMULAN LEVEL 03") over the three emblems; the audio holds the
seven `J1R` clips (`J1R005`-`J1R011`, all there are in RomVO); and the
exhausted mines lie on deposits of `j1` (below). So the Romulan campaign,
like the other two, plays the joint missions as its own: R1, R2, then J1
(the video's "Mission 3"), matching the level.dat order R1, R2, J1.

## Loading and briefing (0:00-2:53)

0:00-0:04 is the uploader's title card. 0:05-0:12 the loading screen
(JOINT LEVEL 01, with the progress lines for the briefing and the officers
and the percent-done lines). From 0:14 the Romulan emblem, and at about 0:16-0:21 the
INCOMING TRANSMISSION page (`jnt01rom.stb` 18: the title and stardate 9041.4)
over the emblem, silent, as in the Federation recording.

| Clip | Starts | Page (`jnt01rom.stb` id) | Shown |
|---|---|---|---|
| `J1R005` (40.2 s, score 0.44) | 0:22 | STELLAR CARTOGRAPHY (3) | the galaxy, the red marked region |
| `J1R006` (15.8 s, 0.88) | 1:03 | PLANETARY DATA (4) | the star system: a yellow banded body and a small cyan/teal banded planet (which one is the star wasn't clear) |
| `J1R007` (28.8 s, 0.77) | 1:19 | BACKGROUND DATA (5) | ground flyover of the Romulan colony site: the Romulan hub arch (`rch`) on the concrete pad, ice spires, a dark rock; RELIEF X2 inset |
| `J1R008` (27.0 s, 0.77) | 1:49 | BACKGROUND DATA (6) | flyover past ice hills to the Taubat temple (`tt`, green-topped) by the ice caves; relief inset |
| `J1R009` (11.2 s, 0.93) | 2:16 | OBJECTIVE (7) | ground: Taubat triangles (`tw`) on the ice, the blue ice caves; relief inset |
| `J1R010` (10.6 s, 0.90) | 2:28 | OBJECTIVE (8) | the relief/grid map turning, Taubat City's blocks and a cross marker, a camera inset of the city |
| `J1R011` (10.4 s, 0.96) | 2:40 | ADDITIONAL (9) | the Romulan emblem, no other view |

So clip = id + 2, as in the Klingon table, and the rule of
[play-k2.md](play-k2.md) holds: one clip per page from STELLAR CARTOGRAPHY,
in order. RomVO has only seven J1R clips, so pages 10-17 (more ADDITIONAL
pages: the Federation as protectors of the temple, the capture option on the
APC pop-up, the 67 % destruction limit, the Federation's embassy plans, the
Klingons' attack plans, the rich mineral fields, the first encounter) have no
voice. They weren't seen: play starts at 2:53, right after clip 011 ends,
so the player apparently signed on from page 9 (or the briefing stops when
the clips run out - unknown). The Romulan galaxy step, like the others,
uses the red-marked galaxy view.

## Messages

10 messages (the counter reaches 10/10), 13 samples read; the player
deleted some along the way, so the counter restarts several times (4/4
shown three times).

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 8:56 | 1/1 | Intelligence | the Federation is building up a large presence on the planet |
| 17:47 | 2/2 | Intelligence | the Klingon hub has been destroyed |
| 17:56 | 3/3 | Intelligence | the Klingons can no longer complete their mission |
| 18:11 | 4/4 | Engineering | the mine at 116, 420 has run out |
| 21:55 | 4/4 | Operations | construction held up for lack of resources; build mines and a resource processor |
| 26:34 | 4/4 | Engineering | the mine at 076, 388 has run out |
| 28:59 | 4/4 | Intelligence | the Federation hub has been destroyed |
| 29:07 | 5/5 (?) | Intelligence | the Federation can no longer complete its mission |
| 29:40 | 6/6 | Engineering | the mine at 172, 452 has run out |
| 33:00 | 6/6 (or 8/8) | Intelligence | readings show this building is the radiation source; its containment field is misaligned and must be fixed from inside |
| 33:37 | 7/7 | Security | the raid on the Taubat base has succeeded |
| 34:11 | 8/8 | Engineering | the Taubat temple is taken |
| 35:30 | 9/9 | Engineering | the alien power source is under Romulan control |
| 35:40 | 10/10 | Intelligence | the Romulans have completed their mission |

(The 29:07 sample was read at 6/6 a second later while the player paged; its
own number is probably 5.) Generic: the three mine-exhausted messages and the
Operations resource-shortage one (new here: the K/F recordings showed the
power-shortage one instead). No power-low message appeared. Mission-specific:
the Federation build-up (goes with briefing page 13), each rival hub destroyed
and each rival side out (as in the Klingon recording, by side name), the
radiation source found (objective 23, located with a science vehicle; the
counterpart of the temple-entered message in the Federation recording,
[play-j1f.md](play-j1f.md)), the raid succeeded (objective
21), the temple seized and the power source safe (objective 25), and the
side completing.

Comparison: in all three recordings the player's side wins, and the other two
are put out by hub destruction. Here the Klingon hub falls first (17:47; the
Romulan force is seen attacking the Klingon colony from 14:30, and the
Klingon hub, owner KLINGON, is selected damaged at 16:47), then the
Federation's (28:59, after the Romulan attack on the Federation colony seen
from 25:30). J1F also had the Klingon hub destroyed first (17:32), J1K the
Federation's then the Romulans'.

Map positions given: 116, 420; 076, 388; 172, 452 (mines).

## Buildings and units

Selection panel (56 samples). Romulan buildings, with the model matched by
the panel portrait and ground views:

| Name in panel | Model | How sure |
|---|---|---|
| colony hub | `rch` (the arch) | sure (also the briefing and the ground) |
| construction yard | `rcy_a` | fairly |
| vehicle yard / "vehicle yard upgrade" | `rvy` (portrait the same for both) | fairly |
| science station / "science station upgrade" | `rsb_a` (domed body on legs) | guess; could be `rms2` |
| resource processor | `rrpp_a` (ridged box; seen large at 35:46) | sure |
| mining station | `rms` (flat X) | fairly |
| shield generator | `rcss` (A-shaped pyramid, green trim) | fairly |
| adv. power generator | `rps2` (tower on splayed legs) | fairly |

Units: ground scout, science vehicle, photon artillery, cloaked tank,
"cloaked assault vehicl" (truncated in the panel). The cloaked units'
portraits are a translucent ghost of the hull, so they couldn't be matched to
`romactor` models. Upgrades again have their own panel names ("vehicle yard
upgrade", "science station upgrade"), as in K2. A vehicle-yard ENGINEERING
build panel appears at 19:00 and 30:00.

Others: the **Klingon colony hub** (owner KLINGON, 16:47), Taubat
**"phaser defense turret"** (owner TAUBAT, 27:56, 28:37; probably `tsg`) and
**"Taubat hangar"** (owner TAUBAT, 29:18, 29:59; the triangle `tw`). The
temple (`tt`) is seen at 33:30. Native names new against J1K's "Taubat house".

## Positions

The Tricorder's first reading is TERRAIN AT <111:442> (from about 3:38; RELIEF
X2 before), i.e. block row 13.9, column 55.3: the colony site. The three
exhausted mines, read as eighths of a block row first (all are 8n + 4):

| Mine | Block (row, col) | Deposit kind in `j1` |
|---|---|---|
| 116, 420 | 14, 52 | 1 (neighbours all 1 but one) |
| 076, 388 | 9, 48 | 1 (all neighbours 1) |
| 172, 452 | 21, 56 | 2 (all neighbours 2) |

All three on deposits, so the map is `j1` and the reading rule holds; the
first mine is 3 blocks from the Tricorder reading. (The first number of the
first mine is blurred, 116 or 156; only 116 lies on a deposit.) The Klingon
recording's mine (052, 148: row 6, col 18) is on the far side of the map.

## Start

Play starts at 2:53, camera over the Romulan concrete pad looking at the hub
arch (`rch`), with a low building and a small one beside it and two ground
scouts (3:05-3:35). The NEW WORLDS panel's JUMP TO list shows five entries
(the hub, an X-shaped building, a short pod, a tall slab, a crystal-like
unit icon); which models these are is a guess (construction yard, power
generator?). Starting stock (3:20): colony size 29/90, dilithium 500,
polycomp 988, thermoconc 2211, transalumin 497, nitrium 3500, duranium 5000.
The hub panel (3:38): ROMULAN, operational, shield cover high, efficiency
100 % (110 % from 4:49); generator LOW, consumption 55 %, demand met 100 %,
reserve 67 %; officers 5, crew 0, pool 144; workbee 7, cargobee 2,
constrctnbee 6, transport 2.

Colony size: 29/90 (4:15), 35/90 (5:31), 60/90 (17:58), 67/90 (23:07), 69
(24:00), 70 (24:15), 71 (26:47), 74 (31:50), 75/90 (35:46). Capacity 90
throughout, like K2; J1K grew from 65 to 90.

## Objectives and end

`jnt01rom.stb` objectives: 21 (capture and control 30 % of the settlement),
23 (locate the temple with a science vehicle), 25 (secure the temple and make
the reactor safe); failures 22 (hub destroyed), 24 (beaten to the temple), 26
(temple destroyed). No COMMAND / objectives panel was opened in the
recording. Won: the Romulans' completion message at 35:40; at 35:52 a loading
screen with the debriefing line and JOINT LEVEL 01; at 35:55 the Romulan emblem with a
CONTINUE button; at about 35:58 a MISSION REPORT panel over the emblem: a
"mission log" line and the three objectives 21, 23, 25 each marked complete,
with small Romulan emblem icons. The recording ends at 36:01.

## Interface

The Romulan panels have the same layout as the others (NEW WORLDS, TRICORDER
with COMMS / ALLY / ZOOM, COMMUNICATIONS with DELETE, arrows, MISSION and X)
with grey-green frames, cyan status text and blue message text. Briefing page
boxes have a green title bar. Portraits by section:

- **Intelligence**: pale grey-white skin, black bowl-cut hair with straight
  fringe, pointed ears, grey-violet uniform; green-lit panel behind the head.
- **Engineering**: darker, ruddy-olive skin, frowning, dark purple-black hair
  in the Romulan cut with a V fringe, dark uniform, two cyan lights either
  side at shoulder height.
- **Security**: the same portrait as Engineering, as far as the 300-pixel
  panel shows.
- **Operations**: smooth tanned face, dark purple hair, dark uniform, grey
  consoles with green screens either side.

## Open questions

- Did the briefing end because the player signed on, or does it stop after
  the last clip? Pages 10-17 never shown.
- Is the Security portrait really the Engineering one, or a near-identical
  face? Match all four against `ptrt/crew/lg/r_c1..`.
- Which body in the PLANETARY DATA view is the star.
- The science station model (`rsb_a` or `rms2`), the start buildings in
  JUMP TO, and the cloaked units' models.
- The exact numbers on the 29:07 and 33:00 messages (deletions make the
  counter jump).
- Whether the 30 % capture was done with the APC "capture" option (page 11);
  the recording shows a raid, then the success message, not the capture
  itself.
