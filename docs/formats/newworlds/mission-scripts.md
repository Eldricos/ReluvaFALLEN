# Star Trek: New Worlds: Reluva's mission scripts

The original keeps its missions' logic in `STNW.exe`; none of it is in the
data files. Reluva writes its own, from what recordings of play showed
([play.md](play.md)): which events each mission reacts to, in what order,
and what follows. The scripts are in `src/engines/newworlds/mission_scripts.cpp`,
read by `mission_script.h/.cpp`.

## What's in a script

- **Objectives** are the mission's own strings, named by their id in its
  table (`objective 16` is `fed01.stb`'s string 16), so their text comes
  from the player's game data and none of it is copied here. A `hidden`
  objective appears only when revealed (F1's temple capture); an
  `optional` one doesn't hold up the end.
- **Fail conditions** name the table's fail string: `lose 21 destroyed
  colony-hub`.
- **Messages are Reluva's own words** for what the game's messages say
  (the recordings' gist, see the play-*.md pages), with the sender the game
  used (a section, or "Incoming message", "Message from Starfleet", …).
- **Sites** are named places with map positions as the game gives them
  (eighths of a block, row first, [gmf.md](gmf.md#map-positions-in-the-game)),
  where a message named one.
- **Events** are what a game would report: a building finished, a set of
  buildings complete (`built-all vehicle-yard*4 power-generator*5|advanced-generator*3`),
  a unit nearing a site, a scan started, done, lost or of the wrong place,
  research done, a site entered or captured, something destroyed, a rival's
  hub destroyed or mission failed, a stock reached, a timer.
- **Generic rules** (`mission generic`) are the messages every mission
  shares: a building finished, a mine exhausted at a position, short of
  power, resources or crew, a rival's hub destroyed or mission failed.

The format is described at the top of `mission_script.h`.

## How far they go

Scripted: the 44 missions with recordings, F1-F14 and the Klingon
campaign with the joint missions from each of those sides, the Romulan
recordings (R1, R2, R9, R12, R14 and J1-J4, J6-J9 as the Romulans), and the
bonus missions C9, C10 and C11. Each one's blocks are in the order the recording
showed them, and a test plays every script through in that order (the
walkthrough) and checks it ends in a win where the recording did. Blocks
the recordings didn't show (a fail never reached, a message the panel
hid) are inferred from the objective and fail strings and marked
"(inferred)".

Not known, so not scripted: where the starting colonies, units and enemies
are (the missions page approximates the colonies: [colony.md](colony.md)); the timing of attacks; how close "approach" is; the exact amounts
(the dilithium targets come from the objective strings). Those need a
running game to fill in. Of the Romulan campaign only R8 has no recording,
so no script. Where a Romulan recording's beat had no event of its own (a
repair or a hold before the end, a taunt minutes after a fight) the script
approximates it with a timer, and says so.

## In Reluva

The missions page plays a mission's script a step at a time with **X**,
feeding it the walkthrough's events in order: the objectives (the game's
strings, ticked as they're met) and the latest messages are shown over the
map, and the title names the event. **Z** stops.
