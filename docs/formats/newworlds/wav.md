# Star Trek: New Worlds: `.wav` sounds

Status: **fully decoded**. Stored in the `.cat` archives with chunk tag
`0x33` under `stnwsfx/` (85 in `kliactor.cat`, 91 in `fedactor.cat`, 35 in
`fedmisc.cat`, 3 each in the alpine and desert planet archives). Worked out from the files themselves, October 2026.
`reluva-tool wav-info <file.wav | archive.cat>` shows one or surveys them
all; `reluva-tool wav-decode <in.wav> <out.wav>` writes 16-bit PCM. All
decode.

## Layout

Ordinary RIFF WAVE files (little-endian):

```
"RIFF" u32 size "WAVE"
"fmt " u32 20:
    u16 format          0x11 (IMA ADPCM) in every file seen
    u16 channels        1
    u32 rate            22050 (one file, kneg1, 11025)
    u32 bytes per second
    u16 block size      512
    u16 bits per sample 4
    u16 2, u16 samples per block (1017)
"fact" u32 4: u32 frames
"data" u32 size: the blocks
```

Other chunks are skipped. The last block is usually short; the `fact` count
says where the sound really ends (a few samples before the block does).

## IMA ADPCM blocks

Each block starts, per channel, with s16 the first sample, u8 the step index
(0-88) and a u8 0. Then 4-bit codes, in groups of 4 bytes per channel
(8 samples), low nibble first. Each code updates the sample and step with
the standard IMA tables (89 steps, 7 to 32767; index changes −1, −1, −1, −1,
2, 4, 6, 8):

```
diff = step >> 3
if code & 4: diff += step
if code & 2: diff += step >> 1
if code & 1: diff += step >> 2
sample += code & 8 ? −diff : diff   (clamped to 16 bits)
index  += change[code & 7]          (clamped to 0-88)
```

A 512-byte mono block holds 1 + 508 × 2 = 1017 samples.

Checked against ffmpeg's decoder: the samples differ by at most 33, from
ffmpeg computing `diff` exactly rather than by the shifts above; lengths
differ by the few samples ffmpeg keeps past the `fact` count.

## Copies

The actor archives store some sounds many times over, each copy a full
file of its own:

| Archive | Sounds | Different | General | Per vehicle (12 each) |
|---|---|---|---|---|
| `kliactor.cat` | 85 | 62 | 13 | `apc`, `fgs`, `mdb`, `pa`, `dt`, `sv` |
| `fedactor.cat` | 91 | 68 | 19 | `apc`, `fgs`, `msg`, `pa`, `pv`, `sv` |

The general sounds (weapons, shields, doors, crates, each vehicle's
start-up) come first. Each vehicle's set of 12 is then always:

```
line A voice 1, line B voice 1, line A voice 2, line B voice 2,
line C voice 1, line D voice 1, line C voice 2, line D voice 2,
4 × the side's refusal sound (kneg1.wav, 0.79 s; fneg1.wav, 0.40 s; 11025 Hz)
```

for example `fapc003a, fapc004a, fapc003b, fapc004b, fapc007a, fapc012a,
fapc007b, fapc012b`, then `fneg1` four times. The number is the line
(1-12, each vehicle using four of them) and the letter the voice (a-e;
two per vehicle). So a set looks like three groups of four responses, the
last group the same refusal for every vehicle; which group is played when
(selecting, ordering, refusing?) isn't known yet.

The same refusal sound is in `fedmisc.cat` 34 more times, as
`default.wav`, `pass.wav` and `fail.wav` (byte for byte `fneg1.wav`; 58
copies in all). There they follow the mission string tables: after
`fed01.stb` 7 `default` then `pass` and `fail`, after `fed02.stb` 8, and
after `fed14.stb` (following `fed03`, `fed08` and `fed12`) 13. Each mission
table has `~MISSION PASS~` and `~MISSION FAIL~` pages, so these look like
the missions' voice-over slots (briefing pages, then pass and fail) with
no recording, filled with the stand-in sound. The `default` counts don't
match the tables' page counts (10, 10 and 11 pages before the pass page),
so which page each slot goes with isn't known. `klimisc.cat` and
`rommisc.cat` have no such slots (only `alert003.wav`).

`reluva-tool wav-info <archive.cat>` marks each copy with the first one it
repeats.

## In Reluva

The decoder is in `src/common/audio/wav.cpp`, shared by every engine, and
so is the mixer that plays them (`src/common/audio/mixer.cpp`).
The data viewer's sounds page (**Tab**) lists the sounds with their waveform
(and the game folder's loose `.mp3`/`.wav` files after them, see
[media.md](media.md));
**Space** or **Enter** plays or stops one, **A** plays each as it's
selected. The title names the sound's archive. Copies are listed once, with their
count and any other names and archives in the title; **F** lists every copy.
