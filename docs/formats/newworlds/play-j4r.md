# Star Trek: New Worlds: watched in play, mission J4 (Romulan side)

Joint mission J4 ("Cry Havoc", `alnwrld`'s `j4` map, the bleak world
Al-Fadir in the Al-Deta system) played as the Romulans, in a 26.7-minute
recording (1602.4 s) with commentary (the Drive file "Romulan Mission 6").
Times are the recording's; messages are paraphrased. Other
sides: [play-j4f.md](play-j4f.md), [play-j4k.md](play-j4k.md).

Identified three ways: the loading screen (0:05-0:20) reads "JOINT LEVEL 04"
(progress lines for the colony systems and then the briefing, and the
percent-done lines) over the
Federation, Klingon and Romulan emblems; the audio holds the `J4R` clips
(`J4R005`-`J4R013`, `J4R015`, see below); and the exhausted mine and all
eight mineral readings lie on deposits of `j4` (below). So the sixth Romulan
mission is J4, matching the level.dat order R1, R2, J1, J2, J3, J4.

## Loading and briefing (0:00-3:58)

0:00-0:04 is the uploader's title card ("Romulan Mission 6 commentary").
0:05-0:20 the loading screen. From about 0:25 the Romulan emblem with two
silent INCOMING TRANSMISSION pages bottom left: `jnt04rom.stb` 3 (title and
stardate 9133.4, 0:26-0:33) then 4 (coded transmission, the admiral and the
research facility, 0:36-0:43). No clip under either.

| Clip | Starts | Page (`jnt04rom.stb` id) | Shown |
|---|---|---|---|
| `J4R005` (11.8 s, score 0.45) | 0:46 | STELLAR CARTOGRAPHY (5) | the galaxy, sparse stars, a red winding arm marked, turning |
| `J4R006` (22.1 s, 0.19) | 0:59 | PLANETARY DATA (6) | the star-system step: only a yellow-brown haze (a lit dust band across the middle, dark starry edges); no planet and no star body seen |
| `J4R007` (9.6 s, 0.52) | 1:22 | (no page) | the relief/grid map turning, cross marker; a magenta block cluster with yellow black-centred squares and red squares, a cyan patch at the right edge |
| `J4R008` (16.8 s, 0.34) | 1:31 | (no page) | ground: a pad with a long dark brown box building (the Klingon arsenal, see below) and a smaller box, three purple-blue Metar spires/portals beside it, small red-and-white craft flying |
| `J4R009` (22.4 s, 0.71) | 1:48 | (no page) | ground: canyon flyover, Metar spires among hills |
| `J4R010` (23.4 s, 0.76) | 2:11 | (no page) | ground: a grey box-hulled Romulan craft with two green lights flying over dark hills (`rwb`/`rcb`-like; guess) |
| `J4R011` (18.0 s, found by hand, 0.85-0.99 per 2 s window) | 2:34 | (no page) | the relief map again, turning; a second marker cluster (yellow/green and red squares) top left |
| `J4R012` (10.9 s, 0.69) | 2:53 | PRIMARY OBJECTIVE 1 (7) | ground: the arsenal pad and Metar spires again, from the other side |
| `J4R013` (17.1 s, 0.84) | 3:04 | (no page) | ground: past the Romulan hub arch (`rch`), then a group of grey-blue Romulan winged vehicles flying low over red-brown plain, a big red dome (native?) behind |
| `J4R011` again (0.88-0.99) | 3:22 | (no page) | the relief map, turning |
| `J4R015` (15.8 s, 0.69) | 3:40 | (no page seen) | the Romulan emblem |

`J4R014` (26.9 s) is not in the audio (best score 0.07). Instead `J4R011`
plays a second time over the second relief-map step (3:22-3:39, 18 s, the
step's exact length): either the script points this step at 011, or the
player stepped back (check). `jnt04rom.stb` has 13 strings: title (1),
stardate line (2), transmissions 3-4, pages 5-8, pass/fail 10-11, objective
12, failures 13-14 (no 9). Only pages 3-7 were shown; MISSION PARAMETERS (8)
never appeared (J4K shows its parameters page over the emblem with the last
clip). So the Romulan J4 briefing has one objective page and eleven clip
slots, most over views with no page: the same per-mission scripting as the
Romulan J2 ([play-j2r.md](play-j2r.md)) and J4K. There is no
model-on-a-grid step (J4F has the Metar portal, the Metar nak and a Klingon
science station on the grid).

Star-system step: J4's system has no model of its own name, and what shows
is just the haze background with nothing in it, the same yellow haze
J4F/J4K describe. No planet colours; the faint dot at the centre is the
mouse pointer.

Play starts at about 4:02, after a loading box with the three emblems
(4:01).

## Messages

7 messages; the COMMUNICATIONS panel was closed for most of the game and is
only seen from 21:03 (the overview samples show no panel before that).

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 21:03 | 1/1 | Operations | building held up for lack of resources; build mines and a resource processor (deleted at 21:06) |
| 24:48 | 1/1 | Engineering | the mine at 276, 380 has run out (deleted at 24:50; the panel then says no messages are stored) |
| 24:55 | 1/1 | Intelligence | the team is inside, searching the building |
| 25:05 | 2/2 | Intelligence | the building is captured; calibration of the anti-matter fields has begun |
| 25:18 | 3/3 | Intelligence | the Federation hub is destroyed |
| 25:29 | 4/4 | Intelligence | the Federation can no longer complete its mission |
| 26:21 | 5/5 | Intelligence | the Romulans have completed their mission |

(At 25:18.5 the new text shows with the old counter 2/2 for a moment; 3/3
from 25:20.)

Generic: the Operations resource-shortage and the mine-exhausted messages,
and the end-of-mission pair (rival hub destroyed / can no longer complete;
side completed), worded as in J4K. Mission-specific: the team-inside then
building-captured pair, as in J4K's recaptures and J4F's searches;
here the second one names the anti-matter fields of objective 7/12. The
counter shows 1/1 at 21:03, so any earlier messages were deleted unseen,
or there were none.

Map positions given: 276, 380 (mine).

## Buildings and units

Selection panel (37 samples). Romulan buildings, matched to `rombuild` by
the panel portrait and ground views:

| Name in panel | Model | How sure |
|---|---|---|
| colony hub (5:24, 7:55; shield HIGH, 100 %) | `rch` (the arch) | sure |
| construction yard (6:17, 14:27) | `rcy_a` (cross-shaped) | fairly |
| vehicle yard (8:05 ... 22:42; 120 %) | `rvy` | fairly |
| science station (7:48, 12:17, 16:03, 18:26) | `rsb_a` | sure (portrait; [colony.md](colony.md#romulan-matches)) |
| utility hospital / "utility hospital upgrade" (9:14, 11:00) | `ruh_a` / `ruh_b` (portrait a box with a tall block: the base with its upgrade) | fairly |
| resource processor / "resource processor upg" (11:30, 11:39) | `rrpp_a` / `rrpp_b` (ribbed block portrait) | fairly |
| adv. power generator (13:56, 25:57) | `rps2` (tower on splayed legs) | fairly |
| mining station (22:13, damaged) | `rms` (low four-armed cross, seen on the ground beside it) | fairly |

Upgrades again have their own panel names, truncated to fit ("resource
processor upg"). Units: ground scout (`rfgs` by the portrait file; `rs` is the shuttle), "cloaked
assault vehicl" (truncated; ghost-like portrait; "en route"), and a
MULTISELECTION of six identical units at 4:03 (claw-like icons; which unit
is unclear; the icons are forked, like the tank portraits `rt`/`rct`,
so probably tanks: tentative). An ENGINEERING build panel at 4:05 (construction-type
vehicles).

Others: a **Taubat tank** (owner TAUBAT, damaged, 11:58), HOSTILE UNIT
Tricorder readings at 11:50-14:06 and 19:10. The Metar spires (purple-blue
folded shapes, as in J4F's `mport`) stand by the arsenal. The Klingon
arsenal: a long dark brown box building with a red-and-white drop emblem
over it (24:20-24:40; the emblem looks like an objective/target marker, not
the Klingon trefoil), many Romulan units around it from about 23:30. The
large red dome seen in the briefing and from 16:00 on the ground was not
selected.

## Positions

**Mineral readings** (Tricorder, 2 samples a second, deduplicated):

| Reading | Time | Block (row, col) | Deposit kind there | Around (3x3) |
|---|---|---|---|---|
| SILICON AT <260:328> | 5:00 | 32, 41 | 3 | 3 and 6 |
| SILICON AT <305:373> | 11:46 | 38, 46 | 3 | all 3 |
| SILICON AT <253:346> | 11:51 | 31, 43 | 3 | all 3 |
| SILICON AT <246:349> | 11:54 | 30, 43 | 3 | 3, one 2 |
| SILICON AT <272:356> | 13:35 | 34, 44 | 3 | 3, two 0 |
| SILICON AT <264:348> | 14:16 | 33, 43 | 3 | 3, two 0 |
| KELBONITE AT <323:338> | 8:35 | 40, 42 | 4 | 3, one 4 |
| KELBONITE AT <308:313> | 19:59 | 38, 39 | 4 | 4, 0, 3 |

So on this map **kind 3 = silicon** (6 of 6) and **kind 4 = kelbonite**
(2 of 2, agreeing with the earlier kind 4 = kelbonite finding,
[play-r1.md](play-r1.md)). `j4`'s kinds: 0 (3313 blocks), 3 (262), 2 (133),
1 (117), 6 (112), 5 (80), 4 (79): six deposit kinds; 1, 2, 5, 6 not named
here (named in other recordings: 1 dilithium, 2 talgonite, 5 magnesite,
[play-c11.md](play-c11.md), 6 dolamide, [play-r9.md](play-r9.md)).

Mine 276, 380: block row 34, col 47, kind 3 with all 8 neighbours kind 3,
so on silicon. That confirms `j4` and the row-first reading.

TERRAIN AT readings: <231:339>, <201:205>, <259:283>, <239:292>, <235:214>,
<218:279>, <227:289>, <251:328>, <252:330>, <287:303>, <342:280>,
<307:393>, <310:341>, <238:378>, <226:301>, <259:350>, <234:296>,
<320:274>, <237:378>, <245:360>, <292:354>, <241:380> (twice), <257:356>,
<264:377>, <249:357>, <268:360>. Most cluster at rows 28-34, cols 41-47,
around the colony; the last two (25:48, 25:55) were taken while the camera
was over the colony buildings. So the Romulan colony is at about block row
31-33, col 43-45, on the large silicon (kind-3) field, about 3-4 blocks
from the exhausted mine (guess from the cursor readings; the camera
position itself isn't shown).

## Start

Play starts at about 4:02, camera over the Romulan pad, north up: the hub
arch (`rch`) at the top left, a ribbed box (`rsf`?), a small pod and a
post, the construction yard (cross shape) in the middle, dark rocky ground
either side. JUMP TO lists five entries: the hub arch, the cross-shaped
yard, a short pod, a tall slab and a crystal-like grey-blue icon, as in J1
and J2 ([play-j1r.md](play-j1r.md), [play-j2r.md](play-j2r.md)).

Starting stock (4:12): colony size 34/50, dilithium 658, polycomp 1156,
thermoconc 2300, transalumin 658, nitrium 5600, duranium 6600 (small digits;
check). Higher than J1's and J2's (500/988/2211/497/3500/5000).

Colony size: 34/50 (4:12), 36/50 (4:23), 56/75 (11:23, 11:52), 61/80
(17:29), 63/80 (18:32), 66/90 (20:10), 67/90 (21:29), 69/90 (23:43),
70/90 (25:06). Capacity 50 -> 75 -> **80** -> 90: an 80 step not seen in
R1 (50/55/75/90).

## Objectives and end

`jnt04rom.stb`: PRIMARY OBJECTIVE 1 (7, and the same text as 12): capture
the Klingon arsenal and hold it until the anti-matter fields are set up.
MISSION PARAMETERS (8): may fight Federation or Klingon units that threaten
the mission. Pass (10), fail (11); failures 13 (Klingon security center
destroyed) and 14 (Romulan hub destroyed). No COMMAND / objectives panel
was seen opened.

The player massed units (cloaked assault vehicles among them) at the
arsenal from about 23:30; the team got in at 24:55, the building was
captured at 25:05, and the mission was won at 26:21, 76 s later (the
objective's wait while the fields are set up), with the Federation knocked out in
between (25:18, 25:29). Nothing about the Klingons was reported. Then a
loading box with the debriefing line and JOINT LEVEL 04, with the three
emblems (26:33-26:36) and the Romulan emblem with a CONTINUE button
(26:37); the recording ends at 26:42 before any MISSION PASS page or
debriefing text. Won (by the message); the end page wasn't shown.

Compared with the other sides: J4K's four recaptures took 2 minutes but its
secondary objective another 70; J4F ended at 26:15 after two objectives.
The Romulan side has one objective and ends on it.

## Interface

Same layout as J1 and J2: NEW WORLDS top right (colony size, six stocks, JUMP
TO, BUILD), TRICORDER bottom right (RELIEF X2, COMMS / ALLY / ZOOM),
COMMUNICATIONS bottom centre (DELETE, the four arrows, MISSION, X), grey-
green frames, cyan status text, blue message text; briefing pages are
small boxes with green title bars bottom left (transmissions) or top left
(cartography, planetary data), and the objective page bottom left over a
ground view. The compass strip ("--350--+--[ N ]--+--010--") shows in
ground views and the briefing flyovers. NEXT and MISSION buttons bottom
right during the briefing. Portraits:

- **Intelligence** (all five closing messages): pale grey-white face,
  slight smile, dark slicked purple-black hair with a short fringe, pointed
  ears; warm brown background with teal-green lights along the top. This is
  the face [play-j1r.md](play-j1r.md) calls Intelligence and
  [play-j2r.md](play-j2r.md)'s second Intelligence face.
- **Operations** (21:03): young face, frowning, purple-black hair with a
  straight fringe, dark collar; grey consoles with green "57" screens either
  side (as in J2).
- **Engineering** (24:48): darker ruddy face, frowning, purple-black hair
  with a V fringe, dark uniform, two cyan lights at shoulder height (the
  Security/Engineering face of J2).

## Open questions

- Why `J4R011` plays twice and `J4R014` never: does the second relief-map
  step really reference 011, or did the player step back? What does 014 say
  and where should it go (26.9 s, longer than the step)?
- MISSION PARAMETERS (8) not shown: is it skipped for this side, or under
  the emblem step after the recording's frame crop? Check in play.
- The grey craft in `J4R010` and the winged vehicles in `J4R013`: which
  Romulan models (`rwb`/`rcb`, `rpa`/`rapc`?).
- The six-unit MULTISELECTION at 4:03: the forked icons suggest tanks
  (`rct`); select one to be sure.
- Where exactly the Klingon arsenal is (no Tricorder reading on it), and
  whether the red-and-white emblem over it is the capture-target marker.
- Messages before 21:03: none, or deleted with the panel closed?
- Starting stock digits (658/1156/2300/658/5600/6600) are small; recheck.
- The models for science station, utility hospital (+upgrade) and resource
  processor (+upgrade) are guesses.
- The Klingon side wasn't reported destroyed here (J4K won its own game):
  in this game were the Klingons still in, and did failure 13 (security
  center) apply?
