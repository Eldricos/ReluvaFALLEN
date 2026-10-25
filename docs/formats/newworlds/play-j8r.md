# Star Trek: New Worlds: watched in play, mission J8 (Romulan side)

Joint mission J8 ("Checkmate", `jnt08rom.stb` string 1; the dust planet
Cadimus II, `alnwrld`'s `j8` map) played as the Romulans, in a 7.8-minute
recording (465.7 s) with commentary (the Drive file "Romulan Mission 12").
Times are the recording's; messages and pages are
paraphrased. Guesses are marked. Federation side: [play-j8f.md](play-j8f.md);
Klingon side: [play-j8k.md](play-j8k.md).

Identified three ways: the briefing loading box reads "JOINT LEVEL 08"
(0:05-0:15); all five `J8R` clips there are (`J8R005`-`J8R009`) are in the
audio; and the stardate (9276.0), the page texts and the objectives match
`jnt08rom.stb`. Campaign place (level.dat): R1, R2, J1-J4, R8, R9, J6, J7,
R12, **J8**, R14, J9 - the twelfth Romulan mission, matching the Drive file's
number (the files go by campaign place, as for "Mission 10" = J7,
[play-j7r.md](play-j7r.md)). The map
check by exhausted mines is not possible (no mine ran out); the Tricorder
readings fall on and beside `j8`'s kind-1 field, which fits (below).

## Loading and briefing (0:00-2:28)

0:00-0:04 the uploader's title card ("Romulan Mission 12 commentary").
0:05-0:15 the loading box (three emblems; JOINT LEVEL 08 with the progress
lines for the briefing, the command routines and the officers, and the
percent-done lines). 0:20-0:27 the
Romulan emblem with the INCOMING TRANSMISSION page (string 7: title and
stardate 9276.0) top right, **silent**.

All five clips matched, back to back:

| Clip | Starts | Page (`jnt08rom.stb` id) | View |
|---|---|---|---|
| `J8R005` (23.7 s, score 0.37) | 0:28 | STELLAR CARTOGRAPHY (3), panel top left | the galaxy: white stars, red and blue streaks, turning |
| `J8R006` (45.0 s, 0.63) | 0:52 | PLANETARY DATA (4), top right | the star system (from about 0:50): small bodies coloured yellow-tan (a banded yellow-orange one passes close at about 1:05), teal, pale cyan, blue-grey, one pink-magenta and one green-teal, on a starfield; which is the star not clear |
| `J8R007` (15.3 s, 0.72) | 1:38 | OBJECTIVE (5) (research the device), top centre at 1:38, top right from about 1:45 | the anomaly device: a blue faceted crystal turning on the green-cross grid (`mad`, as on the Klingon side; fairly sure) |
| `J8R008` (17.9 s, 0.67) | 1:54 | OBJECTIVE (6) (protect the vehicle yard), bottom left; a relief-map inset top right | the vehicle yard, a green-lit octagonal ring, turning on the grid: `rvy` (sure, the same shape as the yard on the colony's pad) |
| `J8R009` (12.3 s, 0.95) | 2:12 | (none) | the Romulan emblem |

So the usual rule (one clip per step in id order, starting at STELLAR
CARTOGRAPHY, the transmission page silent) and, unlike J7, the backdrops
**keep pace** with the pages (galaxy under 3, star system under 4, device
under 5, yard under 6). Only five clips against J8K's seven (`J8K009`-`011`:
a Hubrin craft and the colony) and J8F's ten: the Romulans get no
ground-view or relief-map step, only the inset on page 6. SPEECH (8),
SECONDARY OBJECTIVE 1 (9) and MISSION FAIL CRITERIA (10) are empty strings
and were not shown.

2:25-2:28 the game-start box (lines about copying files and the officers, and
the percent-done line). Play starts at about 2:28.

`jnt08rom.stb` (18 strings): title (1), stardate line (2: the planet has
dilithium), pages 3-6, transmission (7), empty 8-10, pass/fail (11, 12),
objective 13 (research and build the device), failure 14 (hub destroyed),
objective 15 (protect the vehicle yard), failure 16 (yard lost), objective
17 (protect the device until it goes off), failure 18 (device destroyed
first). The same layout as the other two sides' J8 tables.

## Messages

Three messages; the counter reaches 3/3 (the last digit blurred). All sat at
the bottom centre; no panel at the top centre was seen (the whole play
checked at one frame every 5 s, and the panel area once a second).

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 5:26 | 1/1 | Science, "study of anomaly device" | the researchers have found how to make the device (open until about 5:31) |
| 6:41 | 2/2 | Engineering | the device is built and being launched; protect it whatever the cost (open to about 7:25) |
| 7:25 | 3/3 | Engineering | the device has finished its journey and is activating: mission complete |

All three are mission-specific and the same three as J8K (4:29, 5:44, 7:03
there). No generic messages (no building complete, power low, mine
exhausted). No map positions given.

The 5:26 Science message is easy to miss: a scan of the panel area once a
second skips it, and it was found frame by frame (it rises into place at
5:26).

## Buildings and units

Selection panel (16 samples), all owner ROMULAN:

| Name in panel | Model | How sure |
|---|---|---|
| colony hub (4:14, 4:19; OPERATIONAL, efficiency 120 %) | `rch` (the arch) | sure |
| construction yard (4:55; 80 %) | `rcy_a` (low base with four raised corners, green-lit) | fairly (ground view at 3:35 and the JUMP TO icon match) |
| science station (5:21; INCOMPLETE, 0 %) | portrait splayed, crab-like (as in J7): `rsb_a` | sure ([colony.md](colony.md#romulan-matches)); it is being built, so not a starting building |
| vehicle yard (5:42, 6:34; OPERATIONAL, 100 %) | `rvy` (octagonal ring) | sure (also the page-6 model) |
| APC (2:32; READY, shield BASIC) | pale crystal-like vehicle portrait | - |
| ground scout (3:02; EN ROUTE, BASIC) | same pale crystal portrait | - |
| armed transport (3:38; READY, BASIC) | same pale crystal portrait | - |

Also on the pad: a low ramp-shaped building with steps (top right at 2:30;
`rsf`, the material storage that stands beside the hub in every Romulan
start: likely), two tower-like JUMP TO icons (the portraits of the static
disruptor `rdds` and the photon defense `rpds`, as in J9R's JUMP TO list:
fairly). MULTISELECTION panels of 15 units (3 x 5) at 5:15 and 6:00-7:30, the
escort; some health bars part red after 6:15.

A large grey flying ship with a violet-blue glowing underside passes low over
the pad at 3:25-3:45 (its shadow on the ground); not selected. Whether it is
the armed transport selected at 3:38 or a scripted Romulan warship is not
clear (guess: scripted fly-over).

Others: at 6:05-6:25 the escort fights a **white flier with red markings and
a red emblem** (a red ring under it at 6:15), and similar white-and-red craft
fly among the group at 7:20-7:26. Klingon or Federation? Not identified.
Dark spiky shrubs (`bad` natives? more likely plants) are everywhere. A
second large empty concrete pad with craters is passed at 6:40-7:00 (another
side's colony site, or the device's target? guess). The anomaly device
itself was not clearly seen in flight.

## Positions

Every Tricorder heading read (1/s, 2:20-7:45), eighths of a block, row first:

| Reading | Time | Block (row, col) | Deposit kind | Neighbours (kind) |
|---|---|---|---|---|
| TERRAIN AT <210:303> | 2:32-3:02 | 26, 37 | 1 | 8 of 9 kind 1 |
| TERRAIN AT <280:333> | 3:02-3:40 | 35, 41 | 1 | 7 of 9 kind 1 |
| TERRAIN AT <313:403> | 3:40-3:50 | 39, 50 | 0 | none |
| TERRAIN AT <267:329> | 3:50-3:58 | 33, 41 | 0 | 4 of 9 kind 1 |
| TERRAIN AT <227:392> | 4:00-4:21 | 28, 49 | 0 | none |
| IMPASSABLE | 4:21-6:46 | - | - | - |
| TERRAIN AT <184:299> | 6:46-7:07 | 23, 37 | 1 | 6 of 9 kind 1 |
| RELIEF X2 | 7:07-7:09 | - | - | - |
| TERRAIN AT <246:397> | 7:09 | 30, 49 | 0 | none |
| TERRAIN AT <279:404> | 7:10-7:35 | 34, 50 | 0 | none |

**Mineral readings: none.** Three readings sit on kind-1 blocks and still say
TERRAIN, as the main view does: mineral names show when the cursor is over
the Tricorder's relief inset ([play-j9r.md](play-j9r.md)). `j8` (64 x 64)
has only kinds 0 (3760) and 1 (336, rows 11-43, cols 18-44), which fits
string 2 (dilithium only).

Colony: the first reading (2:32, at the hub) is <210:303>, block 26, 37, and
the relief map at 6:49-6:52 shows the colony (black building blocks on a
light-green area) in the upper right quarter of the map. So the colony is
around block rows 22-35, cols 37-41, on or by the dilithium field (guess).
The readings at cols 49-50 (3:40-4:21, 7:09-7:35) are where the player
looked or sent the escort, east of the colony.

## Start

Play starts at 2:28 on a concrete pad between a dark blue-grey ridge and the
dust plain: the hub arch (`rch`) in the middle, the vehicle yard (`rvy`) at
the bottom edge, a low ramp building top right, the construction yard; dark
spiky shrubs on the dust; a cluster of pale crystal-shaped units on the plain
(2:50). The first selection is an APC. Compass strip at the top
("350 ... N ... 010"). At 2:50 and 4:00 NEW WORLDS shows a MULTISELECTION.

Starting stock (2:46): colony size **45/50**, dilithium 656, polycomp 1140,
thermoconc 2211, transalumin 653, nitrium 5100, duranium 6600. JUMP TO
icons: hub, construction yard, two towers, a vehicle; BUILD below.

Later: 5:29 colony 45/50, dilithium 656, polycomp 1118 (read "118"; a
leading 1 lost? check), thermoconc 2010, transalumin 591, nitrium 4980,
duranium 5600; 6:26-6:30 colony 44/50; 6:41 dilithium 651, polycomp 1114
("114"), thermoconc 2010, transalumin 591, nitrium 4980, duranium 5580;
7:15 colony 45/50. Capacity **50 throughout** (a small cap, below J7's 90
and R1's 50 -> 90 growth). The drop in polycomp, thermoconc, transalumin and
nitrium between 2:46 and 5:29 is the research/science station; the
dilithium hardly moves.

## Objectives and end

Objective 13 (research and build the device): researched at 5:26, built and
launched at 6:41. Objective 17: the device activates at 7:25 (mission
complete); a large pale ring-shaped shockwave goes off among the units at
about 7:30. Objective 15 (the yard) was not lost. No COMMAND / objectives
panel was opened. About 5 minutes of play, as short as J8K.

End: 7:35 the debriefing loading box (three emblems); 7:40 the Romulan
emblem with CONTINUE bottom right; the recording ends at 7:45 before any
MISSION REPORT. So **won** (by the closing message), the report not seen.

## Interface

Same layout as J7: NEW WORLDS top right (stock, JUMP TO, BUILD /
MULTISELECTION), TRICORDER bottom right (heading line, relief inset, COMMS /
ALLY / ZOOM), COMMUNICATIONS bottom centre (DELETE, first/back/next/last,
MISSION, close); green-titled briefing pages with NEXT and MISSION. Portraits:

- **Science** (5:26): pale face, short dark violet-black hair standing up
  flat on top, a dark plain collar, straight unsmiling mouth, dark panelled
  background. Not seen in J7; a new face for the Romulan set.
- **Engineering** (6:41, 7:25): pale face, slight smile, black-violet hair
  with a high widow's peak, grey-violet uniform, teal lights above the head -
  the face [play-j7r.md](play-j7r.md) shows for Intelligence and for the
  quota message (the pale face). So again the pale face speaks for Engineering.

## Open questions

- Who the white-and-red fliers with the red emblem are (6:05-7:26):
  Klingon, Federation or something scripted?
- The big grey flier with the violet-blue glow at 3:25-3:45: the armed
  transport, or a scripted Romulan warship?
- The empty cratered concrete pad passed at 6:40-7:00: another side's colony,
  or the device's target?
- What the device looks like in flight (not clearly seen); where it went off
  (the shockwave at 7:30, near <279:404>?).
- The MISSION REPORT screen (recording ends at the CONTINUE emblem).
- Polycomp after 5:29: 118/114 or 1118/1114?
- (Answered since: ramp building `rsf`, tower icons the static disruptor and
  photon defense, science station `rsb_a`.)
- Which body in the star-system view is the star.
- Match the Science face (new) and the pale Engineering face to
  `ptrt/crew/lg/r_c1..`.
- Starting colony 45/50 and stock: same on all three J8 sides? (J8K's stock
  not recorded.)
