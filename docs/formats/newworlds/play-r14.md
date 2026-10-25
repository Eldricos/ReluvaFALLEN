# Star Trek: New Worlds: watched in play, mission R14 (Romulan)

Romulan mission R14 ("The Finest Hour", `rom14.stb` string 1; Bezouk 2
again, played on `icewrld`'s `r12` map) in a 47.9-minute recording
(2873.9 s) with commentary (the Drive file "Romulan Mission 13"). Times are the recording's; messages and pages are paraphrased.
Guesses are marked. Federation side: [play-f14.md](play-f14.md); Klingon
side: [play-k14.md](play-k14.md).

Identified three ways: the loading box reads "ROMULAN LEVEL 14" (0:05-0:08,
and again on the debriefing box at 47:46); all six `R14` clips in RomVO
(`R14005`-`R14010`) are in the audio; and both exhausted mines lie on kind-1
deposit blocks of `r12` (below), so the map is R12's
([play-r12.md](play-r12.md)), as for K14 and F14.
The transmission page reads title and stardate 9297.2 (`rom14.stb` 7).
Campaign place (level.dat): R1, R2, J1-J4, R8, R9, J6, J7, R12, J8, **R14**,
J9 - the thirteenth Romulan mission, matching the Drive file's number
(files go by campaign place).

## Loading and briefing (0:00-2:33)

0:00-0:04 the uploader's title card ("Romulan Mission 13 commentary").
0:05-0:08 the loading box (three emblems; ROMULAN LEVEL 14 with the progress
lines for the briefing, the command routines, the officers and the geography,
and the percent-done lines). 0:10-0:17 the Romulan emblem with the INCOMING TRANSMISSION
page (7: title, stardate 9297.2) top centre-right, **silent**.

All six clips matched, back to back:

| Clip | Starts | Page (`rom14.stb` id) | View |
|---|---|---|---|
| `R14005` (23.3 s, score 0.33) | 0:18 | STELLAR CARTOGRAPHY (2), top left | the galaxy: white stars with red and blue streaks, turning |
| `R14006` (24.4 s, 0.38) | 0:42 | PLANETARY DATA (3), top right | the star system: a **red star with a red ring** in the centre, a **banded orange planet** passing (left at 1:03, right at 1:05), a small brown body, tiny red dots, faint blue star clusters - the same system as R12 |
| `R14007` (19.8 s, 0.66) | 1:07 | BACKGROUND DATA (4) (time to show our power and wipe out the aliens), bottom left; relief-map inset top right | a **model on the grid**: tall slim translucent dark-green spire with light-green slashes, the containment pillar (`hcp`, as R12's page 5; fairly sure) |
| `R14008` (21.8 s, 0.72) | 1:28 | OBJECTIVE (5) (Intelligence wants scans of the Metar colony core), left; camera inset right | the relief/grid map turning (top heading IMPASSABLE); the inset shows red ground with a dark spire (the Metar core? guess) |
| `R14009` (18.3 s, 0.76) | 1:50 | OBJECTIVE (6) (keep the field device running until told otherwise), top right; RELIEF X2 inset top left | ground view of the colony under a dark green-grey sky: the hub arch (`rch`), green-lit pyramids behind left, a pyramid roof in front, low buildings right, a small tree on the red plain |
| `R14010` (16.4 s, 0.63) | 2:09 | (none) | the Romulan emblem, to about 2:26 |

So one clip per step, in id order, starting at STELLAR CARTOGRAPHY, the
transmission page silent; the backdrops keep pace with the pages. Six clips
(K14 seven, F14 nine): no Metar ship or spire steps as in K14/F14. Unlike
the Klingon and Federation briefings, the model step (pillar) comes before
the objective pages, under BACKGROUND DATA. SECONDARY OBJECTIVE 1 (8) and
MISSION FAIL CRITERIA (9) are blank and were not shown.

2:30-2:33 the game-start box. Play starts at about 2:34.

`rom14.stb` (16 strings): title (1), stardate line (16: Bezouk 2 has
dilithium), pages 2-6, transmission (7), blank 8-9, pass/fail (10, 11),
objective 12 (survey the Metar colony core with a science vehicle), failure
13 (hub destroyed), objective 14 (protect the field device), failure 15
(device destroyed). The same objective ids as K14 (12 and 14).

## Messages

Only **four** messages in 45 minutes of play. The panel was always at the
bottom centre; a template scan of the COMMUNICATIONS title bar at 1 frame/s
over the whole recording, bottom and top centre, found nothing else (no top
panel at all). Both mine messages are short-lived and easy to miss with a
coarser scan of the panel area.

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 18:59 | 1/1 | Engineering | the mine at 244, 260 has run out (open to 19:02, then deleted) |
| 19:07 | 1/1 | Engineering | the mine at 276, 284 has run out (open to 19:10, deleted) |
| 47:18 | 1/1 | Science | the readings suggest the Metar are building an anomaly generator (objective 12 met) |
| 47:28 | 2/2 | Intelligence | the mission is complete |

Generic: the two mine-exhausted messages. Mission-specific: the anomaly
generator finding and the closing line - the same ending as K14 (36:17,
36:27) and F14 (37:32, 37:43), here with Intelligence closing (K14's is the
all-objectives-complete message; check whether the Romulan closing line is
a script message or a generic one). No power, resource-shortage or
all-objectives-met messages. Map positions given: only the two mines.

## Buildings and units

Selection panel (61 samples). Romulan (owner ROMULAN):

| Name in panel | Model | How sure |
|---|---|---|
| colony hub (2:38, 9:28; 120 %; generator LOW, consumption 66 %, demand met 100 %) | `rch` (the arch) | sure |
| construction yard (3:07, 17:38; 80 %) | splayed tan portrait: `rcy_a` | sure (portrait; [colony.md](colony.md#romulan-matches)) |
| science station (5:02, 14:24, 15:44; 80 %) | crab-like tan portrait (`rsb_a`); builder help at 2:41 "science research station" | sure |
| resource processor (7:23, 9:33, 9:39; damaged, 66-68 %) | ribbed block (`rrpp_a`) | sure |
| vehicle yard (12:36 on, several; 80-110 %) | tan shell-like portrait, octagonal on the pad (`rvy`) | fairly |
| static disruptor (11:39, 42:31; 120 %) | grey finned column (`rdds`, portrait `rdsb`) | fairly |
| photon defense (36:01-44:15; 80-120 %, one INCOMPLETE 0 % at 41:05) | grey column (`rpds`) | fairly |
| shield generator (40:26; 80 %) | tan pyramid/tent-shaped portrait: `rsg`, the model `rcss` | fairly |

Units: **cloaked tank** (10:40 on), photon artillery (19:20 on, often part
red), science vehicle (30:31, 42:56), **"cloaked assault vehicl[e]"** (42:11
on; the name is cut off at the panel edge), MULTISELECTIONs. Shield
cover BASIC and HIGH.

Others: **Metar** "active portal" (27:25, 27:50, 36:43, 37:03; blue empty
portrait, heavily damaged) - next to large black spiky structures with a
violet-blue glow (Metar buildings; one of them the colony core? guess; the
Science message came with the camera on one at 47:18). **Taubat** "Taubat
temple" (34:10; tan base with a green-lit top: the black towers with a glowing
green ring seen from 29:00 on) and "Taubat hangar" (38:39; triangle
portrait), both owner TAUBAT and damaged - as in R12 and K14. Grey rounded
pods with green and red slashes stand in groups (30:30, 44:00, 47:10; not
selected): `th`, the Taubat house, by shape and texture (sure; the same pods
stand by the Metar portal in R12). Fliers attack from early on (white craft
with red markings 11:00, 15:00; fire at the colony 7:00-8:30). A slim
green-slashed spire stands on the plain (6:30, 13:00, 22:00, 35:30): the
containment field device, `hcp` (fairly sure; never selected).

## Positions

Mines, eighths of a block, row first:

| Mine | Block (row, col) | Deposit kind | Neighbours of kind 1 | Low 12 bits |
|---|---|---|---|---|
| 244, 260 | 30, 32 | 1 | 8 of 9 | 150 |
| 276, 284 | 34, 35 | 1 | 9 of 9 | 125 |

Both on `r12`'s southern dilithium field (rows 25-36, cols 29-46), close to
R12's mines (244, 252 and 276, 276). This confirms that R14 uses R12's map.

**Mineral readings: none.** The Tricorder heading was scanned at 2 frames/s
over the whole play with TERRAIN and IMPASSABLE templates; the only other
headings were RELIEF X2 and FRIENDLY STRUCTURE (21:32, 40:07-40:30). Kind-1
blocks again read TERRAIN: <210:346> (19:00, block 26, 43, kind 1) and
<222:329> (45:00, block 27, 41, kind 1, 9 of 9 neighbours; R12's device
site was block 26, 41). Other samples: <266:393> (18:20, block 33, 49, kind
0), <254:354> (33:20, 31, 44, kind 0), <223:385> (11:55, 27, 48, kind 0),
<070:286> (38:20, 8, 35, kind 0, in the north where the fighting was),
<092:247> (46:00, 11, 30, kind 0). `r12` has only kinds 0 and 1. The main
view reads TERRAIN over deposits; mineral names show when the cursor is over
the Tricorder's relief inset ([play-j9r.md](play-j9r.md)).

Colony: as in R12, around block rows 25-34, cols 39-50 (guess from the
readings); the Metar and Taubat lie to the north (rows 8-11).

## Start

Play starts at 2:34 looking down on the pad (compass N): the hub arch
(`rch`) in the middle with a pyramid roof in front, an octagonal
green-lit building left (construction yard, guess), a four-cornered
building right (science station, guess), an octagonal vehicle yard and a
ribbed resource processor at the bottom, two spidery green-lit buildings
bottom left (mining stations? guess). NEW WORLDS opens on the BUILD list
(science station costs shown); TRICORDER in RELIEF X2. A group of grey
units waits on the plain south of the pad (5:30).

Starting stock (2:50): colony size **60/80**, dilithium 660, polycomp 1148,
thermoconc 2177, transalumin 636, nitrium 5480, duranium 5600 - far less
nitrium and duranium than R12's start (23880/24000), so a fresh R14
setting, not R12's end state.

Colony size: 60/80 (2:50), 65/80 (3:20, 4:00), **69/90** (5:20: capacity 80
-> 90, cause not seen; a hub upgrade? guess), 76/90 (9:20), 77/90 (11:20),
78/90 (12:40), 80/90 (15:20), 81/90 (16:40-16:58), 90/90 (19:36), **91/90**
(22:14, over the cap), 89/90 (31:17), 90/90 (37:50), 87/90 (44:05), 88/90
(47:15). Stock later: 16:49 dilithium 801, polycomp 1114, thermoconc 823,
transalumin 588; 22:14 970 / 1195 / 744 / 782; 31:17 1516 / 1523 / 1136 /
1123; 37:50 1917 / 1744 / 963 / 1289; 44:05 2320 / 1928 / 1218 / 1469;
47:19 2538 / 2014 / 1289 / 1536, nitrium 17162, duranium 4307.

## Objectives and end

Objective 12 (survey the Metar colony core with a science vehicle): met at
47:18 (Science message). Objective 14 (protect the device) held throughout.
Intelligence then says the mission is complete (47:28). No COMMAND /
objectives panel was seen opened. Most of the 45 minutes went on fighting
north to the Metar (portal attacked 27:25-37:03) and Taubat buildings.

End: 47:32 the camera jumps back to the colony pad with the message still
open (to 47:44); 47:45-47:48 the debriefing box (the debriefing line,
ROMULAN LEVEL 14, the officers' line and the percent-done line); 47:49 the Romulan
emblem with CONTINUE bottom right; the recording ends at 47:54 before any
MISSION REPORT. **Won** (by the closing message), the report not seen.

## Interface

Same layout as R12: NEW WORLDS top right (stock, JUMP TO, BUILD, builder
list with costs), TRICORDER bottom right (heading, relief inset, COMMS /
ALLY / ZOOM), COMMUNICATIONS bottom centre (DELETE, first/back/next/last,
MISSION, close); green-titled briefing pages with NEXT and MISSION bottom
right. The hub panel adds a power block (GENERATOR, CONSUMPTION, DEMAND
MET) under the status lines. Portraits:

- **Engineering** (18:59, 19:07): ruddy-brown frowning face, purple-black
  hair, dark uniform, two cyan lights low at both sides - R12's and R9's
  Engineering face.
- **Science** (47:18): pale face with a broad smile/grin, short dark violet
  hair flat on top, dark olive-brown collar, grey panelled background with
  no teal bar. Not R12's Science (which used the pale Intelligence face);
  closer to J8's Science ([play-j8r.md](play-j8r.md); check).
- **Intelligence** (47:28): pale face, mouth slightly open, dark hair with a
  high peak, grey-violet uniform, teal light bar on top - as R12, R9, J7.

## Open questions

- Is Intelligence's "mission complete" (47:28) the stb pass page (10) or a
  separate message? The MISSION REPORT was not reached.
- What raised the capacity 80 -> 90 between 4:00 and 5:20, and how 91/90
  (22:14) happens.
- The "colony core" is the black spire with violet-blue prongs (`mch`, R12
  and J9R); are all the black violet-glowing buildings here colony cores?
- (Answered since: the slashed pods are `th`, Taubat houses; the shield
  generator is `rcss`, [colony.md](colony.md#romulan-matches).) The cloaked
  assault vehicle's full name, and whether it is `rapc` drawn cloaked.
- Who flies the white-and-red craft attacking the colony from 7:00?
- Match the Science grin face (new for R12/R14) and the others to
  `ptrt/crew/lg/r_c1..`.
- The briefing model under page 4: pillar `hcp` or a Metar spire (`mch`)?
  It has the pillar's light-green slashes; check against the geo sheet.
