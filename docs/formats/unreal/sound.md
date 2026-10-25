# Unreal Engine 1: sounds and music

Status: **implemented** (`src/engines/unreal/sound.cpp`) for Sound; the
same reader extracts Music. Tested on packages built by the unit tests,
**not yet on The Fallen's**. Sources as in [package.md](package.md).

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
