# id Tech 3: virtual machine programs (`vm/*.qvm`)

Status: **interpreter implemented** (`src/engines/idtech3/qvm.cpp`), tested
on hand-built programs and on a C program compiled by the Quake III compiler
(`tests/data/qvm/`), whose every result matches the same program compiled
natively. Elite Force's own QVMs not yet surveyed: `reluva-tool qvm-info`
and `qvm-run` are for that.

Sources: the GPL Quake III Arena source (`qcommon/qfiles.h`, `vm.c`,
`vm_interpreted.c`, `vm_local.h`), read for the format and behaviour, not
copied.

Game code that the engine runs in a sandbox: Elite Force's multiplayer
holds three, `vm/qagame.qvm` (the server game), `vm/cgame.qvm` (the
client: HUD, effects, prediction) and `vm/ui.qvm` (the menus). A QVM
reaches the engine only through numbered system calls, whose numbers and
meanings are each game's own.

## File

All values little-endian.

| Offset | Type | Meaning |
|---|---|---|
| 0 | i32 | magic, `0x12721444` (or `0x12721445`, which adds the next field) |
| 4 | i32 | instruction count |
| 8 | i32 × 2 | code offset and length |
| 16 | i32 × 2 | data offset and length (32-bit words) |
| 24 | i32 | literal length (bytes, e.g. strings), stored after the data |
| 28 | i32 | bss length: zero-filled memory after the literals |
| 32 | i32 | (second version only) jump table target count, unused here |

**Code** is a byte stream: each instruction is an opcode byte followed by
a 4-byte operand (`ENTER LEAVE CONST LOCAL BLOCK_COPY` and the 16
branches), a 1-byte operand (`ARG`) or none. Branch and call targets are
instruction numbers, not byte offsets.

**Memory** is the data, then the literals, then the bss, all rounded up
to a power of two; addresses are offsets into it. The program stack is at
the top and grows down (64 KiB); the compiler includes it in the bss.

## Opcodes

0 `UNDEF`, 1 `IGNORE`, 2 `BREAK`, 3 `ENTER`, 4 `LEAVE`, 5 `CALL`, 6 `PUSH`,
7 `POP`, 8 `CONST`, 9 `LOCAL`, 10 `JUMP`, 11-20 `EQ NE LTI LEI GTI GEI LTU
LEU GTU GEU`, 21-26 `EQF NEF LTF LEF GTF GEF`, 27-29 `LOAD1 LOAD2 LOAD4`,
30-32 `STORE1 STORE2 STORE4`, 33 `ARG`, 34 `BLOCK_COPY`, 35 `SEX8`, 36
`SEX16`, 37 `NEGI`, 38 `ADD`, 39 `SUB`, 40 `DIVI`, 41 `DIVU`, 42 `MODI`,
43 `MODU`, 44 `MULI`, 45 `MULU`, 46 `BAND`, 47 `BOR`, 48 `BXOR`, 49
`BCOM`, 50 `LSH`, 51 `RSHI`, 52 `RSHU`, 53 `NEGF`, 54 `ADDF`, 55 `SUBF`,
56 `DIVF`, 57 `MULF`, 58 `CVIF`, 59 `CVFI`.

Values live on an operand stack; memory is reached through addresses on
it. Binary operations pop b (top) then a and push a op b; branches pop
both and jump to the operand if a op b holds. Loads replace an address
with what's there (zero-extended); stores pop a value then an address.
Floats are their bits in the same 32-bit slots.

## Calls

- `ENTER n` starts a function: the stack pointer drops by n, the frame's
  size. `LEAVE n` raises it again and returns to the address stored at the
  stack pointer; the result is left on the operand stack.
- A frame's lowest word holds the return address of calls it makes; the
  arguments it passes start 8 bytes up (`ARG 8`, `ARG 12`...). A function
  finds its own arguments at its stack pointer + n + 8.
- `CALL` pops a target. Zero or more is an instruction to call. A negative
  target is a **system call** numbered -1 - target: its arguments are the
  words from stack pointer + 8 up, its result pushed on the operand stack.
- The engine enters the program through `vmMain(command, args...)`,
  instruction 0, with -1 as the return address that ends the run. System
  calls may call back into the program (nested `vmMain` calls).

## Reluva's interpreter

Every memory access, jump, call target and stack change is checked; a
fault stops the call with an error naming the instruction (the original
masks addresses to wrap within memory instead). An instruction budget
stops runaway code. Integer arithmetic wraps; division by zero is an
error; out-of-range float-to-int conversions saturate.

The tables `switch` statements jump through are found when a program is
loaded (the compiler's pattern: `CONST 2, LSH, CONST base, ADD, LOAD4,
JUMP`, after the range checks `CONST lo, LTI` and `CONST hi, GTI` on the
index; the compiler folds the first case into the constant, `base =
table - 4 * lo`, so the table's hi - lo + 1 entries start at `base + 4 *
lo`) and kept read-only: the program
never writes them, so a write there, by its code or by a system call, is
a fault, reported where it happens and naming the call. Otherwise a stray
write shows up only later, as a jump somewhere absurd. Hosts write the
program's memory through `checkWrite`, `writeInt` and `writeString`, which
apply the same rule.

When the Menu page's menus stop, it writes the error, the last 64 system
calls with their arguments, and what the menus printed to
`menus-stopped.txt` in the game's folder in Reluva's data directory.

## Tools

- `reluva-tool qvm-info <game folder> vm/ui.qvm [-s]`: sizes, function
  count, and every system call number the code uses (a constant negative
  target just before a `CALL`), with how many call sites; `-s` lists the
  strings in its data.
- `reluva-tool qvm-run <game folder> vm/ui.qvm [command [args...]]`: runs
  `vmMain` answering every system call with 0, logging each call's number
  and arguments, with the text of arguments that point at strings. This is
  how Elite Force's system calls are worked out from its own code
  ([eliteforce-syscalls.md](eliteforce-syscalls.md)).
- `reluva-tool qvm-refs <game folder> vm/ui.qvm <address> <size>`: the
  constants in the code that point into a structure at `address` (as seen
  in a `qvm-run` log), by offset, with the instruction that uses each:
  which fields of the structure the code reads or writes, to check its
  layout.
- `reluva-tool qvm-dis <game folder> vm/ui.qvm <first> [count]`: lists
  instructions, noting the text a constant points at, constants that look
  like floats, and the system call a call makes; with `-ref <address>
  [size]`, the code around every use of an address instead.
- `reluva-tool ui-run <game folder> [-frames N] [-menu M]`: runs Elite
  Force's menus with the engine's real system calls (see the Elite Force
  page) and reports what they print, register and draw, and the calls not
  implemented yet.

## System calls shared by every program

Whatever a game numbers the rest, the calls from 100 up have so far been
the same in each module (Quake III's, and Elite Force's going by its logs):
100 `memset(dest, value, count)`, 101 `memcpy`, 102 `strncpy(dest, src,
count)`, 103 `sin`, 104 `cos`, 105 `atan2(y, x)`, 106 `sqrt`; floats are
passed and returned as their bits. `floor`, `ceil` and the vector helpers
come after these, at numbers that differ between modules.

A `vmCvar_t`, through which the programs see console variables, is four
words (handle, modification count, value as a float, value as an integer)
and the value as a 256-byte string: 272 bytes, which is also the spacing of
the structures Elite Force's menus register in turn.

The test programs in `tests/data/qvm/` are compiled with the Quake III
compiler tools (`q3lcc`, `q3asm`), built from the ioquake3 source; only
their output, QVMs of Reluva's own test programs, is committed. `lcc` is
under its own licence (its `COPYRIGHT` file) and isn't part of Reluva.
