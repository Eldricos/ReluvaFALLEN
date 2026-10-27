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
| RETURNLINE | the key of a line to go back to (the communicator's menus) |
| TAG | a label scripts ask for lines by (`HAILOBN01`, `START_K11A`); `DoNotPlay` on some |
| EVENT | what the line sets off: actors with this tag are triggered, or one of the HUD's beam-downs |
| CONDITION | when it may be said: tests joined by `and` and `or`, maybe after `!` (`SpecialCondition( InTemple ) and ! CommunicationBlocked`, `DS9Phase( 2 )`); one in brackets is a note to the writers |
| CAMERA | a cut scene camera (`CAM(V:1)`, `CAM(R:19,15,35,5,-144,0 F:90)`) |
| SPEAKER ANIMATION, LISTENER ANIMATION | an animation each plays (`CnvFirm1`) |
| SPEAKER FACE, LISTENER FACE | a face (`FEX_DETERMINED`, the names of Pawn's `EFEX_Type`) |

A line's key is its mission, type, speaker and line ID run together
(`K01AMBBFC000`); NEXTLINE and RETURNLINE name lines so, across tables
(`GENHCLSKO0?0`).

The voices are ADPCM WAV files ([sound.md](sound.md)); a line lasts as
long as its WAV's data chunk at its bytes a second. The `.uax`'s
ScriptText is only the `#EXEC AUDIO IMPORT` lines that built it.
