# Unreal Engine 1: packages (`.u`, `.unr`, `.utx`, `.uax`, `.umx`)

Status: **implemented** (`src/engines/unreal/package.cpp`,
`properties.cpp`). **Checked on The Fallen** (`reluva-tool ue-check`,
October 2026): all 175 packages open (49 of version 71, 20 of 72, 106 of
73, licensee 0), and every one of their 3801 palettes reads, so the
tables, properties and object headers hold for versions 71-73.

Sources: public community documentation of the UE1 package format (the
format notes accompanying community package tools, and modding wikis).
No engine source code was used.

All values little-endian. Floats IEEE single.

## Compact integers

Counts, sizes, name indices and most object references are stored as a
variable-length signed integer of 1 to 5 bytes:

- first byte: bit 7 the sign, bit 6 set if another byte follows, bits 0-5
  the lowest 6 bits of the value;
- each further byte: bit 7 set if another byte follows, bits 0-6 the
  next 7 bits.

## Header

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | tag, `0x9E2A83C1` |
| 4 | u16 | file version (61 in early Unreal, 68-69 in Unreal Tournament; The Fallen's are 71, 72 and 73) |
| 6 | u16 | licensee version (0 for Epic's own games) |
| 8 | u32 | package flags |
| 12 | u32 × 2 | name count, name table offset |
| 20 | u32 × 2 | export count, export table offset |
| 28 | u32 × 2 | import count, import table offset |
| 36 | ... | before version 68: heritage count and offset (GUIDs); from 68: a GUID (16 bytes), a generation count, and per generation the export and name counts |

The tables are read at their offsets; nothing else in the header is needed.

## Name table

Each entry is a string then a u32 of flags. Before version 64 a string is
NUL-terminated; from 64 a compact length (counting the NUL) comes first.

## Object references

A 32-bit or compact integer: 0 none, `n > 0` the export `n - 1`, `n < 0`
the import `-n - 1`.

## Import table

| Type | Meaning |
|---|---|
| compact | name of the package that defines the class ("Core", "Engine") |
| compact | name of the class ("Texture", "Class", "Package") |
| i32 | the import's outer, an object reference (its group, or its package) |
| compact | name of the object |

An import's package is found by following outers until one has none.

## Export table

| Type | Meaning |
|---|---|
| compact | class, an object reference; 0 means the object is a class |
| compact | super, an object reference (what a class or struct extends) |
| i32 | outer, an object reference (its group); 0 for the package itself |
| compact | name of the object |
| u32 | object flags (0x02000000: has a state frame) |
| compact | size of the object's data |
| compact | offset of the object's data, only when the size isn't 0 |

## An object's data

Objects that aren't classes start with:

1. **A state frame**, if flag 0x02000000 is set: compact node, compact
   state node, u64 probe mask, i32 latent action, and when node isn't 0 a
   compact code offset.
2. **Tagged properties**, until the name `None`. Each:
   - compact name index;
   - an info byte: bits 0-3 the type, bits 4-6 the size code, bit 7 a
     Bool's value or, for any other type, that an array index follows;
   - for Struct (10), the struct's name (compact);
   - the size, by code: 0 → 1, 1 → 2, 2 → 4, 3 → 12, 4 → 16 bytes; 5 a
     u8, 6 a u16, 7 a u32 follows with the size;
   - the array index, if flagged: one byte if its top bit is clear, two
     (14 bits) if the top bits are 10, four (30 bits) if 11;
   - the value, of that size (none for a Bool).

   Types: 1 Byte, 2 Int, 3 Bool, 4 Float, 5 Object (compact reference),
   6 Name (compact index), 7 String (fixed, NUL-terminated), 8 Class,
   9 Array, 10 Struct, 11 Vector, 12 Rotator, 13 Str (a string as the
   name table stores them), 14 Map, 15 Fixed array. Vector structs are
   3 floats; Rotator structs 3 i32 (pitch, yaw, roll; 65536 a turn).

   Properties at their class's default value aren't stored.
3. **The class's own data**, laid out by the class: see
   [texture.md](texture.md), [sound.md](sound.md), [model.md](model.md).

Classes (class reference 0) are laid out differently (their script and
defaults); not read yet.

## Search path

The engine finds a package by name, in the folders the game's `.ini`
lists in its `[Core.System]` section, one `Paths=` line each, relative
to `System/` (`..\Textures\*.utx`). The Fallen's is `System/DS9.ini`,
made from `System/Default.ini` on first run (Reluva reads `Default.ini`
when there's no `DS9.ini`). Its maps are `.dsm` (`..\Maps\*.dsm`); a
package is taken as a map when its extension is the one the `Maps`
folder's line gives, or `.unr`. The Fallen's install has no `Music`
folder although its `.ini` lists one.
Without one Reluva uses `*.u`, `../Maps/*.unr`, `../Textures/*.utx`,
`../Sounds/*.uax`, `../Music/*.umx`.
