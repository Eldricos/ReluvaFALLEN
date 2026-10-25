# Star Trek: New Worlds: watched in play, mission K1

Klingon mission K1 ("The Insult", on `alnwrld`'s `k1` map) as seen in a
36-minute recording with the player's commentary over it (YouTube
`ZBlc8_FGXD4`; 1280×720, the 640×480 game pillarboxed to x 160-1120).
Observed behaviour, as for F1 ([play-f1.md](play-f1.md)); times are the
recording's. October 2026.

The `Media/KlingVO` clips were placed by cross-correlating them with the
recording's audio, leaving out its near-silent stretches (which otherwise
give meaningless scores): all nine K1 clips match, at 0.35-0.75 against
0.12 or less anywhere else (0.09 against 0.03 for `K1005`, under music and
commentary). The messages' wording isn't copied: the tables give their gist.

## The briefing (0:00-3:20)

As in F1, the engine plays the mission's string table (`klg01.stb`) a page
at a time, each page lasting as long as its clip. The panels are drawn in
the Klingon style (copper frames, green heading bar); the map is shown as a
red and orange grid, and the colony site as a Taubat pyramid on concrete
under a purple sky.

| Clip | Starts | Page (`klg01.stb` id) |
|---|---|---|
| `K1005` (42.2 s) | 0:31 | STELLAR CARTOGRAPHY (2) |
| `K1006` (14.0 s) | 1:14 | PLANETARY DATA (3) |
| `K1007` (17.1 s) | 1:28 | OBJECTIVE (4) |
| `K1008` (15.7 s) | 1:46 | OBJECTIVE (5) |
| `K1009` (19.7 s) | 2:03 | OBJECTIVE (6) |
| `K1010` (21.2 s) | 2:23 | ADDITIONAL (7) |
| `K1011` (11.0 s) | 2:45 | ADDITIONAL (8) |
| `K1012` (15.0 s) | 2:57 | ADDITIONAL (9) |
| `K1013` (4.0 s) | 3:12 | ADDITIONAL (10), over the Klingon emblem |

Here every clip goes with the page whose string id is the clip's number
minus 3. That's particular to K1's table, not a rule: K2's clips are id + 1,
its table having two transmission pages first ([play-k2.md](play-k2.md)).
In K1: the pages are the table's strings 2-10 in
order, one clip each. Strings 11 (a last ADDITIONAL page) and 12 (INCOMING
TRANSMISSION) have no clip and weren't shown. The player then starts from
the emblem screen (3:20).

## In play

- **Interface:** the same panels as the Federation's (NEW WORLDS, the
  colony panel with JUMP TO and BUILD, COMMUNICATIONS, TRICORDER with
  RELIEF X2 and COMMS / ALLY / ZOOM buttons), in copper frames with orange
  titles.
- **Start:** the colony hub on a concrete pad with three other buildings.
  At 3:39 the colony panel reads `COLONY SIZE = 23/50` and 500, 988, 2211,
  450, 3970, 5000: within a few units of F1's at the same point (500, 988,
  2211, 495, 3950, 5000), so both sides seem to start with the same stock
  (F1's untouched start: 500, 1000, 2300, 500, 4000, 5000).

## Messages

30 in all (the counter's last value); those on screen when sampled:

| Time | # | From | What it says (paraphrased) |
|---|---|---|---|
| 3:25 | 1 | Operations | welcomes the commander to the planet |
| 4:03 | 2 | Engineering | construction yard finished |
| 7:13 | 7 | Engineering | first construction yard upgrade finished |
| 8:46 | 10 | Engineering | arsenal finished |
| 10:05 | 11 | Operations | not enough power: systems are shutting down; build generators |
| 14:48 | 13 | Operations | building held up by a lack of resources; build mines and a resource processor (the same message as F1's 13) |
| 16:13 | 14 | Incoming message (a Taubat) | orders the intruders off their sacred lands |
| 19:03 | 18 | Intelligence | a Taubat lookout post at <271:89> |
| 23:41 | 20 | Science, "study of fortress" | the ruins were home to an ancient race, the Hubrin |
| 25:31 | 21 | Intelligence | the Taubat vehicle factory at <372:115> |
| 25:41 | 22 | Intelligence | the Taubat temple at <372:107> |
| 26:44 | 23 | Intelligence | the vehicle factory is destroyed; no Taubat vehicles may be left |
| 29:25 | 25 | Security | the temple is taken; its mineral transport equipment is being switched on |
| 29:34 | 26 | Engineering | minerals sent to the colony; suggests leaving and destroying the temple |
| 35:02 | 28 | Engineering | the mine at 244, 092 has run out |
| 35:43 | 29 | Intelligence | the Taubat are no longer a threat |
| 35:51 | 30 | Operations | all objectives met |

Compared with F1:

- **Generic messages are shared by both sides**: "<building> finished"
  from Engineering, the resource shortage from Operations (the same text in
  both missions), a mine running out, all objectives met. Klingon
  Operations takes the part the Federation's Command plays.
- **Mission messages** come from the script: discoveries with map
  positions (lookout post, factory, temple), a message from the natives
  with their own portrait, science reports on sites (the Hubrin ruins), and
  the temple's capture (optional objective 24).
- K1 adds an upgrade message ("first construction yard upgrade") and a
  power shortage, which F1's player didn't run into.

## Buildings and their models

The selection panel names what's selected, with a small portrait of it, so
41 selections in the recording give names directly; shapes matched against
`klibuild.cat` and `forbuild.cat` drawn in the data viewer.

| Name in the panel | Model | How sure |
|---|---|---|
| Colony hub | `kch` (the stepped pyramid with a spire); later two wedge wings (`kch_b`) | sure (shape) |
| Colony hub upgrade (selectable on its own) | `kch_b` | sure |
| Vehicle yard | `kvy` | sure (portrait) |
| Storage facility | `ksf` (the eight-armed rosette) | sure |
| Resource processor | `krps` | likely |
| Sickbay; "sickbay upgrade" | `ksb_a`; `ksb_b`/`ksb_c` | likely |
| Construction yard | `kcy` | likely (its model is mostly a few marker faces) |
| Science station | `kss_a` (the tall block) | sure (shape) |
| Arsenal (complete 8:46) | `kab` (the flat square with the Klingon emblem, 12:15) | sure (shape) |
| Adv. power generator | `kpgs2`; the plain power generators `kpgs` (striped towers) | likely |
| Mining station | `kms` | likely |
| Livestock farm | `klf` (34:10) | sure (shape) |
| A low curved gun (12:15, 34:10) | `ksg` (static gun) | likely |
| Hubrin fortress (owner HUBRIN, 16:43) | `hf` in `forbuild.cat` | sure (shape) |
| Phaser defense turret (owner TAUBAT, 25:29) | `tsg` | likely (portrait) |
| The Taubat temple, a tower with a glowing green top (30:35) | `tt` | sure (shape; F1's temple too) |
| Flat triangular Taubat structures (25:55) | `tw` | sure (shape) |

So an upgrade piece **is selectable and has its own name** ("Colony hub
upgrade", "Sickbay upgrade"), as the Federation's `_b`/`_c` pieces
presumably are. The panel also shows a building's state (OPERATIONAL,
INCOMPLETE while built), shield cover (BASIC, later HIGH: after an
upgrade, from about 9:59), efficiency (0 % while built, then 40-120 %),
power (CONSUMER / SUPPLY / RESERVE) and its OFFICERS and CREW. Units show
READY or EN ROUTE and shield cover; seen: armed transport, ground scout,
science vehicle, disruptor battery. A STAFF panel lists officers by name,
section, skill and post ("Operations, skilled, colony hub").

## Positions

The mine positions reported here lie on deposits of the map once read as
eighths of a block, row first; see [gmf.md](gmf.md#map-positions-in-the-game),
where all the recordings' mines are put together. (An earlier reading, 16
per block, didn't fit.)

## Still to look at

The COMMAND panel and mission report, and the messages missed (3-6, 8-9,
12, 15-17, 19, 24, 27).
