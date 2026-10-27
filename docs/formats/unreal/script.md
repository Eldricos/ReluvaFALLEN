# Unreal Engine 1: compiled UnrealScript

Status: **read** (`src/engines/unreal/script.cpp`), **written out as
text** (`script_text.cpp`, `reluva-tool ue-script`); not run yet.
**Checked on The Fallen** (`reluva-tool ue-check`, October 2026): all
40,603 script objects in its 175 packages read, 11,911 of them with
bytecode; every function's, state's and class's bytecode ends exactly at
its stored size, and nothing is left over after any of them. All 1,442
classes write out as text with every native call named (334 natives).

Sources: public community documentation of UE1's package format and of
UnrealScript's bytecode (the token list and the layouts of the field
classes), checked field by field against The Fallen's data as below.
None of it comes from Epic's source or the clean-room port's code.

## Objects

A class is a tree of UField objects, each an export of its own: classes,
states, functions and structs (UStruct and below), properties, enums and
constants. All but classes start with their (empty) tagged properties, a
single `None`; classes start straight with their fields. Then, in order
of the class hierarchy (each class's fields after its parent's):

| Class | Fields |
|---|---|
| UField | super (compact object ref), next (compact object ref: the next field of the same struct) |
| UConst | its value as source text (a string) |
| UEnum | compact count, then that many compact name indices |
| UProperty | array size (i32), property flags (u32), category (compact name), replication offset (u16, only with the Net flag 0x20) |
| ByteProperty | + its enum (compact ref, 0 for a plain byte) |
| ObjectProperty, StructProperty, ArrayProperty | + the class, struct or inner property (compact ref) |
| ClassProperty | + the class (Class) and the meta-class its values are or descend from (compact refs) |
| UStruct | script text (a TextBuffer ref), children (ref: the first field), friendly name (compact name), line (i32), text position (i32), script size (u32), then the bytecode |
| UFunction | native number (u16, 0 if none), operator precedence (u8), function flags (u32), replication offset (u16, with the Net flag 0x40) |
| UState | probe mask (u64), ignore mask (u64), label table offset (u16), state flags (u32) |
| UClass | class flags (u32), GUID (16 bytes), dependencies (compact count; each a class ref, deep (u32), script text CRC (u32)), package imports (compact count of compact names), within (class ref), config name (compact name), then the class's default properties (tagged, to `None`) |

A struct's fields are the chain from its children through each field's
next; a function's parameters come in the order they were declared, the
return value (flag 0x400) and locals after them. Flags as in
`script.h` (`propflag::`, `funcflag::`). An operator function's name is
its friendly name (`!`, `+=`); its own name says what it does
(`Not_PreBool`).

Checked on The Fallen: `Actor.Role` (17 bytes: none, super 0, next,
array size 1, flags 0x20, category none, replication offset, enum
ENetRole) and `Actor.Physics` (category, replication offset, enum
EPhysics); `Actor.Destroy` (no script, native 279, flags final|native);
`Actor.ZoneChange` (script `04 0b`, `return;`, flags event); the class
`Light` (to its config name, then its defaults). The class defaults found
this way differ from the earlier guess at their start
([actors.md](actors.md#class-defaults)) in 810 of the 1,442 classes,
all where the guess was wrong.

## Bytecode

The script size counts bytes as the engine lays the code out in memory,
where an object reference or a name takes 4 bytes; on disk they're
compact indices of 1-5 bytes. Jump targets and case offsets count the
same way. So the bytecode has to be decoded expression by expression to
find its end, and the decoding is checked by ending exactly at the
script size.

Each expression starts with a token byte:

| Token | Name | Then |
|---|---|---|
| 00, 01, 02 | local, instance, default variable | the property (ref) |
| 04 | return | an expression (`0b` nothing for none) |
| 05 | switch | size (u8), the expression |
| 06 | jump | offset (u16) |
| 07 | jump if not | offset (u16), the condition |
| 08 | stop | |
| 09 | assert | line (u16), the condition |
| 0a | case | offset of the next case (u16); unless it's 0xffff (default), the value |
| 0b | nothing | |
| 0c | label table | (name, offset u32) pairs up to the name `None` |
| 0d | goto label | the label name expression |
| 0e | eat string | an expression (its value thrown away) |
| 0f, 14 | let, let bool | destination, value |
| 11 | new | outer, name, flags, class |
| 12, 19 | class context, context | object, skip (u16), size (u8), the member expression |
| 13, 2e | meta cast, dynamic cast | class (ref), expression |
| 16 | end of function parameters | |
| 17 | self | |
| 18 | skip | size (u16), expression (a short-circuit operator's right side) |
| 1a | array element | index, array |
| 1b, 38 | virtual, global function | name, arguments up to `16` |
| 1c | final function | the function (ref), arguments up to `16` |
| 1d, 1e | int, float constant | 4 bytes |
| 1f | string constant | bytes up to a 0 |
| 20, 21 | object, name constant | ref, name |
| 22, 23 | rotator, vector constant | 3 × i32, 3 × f32 |
| 24, 2c | byte constant, int constant as a byte | u8 |
| 25, 26, 27, 28 | 0, 1, true, false | |
| 29 | native parameter | the property (ref) |
| 2a | None | |
| 2d | bool variable | the variable expression |
| 2f | iterator | the iterator call, end offset (u16) |
| 30, 31 | iterator pop, next | |
| 32, 33 | struct ==, != | the struct (ref), two expressions |
| 34 | unicode string constant | u16s up to a 0 |
| 36 | struct member | the member (ref), the struct expression |
| 39-59 | conversions | the expression |
| 60-6f | native function (token - 0x60) × 256 + the next byte | arguments up to `16` |
| 70-ff | native function `token` | arguments up to `16` |

Tokens 03, 10 (dynamic array element), 15, 35 and 37 aren't used in The
Fallen.

The conversions, each to a type: 39 rotator → vector; 3a-3c byte → int,
bool, float; 3d-3f int → byte, bool, float; 40-42 bool → byte, int,
float; 43-45 float → byte, int, bool; 46-47 object → int, bool; 48
name → bool; 49-4e string → byte, int, bool, float, vector, rotator;
4f-50 vector → bool, rotator; 51 rotator → bool; 52-59 byte, int, bool,
float, object, name, vector, rotator → string. Checked against what The
Fallen applies them to: for each conversion of a variable, the
variable's type, and for each assignment of a conversion, the type
assigned to. Every one fits (3a on bytes giving ints, 3f on ints giving
floats, 4a on strings giving ints, 52-58 giving strings from bytes,
ints, floats, objects, names and vectors, 39 and 50 between structs).

## As text

`reluva-tool ue-script <game folder> <name>` writes a class (its
constants, enums, structs, variables, functions, states and default
properties), a struct, a function (`Class.Function`) or a state as text
close to the source; `-all <dir>` writes every class to
`<dir>/<package>/<class>.uc`. Statements keep their code offsets, and
jumps are shown as jumps (`if (!(cond)) jump 0x0058`) rather than
rebuilt into `if`/`while`. Native calls are named from the functions
that declare them (`NativeTable`), operators written as operators.

What it has shown so far: `CTV_TriggerLight.PostBeginPlay` sets
`DrawType = 0`, so the light hides its editor icon when the game
starts, and a trigger light with `bInitiallyOn` off sets its
`LightBrightness` to 0, so it starts dark (the Maps page lights it, as
it doesn't run scripts).
