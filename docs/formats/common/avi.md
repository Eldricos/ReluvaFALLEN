# AVI movies

Status: **implemented** (`src/common/video/avi.cpp`) for the first video
stream (its frames kept compressed) and a PCM audio stream. **Checked on
The Fallen** (October 2026): `movies/intro.avi`, 640x480 Cinepak at 25
frames a second, 251 frames, with 10 seconds of 16-bit stereo PCM at
44,100 Hz.

## Layout

A RIFF file of form `AVI `. Every chunk is a four-character id, a
32-bit little-endian size (of its body, not counting these 8 bytes),
then the body, padded to an even length. A `LIST` chunk's body starts
with its own four-character type and holds more chunks.

- **`LIST hdrl`**: the headers.
  - **`avih`**: the main header. Its first 32 bits are the microseconds
    a frame; at 32 and 36 are the width and height.
  - **`LIST strl`**, one a stream, in stream order (the first is stream
    `00`, then `01`):
    - **`strh`**: the stream header. Its type (`vids` or `auds`), its
      handler (`cvid`), and at 20 and 24 its scale and rate: frames a
      second is rate / scale.
    - **`strf`**: the stream's format. For video a `BITMAPINFOHEADER`:
      the width and height at 4 and 8 (a negative height means top
      down), the compression at 16 (`cvid`). For audio a `WAVEFORMATEX`:
      the format (1 = PCM), channels, samples a second, and at 14 the
      bits a sample.
- **`LIST movi`**: the data. Each chunk's id is its stream's number in
  two digits then two letters: `dc` (a compressed video frame), `db` (an
  uncompressed one), `wb` (audio). They can be grouped in `LIST rec `
  chunks, as the intro's are (a frame and its sound together).
- **`idx1`**: an index of the `movi` chunks, not needed when reading it
  through.

An empty video chunk repeats the frame before it.
