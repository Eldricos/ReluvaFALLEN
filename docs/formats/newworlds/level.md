# Star Trek: New Worlds: `level.dat`

Status: **fully understood**. Reader: `src/engines/newworlds/levels.cpp`.
A loose text file in the game folder (1,134 bytes, CRLF lines): the
missions the main menu offers. `olevel.dat`, `kapla.rse` and `game.ini` are
covered below. `reluva-tool level-info <level.dat>
[planet.cat...]` lists them, with the archives holding their maps.

```
// format for levels displayed in the main menu
// parser short name, max players, text description
// --------------------------------------------
F1,2,Power Corrupts
K1,2,The Insult
...
```

One mission per line: short name, maximum players, title. `//` starts a
comment; some comments are disabled missions (an early `J1,6,Defiant`, and
the bonus missions C9-C11). 37 missions are listed, plus those 4:

| Short names | Missions |
|---|---|
| `F1`-`F14`, `K1`-`K14`, `R1`-`R14` | Federation, Klingon and Romulan campaigns (not every number; 12 and 14 are "Death Rattle" and "The Finest Hour" for all three) |
| `J1`-`J9` | joint missions, 3 players |
| `Test` | "Constraints" |
| `E1`-`E3` | "ECTS missions" (a trade show): Heisenberg, Excalibur, Constitution |
| `C1`, `C4`-`C8` | the tutorials (C1, 1 player); `C9`-`C11` the commented-out bonus missions |

## What the short name ties together

The short name is the mission's key across the files:

- **its map:** `map/<name>.gmf` in one planet archive (`F1` → `map/f1.gmf`).
- **its text:** `str/eng/miss/` in the side's misc archive: `F1` → `fed01.stb`,
  `K1` → `klg01.stb` (`klimisc`), `R1` → `rom01.stb` (`rommisc`), `J1` →
  `jnt01fed.stb`, `jnt01klg.stb` and `jnt01rom.stb`, one per side. All the
  C missions share `cvr03.stb`: training ("Duty first.", C1), the five
  tutorials (C4-C8) and C9-C11, in level.dat's order in the file, though by
  id "Slaver's Run" (C9, ids 57-61) sits between the fourth and fifth
  tutorials. The tables'
  titles match level.dat's ("A Piece of paper." for "A Piece of Paper").

The maps found so far:

| Planet archive | Maps | Briefings describe |
|---|---|---|
| `icewrld.cat` | `f1`, `j1`, `k7`, `r8`, `k12`, `r12` (+ `kurt64`) | ice world (F1), snow-covered world (J1) |
| `deswrld.cat` | `f2`, `r9`, `j7` (+ `demo1`) | desert world (F2) |
| `alpwrld.cat` | `f3`, `j3`, `f12` (+ `coverd1`-`3`) | forest world (F3), temperate world (F12) |

Across all six planet archives and the three sides' mission text
(`fedmisc`, `klimisc`, `rommisc`), each map's briefing names its kind of
world, and the kinds go with the archives:

| Planet archive | Briefings call it | Missions |
|---|---|---|
| `icewrld` | ice, snow, snow-covered, bleak | F1, J1, K7, K12, R8, R12 |
| `deswrld` | desert | F2, R9, J7 |
| `alpwrld` | forest, temperate | F3, F12, J3 |
| `alnwrld` | dust | F8, K1, R2, J4, J8, J9 |
| `redwrld` | barren (R1: "type 6") | J2, K8, R1 |
| `vlcwrld` | volcanic | K2, J6 (and `k4`, not in level.dat) |

No map exists for F14, K14 or R14 ("The Finest Hour"): each side's briefing
names the same world as its mission 12 ("Death Rattle"), and K14 is played
on `k12.gmf` (a mine position reported in a recording of it lies on that
map's deposits, [play-k14.md](play-k14.md)), so 14 reuses 12's map. The joint missions (J) have one table per side
(`jnt01fed`, `jnt01klg`, `jnt01rom`, …) with the same briefing world.
`kurt64`, `demo1`, `demo3`, `volc1` and `coverd1`-`3` aren't in level.dat.

## The other lists: `olevel.dat` and `kapla.rse`

Two more files in the same format sit beside it, perhaps lists from other
builds. `olevel.dat` (1,241 bytes, presumably "old level") is `level.dat`
with the bonus missions enabled under other short names (`U9`-`U11` instead
of `C9`-`C11`) and six more; `kapla.rse` (1,346 bytes) has the same, three
odd test entries (`U2,2,LOSTYOU`, `U3,3,LOSTME`, `U3,2,WHATARUE`) and the six
renamed:

| Short name | `olevel.dat` | `kapla.rse` | Planet archive |
|---|---|---|---|
| `D1` | Forest of Brad | Head to Head: Lupercal Jade | `alpwrld` |
| `D2` | Ice of Brad | Head to Head: Nemphos | `icewrld` |
| `D3` | Volcano of Brad | Head to Head: Lestrigon | `vlcwrld` |
| `D4` | Desert of Brad | Head to Head: Karalute | `deswrld` |
| `D5` | Alien of Brad | Head to Head: Xix Xix | `alnwrld` |
| `D6` | Red of Brad | Head to Head: Pellion Red | `redwrld` |

That names the six planet types, one per world archive; the archive column
follows from the names (and, for `alpwrld`, from F3's "forest world" being
in it). No `d1`-`d6` maps are in the three planet archives seen.

## `game.ini`

```
[STNW]
GAMEXRES= 640
GAMEYRES=480
FedLevel=1
KliLevel=2
RomLevel=3
TauLevel=4
MetLevel=5
HubLevel=6
```

The screen size (640×480, the size the data viewers draw at), and a number
for each side: the three playable races, then the native races the
missions' text names: the Taubat, the Metar ("the Metar colony core") and the
Hubrin, whose buildings and units are in `forbuild.cat` (see
[geo.md](geo.md)). What the numbers are used for isn't
known; they may be the sides' player numbers in the missions.

## Not here

level.dat names missions but says nothing about how they're set up: the
planet, starting colonies, units and objectives' triggers are elsewhere
(presumably `STNW.exe`). How the briefing plays its pages and voice clips
was seen in a recording of F1: [play-f1.md](play-f1.md).

## In Reluva

The engine reads level.dat from the game folder, and the map viewer's title
names each map's mission (`map/f1.gmf [F1 "Power Corrupts", 2 players]`).

The data viewer's first page, **missions**, goes through level.dat's
missions in its order, commented-out ones included (marked "not listed"),
and puts together what the short name ties to (`levels.h`:
`missionMapNames`, `missionTableName`, `missionSides`, `isMissionClip`,
`planetTheme`):

- the map, drawn as on the maps page (with its keys); mission 14 shows its
  side's mission 12 map. Test, E1-E3 and the C missions have no map in the
  planet archives. (The C missions' table is `cvr03`, and `alpwrld` holds
  `coverd1`-`3`, so those may be theirs; not assumed. A recording of the
  bonus mission C10 is most likely on `f2`'s map, [play-c10.md](play-c10.md).)
- the briefing, a page at a time in the game's `fe8pt` font: the table's
  `~HEADING~` strings, each a page, and the plain strings between them run
  together (the title and stardate first; the in-game objectives and fail
  reasons last). Joint missions have a table per side (**J**).
- the voice clips from `Media/` (**Space**, **N**, **P**), played as speech
  through the shared mixer, and the planet's music theme (**M**, looped,
  under the speech).

**Enter** plays the briefing as the game does: a step at a time, each
showing its page over its view and lasting as long as its voice clip (5 s
without one). The steps are in `src/engines/newworlds/briefing.cpp`: the 16
missions seen in recordings of play are scripted as they were shown
([play.md](play.md)); the rest follow the rule those recordings showed
(clips in number order over the table's heading pages, the first over the
galaxy, the second over the star system, the rest over the map, leftover
clips over the emblem), and the title says so. The map views use the
mission's map (the relief grid drawn with the terrain colours), turning
slowly about the place the step is about: the colony or a site a message
in the recording located (its temple, an allied base, an alien structure),
close to the ground or from high above, else the whole map. The
recordings' own camera positions can't be read (the position readout over
the briefing follows the mouse pointer, and the ground views show only a
compass), and the colonies' positions are taken from the mines they
reported exhausted early, so the places are approximate; the galaxy is
the game's `tabras` model, turning, and the emblem the side's emblem screen
from `glblgui.cat` ([gui.md](gui.md#emblem-screens), [geo.md](geo.md));
a model view shows the
model the recording showed (the Taubat temple `tt`, the Hubrin craft `h`,
the Metar device `mad`, the containment pillar `hcp`, a Metar spire `mch`,
the Federation shield generator `fsg`, a Klingon disruptor tank `kdt1`;
matched by shape and colour, so a few are judgement calls, J4's ship most
of all) turning on the red grid, with its own textures. The star system
view shows the system the mission's table names, from near its planet,
each body turned to face the camera ([geo.md](geo.md#star-systems); the
planets' pictures are Reluva's choice). Model views of the missions not
recorded are still Reluva's own stand-ins.

There are no units, buildings or starting colonies to show: the maps don't
hold them ([gmf.md](gmf.md)), and nothing found in the data files says
where they start.
