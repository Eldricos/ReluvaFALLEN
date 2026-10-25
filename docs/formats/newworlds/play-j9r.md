# Star Trek: New Worlds: watched in play, mission J9 (Romulan side)

Joint mission J9 ("The Lost Tribes", `jnt09rom.stb` string 1; the dust
planet Cadimus II, `alnwrld`'s `j9` map) played as the Romulans, in a
13.5-minute recording (811.9 s) with commentary (the Drive file "Romulan
Mission 14"). Times are the recording's; messages and pages are
paraphrased. Guesses are marked. Federation side: [play-j9f.md](play-j9f.md);
Klingon side: [play-j9k.md](play-j9k.md).

Identified three ways: the loading box reads "JOINT LEVEL 09" (0:05-0:10);
all five `J9R` clips (`J9R005`-`J9R009`) are in the audio; the stardate
(9309.4), the page texts and the two objectives match `jnt09rom.stb`.
Campaign place (level.dat): R1, R2, J1-J4, R8, R9, J6, J7, R12, J8, R14,
**J9** - the fourteenth and **last** Romulan mission, matching the Drive
file's number (the files go by campaign place, as for "Mission 12" = J8,
[play-j8r.md](play-j8r.md)).
No mine ran out, so the map check by exhausted mines is not possible; the
Tricorder readings fit `j9` (below).

## Loading and briefing (0:00-1:53)

0:00-0:04 the uploader's title card ("Romulan Mission 14 commentary").
0:05-0:10 the loading box (three emblems; JOINT LEVEL 09 with the progress
lines for the briefing and the officers, and the percent-done lines). 0:15-0:20 the Romulan emblem with
the INCOMING TRANSMISSION page (string 7: title, stardate 9309.4, the link to
High Command cut, backup systems on) top right, **silent**.

A first pass over the whole recording finds only `J9R005` (score 0.13) and
`J9R006`. A normalised cross-correlation of the first 200 s against all five
clips finds every one, back to back (each starts within 0.6 s of the
previous one's end; background 0.011-0.016):

| Clip | Starts | Page (`jnt09rom.stb` id) | View |
|---|---|---|---|
| `J9R005` (17.6 s, score 0.13) | 0:21 | STELLAR CARTOGRAPHY (3), top left | the galaxy: white stars, red and blue streaks, turning |
| `J9R006` (14.1 s, 0.68) | 0:40 | PLANETARY DATA (4), bottom left; relief inset top right | **a ground view** (no star system): the dust plain under a cloudy green-yellow sky, dark blue-grey rock ridges and mesas, dark shrubs, and tall two-pronged dark spires with violet-blue tips standing on the plain (Metar spires) |
| `J9R007` (10.4 s, 0.91) | 1:02 | OBJECTIVE (5) (scan and research the device), top right; relief inset top left | the anomaly device: a blue faceted crystal turning on the green-cross grid (`mad`, as in J8; fairly sure) |
| `J9R008` (22.0 s, 0.41) | 1:13 | OBJECTIVE (6) (capture a Metar starship), bottom left; relief inset top right | a dark blue two-pronged spire with violet tips turning on the grid - the same shape as the spires in the ground view (`mch`, the "Metar spire" of J9F; fairly sure). Shown for the starship page, so perhaps the Metar ship model (guess) |
| `J9R009` (15.6 s, 0.75) | 1:35 | (none) | the Romulan emblem |

So the usual rule (one clip per step in id order, starting at STELLAR
CARTOGRAPHY, the transmission page silent), and the backdrops keep pace with
the pages. Like J9K (relief map) the PLANETARY DATA step shows no star
system; here it is a ground view instead. No star-system step, so no planet
colours to record. SPEECH (8), SECONDARY OBJECTIVE 1 (9) and MISSION FAIL
CRITERIA (10) are empty and not shown. Play starts at about 1:55.

`jnt09rom.stb` (17 strings): title (1), stardate line (2: the planet has
dilithium), pages 3-6, transmission (7), empty 8-10, pass/fail (11, 12),
objective 13 (scan the temple and research the device), failure 14 (hub
destroyed), objective 15 (capture the starship), failure 16 (Metar cores
destroyed before the capture), failure 17 (Metar destroyed, device not
researchable).

## Messages

Four messages, all at the bottom centre in the small Communications panel
(about 290 x 95 px in the 960-wide picture, at x 290-580, y 625-720). Nothing
at the top centre: the top and bottom strips were checked every 2 s over the
whole play, and 4 times a second around 13:05-13:13.

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 12:06 | 1/1 | Engineering | scanning for the anomaly device technology (open until 12:58) |
| 13:00 | 2/2 | Engineering | the needed technology has been beamed back to the colony hub for more research |
| 13:10.25 | 3/4 | Science, "study of anomaly device" | the researchers have found how to use the device's technology (objective 13); shown for only 0.25 s, then replaced |
| 13:10.5 | 4/4 | Engineering | the starship is now under our control (objective 15) |

At 13:12 the player steps back to 3/4 (back arrow lit), then forward to 4/4.
All four are mission-specific and the same four as J9K (31:26, 32:38, 32:52,
33:37 there) and J9F. **No generic messages** (no power low, no mine
exhausted, no building complete). No map positions given.

Message 3 stood only 0.25 s and is easy to miss with a coarser scan; a scan
of the panel area also flags changes before 12:06 (0:02, 0:13, 1:35, 3:22,
5:18, 6:14, 8:40, 8:54) that are not messages (no panel in the frames).

## Buildings and units

Selection panel (30 samples), Romulan unless said:

| Name in panel | Seen | Model | How sure |
|---|---|---|---|
| colony hub (OPERATIONAL, 120 %) | 4:05, 6:58 | `rch` (the arch) | sure |
| COLONY HUB UPGRADE (OPERATIONAL, 120 %) | 4:06 | portrait the arch with a tower; on the pad a tall tower beside the arch, `rch_b` | fairly |
| construction yard (80 %; shield BASIC at 6:19, HIGH at 12:38) | 6:19, 12:38 | `rcy_a` (low base, four raised green-lit corners) | sure |
| science station (80 %; BASIC 7:08-7:49, HIGH from 11:03) | 7:08-13:01 | `rsb_a` (splayed dome portrait; the six-bayed dome on the pad at 7:08, with three ribbed `rsb_b` slabs in its bays by 13:05) | sure |
| resource processor upg (80 %) | 10:57 | portrait `rrpp` (the base); the panel lists five plant upgrades, model `rrpp_b` | fairly |
| vehicle yard (80 %, shield HIGH) | 11:53 | `rvy` (octagonal ring) | fairly |
| APC (READY) | 2:03 | pale crystal craft | `rapc` (portrait) |
| photon artillery | 3:36-9:45 | pale crystal craft (cannon) | `rpa` (portrait `rptl`) |
| science vehicle | 6:09, 11:40 | pale crystal craft | `rsv` (portrait) |
| ground scout | 6:14-12:09 | pale crystal craft | `rfgs` (portrait) |
| **cloaked assault vehicl** (sic, cut by the panel) | 10:35, 11:14 | a translucent pale-green wedge portrait (`rcapc`, the `rapc` shape cloaked) | tentative |
| **phaser defense turret**, owner **METAR** (OPERATIONAL) | 4:40 | - | Metar |
| **Metar colony core**, owner METAR (red-backed portrait with a violet spike) | 11:34 | `mch` (a dark spire with violet-blue prongs at the frame's edge; the same model named so in R12) | fairly; Metar (the "cores" of failure 16) |

Shield cover goes from BASIC to **HIGH** on the buildings between 7:49 and
11:03 (an upgrade researched or built; not seen when). The pad also shows a
low ramp building with vents (`rsf`, the material storage: likely) and small
spider-like towers (`rps2`, the adv. power generator: fairly). The hub at
13:08 has grown: a tall column (`rch_b`) stands inside the four-legged arch
and a spiked grey plate (`rch_c`) at its foot, neither seen at 4:06 from
above, when only the column could be hidden by the arch. The build menu (4:15) offers the science
station (47 polycomp, 58 thermoconc, 22 trans al, 120 kg nitrium); at 4:10
a resource processor and a science station were queued and the game asked
whether to cancel a construction (the player answered at 4:12).

Enemies: a big grey flier with violet-blue panels (`mport`, the Metar portal
craft among the Metar models: sure) roams over the plain from 3:20 and fights the
Romulan units (3:20-6:00, 9:44-11:28); at 5:18 a large pale ring shockwave
spreads around it. White fliers with red markings (`md_a`/`mj_a`/`my_a`/`mb`,
the Metar craft: sure) attack the colony at 8:30-8:55. This also answers the
question of [play-j8r.md](play-j8r.md): the white-and-red fliers there are
Metar craft. A grey
octagonal tower with a glowing green top and ring (11:40-11:42, next to a red
boulder) is probably the scan target, the Metar temple (guess; J9K names a
"Metar temple"). The starship capture itself was not on screen (the camera
was on the colony at 13:10).

## Positions

Every Tricorder heading read at 10 per second (1:53-13:32, a frame-by-frame
scan of the Tricorder heading), eighths of a block, row first; block = (a/8,
b/8); kind from `j9`'s grid (64 x 64:
kinds 0 = 3773 blocks and 1 = 323 only; kind 1 is one long band, rows 14-41,
cols 19-51).

**Mineral readings: three, all DILITHIUM, all on kind-1 blocks.**

| Reading | Time | Block | Kind | Neighbours | Cursor | Camera |
|---|---|---|---|---|---|---|
| DILITHIUM AT <161:265> | 8:14.0-8:14.1 | 20, 33 | 1 | 8 of 9 kind 1 | **over the Tricorder relief inset** | medium height, oblique, unchanged (colony pad and hub in the foreground, dust plain beyond) |
| DILITHIUM AT <191:272> | 8:15.6 | 23, 34 | 1 | 5 of 9 kind 1 | over the inset | same view |
| DILITHIUM AT <175:264> | 8:16.5 | 21, 33 | 1 | - | over the inset | same view |

All TERRAIN readings and the block kind:

| Reading | Time | Block | Kind | Cursor / camera |
|---|---|---|---|---|
| <163:332>, <164:376> | 2:53-3:22 | 20,41; 20,47 | 0 | main view, desert east of the colony, medium-high camera |
| <136:336>, <136:338> | 3:19-4:07 | 17,42 | 0 | main view, the two pink crater pools, medium |
| <130:349>, <132:339>, <138:347> | 4:20-4:24 | 16-17, 42-43 | 0 | main view, the portal fight |
| <144:262> | 4:59-5:11 | 18, 32 | **1** (9 of 9) | main view, medium-high (units small, the portal fight) |
| <169:318> | 6:24 | 21, 39 | **1** (5 of 9) | main view, **low** (dark rock, trees large) |
| <152:300> | 6:25 | 19, 37 | **1** (8 of 9) | main view, **low**, same place |
| <126:288> | 6:26 | 15, 36 | 0 | main view, low |
| <218:241>, <199:258>, <192:225>, <165:206>, <196:266>, <197:248>, <202:254>, <179:264>, <171:247> | 8:13-8:34 | 20-27, 25-33 | 0 (all) | over the relief inset (same sweep as the DILITHIUM readings) |
| <143:232>, <196:195>, <143:207>, <225:188> | 8:34-8:41 | 17-28, 23-29 | 0 | sweep (inset or view, not sure) |
| <219:271> | 8:41-9:27 | 27, 33 | **1** (7 of 9) | main view, right edge, medium height |
| <169:233> / <168:233>, <174:184>, <128:375>, <126:369> | 9:27-10:31 | 21,29; 21,23; 16,46; 15,46 | 0 | main view |
| <188:255>, <210:241>, <213:238>, <222:253> | 10:31-11:49 | 23-27, 29-31 | 0 | main view; <222:253> stays up 75 s while the cursor sits on panels |

Also IMPASSABLE (5:11-6:24, 7:01-7:44, brief flashes), FRIENDLY STRUCTURE
(8:13, 8:16, 8:35, 10:32) and FRIENDLY UNIT (8:13.7, 10:33) - so the
Tricorder names friendly objects too.

**Camera height:** the idea that mineral names need a low camera is not
confirmed by this recording. Main-view
readings on kind-1 blocks said TERRAIN four times, twice with a **low**
camera (6:24-6:25, trees and rocks large) and twice at medium height. The
only mineral names came while the **cursor swept over the Tricorder's relief
inset**, with the main camera at medium height and not moving. Over the inset
the readings are fully consistent: 3 of 3 on kind 1 say DILITHIUM, 9 of 9 on
kind 0 say TERRAIN. So here the mineral name went with hovering the relief
map, not with camera height. Maybe the main view needs a still lower camera
than reached here (the lowest views here still look down at about 45
degrees), or the main view never names minerals; earlier recordings' mineral
readings should be checked for where the cursor was. This fits kind 1 =
dilithium (string 2: dilithium only).

Colony site (estimate): over the inset the FRIENDLY STRUCTURE readings come
between <199:258>, <192:225>, <197:248>, <202:254> and <179:264>, and the
inset shows the colony as black blocks there, so the colony is around block
rows 22-27, cols 28-33, on kind-0 ground at the west edge of the kind-1 band
(guess, ±2 blocks). The early readings (2:53-4:24, rows 16-20, cols 41-47)
are where the player looked out east at the Metar portal. The Klingon and
Federation mines (<316:380>, <300:356>, <316:404>, <308:380>, blocks 37-39,
44-50) are in the band's south-east lobe, away from this colony.

## Start

Play starts at 1:55 looking down on the hub arch (`rch`) on a concrete pad
at the edge of the dust plain; around it the construction yard (`rcy_a`),
the ramp building, a spider-like tower, a pink crater pool beside the pad,
dark shrubs. 2:03-2:10 a group of about 15 pale crystal-shaped units (APC
first selected) stands on the plain in rows; 2:46 they sit in a curved
hollow. No briefing camera move after the emblem.

Starting stock (2:46): colony size **39/90**, dilithium 500, polycomp 988,
thermoconc 2211, transalumin 497, nitrium 3500, duranium 5000 (5000 or 6000;
blurred). Colony hub panel (4:05): generator LOW, consumption 6 %, demand
met 100 %, reserve 100 %, officers 5, crew 0, pool 143, workbee 7, cargobee
4, cnstrctnbee 4, transport 2. JUMP TO icons: hub, construction yard, two
towers, a vehicle, a crystal craft.

Colony size: 39/90 (2:46), 43/90 (5:00), 46/90 (6:24), 48/90 (8:14), 50/90
(9:20), 52/90 (10:00), 54/90 (10:17), 55/90 (13:10). Capacity **90
throughout** (no 50 -> 90 steps as in R1). Dilithium falls 500 -> 366
(10:17) -> 352 (13:10); polycomp 988 -> 827; thermoconc 2211 -> 1511? by
12:40 (read from the construction yard panel; blurred).

## Objectives and end

Objective 13 (scan the temple, research the device): scan started 12:06,
technology at the hub 13:00, researched 13:10. Objective 15 (capture the
starship): 13:10.5. No COMMAND panel seen. About 11 minutes of play, much
shorter than J9K (34 min) and J9F (47 min).

End: 13:22 black; 13:23 the debriefing loading box (three emblems; the
debriefing line, JOINT LEVEL 09, and the lines for the officers, the
geography and synchronizing systems); 13:26 the Romulan emblem with
CONTINUE bottom right; 13:30 a **MISSION REPORT** panel over the emblem:
Mission Log, then both objectives (13 and 15) each marked complete. The
recording ends at 13:32. So **won** - the end of the Romulan campaign; no
closing campaign screen or credits seen (the recording stops first).

## Interface

Same layout as J7 and J8: NEW WORLDS top right (stock, JUMP TO, BUILD /
MULTISELECTION; building pages with INFO / BUILD / STAFF), TRICORDER bottom
right (heading line, relief inset RELIEF X2, COMMS / ALLY / ZOOM), compass
strip top centre. The Communications panel here is the **small** one at the
bottom centre (DELETE, first/back/next/last, MISSION, close), never at the
top. A centred confirm box (icon, question, two buttons) asks before
cancelling a construction (4:10). Portraits:

- **Engineering** (12:06, 13:00, 13:10): pale face, slight smile,
  black-violet hair with a high widow's peak, grey-violet uniform, teal lights
  above the head - the same face as J8's Engineering (and J7's
  Intelligence, [play-j7r.md](play-j7r.md)).
- **Science** (13:10, 13:12): black hair cut straight across in a fringe,
  V-shaped forehead ridges, broader face, straight mouth, dark collar, dark
  grey panelled background - the same Science face as J8 (fairly sure).

## Open questions

- Camera height: does any main-view reading name a mineral if the camera is
  put right down on a kind-1 block? Here only relief-inset hovering
  did. Check where the cursor was for the mineral readings in earlier
  recordings.
- Page 6's model: is the two-pronged spire `mch` (a Metar spire) or the Metar
  starship? And what is the starship that was captured (not on screen)?
- The grey octagonal tower with the green top at 11:40 is `tt` by shape (the
  Taubat temple model); is it the scan target the scripts call the Metar
  temple?
- What happened at 5:18 (the big pale ring around the portal craft): its
  weapon, a cloak, or a teleport?
- When shield cover went to HIGH (between 7:49 and 11:03), and what did it.
- Duranium at 2:46: 5000 or 6000? Thermoconc at 12:40.
- Is there a campaign-end screen after the MISSION REPORT / CONTINUE?
- (Answered since: ramp building `rsf`, spider-like towers `rps2`, science
  station `rsb_a`, resource processor upgrade `rrpp_b`;
  [colony.md](colony.md#romulan-matches).)
- Match the Engineering and Science faces to `ptrt/crew/lg/r_c1..`.
