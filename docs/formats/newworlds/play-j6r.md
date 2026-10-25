# Star Trek: New Worlds: watched in play, mission J6 (Romulan side)

Joint mission J6 ("Keep Your Friends Close", `vlcwrld`'s `j6` map, the
volcanic world Pellion Blue) played as the Romulans. The recording (the Drive
file "Romulan Mission 9") runs 38.2 minutes (2293.8 s) and has commentary.
Times are the recording's. Messages are paraphrased. Other
sides: [play-j6f.md](play-j6f.md), [play-j6k.md](play-j6k.md).

How it was identified:
- The loading screen (0:05-0:19) reads "JOINT LEVEL 06" and shows the three
  emblems.
- All nine `J6R` clips (`J6R005`-`J6R013`) are in the audio.
- The transmission page names "Keep Your Friends Close", stardate 9224.4.
- The exhausted mine lies on a deposit of `j6` (see below).

So the ninth Romulan mission is J6. That matches the level.dat order R1, R2,
J1-J4, R8, R9, **J6**.

## Loading and briefing (0:00-3:54)

- 0:00-0:04: the uploader's title card.
- 0:05-0:19: the loading screen (JOINT LEVEL 06, with progress lines for
  the colony systems and then the briefing, and the percent-done lines).
- From 0:20: the Romulan emblem, with NEXT and MISSION buttons bottom right.
- 0:21-0:26: one silent INCOMING TRANSMISSION page top right
  (`jnt06rom.stb` 8: the title and stardate). No clip plays under it.

| Clip | Starts | Page (`jnt06rom.stb` id) | Shown |
|---|---|---|---|
| `J6R005` (25.0 s, score 0.57) | 0:28.6 | (no page) | the Romulan emblem only (checked twice a second, 0:26-0:53) |
| `J6R006` (24.3 s, 0.76) | 0:53.7 | PLANETARY DATA (4), top left | the galaxy: sparse stars, a red winding arm and a blue streak, turning |
| `J6R007` (19.6 s, 0.85) | 1:18.6 | OBJECTIVE (5), top right | the star system: a pale yellow-green planet moving across, two small dim red dots, no bright star body |
| `J6R008` (18.5 s, 0.85) | 1:38.9 | OBJECTIVE (6), top left | ground under a grey cloudy sky: a dark pyramid building with lit slits (Klingon hub-like, guess) on a grey pad, Klingon buildings around it, mesas and lava streaks behind; small relief-map inset bottom left |
| `J6R009` (26.5 s, 0.88) | 1:58.2 | OBJECTIVE (7), bottom left | the relief/grid map turning, cross marker; one site cluster marked with magenta and light-green squares (yellow/black-centred) near an edge |
| `J6R010` (13.5 s, 0.88) | 2:25.3 | (no page) | a translucent blue crystal on the green-cross grid: the Metar `mad` (matches the `bad` model; sure) |
| `J6R011` (32.0 s, 0.76) | 2:38.9 | (no page) | ground flyover of the same pyramid colony from other angles; a red-lit tower building in front, white rubble/pad pieces |
| `J6R012` (20.9 s, 0.93) | 3:11.0 | (no page) | the relief map again, turning; part of it drawn as an open wireframe grid at one edge |
| `J6R013` (16.3 s, 0.96) | 3:32.0 | (no page) | the Romulan emblem |

The pages do not line up with the views the way they do on J6K. On J6K,
`J6K005` has STELLAR CARTOGRAPHY (3) over the galaxy and PLANETARY DATA (4)
over the star system. Here:
- the first clip plays over the bare emblem;
- STELLAR CARTOGRAPHY (3) is **never shown**;
- every later page appears one step "early": 4 over the galaxy, 5 over the
  star system, 6 over the ground, 7 over the relief map.

Either this side's script has no cartography page, or the cartography step
was skipped. The model-on-grid step shows the Metar crystal, as on J6F
(J6K shows a Federation shield generator and a Metar spire).

The table pages SECONDARY OBJECTIVE 1 (9) and MISSION FAIL CRITERIA (10) are
empty strings and were not seen.

The system is the Pellion System (page 4). The star system step shows only a
pale yellow-green planet and two faint red points.

At 3:50 a loading box with the three emblems appears. Play starts at about
3:54.

## Messages

11 messages in the counter. The COMMUNICATIONS panel was closed most of the
time, and messages 4, 5 and 9 were never seen.

| Time | # shown | From | What it says (paraphrased) |
|---|---|---|---|
| 12:23 | 1/1 | Science | running a thorough scan |
| 12:35 | 2/2 | Science | the scan worked. The biodata is fascinating: the aliens are adapting themselves. Medical should study the data |
| 18:23 | 3/3 | Intelligence | the Federation hub is destroyed |
| (18:30-23:32) | 4, 5 | ? | unseen, panel closed (guess: the Federation being out of the mission, and one more) |
| 23:32 | 6/6 | Engineering | starting the survey, looking for a likely target |
| 23:44 | 7/7 | Engineering | wryly: the council chamber suits the test; waiting for the shields to go down |
| 26:37 | 8/8 | Engineering | the mine at 092, 196 has run out |
| 37:32 | 8/8 (blurry, could be 9/9) | Engineering | the Klingon shields have collapsed; transporting |
| 37:41 | 10/10 | Intelligence | the Klingon hub is destroyed |
| 37:53 | 11/11 | Intelligence | the Romulans have completed their mission |

At 30:27 the panel shows message 7 as **7/7**, so the mine message was
deleted in between. That makes the shields message the new 8/8. One message
(9) then arrived unseen between 37:33 and 37:41, unless the 37:32 counter
actually reads 9/9. The short changes that a frame-by-frame scan of the
panel area picks up at 26:10, 30:27, 31:54, 33:33, 34:02, 35:51, 36:01, 36:19
and 36:24 are explosions and lava under the panel area, not messages. The
panel was closed.

Generic messages:
- mine exhausted;
- rival hub destroyed;
- side completed (the same end set as J4, [play-j4r.md](play-j4r.md)).

Mission-specific messages:
- the Science scan pair. This is objective 17, the Metar colony core. Unlike
  J6K, the Science portrait here is a Romulan face, not a Metar;
- the Engineering survey pair. This is objective 13. The test site is the
  Klingon "council chamber", where J6F/J6K find the hub;
- shields collapsed and transporting. This follows objective 15 (shield
  generator).

The order of play: Metar scan at 12:23, Federation knocked out at 18:23,
Klingon survey at 23:32, shields down at 37:32. That is the reverse of
J6K's order: survey 8:24, shields 19:47, Metar 30:31.

Map positions given: 092, 196 (mine).

## Buildings and units

Selection panel (51 samples). The selection replaces the NEW WORLDS panel
contents (the name, a portrait, then owner, status, shield cover and
efficiency).

Romulan selections:

| Name in panel | Model | How sure |
|---|---|---|
| colony hub upgrade (4:00) / colony hub (4:06-4:19; shield HIGH, 110 %) | `rch` arch (portrait a tan arrow-like arch; the upgrade shows its base's portrait and is `rch_b`, [colony.md](colony.md#romulan-matches)) | sure |
| construction yard (4:38, 80 %) | `rcy_a` cross shape | fairly |
| resource processor (10:05), "resource processor upg" (9:56, 10:13) | `rrpp_a` / `rrpp_b` (portrait `rrpp`) | fairly |
| science station (10:22 80 %, 16:52 110 %) | `rsb_a` (splayed portrait `rsb`) | sure |
| photon defense (25:26 INCOMPLETE 0 %; 30:18, 32:39, 33:48 at 120 %) | a tall pale slab portrait: `rddt`, the model `rpds` | fairly |
| surveillance squad (3:54) | the ground scout's portrait (`rfgs`), so probably the same craft | tentative |
| ground scout (8:43 onward, many) | `rfgs` (portrait file `rfgs`; `rs` is the shuttle) | fairly |
| cloaked assault vehicl (17:19 shield HIGH, 17:45 BASIC) | ghost-like translucent | as in J4 |
| photon artillery (24:00, 33:15, 33:23; often damaged) | grey hover craft portrait (`rptl`); a group of six on the ground at 33:23, pale with green lights on top: `rpa` | fairly |

The selection panel also shows:
- MULTISELECTION from 30:36 to 36:16 (groups of the same grey units,
  probably photon artillery);
- ENGINEERING: 1 OF 2 (23:16) and BUILDING: 1 OF 1 (12:36) counters in the
  same panel.

The colony size line shows "Adv. power generator" at 13:16. In the selection
panel at 13:20 that name is a **Federation** building (white-blue portrait).

Rival selections:
- **Federation**: adv. power generator (13:20), vehicle yard (14:53), colony
  hub (16:29 shield HIGH; 18:11 nearly destroyed, red bar; destroyed at
  18:23), science station (21:12), security center (21:00). So the
  Federation colony is also on this map, and the Romulans destroyed its hub
  first.
- **Klingon**: static disruptor (31:49), **shield generator** (36:56 and
  37:20, shield HIGH, health falling to red). It was destroyed just before
  the 37:32 message.
- **Metar ras** (owner Metar, pink-red portrait; 15:16-18:26), as on J6K.
- HOSTILE STRUCTURE and FRIENDLY UNIT Tricorder readings appear (17:30,
  17:36, 20:45, 35:04).

## Positions

**Mineral readings: none.** The Tricorder line was scanned at 8 samples a
second, 3:50-38:13, and its first word sorted into groups. Only these
appear:
- RELIEF X2;
- TERRAIN AT <a:b>;
- IMPASSABLE;
- HOSTILE STRUCTURE;
- FRIENDLY UNIT;
- the compass strip (while the relief map was open, 17:15-17:24).

No "<MINERAL> AT" text appears anywhere. So this recording gives **no new
evidence for deposit kinds 5 or 6**.

The `j6` deposit kinds are 0 (1698 blocks), 3 (777), 1 (755), 5 (445), 4 (240)
and 2 (181). **There is no kind 6 on `j6`.**

Notably, TERRAIN AT is shown on deposit blocks too:
- 21 of the 54 readings lie on kind-5 blocks with kind 5 all round them, for
  example <331:176>, <200:253>, <212:254>, <178:267>, <207:264>, <171:240>
  and <184:231>, all on 8 kind-5 neighbours;
- two readings lie on kind-1 blocks: <107:203> (row 13, col 25) and
  <052:198> (row 6, col 24).

On other maps, kind 1 read as DILITHIUM. The reason: mineral names appear
when the cursor is over the Tricorder's relief inset, not over the main view,
which reads TERRAIN ([play-j9r.md](play-j9r.md)).

The briefing's stardate line (string 2) says the world has extractable
**dolamide**. Kind 5 is the largest deposit field in the middle of the map
(rows about 21-41, cols 17-33), but kind 6 is dolamide
([play-r9.md](play-r9.md)) and kind 5 magnesite
([play-c11.md](play-c11.md)), so the dolamide the stardate line names has no
deposit on `j6`.

Mine 092, 196 is at block row 11, col 24: kind 1, with all 8 neighbours kind
1 (5x5 all kind 1), so on dilithium by the earlier finding. The swapped
reading (row 24, col 11) is also kind 1, so this map does not tell the two
orders apart. Over the stock line, dilithium stood at 531 at both 10:00 and
12:00, then rose to 1057 by 20:43. That does not settle it either.

TERRAIN readings (54, row:col in eighths):
- 4:57-6:06: <107:203>, <052:198>, both on kind 1, near the mine;
- then <331:176>, <292:252>, <200:253>, <267:225>, <290:219>, <224:139>,
  <143:209>, <336:271>, <340:302>, <238:172>, <300:211>, <301:168>,
  <295:146>, <291:209>, <238:193>, <294:136>, <226:202>, <258:178>,
  <242:204>, <262:207>, <264:2?4>, <208:192>, <242:196>, <209:222>,
  <228:233>, <221:217>, <247:152>, <316:234>, <250:171>, <271:235>,
  <259:184>, <212:254>, <283:173>, <178:267>, <227:228>, <282:273>,
  <331:271>, <292:339>, <221:321>, <265:261>, <178:350>, <207:264>,
  <254:221>, <251:217>, <171:240>, <184:231>, <180:270>, <207:349>,
  <222:317>, <192:299>, <199:299>, <201:299>, <221:304>.

The first readings and the mine put the Romulan colony at the north of the
map, about block row 11-13, col 24-25, on the big kind-1 (dilithium) field.
This is a guess: the camera position isn't shown. The later readings run
south across the kind-5 field (rows 21-42) towards the rival colonies. The
Klingon colony, judging by when the readings near the shield generator were
taken (35:06-35:45: <192:299>-<221:304>), is around row 24-27, col 37-38
(guess).

## Start

Play starts at about 3:54 with the camera over the Romulan pad. The scene
holds:
- the upgraded hub arch (`rch`, "colony hub upgrade") in the middle;
- the cross-shaped construction yard;
- a ribbed box and tan dart-shaped buildings around it;
- dark volcanic ground with red-brown dead trees and small lava vents.

A surveillance squad is selected at 3:54. A colony details panel (power
consumption 68 %, demand met 100 %, reserve 14 %, ready 132 / active 13)
shows at about 3:55.

Stock at about 4:13 (soon after the start):
- colony size 42/60;
- dilithium 660, polycomp 1144, thermoconc 2147, transalumin 634;
- nitrium 5480, duranium 5600.

These are close to J4's (658/1156/2300/658/5600/6600,
[play-j4r.md](play-j4r.md)).

Colony size: 42/60 (4:16), 47/60 (4:56), 49/60 (5:56), 52/60 (6:36), 53/60
(8:16), **60/80** (9:36), 63/90 (10:16), 64/90 (11:36-19:36), 66/90
(19:56), 69/90 (20:16), 70/90 (22:36), 73/90 (24:16), 76/90 (25:36), 77/90
(26:36), 78/90 (27:36), 77/90 (29:16), 76/90 (33:16), 75/90 (33:36), 72/90
(36:36). Capacity goes **60 -> 80 -> 90** within 40 s (9:36-10:16), around
the time of the resource processor upgrade (9:56-10:13). The size falls at
the end as units are lost.

## Objectives and end

`jnt06rom.stb`:
- OBJECTIVE pages 5-7: find a place in the Klingon colony to test the alien
  device; take their shields down and pull back; get a fresh science scan
  of the aliens' base.
- In-game objectives: 13 (scan the Klingon colony for a test site), 15
  (destroy the primary Klingon shield generator), 17 (science scan the
  Metar colony core).
- Failures: 14 (the Klingon hub destroyed before the test), 16, 18, and 19
  (own hub destroyed).
- Pass (11) and fail (12) pages.

No COMMAND panel was seen opened.

How it ended:
- 37:20: the shield generator is at low health.
- 37:32: its shields collapse and the device is transported.
- 37:41: Intelligence reports the Klingon hub destroyed. That is presumably
  the device's test, since failure 14 did not fire.
- 37:53: the Romulans have completed their mission. **Won** at about 37:53.

The end screens:
- 38:00-38:04: a loading box (the debriefing line, JOINT LEVEL 06, the
  three emblems).
- 38:06: the Romulan emblem.
- About 38:12: a **MISSION REPORT** box right of centre. It lists the three
  MISSION OBJECTIVE lines (13, 15, 17), each marked COMPLETE, under a short
  first line.

The recording ends at 38:13.

Compared with the other sides: J6F ended at 30:05 after taking the anomaly
device from the Romulan hub. J6K captured the Vulcan from the Federation hub
by 19:57. Here the Romulans are the attackers of the Klingon colony, in a
game where the Federation was destroyed first.

## Interface

The layout is as in J1, J2 and J4:
- NEW WORLDS top right: colony size, six stocks, JUMP TO (10 slots: arch,
  wedges, cross, posts, one red-highlighted), BUILD;
- TRICORDER bottom right: relief inset, COMMS / ALLY / ZOOM;
- COMMUNICATIONS bottom centre: DELETE, four arrows, MISSION, X;
- grey-green frames, cyan status text, blue message text, the compass strip
  top centre.

Briefing pages are small green-titled boxes at the top left, top right or
bottom left. The BUILD panel shows a model preview with the Romulan emblem
top left (4:00-4:15, a pink highlighted building and a grey hull).

Portraits:
- **Science** (12:23, 12:35): pale face, neat dark purple-black hair with a
  short straight fringe, pointed ears, black collar; warm brown wood-panel
  background. New: not seen in J4.
- **Intelligence** (18:23, 37:41, 37:53): pale grey-white face, slight smile,
  slicked purple-black hair; warm brown background with teal-green lights
  along the top. The same face as J4's Intelligence.
- **Engineering, face A** (23:32, 23:44): young face, frowning,
  purple-black hair with a straight fringe, dark collar; grey consoles with
  green "57" screens either side. This is the face **labelled Operations in
  [play-j4r.md](play-j4r.md)**: the same portrait appears under two
  sections.
- **Engineering, face B** (26:37, 37:32): darker ruddy face, frowning,
  purple-black V fringe, two cyan lights at shoulder height. J4's
  Engineering face.

## Open questions

- No Tricorder mineral names at all, even over kind-1 (dilithium) blocks:
  explained by the relief inset ([play-j9r.md](play-j9r.md)). Kind-5
  blocks (e.g. <200:253>, <212:254>) should read magnesite there.
- STELLAR CARTOGRAPHY (3) never appears and the pages run one view early.
  Is that this side's script, or was a step skipped? `J6R005` plays over
  the bare emblem.
- Messages 4, 5 and 9 were never seen. 4 and 5 arrived 18:30-23:32 with the
  panel closed. 9 arrived 37:33-37:41, or the 37:32 counter reads 9/9.
- Is the same Engineering face A really J4's Operations face, so one of
  the two labels is wrong or the portrait is shared?
- (Answered since, [colony.md](colony.md#romulan-matches): photon defense
  `rpds`, photon artillery `rpa`, science station `rsb_a`, resource
  processor `rrpp_a` (+ `rrpp_b`).) The surveillance squad shows the ground
  scout's portrait: is it a ground scout carrying a squad, or a craft of
  its own?
- The capacity step 60 -> 80 -> 90: which building gives which step?
- The ground views at 1:38 and 2:38: are they the Klingon colony (dark
  pyramid = Klingon hub) or the "council chamber"? Which building is the
  council chamber?
- Was the Klingon hub destroyed by the tested device or by Romulan fire? Why
  didn't failure 14 apply?
- The exact Romulan colony position (the camera isn't shown). The mine and
  the early readings suggest row about 11-13, col about 24-25.
