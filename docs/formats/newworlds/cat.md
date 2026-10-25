# Star Trek: New Worlds: `.cat` archives

Status: **archive structure fully understood** (verified on all 24 retail
archives); the formats of the stored files are not studied yet. Reader:
`src/engines/newworlds/cat_archive.cpp`.
Worked out from hex dumps and `reluva-tool cat-probe` runs on the clean
retail `rommisc.cat` and `glblgame.cat`, October 2026. Run
`reluva-tool cat-probe <file>` to check this description against any archive.

All values are little-endian.

## Chunks

The whole file is a tree of chunks. Every chunk is:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | tag |
| 2 | u32 | payload size (excluding these 6 bytes) |
| 6 | … | payload |

Tags seen in the 24 retail archives:

| Tag | Kind | Stored extensions | Notes |
|---|---|---|---|
| `0x00` | container | | purpose unknown; in `*actor`, `formisc`, `glblgame`, `misc` |
| `0x01` | root | | the whole file; size = file size − 6 |
| `0x05` | table of contents | | see below |
| `0x11` | file | `.gmf` | only in the planet (`*wrld`) archives; source size always differs |
| `0x16` | file | `.lbp`, `.mp0`–`.mp4`, `.eb0`, `.eb1`, `.fbp` | textures, see [lbp.md](lbp.md) (and one `.stb` in `misc.cat`) |
| `0x17` | file | `.geo` | models, see [geo.md](geo.md); source size always differs |
| `0x25` | file | `.arf` | only in the `*actor` archives |
| `0x26` | file | `.fif` | font glyph table, see [fif.md](fif.md); inside `0x27` in the `*gui` archives |
| `0x27` | container | | inside `0x28`: a font's `.fif` and its `.fbp` image |
| `0x28` | container | | one font, in the `*gui` archives |
| `0x29` | container | | texture set: an `.mmb` followed by texture levels |
| `0x2a` | file | `.mmb` | 8 bytes in the cases seen; first item of a `0x29` set |
| `0x2c` | file | `.mdl` | 522 in `misc.cat` |
| `0x32` | file | `.stb` | string tables, see [stb.md](stb.md) |
| `0x33` | file | `.wav` | complete WAV files; `alert003.wav` is IMA ADPCM (format `0x0011`) |

"Source size differs" means the size in the entry's copied metadata is not the
stored size, presumably because those files were converted when packed.

## File layout

```
0x00  chunk tag 1 (root), size = file size - 6
0x06    chunk tag 5 (table of contents), size T
0x0c      u32 entry count
0x10      entries, one after another (file 334 bytes, container 264 bytes)
0x0c+T  item chunks, in entry order; folders contain their children's chunks
```

Check: `rommisc.cat` has 29 files and 2 folders: 29 × 334 + 2 × 264 + 4 =
10218 = T. For `glblgame.cat`, T = 107372 with 342 entries works out to
exactly 244 files and 98 folders.

## Table entries

| Entry offset | Type | Meaning |
|---|---|---|
| +0x000 | u32 | offset: the item's chunk is at file position offset + T. Equivalently, offsets are positions in the file as if the table's contents were removed: the first is 12, right after the two chunk headers |
| +0x004 | u16 | index. Increases by 1 per entry, except that each folder uses one extra index after its last child (as if an unstored "end of folder" entry existed): in `rommisc.cat`, folder 18 holds 19 and 20, and the next entry is 22 |
| +0x006 | u8 | 0 |
| +0x007 | u8 | same in every entry of an archive (`0x15` in `rommisc.cat`, `0x17` in `glblgame.cat`); possibly an archive ID |

Container entries end there plus 256 bytes (264 total). Which tags are
containers isn't stored anywhere, so a reader needs the list above.
File entries continue:

| Entry offset | Type | Meaning |
|---|---|---|
| +0x008 | char[260] | original build-machine path, NUL-terminated (260 = Windows `MAX_PATH`). Bytes after the NUL are uncleared memory from the packing tool |
| +0x110 | u16 | mode, e.g. `0x8124` = regular file, read-only (apparently a C `stat` copy) |
| +0x112 | u16 | link count (1) |
| +0x11e | u32 | size of the original source file. Usually equals the chunk's payload size, but differs for some files (in `rommisc.cat`, 6 of 29, matching its 6 `.geo` files), so presumably those were converted when packed. The chunk size is authoritative |
| +0x122 | u32 | access time (Unix time, date only, as FAT stores it) |
| +0x126 | u32 | modification time (Unix time) |
| +0x12a | u32 | creation time (Unix time) |
| +0x12e | u32 | modification time again |
| +0x132 | u32 | Windows file attributes (`0x20` archive, `0x21` archive + read-only) |
| +0x142 | 12 bytes | unknown; values look random across entries, so probably more uncleared memory |

Example (`rommisc.cat` entry 0): offset 12 → chunk at 10230, tag `0x32`,
size 3168, modified 2000-08-14 23:14 UTC, path
`c:\prjs\game\stc\bin\rsc\dat\pc_win32\str\eng\miss\rom01.stb`.

## Stored file types seen so far

From the stored paths: `.stb` (string tables; leftover UTF-16 English text in
the path buffers suggests UTF-16), `.lbp` (textures, under `lbp\txr`), `.mmb`
+ `.mp0` and `.mmb` + `.eb0` pairs grouped in folders (possibly mipmapped and
environment-map textures), `.geo` (geometry), `.wav`.

## Verified

`reluva-tool cat-probe` walks the table using only the rules above and checks
that every chunk starts where the previous one ended, that the walk ends
exactly at the end of the table chunk, and that the last chunk ends exactly
at the end of the file. All 24 archives of the retail install pass.

## Open questions

- The meaning of the u8 at entry +0x007, and of the `0x00` containers.
- The formats of the stored files themselves.
