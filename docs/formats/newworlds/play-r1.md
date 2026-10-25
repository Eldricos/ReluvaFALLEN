# Star Trek: New Worlds: watched in play, mission R1

Romulan mission R1 ("Et Tu?", `rom01.stb` string 1; the planet Sakuntula in
the Manuka system, `redwrld`'s `r1` map) as seen in a 23½-minute recording
with commentary (the Drive file "Romulan Mission 1", 1415.96 s = 23:36;
1280×720, the 640×480 game pillarboxed to x 160-1120, as for K1,
[play-k1.md](play-k1.md)). R1 is the first entry in `level.dat`'s order, so
it opens the Romulan campaign (R1, R2, J1-J4, R8, …). Observed behaviour;
times are the recording's. Messages and pages are paraphrased; guesses are
marked. October 2026.

## Loading and briefing (0:00-4:10)

0:00-0:04 is the uploader's title card. The loading box (purple frame, the
three sides' emblems behind the text) then shows the usual progress lines,
with "ROMULAN LEVEL 01" from about 0:11 under the line announcing the
briefing, and percent-done lines.

From 0:20 the Romulan emblem (the green and blue bird with a silver
nameplate in Romulan script, NEXT and MISSION buttons bottom right) carries
the INCOMING TRANSMISSION page (string 10: the mission title and stardate
8988.4), **silent**, until the first clip at 0:28.

All nine R1 clips were found in the audio (`RomVO`, back to back):

| Clip | Starts | Page (`rom01.stb` id) | View |
|---|---|---|---|
| `R1005` (47.2 s) | 0:28.6 | STELLAR CARTOGRAPHY (22) | the galaxy (white stars, red and blue streaks), turning |
| `R1006` (22.3 s) | 1:16.5 | PLANETARY DATA (2) | the star system: a small dull red body at the centre (the star, presumably), planets yellow (banded), teal, small green, and two purple/magenta |
| `R1007` (17.4 s) | 1:39.4 | OBJECTIVE (3)? **no page panel seen** | ground view at the colony site: the camera flies low over concrete, past the colony hub's legs (`rch`), a dark blue-grey Romulan vehicle with green lights on the pad; RELIEF X2 inset top right |
| `R1008` (23.1 s) | 1:57.5 | ADDITIONAL (4) | a model on the green grid: a swept, dark blue/purple streaked shape, very likely `mport` (the Taubat "inactive portal" of 23:00); relief inset |
| `R1009` (11.7 s) | 2:21.2 | ADDITIONAL (5) | a model on the grid drawn translucent green: a low four-winged pyramid with a doorway: `rcss`, which the panel portraits show is the shield generator ([colony.md](colony.md#romulan-matches)), though the page is about the science block; relief inset |
| `R1010` (23.0 s) | 2:33.5 | BACKGROUND DATA (6) | the relief/grid map, turning, colony marked; ground-view inset |
| `R1011` (33.0 s) | 2:57.2 | ADDITIONAL (7) | ground view: the colony site from the red plain (hub arch, a low building, two grey-blue Romulan vehicles) |
| `R1012` (11.5 s) | 3:30.9 | OBJECTIVE (8) | the relief map again |
| `R1013` (19.6 s) | 3:43.2 | ADDITIONAL (9) | the Romulan emblem |

Scores 0.43-0.88 against 0.06 or less elsewhere, except `R1005` (0.14
against 0.03, under the commentary).

So R1 follows K1's pattern exactly: **clip = page id + 3** for strings 2-9,
one clip per heading page in table order, starting with STELLAR
CARTOGRAPHY; the transmission page (10) is shown first and silent. R1's table
puts STELLAR CARTOGRAPHY last in id order (22, after the objective strings)
but it is still shown first, so the order is the briefing's own, not the id
order. String 23 (a stardate line about the planet's dilithium) was not seen.
Strings 11/12 (MISSION PASS / FAIL) were not seen either (see the end).

The relief/grid steps show the Tricorder readout as a heading, "TERRAIN AT
<a:b>", for whatever the (fixed) cursor is over as the map turns: <244:240>,
<207:231>, <251:236>, <232:259>, <264:195>, and "FRIENDLY UNIT".

The 4:05 box shows the game-start lines (about copying files, deploying the
perimeter sensors and starting colony operations, …, ROMULAN LEVEL 01); play
starts at about 4:10.

## Messages

19 in all (the counter's last value, 19/19); 15 read:

| Time | # | From | What it says (paraphrased) | Kind |
|---|---|---|---|---|
| 4:10 | 1 | Operations | greetings from the command staff, who trust the commander fully | mission opener |
| 5:45 | 2 | Engineering | construction yard finished | generic |
| 6:15 | 3 | Intelligence | the 3 rival Romulan cloaked tanks are destroyed | mission (objective 16) |
| 7:08 | 4 | Engineering | vehicle yard finished | generic |
| 7:09-10:58 | 5, 6 | (not seen: panel closed) | probably more "finished" messages | ? |
| 10:58 | 7 | Engineering | transporter pad finished | generic |
| 12:16 | 8 | Operations | not enough power, systems shutting down; build generators | generic (as K1/K2) |
| 13:38 | 9 | Engineering | every required building is finished | mission (objective 13) |
| 17:16 | 10 | Intelligence | the rival's base found at **<165:323>** | mission |
| 19:27 | 11 | Intelligence | a rival Romulan power generator destroyed | mission |
| 19:44 | 12 | (not seen: arrived with 13; the panel jumped from 11 to 13) | | ? |
| 19:44 | 13 | Intelligence | the rival base is destroyed; proposes finding out who was behind it | mission (objective 19) |
| 20:07 | 14 | Intelligence | all the rival Romulan tanks destroyed | mission |
| 20:14 | 15 | Science, "study of rivals" | the site looks like the work of an Admiral Narok | research result (objective 20?) |
| 22:38 | 16 | Intelligence | an alien building found at **<143:260>**; suggests researching what it does | mission |
| 23:01 | 17 | Science, "study of temple" | the Taubat temple seems to power a stasis field under four groups of ruins to the north | research result (objective 17) |
| 23:13 | 18 | (not seen: arrived with 19) | | ? (all objectives met?) |
| 23:13 | 19 | Operations | mission complete | generic end |

Generic messages are the same kinds as F1/K1/K2 ("<building> finished" from
Engineering, the power shortage from Operations); Operations plays the
Federation's Command part as in K1. No "mine exhausted" message came up. The
mission-specific ones report the scripted rival (a renegade Romulan colony
with cloaked tanks), map positions, and research results headed "Science:
study of <project>", as K1's "study of fortress". The research project is
started from the science station (RSRCH button), whose panel lists projects
("STUDY OF RIVALS", with a vehicle picture) under the Romulan emblem (20:07).

## Buildings and units

28 selection samples; shapes against the `rombuild`/`romactor` models and the
natives' `bad` models.

| Name in the panel (owner) | Model | How sure |
|---|---|---|
| Colony hub (Romulan) | `rch`, the tall pointed arch on three legs | sure (shape, many views) |
| Colony hub upgrade (5:52; selectable on its own) | `rch_b`, the column inside the arch; its panel offers the next stage, "improved hub" (`rch_c`) | fairly ([colony.md](colony.md#romulan-matches)) |
| Construction yard (7:19) | `rcy_a` (flat cross with raised wings) | likely (icon) |
| Vehicle yard (10:24); Vehicle yard upgrade (12:45) | `rvy`; `rvy_b`, the pointed wedge with a grey face joined to the yard's ring, selected on its own | sure; fairly |
| Science station (8:43, 11:55, 13:05, 14:36, 22:54) | `rsb_a` (low six-bayed dome; the four-legged pyramid `rcss` is the shield generator) | sure (portrait) |
| Resource processor (11:49) | `rrpp_a` | likely (icon) |
| Adv. power generator (12:39; 22:05 INCOMPLETE 0 %) | `rps2` (tower on splayed legs) | likely (icon) |
| Mining station (16:52) | `rms` (flat X) | likely (icon) |
| Ground scout (13:19 EN ROUTE, 21:32, 22:37, 22:44) | `rfgs` (`rs` is the shuttle) | fairly (portrait, metamodel name) |
| Colony hub (owner RIVAL, 19:37, damaged health bar) | `rch` under a blue shield dome | sure (shape) |
| Inactive portal (owner TAUBAT, 23:12; pink icon) | `mport` (the swept blue-black structure on the ground at 23:00) | likely (shape, colour) |
| Transporter pad (message 7 only) | `rbcs` (selected in R2) | fairly |
| Starting low slatted building beside the hub (not selected) | `rsf` | likely (shape) |

The panel shows owner, state (OPERATIONAL / INCOMPLETE; units READY / EN
ROUTE), shield cover (BASIC, HIGH on several buildings from about 12:39) and
efficiency (68-120 %). The hub's full panel (4:45) adds GENERATOR LOW,
CONSUMPTION 100 %, DEMAND MET 44 %, RESERVE 0 %, OFFICERS 5, CREW 0, POOL 143,
and its vehicles: WORKBEE 4, CARGOBEE 2, CNSTRCTNBEE 4, TRANSPORT 2. The
science station's panel has power (CONSUMER, SUPPLY 100 %, RESERVE 100 %),
OFFICERS 0 / CREW 1, and RAZE / RSRCH buttons. A vehicle yard opens an
ENGINEERING panel listing vehicles (20:30).

Enemies: the rival Romulans (owner "RIVAL": colony hub, power generator,
cloaked tanks, shown in Romulan models). Natives: Taubat (the portal; the
temple only in messages).

## Positions

- **Rival base <165:323>** (block row 20, column 40). No deposit under it;
  the large kind-1 (dilithium) patch, rows 21-26 / columns 31-40, is just
  beside it.
- **Alien building <143:260>** (row 17, column 32): no deposits near; very
  probably the Taubat inactive portal selected at 23:12.
- **Mineral readouts.** No mine ran out, but the Tricorder names the mineral
  under the cursor, and these land on deposits of `r1` read as eighths of a
  block, row first:
  - KELBONITE AT <255:266> (6:28): block (row 31, col 33) is **kind 4**,
    among other kind-4 blocks;
  - DILITHIUM AT <222:242> (9:33): block (27, 30) is **kind 1**;
  - DILITHIUM AT <208:270> (20:09): block (26, 33) is **kind 1**, inside the
    big kind-1 patch.

  Read column first they'd fall on kind 2, none and none. So this confirms
  the reading again and gives **kind 1 = dilithium, kind 4 = kelbonite**,
  since borne out across the recordings: 1 dilithium, 2 talgonite, 3 silicon,
  4 kelbonite, 5 magnesite ([play-c11.md](play-c11.md)), 6 dolamide
  ([play-r9.md](play-r9.md)). `r1` has 162 deposit blocks
  (kind 1 ×122, 3 ×11, 6 ×11, 4 ×10, 2 ×6, 5 ×2), all in rows 21-35.
- **Colony site:** no opening "TERRAIN AT" line on start (the Tricorder shows
  RELIEF X2 until about 4:25). The first readout, <161:184> (row 20, column
  23), was taken while the cursor sat between the hub's legs at 4:45, so the
  hub is probably near there (a guess). The briefing relief-map readouts and
  the readouts in play while building (<196:209>, <233:206>, <199:201>,
  <209:218>, <183:276>, <272:220>, <234:299>, …) all fall in rows 20-35,
  columns 23-38, around the mixed deposit cluster.

## Start

The camera starts high over the concrete pad looking north (the compass
strip reads "350 … [N] … 010"), the hub arch in the middle with a low
slatted building beside it and a flat structure behind, the red plain and
dark rocky outcrops around. At 4:25 the colony panel reads COLONY SIZE =
18/50 and Dilithium 500, Polycomp 988, Thermoconc 2211, Transalumin 497,
Nitrium 3500, Duranium 5000. Compared with F1/K1 at the same point (500, 988,
2211, 495/450, 3950/3970, 5000), Nitrium is lower. Four vehicles are selected
together at 4:11 (multiselection; probably the starting vehicles or the
rival tanks' targets). The JUMP TO column shows five icons (hub, construction
yard, two grey towers, a vehicle) that are the same at 23:16, so they look
like fixed categories, not a building list (guess).

Colony size: 18/50 at 4:25, 37/55 at 12:30, 41/75 at 15:27, 45/90 at 20:30,
49/90 at 22:30, 50/90 at 23:16. **The capacity grows as the colony builds**
(50 → 90). K2's 90 by 6:20 ([play-k2.md](play-k2.md)) may then come from
building too, not from a larger starting capacity.

## Objectives and end

The objective strings are 13-21. The COMMAND panel wasn't opened. Messages
show 13 met (9, 13:38), 16 (3, 6:15), 19 (13, 19:44) and 17 (17, 23:01);
"mission complete" (19) came at 23:13. **Won**, at about 23:13 game time.

End screens: 23:28 a loading box with a **new line, announcing the
debriefing**, with ROMULAN LEVEL 01; then the Romulan emblem with a MISSION
REPORT panel (about 23:35): a "mission log" line, then four objectives
(strings 13, 16, 17, 19), each with a small blue Romulan-emblem icon and
marked complete. A small button sits bottom right (CONTINUE?). The recording
ends at 23:36. The MISSION PASS page (string 11) wasn't seen.

## Interface

- Panels: grey-green metal frames with a green indicator light top right;
  white titles (NEW WORLDS, COMMUNICATIONS, TRICORDER); colony and selection
  text in cyan; COMMUNICATIONS text, sender and the n/n counter in blue, with
  DELETE, |< < > >|, MISSION and X buttons. Briefing pages have a green
  heading bar over a dark maroon field in grey frames. The relief map is
  green.
- Colony panel buttons INFO / BUILD / STAFF (on a selected hub).
- Tricorder lines: TERRAIN AT <a:b>, <mineral> AT <a:b>, FRIENDLY STRUCTURE,
  FRIENDLY UNIT, IMPASSABLE, RELIEF X2; COMMS / ALLY / ZOOM buttons.
- Portraits (for matching to `ptrt/crew/lg/r_c*`):
  - **Operations**: young man, light tan skin, purple-black bowl cut,
    sharply up-slanted brows, stern; dark uniform; behind him grey-green
    consoles with cyan figures.
  - **Engineering**: darker, ruddy-brown skin, purple-black bowl cut, heavy
    V brows, mouth open; dark uniform; dark background with a row of cyan
    lights below.
  - **Intelligence**: very pale smooth face, faint smile, purple-black bowl
    cut, pointed ears visible; grey-violet uniform; green lights above, a
    grey panelled background.
  - **Science**: heavier, older-looking man, pale pinkish skin, bowl cut
    with a pointed fringe, raised V brows, slight smile; dark olive uniform;
    a stone-pillar background.

## Open questions (for someone playing R1)

1. Messages 5, 6, 12 and 18 were never on screen: open COMMUNICATIONS and
   step back through them (is 18 "all objectives met"? are 5/6 the
   resource processor and power station?).
2. Does the OBJECTIVE page (3) appear during `R1007`'s ground step? It
   wasn't seen. And are strings 11 (MISSION PASS) and 23 shown at all?
3. The COMMAND panel's objective list (strings 13-21): which ones are
   listed, and are 14/15 (construction yard, first power generator) shown as
   separate steps? Does the report go past the four lines seen (18, 20)?
4. (Mostly answered since: the "Colony hub upgrade" is the `rch_b` tower
   and the vehicle yard upgrade `rvy_b`, [colony.md](colony.md#romulan-matches).
   Still to see: the spiked `rch_c` going up as the second stage.)
5. (Answered since: ground scout `rfgs`, cloaked tank `rct`, transporter
   pad `rbcs`, by the panel portraits; [colony.md](colony.md#romulan-matches).)
6. The colony hub's exact map position (hover over open ground beside it);
   whether <161:184> is right.
7. (Answered since: the other kinds have been named in other recordings,
   kind 5 as magnesite in [play-c11.md](play-c11.md); mineral names show
   when the cursor is over the Tricorder's relief inset,
   [play-j9r.md](play-j9r.md).)
8. Where the Taubat temple and the four groups of ruins are; the temple
   was never shown.
9. Does colony capacity rise with accommodation blocks (55 → 75 → 90)?
