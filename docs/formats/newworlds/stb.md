# Star Trek: New Worlds: `.stb` string tables

Status: **fully understood**. Reader: `src/engines/newworlds/string_table.cpp`.
Stored in `.cat` archives with chunk tag `0x32`, under `str/eng/...`; one,
`str/eng/frend.stb` in `misc.cat`, has the texture tag `0x16` instead.
Checked on the 47 tables in `fedmisc.cat`, `klimisc.cat`, `rommisc.cat`
(14 each in the last two) and `misc.cat`, October 2026.
`reluva-tool stb-dump <file.stb>` prints one; `reluva-tool stb-dump
<archive.cat>` lists an archive's tables. All read exactly.

All values little-endian.

```
u32 count               always the number of strings
repeated count times:
  u32 id
  UTF-16LE text, terminated by a 0x0000 code unit
```

The strings end exactly at the end of the file. Ids aren't in order
(`fed01.stb` starts 1, 24, 2, 3, …; `frend.stb` 21, 1, 2, …) and can have
gaps (`cvr03.stb` has 82 strings with ids up to 87). The game presumably
looks strings up by id.

## Text

The text is plain, with a little markup:

| Markup | Meaning | Example |
|---|---|---|
| `~HEADING~` | a page's heading, before its text | `~PRIMARY OBJECTIVE 1~Build 1st stage Resource Processing Station, …` |
| `\n` (U+000A) | a line break | `~INCOMING TRANSMISSION~Power Corrupts\nStardate: 8987.8` |
| `%d`, `%%` | printf-style codes filled in by the game | `Done: %d%%` (the loading screen) |

One heading has a tab instead of a space (`~INCOMING\tTRANSMISSION~` in
`fed02`). Apart from those, the text is ASCII with the odd `’` and `é`.

## The tables

**`frend.stb`** (front end): the main menu (`STC: Main Menu`, `New one
player game`, …), the loading screen's messages (`Loading textures...`,
`Done: %d%%`) and the race choice (`Federation`, `Klingon`, `Romulan`).

**Missions** (`str/eng/miss/`): `fed01`-`fed14` (Federation), `jnt01fed`-
`jnt09fed` (presumably joint missions, Federation side), `cvr01`-`cvr03`, and `tst`
("Deathmatch Test"). A mission's table holds, usually in this order:

1. the title, with a leading space and in quotes (`'Power Corrupts.'`)
2. a stardate line (`Stardate 8987.8.  The ice world of Karazin has …`)
3. the briefing pages, each a `~HEADING~` and its text: `STELLAR
   CARTOGRAPHY`, `INCOMING TRANSMISSION`, `PLANETARY DATA`, `PRIMARY
   OBJECTIVE 1`-`3`, `SECONDARY OBJECTIVE 1`, `MISSION RESTRICTIONS`,
   `MISSION FAIL CRITERIA`, `SPEECH`, `BACKGROUND DATA`, `ADDITIONAL`, …
4. `~MISSION PASS~` and `~MISSION FAIL~` pages
5. the objectives as listed in game (`Build a power generator.`)
6. the reasons for failing (`Colony hub destroyed.`)

`cvr03.stb` holds several missions at once: the training mission "Duty
first." (`~INSTRUCTION~` pages), the five tutorials (`TUTORIAL 1:
CONSTRUCTION AND UPGRADES` … `TUTORIAL 5: COMBATING A HOSTILE FORCE`),
and three short ones ("Slaver's Run", "The Archer of Heaven", "False
Sanctuary") with one-line briefings that end in "...".

The `default.wav`, `pass.wav` and `fail.wav` sounds stored after some
mission tables seem to be those missions' voice-over slots; see
[wav.md](wav.md).

## In Reluva

The data viewer's strings page (**Tab**) shows each table word-wrapped
with its ids, headings in yellow and printf codes in blue, in Reluva's own
built-in font (`src/common/font.cpp`). **Left/Right** picks a table,
**Up/Down**, **PageUp/PageDown** and **Home/End** scroll, **+/-** change
the text size.

## Open questions

- How the game finds a mission's table and which ids it asks for (the
  layout above varies a little between missions).
- Whether `~` does more than mark headings.
