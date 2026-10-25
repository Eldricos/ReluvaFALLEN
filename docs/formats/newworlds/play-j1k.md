# Star Trek: New Worlds: watched in play, mission J1 (Klingon side)

Joint mission J1 ("Into the Arena", `icewrld`'s `j1` map, the snow world
Mazikaan), played as the Klingons: the third mission of the Klingon
campaign, in a 39-minute recording with commentary (YouTube
`dw6hjzszR9g`). Observed behaviour; messages paraphrased. October 2026.

The recording's mission was identified from its audio alone: of all 124
`KlingVO` clips, only the ten `J1K` ones match it. So the Klingon campaign
plays the joint missions as its own (K1, K2, then J1); the other two sides
are run by the computer here.

## Briefing (0:00-3:45)

The ten clips play back to back (0.16-0.83 against 0.09 or less elsewhere),
one per page of `jnt01klg.stb`, in order from STELLAR CARTOGRAPHY:

| Clip | Starts | Page (id) | Shown |
|---|---|---|---|
| `J1K005` | 0:28 | STELLAR CARTOGRAPHY (3) | the galaxy |
| `J1K006` | 0:56 | PLANETARY DATA (4) | the star system |
| `J1K007` | 1:26 | BACKGROUND DATA (5) | the relief map, a camera view inset |
| `J1K008` | 1:39 | BACKGROUND DATA (6) | the Taubat temple as a model on a grid |
| `J1K009` | 1:56 | OBJECTIVE (7) | the relief map with Taubat City's buildings in orange |
| `J1K010` | 2:26 | ADDITIONAL (8) | the colony site on the ground |
| `J1K011` | 2:41 | ADDITIONAL (9) | a Taubat building by the ice caves |
| `J1K012` | 2:55 | OBJECTIVE (10) | the relief map, the temple marked |
| `J1K013` | 3:23 | ADDITIONAL (11) | the relief map |
| `J1K014` | 3:38 | ADDITIONAL (12) | the Klingon emblem |

So clip = id + 2 here, and the rule found in K2 holds
([play-k2.md](play-k2.md)): one clip per step from STELLAR CARTOGRAPHY, in
the table's order. Strings 13-16 (more ADDITIONAL pages) have no clip, and
this table's INCOMING TRANSMISSION page is string 17, near the end, also
without one. The briefing's views now include a **camera inset**: a small
live picture of the place being described, with a label over the relief
map ("FRIENDLY STRUCTURE", "TERRAIN AT <x:y>").

## Messages

10 in all; 12 samples read:

| Time | # | From | What it says (paraphrased) |
|---|---|---|---|
| 11:40 | - | | (the panel with no messages yet: "no messages on file") |
| 19:15 | 1 | Engineering | the mine at 052, 148 has run out |
| 20:52 | 1 | Security | victory: the Taubat base is destroyed |
| 21:00 | 2 | Intelligence | the Federation can no longer complete its mission |
| 35:37 | 4 | Intelligence | the Federation hub is destroyed |
| 35:48 | 5 | Engineering | the Taubat temple is taken; tracing its power source |
| 36:29 | 6 | Intelligence | the Romulan hub is destroyed |
| 36:38 | 8 | Intelligence | the Romulans can no longer complete their mission |
| 38:46 | 9 | Intelligence | the Taubat temple is destroyed |
| 39:00 | 10 | Intelligence | the Klingons have completed their mission |

(The counter restarts at 1/1 at 20:52: the player deleted the messages.)
In a joint mission the script reports **each side's progress**: a side
that can no longer win, each side's hub destroyed, and a side completing
the mission, by name.

## Buildings and units

The colony's buildings are K1's (colony hub, construction yard, sickbay,
science station, resource processor and "resource processor upg", storage
facility, mining station), and the units its (ground scout, photon
artillery, armed transport). New in the selections: a **"Taubat house"**
(owner TAUBAT) and the **Federation colony hub** (owner FEDERATION, its
portrait the `fch` model) while it was being attacked (34:13). From 31:30
the recording shows the Federation colony, `fch`, `frps` and the other
models of [play-f1.md](play-f1.md#buildings-and-their-models), on its
concrete; the Taubat temple (`tt`) by the ice caves at 36:30, captured and
then destroyed.

## Colony

Colony size 54/65 at 8:24, 63/70 at 12:58, 75/90 at 22:19, 87/90 at 34:13.
The loading screen after the mission (39:00) is the next level's.
