# Unreal Engine 1: sounds and music

Status: **implemented** (`src/engines/unreal/sound.cpp`) for Sound; the
same reader extracts Music. **Checked on The Fallen** (October 2026): 2868 of
2930 sounds decode. The other 62 (looping ones such as
`Looping_Fear.moan_fear_lp`, music cues such as `Trks_MagicBeaut.orb_thme`)
are placeholders: format `wav` but only the 4 bytes `58 20 0d 0a`
("X \r\n"), nothing to play. Reluva treats anything under 16 bytes that
isn't RIFF as one. Whether the game plays something else in their place
(another file, streamed music) is still to be found out. Sources as in [package.md](package.md).

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
