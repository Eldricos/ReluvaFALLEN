# Unreal Engine 1: sounds and music

Status: **implemented** (`src/engines/unreal/sound.cpp`) for Sound; the
same reader extracts Music. **Checked on The Fallen** (October 2026): 2868 of
2930 sounds decode. The other 62 (looping ones such as
`Looping_Fear.moan_fear_lp`, music cues such as `Trks_MagicBeaut.orb_thme`)
are placeholders: format `wav` but only the 4 bytes `58 20 0d 0a`
("X \r\n"), nothing to play. Reluva treats anything under 16 bytes that
isn't RIFF as one.

They stand for The Fallen's music. The install's `mp3` folder holds 62
MP3 files, as many as there are placeholders, named as the placeholder
Sounds are (`Looping_Fear.moan_fear_lp` - `mp3/moan_fear_lp.mp3`,
`Recurring_cues.UnderWater` - `mp3/underwater.mp3`,
`Trks_MagicBeaut.orb_thme` - `mp3/orb_thme.mp3`), and the game ships
Miles' MP3 decoder (`redist/win32/Mp3dec.asi`) to stream them. So a
placeholder's audio is `mp3/<the Sound's name>.mp3`, found ignoring case
(`loadSound()`); the Sounds page plays it as music. (Worked out from the
install's file listing, October 2026.) How the game picks which to play
(the music cues in `Sounds/Music.uax` and the maps' script, presumably) is
for when the script runs. Sources as in [package.md](package.md).

Sound and Music objects hold a whole file:

| Type | Meaning |
|---|---|
| properties | see [package.md](package.md) |
| compact | name index of the format: `WAV` for sounds; a tracker format for music (`IT`, `S3M`, `XM`, `MOD`) |
| i32 | from version 63: the file offset just past the data |
| compact | size |
| bytes | the file: a RIFF WAVE for sounds, decoded by `common/audio/wav.cpp` |

Whether Music has exactly this layout in every version is to be checked
(`reluva-tool ue-check` counts music objects read).
