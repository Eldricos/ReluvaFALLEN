# The Fallen: dialogue tables

Status: **implemented** (`src/engines/unreal/dialog_table.cpp`), read by
the level's dialogue (`vm_dialog.cpp`, [unreal_vm.md](../../engines/unreal_vm.md)).
**Checked on The Fallen** (October 2026): the 12 tables in the install's
`int` folder give 2040 lines.

The lines the characters say are in the install's `int` folder, one
table per mission or group: `M01_Dialogue.txt` to `M10_Dialogue.txt`,
`GEN_Dialogue.txt` (general lines, any mission), `DS9_Dialogue.txt` (Ops
and the Promenade), `CIN_Dialogue.txt` (cut scenes), `GTG_Dialogue.txt`
and `M00_Dialogue.txt`. A level names its own table in its LevelInfo's
`DialogFile` (`M01_Dialogue`; some levels name none).

Each file is text, Windows-1252, lines ending CR LF, columns separated
by tabs: a spreadsheet saved as text. A field in double quotes has its
own quotes doubled. The first rows are a `Missions` row (the missions
the table covers), a `Speakers` row and the column headers; blank rows
of tabs space the rest out. Every other row is a line:

| Column | Meaning |
|---|---|
| REC ID | the speaker's code ("BFC") |
| REC ID | a number ("002"); with the code, the name of the line's voice: a Sound of that name ("BFC002") in the `.uax` of the table's name (`Sounds/m01_Dialogue.uax`) |
| MISSION | a letter and a number: K, S or W for Kira's, Sisko's or Worf's version of a mission, G or M for anyone's; 01 to 11 the mission, OP Ops, PM the Promenade, EN any (GEN, KEN...). GEN_Dialogue.txt also has legend rows listing the types and faces (in this column), which aren't lines |
| TYPE | what kind of line: HAL (the player hailing someone), HCL (closing a hail), CIN (a cut scene), AMB (ambient), EVT (an event's), KIL, DIE, FSE, LSE, TTP, PAN... 27 in all |
| SPEAKER, LISTENER | speakers' codes: KRA Kira, SKO Sisko, WRF Worf, OBN O'Brien, DAX, BSR Bashir, ODO, OBK Obanak, GRK Garak, CPU the computer, and the others' |
| LINE ID | "000", "010"... |
| LINE | the text |
| DIRECTION | the voice actor's direction ("(urgent)") |
| PAUSE | seconds after the line (a few lines) |
| NEXTLINE | the key of the line that follows; `?` stands for any character, so one of several |
| RETURNLINE | the key of a line to go back to once what NEXTLINE leads to is done: a hail's sign-off ("GENHCLSKO010", "Understood. Sisko out.") |
| TAG | a label scripts ask for lines by (`HAILOBN01`, `START_K11A`); `DoNotPlay` on some; on the AI's and the injured's lines, the `Dialogue_Tag` of the one actor who says it (`FC1`, `BM2`) |
| EVENT | what the line sets off: actors with this tag are triggered, or one of the HUD's beam-downs |
| CONDITION | when it may be said: tests joined by `and` and `or`, maybe after `!` (`SpecialCondition( InTemple ) and ! CommunicationBlocked`, `DS9Phase( 2 )`); one in brackets is a note to the writers |
| CAMERA | a cut scene camera (`CAM(R:19,15,35,5,-144,0 F:90)`: R a place and turn from the speaker, A in the world, F the field of view); `V:1` a voice-over (a log), `H:1` heard over the communicator |
| SPEAKER ANIMATION, LISTENER ANIMATION | an animation each plays (`CnvFirm1`); a face here (`FEX_HAPPY`, as some of CIN_Dialogue's lines have, their face column empty) is taken as the face |
| SPEAKER FACE, LISTENER FACE | a face (`FEX_DETERMINED`, the names of Pawn's `EFEX_Type`) |

Lines numbered one after another (010, 011) with no NEXTLINE between
them, the later untagged, are one speech: Kira's log, a Vorta's report.

The type legend in GEN_Dialogue.txt groups them: 1 DIE; 2 CIN; 3 HAL HOP
HBO HCL TRN (the communicator: a hail, the player's options, the
crewmate's report, signing off); 4 PAN EXB EXG GSP DWN; 5 FSE; 6 SUR TTP
GPI KIL FHE EVT HUD TBD; 7 BAT LSE INT LED ACK; 8 AMB REP. No script asks
for FSE, LSE, KIL, SUR, AMB or REP: the AI's own, the engine's.

The game ships tables for missions 1, 3 to 7, 10 and 11, the training
(`TRN_Dialogue.txt`) and the others above: 2,181 lines in all. Some
copies of the install lack `M11_Dialogue.txt` and `TRN_Dialogue.txt` (and
mission 11's and the training room's `.int` files); the full CD install
has them. Mission 11's has 73 lines, 70 with voices (KRA134 to 136 have
none); the training's 68, all voiced. No copy has a table for mission
12: its voices are in `m12_Dialogue.uax`, their text nowhere.

A line's key is its mission, type, speaker and line ID run together
(`K01AMBBFC000`); NEXTLINE and RETURNLINE name lines so, across tables
(`GENHCLSKO0?0`).

The voices are ADPCM WAV files ([sound.md](sound.md)); a line lasts as
long as its WAV's data chunk at its bytes a second. The `.uax`'s
ScriptText is only the `#EXEC AUDIO IMPORT` lines that built it.
