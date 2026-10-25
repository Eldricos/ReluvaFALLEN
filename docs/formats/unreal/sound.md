# Unreal Engine 1: sounds and music

Status: **implemented** (`src/engines/unreal/sound.cpp`) for Sound; the
same reader extracts Music. **On The Fallen** 2868 of 2930 sounds decode;
62 (looping ones such as `Looping_Fear.moan_fear_lp`, and music cues such
as `Trks_MagicBeaut.orb_thme`) hold something other than a WAVE file,
still to be identified: `ue-check` now reports each one's format name and
first bytes. Sources as in [package.md](package.md).

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
