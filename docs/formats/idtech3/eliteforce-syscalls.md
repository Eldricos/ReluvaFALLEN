# Elite Force: the game modules' system calls

Elite Force's Holomatch code is three QVMs in `pak0.pk3`: `vm/ui.qvm`
(menus), `vm/cgame.qvm` (the client: HUD, effects) and `vm/qagame.qvm` (the
server's game logic). Each reaches the engine through numbered system calls
(see [qvm.md](qvm.md)). The numbers and arguments are Elite Force's own, so
they're worked out here from the game's code, for interoperability:

- `reluva-tool qvm-info` lists the numbers each program's code uses.
- `reluva-tool qvm-run` runs a program, answering every call with 0, and
  logs each call with its arguments and the text they point at.
- Where the evidence fits, the meaning is taken from Quake III Arena's
  calls, documented in the GPL Quake III source (the order of its
  `ui_public.h`, `cg_public.h`, `g_public.h`). The calls are identified
  from that order and the logs below, not from Raven's SDK, which Reluva
  doesn't use (see [the engine page](../../engines/idtech3.md)).

"Seen" means the call appeared in a log with arguments that fit; "order"
means it's assumed from Quake III's order (the seen calls fit that
order); anything else is listed as unknown.

All logs below are from the English Windows release (`pak0.pk3` MD5
`f31871f2891b1e9d6e8d1654ea93352d`).

## Menus (`vm/ui.qvm`)

113,286 instructions in 754 functions; it uses calls 0–58 and 100–106.
`vmMain(0)` returns 4, the API version; `vmMain(1)` initialises. The other commands are taken from
Quake III's order: 2 shutdown, 3 key event (key, down), 4 mouse event
(dx, dy), 5 refresh (time), 6 is fullscreen, 7 set active menu (menu),
8 console command, 9 draw the connect screen.

Each frame, while not connected to a game, the engine asks `vmMain(6)`
(is a menu covering the screen?) and, if not, opens the main menu with
`vmMain(7, 1)` before `vmMain(5, time)` draws; Quake III's client does
the same. The menus rely on it: the main menu sends a player without a
checked CD key straight to the CD key screen, without opening itself,
and Accept closes that screen, leaving no menu. Without this step the
screen stays black.

| # | Call | Basis | Evidence (from `qvm-run ... ui.qvm 1`) |
|---|---|---|---|
| 0 | `Error(text)` | seen | `"UI_LoadMenuText : MP_NORMALTEXT.DAT file not found!\n"`: missing menu text is fatal |
| 1 | `Print(text)` | seen | `"0 arenas parsed\n"`, `"^1file not found: scripts/arenas.txt\n"` |
| 2 | `Milliseconds()` | order | |
| 3 | `Cvar_Set(name, value)` | order | 36 call sites |
| 4 | `Cvar_VariableValue(name)` → float | seen | `("fs_restrict")` |
| 5 | `Cvar_VariableStringBuffer(name, buffer, size)` | seen | `("g_language", buffer, 32)`, before choosing a language's text files |
| 6 | `Cvar_SetValue(name, float)` | order | 118 call sites (the settings menus) |
| 7 | `Cvar_Reset(name)` | order | |
| 8 | `Cvar_Create(name, value, flags)` | seen | `("ui_initialsetup", "…", 1)` at the end of init |
| 9 | `Cvar_InfoStringBuffer(flag, buffer, size)` | order | not called by the code |
| 10, 11 | `Argc()`, `Argv(n, buffer, size)` | order | |
| 12 | `Cmd_ExecuteText(when, text)` | order | 41 sites |
| 13 | `FS_FOpenFile(path, &handle, mode)` → length | seen | `("scripts/arenas.txt", &h)`, `("ext_data/fonts.dat", &h)`; a missing file reports "file not found" |
| 14 | `FS_Read(buffer, length, handle)` | seen | after opening `fonts.dat`, into a buffer just cleared by `memset(buffer, 0, 20000)` |
| 15 | `FS_Write(buffer, length, handle)` | order | not called by the code |
| 16 | `FS_FCloseFile(handle)` | seen | after the read |
| 17 | `FS_GetFileList(folder, extension, buffer, size)` → count | seen | `("scripts", ".arena", buffer, 1024)` then "0 arenas parsed"; `("scripts", ".bot", …)` |
| 18, 19 | `R_RegisterModel(name)`, `R_RegisterSkin(name)` | order | |
| 20 | `R_RegisterShaderNoMip(name)` → handle | seen | `"gfx/2d/charsgrid_med"`, `"white"`, `"menu/common/cursor"`, `"gfx/2d/numbers/s_zero.tga"` |
| 21–25 | `R_ClearScene`, `R_AddRefEntityToScene`, `R_AddPolyToScene`, `R_AddLightToScene`, `R_RenderScene` | order | 3D models in menus |
| 26 | `R_SetColor(rgba or 0)` | order | 236 sites, the most of any call |
| 27 | `R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader)` | order | |
| 28 | `UpdateScreen()` | order | |
| 29, 30 | `CM_LerpTag`, `CM_LoadModel` | order | |
| 31 | `S_RegisterSound(name)` → handle | seen | `"sound/interface/button1.wav"` |
| 32 | `S_StartLocalSound(sound, channel)` | seen | `(1, 6)` as a menu closes, after the button sounds were registered; 6 is Quake III's local-sound channel. Reluva plays it at `s_volume` (0.8 unless set), a new sound on a channel other than 0 replacing the one there |
| 33–41 | keys: `KeynumToStringBuf`, `GetBindingBuf`, `SetBinding`, `IsDown`, `GetOverstrikeMode`, `SetOverstrikeMode`, `ClearStates`, `GetCatcher`, `SetCatcher` | order | key numbers and names are Quake III's (the menus' own key events use them); bindings are the engine's, loaded from `default.cfg` |
| 42 | `GetClipboardData(buffer, size)` | order | |
| 43 | `GetGlconfig(&glconfig)` | seen | one pointer, during init, just before the fonts' shaders are registered |
| 44 | `GetClientState(&state)` | order | |
| 45 | `GetConfigString(index, buffer, size)` | order | |
| 46–53 | server list and pings | unknown | Quake III's list has `Cvar_Register` at 50, so Elite Force has four more calls before it, presumably for the server browser |
| 54 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | 65 calls in init: `(…, "ui_ffa_fraglimit", "20", 1)`; the structures are 272 bytes apart, a `vmCvar_t` |
| 55 | `Cvar_Update(&vmCvar)` | order | |
| 56 | `MemoryRemaining()` | order | |
| 57 | `SetCDKey(key)` → whether it's taken | seen | the CD key screen's Accept (`qvm-dis -str cdkey`): called with the typed key alone once the menus' own check passes; nonzero closes the screen and sets `ui_cdkeychecked`, zero plays the error sound |
| 58 | `R_ModelBounds(model, mins[3], maxs[3])` | seen | the main menu, right after `R_RegisterModel("models/weapons2/phaser/phaser_w.md3")`: `(1, p, p + 12)`. There is no call to read the CD key back. Taking 58 for `GetCDKey` wrote the key at address 1, over `vmMain`'s jump table, and the menus crashed |
| 100–106 | `memset`, `memcpy`, `strncpy`, `sin`, `cos`, `atan2`, `sqrt` | seen | `memset(buffer, 0, 20000)`, `strncpy(buffer, "ext_data/mp_normaltext.dat", 63)` |

Structures the engine fills in:

- `glconfig_t` (call 43): renderer, vendor and version strings of 1024
  bytes and a 2048-byte extensions string (Quake III's is 8192), then a
  word of Elite Force's own, then Quake III's 17 words: 5192 bytes. Found
  with `reluva-tool qvm-refs` on the address the menus pass: the strings
  at 0, 1024, 2048 and 3072 are each passed on to be printed; the menus'
  next variable starts at 5192 (5196, read 310 times, fits the white
  shader that filled rectangles use). Right after the call the menus
  compute their scale (`qvm-dis -ref`): the word at 5164 times 1/640, the
  word at 5168 times 1/480, so `vidWidth` is at 5164 and `vidHeight` at
  5168, Quake III's words 10 and 11 counted from 5124. With these, the
  main menu's pictures fill a 640×480 screen as expected. (Writing Quake
  III's 11,332 bytes had left the screen size at 0 and overwritten the
  menus' own variables.) What the extra word at 5120 means is unknown;
  Reluva leaves it 0.
- `uiClientState_t` (call 44), assumed to be Quake III's: connection state
  (1 = disconnected), two more words, three 1024-byte strings.

Files the menus read at init: `ext_data/mp_normaltext.dat`,
`ext_data/mp_buttontext.dat` and `ext_data/mp_itemnames.dat` (names built
from `g_language`), `ext_data/fonts.dat`, `scripts/arenas.txt`,
`scripts/*.arena`, `scripts/*.bot`.

Key numbers (`KeyEvent`) are assumed to be Quake III's: ASCII for
printable keys, Tab 9, Enter 13, Escape 27, Backspace 127, then from 128
its list (arrows 132–135, F1 145, mouse buttons 178–180, wheel 183–184).
Typed text comes as `KeyEvent(character + 1024, 1)`, Quake III's
character flag, separately from the keys themselves; the menus' text
fields (the CD key, names) only take those. `MouseEvent` takes movement in
640×480 units; the menus keep their own pointer on the screen. Reluva
keeps it under the system's mouse by watching where the menus draw it
(the picture with `menu/common/cursor`): a large step up and left first
puts it in the corner, which shows how the picture sits relative to the
point, then each frame it sends the step from there to the mouse.

The CD key screen (Holomatch only). The menus check the key themselves
first: at most 22 characters, exactly 18 of them letters or digits (the
others, say, dashes). Then they ask the engine with call 57. The original
engine checks the key further against rules of its own, for the online
authorisation service, which no longer runs; Reluva applies the menus'
rule again, keeps the key in the game's folder in its data directory
(`games/<target>/cdkey.txt`, not where the original keeps it) and answers
that it's taken. The menus ask for a key whenever `ui_cdkeychecked` is 0
when the main menu opens, and set it once one is accepted; with a kept key
Reluva sets it after `Init`, as an earlier accepted entry would have.
Answering 57 as if it were `GetCDKey` had overwritten the typed key with
an empty one and declined every key. The menus never read the key back.

Reluva's side: `src/engines/idtech3/eliteforce/ui_host.cpp`, run by
`reluva-tool ui-run` and shown by the Menu page (`menu_viewer.cpp`).

## Game logic (`vm/qagame.qvm`)

149,568 instructions in 856 functions; it uses 0–38, 100–106, 111, and the
bot library's ranges 200–211, 303–318, 401–427 and 500–572. `vmMain(0,
levelTime, randomSeed, restart)` initialises the level (Quake III's
order; it prints the "Game Initialization" banner). 0–38 follow Quake
III's game calls; unlike the menus, 0 prints and 1 is the error.

| # | Call | Basis | Evidence (from `qvm-run ... qagame.qvm 0 0 0 0`) |
|---|---|---|---|
| 0 | `Print(text)` | seen | `"------- Game Initialization -------\n"`, `"gamename: baseEF\n"` |
| 1 | `Error(text)` | seen | `"SpawnEntities: no entities"`, `"G_FindConfigstringIndex: overflow"` |
| 3 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | `("sv_cheats")`, `("gamename", "baseEF")` |
| 5 | `Cvar_Set(name, value)` | seen | `("g_gravity", "800")` |
| 6 | `Cvar_VariableIntegerValue(name)` | seen | `("bot_enable")`, `("com_buildScript")` |
| 7 | `Cvar_VariableStringBuffer(name, buffer, size)` | seen | `("g_language", buffer, 32)`, `("session", buffer, 1024)` |
| 10 | `FS_FOpenFile(path, &handle, mode)` | seen | `("ext_data/mp_itemnames.dat", &h)` |
| 15 | `LocateGameData(entities, count, entitySize, clients, clientSize)` | seen | `(…, 128, 588, …, 740)`: entities are 588 bytes, clients 740; the count grows as entities spawn |
| 18 | `SetConfigstring(index, text)` | seen | `(12, "baseef-1")`, sounds such as `(544, "sound/player/fry.wav")` |
| 19 | `GetConfigstring(index, buffer, size)` | seen | 510 calls while looking for a free index |
| 37 | `GetEntityToken(buffer, size)` → more? | seen | `(buffer, 1024)`; answering 0 gives "SpawnEntities: no entities" |
| 38 | `FS_GetFileList(folder, extension, buffer, size)` | seen | `("models/players2", …, buffer, 2048)`: Elite Force's player models |
| 23 | `SetBrushModel(ent, name)` | seen | `(ent, *(ent + 324))`: the entity's `model` key, `"*N"` |
| 24 | `Trace(results, start, mins, maxs, end, passEntityNum, contentMask)` | seen | 81 calls on hm_dn1 (items dropping to the floor). The masks are Quake III's values: `0x6000001` (solid, bodies, corpses), `0x2000000` (bodies), `0x2010000` (bodies, player clip), `1` (solid). Afterwards the code reads +4 (`startsolid`), +8 compared with 1.0 (`fraction`), surface flags against 16 (Quake III's `SURF_NOIMPACT`), the entity number against 1023 (none): Quake III's `trace_t` |
| 25–33 | `PointContents`, `InPVS`, `InPVSIgnorePortals`, `AdjustAreaPortalState`, `AreasConnected`, `LinkEntity`, `UnlinkEntity`, `EntitiesInBox`, `EntityContact` | order | `LinkEntity` called 81 times on hm_dn1 |
| 36 | `GetUsercmd(clientNum, usercmd_t*)` | seen | `(clientNum, ent->client + 464)`: `ent+300` is the entity's client, the command at 464 in it. In Quake III the client starts with the player state, then `pers.connected`, then `pers.cmd`, so Elite Force's player state would be 460 bytes. The command is 24 bytes, as Quake III's `usercmd_t`, but laid out differently: server time 0, buttons 4 and weapon 5 (a byte each), angles 8 (pitch, yaw, roll), forward 20, right 21, up 22. The size is confirmed by `qvm-dis -blockcopies`: 24-byte copies six times in qagame, a 460-byte one once |
| 2, 4, 8, 9, 11–14, 16, 17, 20–22, 34, 35 | the rest of Quake III's game calls, in its order | order | |

The entities' first 300 bytes, which the engine reads and writes, are
Quake III's `sharedEntity_t` with a smaller entity state and one field
fewer in the shared part: 204 bytes of `entityState_t` (Quake III's is
208), then `entityShared_t` at 204: `linked` at 204, two of Quake III's
`linkcount`, `svFlags` and `singleClient` (which, not known yet), `bmodel`
at 216, mins at 220, maxs at 232, contents at 244, absmin at 248, absmax
at 260, currentOrigin at 272, currentAngles at 284, ownerNum at 296. The
evidence, from the traces' arguments: the moving box is `ent+220` and
`ent+232`, the entity to ignore `*(ent+296)` or `*ent` (`s.number`); the
game copies 12 bytes to `ent+92` (`s.origin`, as in Quake III) before
`SetBrushModel`, and stores Quake III's `CONTENTS_TRIGGER` at `ent+244`;
it reads `ent+272` after moving things; the client pointer follows at
`ent+300`. The sizes come from `qvm-dis -blockcopies` (structures the
code copies whole): 204 bytes twice in qagame and three times in cgame
(and 408 = 2 x 204), the entity state; 460 bytes once in qagame and six
times in cgame, the player state; 24 bytes, the user command; 56, the
trace. `s.modelindex` is taken to be at Quake III's 160 (the 4 bytes
Elite Force lacks would then come after it), and `s.solid` at Quake
III's 176 (Elite Force lacking only Quake III's last field, `generic1`):
the server writes it on linking as the original does (0xffffff for a
brush model, else a solid box's size, a byte each: half width, depth
below, height above + 32), and the client game draws doors and lifts
as brush models only when it says so. Entities are 588 bytes, clients 740.

### Bots

Bots use Quake III's bot library, in the engine. The game asks for a bot's
client with 34 (`BotAllocateClient`, a free client number or -1), connects
it itself (`ClientConnect` with "is a bot" set) and gives it back with 35.
The library's calls are numbered by range:

- **200–211, the library.** Setting up, settings ("libvars"), loading the
  level's `.aas`, starting a frame, the entities each frame, a bot's
  snapshot entities and console messages, and its command
  (`BotUserCommand`: the engine runs the client's think with it).
- **303–318, the map.** Entity info, the area of a point, routes, BSP
  entity keys, predicting a move.
- **401–427, the elementary actions.** Attack, move, look, and so on.
- **500–572, the AI.** Characters, chat, goals, movement and weapons.

200–211, 303–318 and 500–572 are Quake III's numbers. The game's own
calls confirm them: what each passes, such as travel flags `0x11C0FBE`
(Quake III's default set) to 316, a 13-argument move prediction to 318,
and chat calls next to "game_enter" and "level_start".

The elementary actions are in a different order from Quake III 1.32's,
the bot library's older one, with an alt attack added at the end:

| # | Action | Evidence |
|---|---|---|
| 400 | say | |
| 401 | say to the team | `(client, "what do you say?")`, as Quake III's team orders |
| 402–405 | use an item, drop an item, use an inventory item, drop one | not used |
| 406 | gesture | in the "accompany" goal code |
| 407 | command | `(client, buffer)` after "team %s" |
| 408 | select a weapon | `(client, bs->weaponnum)`, every frame |
| 409 | talk | twice, as Quake III's |
| 410 | attack | with 427 where the bot fires; in the "q3tourney6" map script |
| 411 | use | |
| 412 | respawn | twice, as Quake III's |
| 413, 414 | jump, delayed jump | not used by the game |
| 415 | crouch | in the "accompany" goal code |
| 416–422 | move up, down, forward, back, left, right; move | not used by the game |
| 423 | view | `(client, bs->viewangles)` |
| 424 | end of a regular frame | not used |
| 425 | get the input | `(client, thinktime, bot_input_t*)`, once a bot frame |
| 426 | reset the input | once a bot frame |
| 427 | alt attack | next to every attack |

The actions set flags in the bot's input, which the game turns into its
command (the function after `EA_GetInput`'s caller). Its flags are the
bot library's older values:

| Flag | Action | Becomes |
|---|---|---|
| `0x1` | attack | button 1 |
| `0x2` | use | button 4 |
| `0x4` | respawn | the buttons set to 1 (attack); dropped with attack while attack is held |
| `0x8` | jump, move up | up +127 |
| `0x10` | crouch, move down | up −127 |
| `0x20`, `0x40` | forward, back | forward ±127 |
| `0x80`, `0x100` | left, right | right ∓127 |
| `0x200` | delayed jump | turned into a jump |
| `0x400` | talk | button 2 |
| `0x800` | gesture | button 8 |
| `0x1000` | walk | button 16 |
| `0x2000` | alt attack | button 32 |

The weapon goes into the command's weapon byte. The move direction and
speed (0 to 400) become forward and right, scaled to 127 at 400, in the
view's frame, and the view angles are turned into the command's angles
less the player's delta angles.

The bot frame, `vmMain(10, time)`, runs before each server frame, as on
the original server.

Reluva's side: `src/engines/idtech3/eliteforce/game_bots.cpp` and
`src/engines/idtech3/botlib/`. In place so far: bots joining, the library's
setup and entities, the `.aas` map (areas, routes, travel times), the BSP's
entity keys, characters, the elementary actions, goals (the level's items,
chosen by the bots' item weights), weapons (chosen by weapon weights, and
their descriptions), movement along routes, and chat (their chat files,
replies, match templates and synonyms). Move prediction (318) is
in too. It writes Quake III's 84-byte `aas_clientmove_t` (end point and
area, velocity, the last trace, presence, the event it stopped at,
contents, time, frames). The chat calls are Quake III's with two differences. Setting a
chat state's name (524) takes no client number. Entering a chat (516)
is given the bot's own client, `(chatstate, bs->client, sendto)`, and
the bot then says it with `say` (sendto 0) or `say_team` (1). Structures:
a console message is 276 bytes (handle, time, type, 256 of text, two
pointers); a match is 328 (256 of text, type, subtype, then 8 variables
of an offset byte and a length). `reluva-tool game-run ... -addbot "name skill"` adds bots and lists
the calls they make. `reluva-tool qvm-dis ... -syscalls 200 599`
summarises each of the game's calls in a range, and `-func N` lists the
function holding instruction N with the functions it calls.

Reluva's side: `src/engines/idtech3/eliteforce/game_host.cpp`, run by
`reluva-tool game-run <game folder> <map>`. Starting a level follows the
original server: it sets `mapname` and the server cvars, calls
`vmMain(0, 0, seed, 0)`, during which the game reads the BSP's entity
text with call 37 (each call gives one token, as Quake III's `COM_Parse`
reads them: a quoted string or a run of non-blank characters, comments
skipped; it answers 0 at the end), then runs three frames
(`vmMain(8, levelTime)`, 100 ms apart) to let things settle. Implemented
so far: printing and errors, cvars, files, `LocateGameData`,
configstrings, the entity text, the server info string; linking (filling
in `absmin`/`absmax`, a unit larger than the entity's box, as the original
does), `SetBrushModel`, traces against the world and linked entities,
point contents, entities in a box, entity contact and the PVS tests.
Areas and area portals aren't separated yet. A player joins as on the original server: the
userinfo string is stored for `GetUserinfo` (call 20), `vmMain(2,
clientNum, 1, 0)` (connect: first time, not a bot) returns 0 or the
address of the game's reason for refusing, and `vmMain(3, clientNum)`
puts them in the level. What the game sends to clients
(`SendServerCommand`, call 17) is recorded; `game-run` connects client
0 unless given `-noclient`. Each frame, a joined player's command is handed over with
`vmMain(7, clientNum)` (ClientThink, which reads it with call 36) before
the frame runs; `game-run` sends an idle player's empty commands and
prints where the player is after each frame. With the `g_holoIntro` cvar at its default
(1), a new player is held for Holomatch's holodeck intro: the game doesn't
take their commands (its copy at `client + 464` stays empty) and doesn't
announce them; the client game presumably ends the intro. With
`g_holoIntro 0` (`game-run -set g_holoIntro 0`) the game announces
"entered the game" and moves the player by their commands: run forward
(127 in byte 21 of the command, which is the right move), the player moved 8.9 units
in the first 100 ms on hm_dn1: the game's own movement code running on
Reluva's collision. The order of the fields came from playing: with
Quake III's (angles 4, buttons 16), looking up and down fired the
phaser, the pitch landing in the buttons; with the moves at 21-23 and
the weapon at 20, forward strafed right, right moved up and down, up did
nothing (an invalid weapon) and weapons didn't change: the moves are at
20-22. The weapon at 23 still didn't change weapons; it's the byte
after the buttons (5): with the buttons a byte, the button probe below
found nothing from bit 7 (128) up, bits 8 and above being weapon
numbers instead.

The button bits, from `reluva-tool game-run <game> hm_dn1 -buttons`,
which holds each bit for a second with the phaser and lists the player
state words that change (leaving out those that change with nothing
held): 1 and 32 change the same words (44, the weapon time in Quake
III's player state; 92 to 120, 160, 316, 396; 1 also 376), so 1 is
attack and 32 alt attack. 2 and 64 change only 396; 4 changes 12 (Quake
III's pm_flags), 104 and 120; 8 changes 88 and 92 (Quake III's gesture
plays a torso animation); 16 changes 88 and 92 (Quake III's walking).
128 to 2048 change nothing: the buttons are a byte, and 256 up land in the weapon (1, 2, 4, 8: the phaser held, or weapons not owned). The default bindings (`default.cfg`) put
alt attack on ALT. `game-run` also drops a
player's box from every spawn point (raised 9 units, as Quake III's game
spawns players) as a check of the collision model on its own.
| 111 | `ceil` | order | Quake III's game numbers `floor` 110 and `ceil` 111 |

## Client game (`vm/cgame.qvm`)

124,371 instructions in 583 functions; it uses 0–60 and 100–107.
`vmMain(0, serverMessageNum, serverCommandSequence, clientNum)` starts it
(Quake III's order). Its start-up, run with every call answered 0
(`qvm-run ... cgame.qvm 0 0 0 0`), matches Quake III's client game
numbering wherever it can be checked:

| # | Call | Basis | Evidence |
|---|---|---|---|
| 0 | `Print(text)` | seen | `"^1CG_LoadIngameText : mp_ingametext.dat file not found!"`, and it carries on |
| 1 | `Error(text)` | seen | `"Client/Server game mismatch: baseef-1/"` (configstring 12 against its own version), the last call |
| 3 | `Cvar_Register(&vmCvar, name, default, flags)` | seen | `cg_fov` "80", `cg_drawGun`...; 272 bytes apart |
| 6 | `Cvar_VariableStringBuffer(name, buffer, size)` | seen | `g_language`, `sv_running` |
| 10 | `FS_FOpenFile(path, &handle, mode)` | seen | `ext_data/mp_ingametext.dat`, `ext_data/fonts.dat` |
| 15 | `AddCommand(name)` | seen | `+zoom`, `weapnext`, `say`, `kill`... |
| 17 | `UpdateScreen()` | seen | after each loading caption copied to a buffer: "collision map", "sounds", "game media"... |
| 18 | `CM_LoadMap(name)` | seen | `"maps/<mapname>.bsp"` |
| 19 | `CM_NumInlineModels()` | seen | where the brush models would be registered |
| 20 | `CM_InlineModel(index)` → clip handle | order | |
| 22 | `CM_TempBoxModel(mins, maxs)` → clip handle | order | |
| 23 | `CM_PointContents(point, model)` | seen | `(point, 0)` in a frame |
| 24 | `CM_TransformedPointContents(point, model, origin, angles)` | order | |
| 25 | `CM_BoxTrace(results, start, end, mins, maxs, model, brushmask)` | seen | seven arguments in a frame, the model 0 |
| 26 | `CM_TransformedBoxTrace(...as 25, origin, angles)` | order | |
| 27 | `CM_MarkFragments(numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer)` | seen | `(4, ..., 384, ..., 128, ...)`: wall decals |
| 28 | `S_StartSound(origin, entityNum, channel, sound)` | seen | `(0, 253, 0, 14)` |
| 29 | `S_StartLocalSound(sound, channel)` | seen | `(9, 6)` |
| 30 | `S_ClearLoopingSounds()` | seen | at the start of a frame |
| 31 | `S_AddLoopingSound(entityNum, origin, velocity, sound)` | seen | `(0, 839836, 12912, 109)` every frame while firing (`game-run -cgame -fire -trace`): Quake III's later form, with the entity |
| 32 | `S_UpdateEntityPosition(entityNum, origin)` | seen | 247 calls in three frames, one per entity |
| 33 | `S_Respatialize(entityNum, origin, axis, inWater)` | seen | once a frame |
| 34 | `S_RegisterSound(name)` | seen | the countdown voices, footsteps, item sounds |
| 35 | `S_StartBackgroundTrack(intro, loop)` | seen | two strings made from a configstring |
| 36 | `R_LoadWorldMap(name)` | seen | the map name |
| 37 | `R_RegisterModel(name)` | seen | `models/weaphits/explosion.md3`... |
| 38 | `R_RegisterSkin(name)` | order | |
| 39 | `R_RegisterShader(name)` | seen | `powerups/quad`, `white`... |
| 40 | `R_ClearScene()` | seen | after clearing a structure |
| 41 | `R_AddRefEntityToScene(refEntity_t*)` | seen | about 170 a frame on hm_dn1 |
| 42, 43 | `R_AddPolyToScene`, `R_AddLightToScene` | order | |
| 44 | `R_RenderScene(refdef_t*)` | seen | once a frame, `cg.refdef` (the 112 bytes cleared at start-up) |
| 47 | `R_ModelBounds(model, mins, maxs)` | seen | 7 calls after registering models (on hm_dn1); brush models (`*N`) too in Quake III's client game |
| 48 | `R_LerpTag(orientation_t*, model, startFrame, endFrame, frac, tagName)` → found | seen | `(..., 62, 0, 174, 1.0, ...)` |
| 49 | `GetGlconfig(&glconfig)` | seen | into the field after the game state |
| 50 | `GetGameState(&gameState)` | seen | into the start of the client game's static block |
| 51 | `GetCurrentSnapshotNumber(&number, &serverTime)` | seen | the first call of a frame; unanswered, it read 25376 as the number ("CG_ReadNextSnapshot: way out of range") |
| 52 | `GetSnapshot(number, snapshot_t*)` → whether it's there | seen | into `cg.activeSnapshots[0]` (733228, 12 bytes after the latest snapshot time 51 writes, as in Quake III's `cg_t`) |
| 53 | `GetServerCommand(sequence)` → whether it's there; its words become `Argc`/`Argv` | order | |
| 54 | `GetCurrentCmdNumber()` | seen | before 55 |
| 55 | `GetUserCmd(number, usercmd_t*)` → whether it's kept | seen | 201 calls in three frames, from (current - 63): 64 commands kept, as in Quake III |
| 56 | `SetUserCmdValue(weapon, sensitivityScale)` | seen | `(1, ...)` once a frame |
| 57 | `R_RegisterShaderNoMip(name)` | seen | crosshairs, HUD pieces, fonts |
| 58 | `MemoryRemaining()` | seen | answered 0, the client game prints "Memory is low.  Using deferred model." and skips loading player models |
| 60 | unknown: `("model", value)` | seen | one call site (`qvm-dis -syscall 60`, instruction 42881), after checking the client is the local player; maybe setting the player's `model` setting. Not answered (0) until its second argument is known |
| 100, 102 | `memset`, `strncpy` | seen | |
| 107 | `floor(x)` | seen | one call site, the result converted to a whole number |

The game state (`gamestate_t`) is Quake III's: the client game clears
195,416 bytes for its static block, the game state goes at its start
(call 50), the screen settings 20,100 bytes on (call 49), which is
exactly Quake III's `gamestate_t`: an offset into the text for each of
1024 configstrings, 16,000 bytes of text (offset 0 an empty string), the
count of text used. The configstring numbers are Quake III's too (sounds
from 288, players from 544, the game version at 12). The screen settings
are the menus' `glconfig_t`.

Reluva's side: `src/engines/idtech3/eliteforce/cgame_host.cpp`, started
by `reluva-tool game-run <game folder> <map> -cgame` with the server
game's configstrings. As in Quake III, configstrings 0 and 1 are the
server's own: the info strings of every server-info cvar (`mapname`,
`g_gametype`...) and every system-info one, set after the game starts and
after each frame. The client game finds the map's name in 0; without it,
it asked to load `maps/.bsp`. 
The snapshot (`snapshot_t`) is Quake III's with Elite Force's player and
entity states: snapFlags 0, ping 4, serverTime 8, the area mask 12 (32
bytes), the player state 44 (460 bytes), numEntities 504, the entity
states 508 (256 of 204 bytes), numServerCommands 52732,
serverCommandSequence 52736: 52740 bytes. The evidence: the client game
stores the address 52740 bytes past its first snapshot buffer (its
second); through its current-snapshot pointer (733220) it reads 0, 8, 12,
44 (copied whole), fields up to 340, 504 and 508; through its
next-snapshot pointer (733224) 52736. The server copies the player state
and entity states byte for byte from the game (the first 460 bytes of
the player's client, the first 204 of each entity), so their insides
needn't be known. It sends every linked entity but the player's own and
those flagged not to be sent (SVF_NOCLIENT, bit 1 of svFlags, which is at
208 or 212: Reluva takes whichever word has flags in the running game);
no visibility culling yet.

The view (`refdef_t`, call 44) is Quake III's first 112 bytes, without
its text: x, y, width, height 0-12, fov x 16, fov y 20, the eye 24, the
axes 36 (forward, left, up), time 72, flags 76 (1: no world, for the
HUD's heads), the area mask 80. The evidence: on hm_dn1 the player's
view read 0,0 640x480, fov 80 x 64.4, the eye 26 units above the
player's origin, forward (0, -1, 0).

A scene entity (`refEntity_t`, call 41) is 160 bytes (the size of the
memset that clears each): Quake III's 140, field for field, then 20 more
that were 0 in every entity seen. Type 0 (0: a model), render flags 4,
model 8, lighting origin 12, shadow plane 24, axes 28 (forward, left,
up), non-normalized axes 64, origin 68, frame 80, old origin 84, old
frame 96, back-lerp 100, skin number 104, custom skin 108, custom shader
112, RGBA 116, texture coordinates 120, shader time 128, radius 132,
rotation 136. The evidence, from `reluva-tool game-run ... -cgame
-entities 8` on hm_dn1: the player's legs, torso and head have their
model handles at 8 (`lower.mdr`, `upper.mdr`, `head.md3`) and their skin
handles at 108 (`lower_default.skin`...), the player's origin at 12 and
68 and 84, the floor's height at 24, an identity or turned axis at 28,
frames at 80 (174, 121); the first-person weapon (`phaser.md3`) has
render flags 12 (Quake III's first person 4 and depth hack 8), a
turned, slightly bobbing axis, an origin just in front of and below the
eye, and white (-1) at 116; the player's own body has flag 2 (Quake
III's third person: drawn only in mirrors), as Quake III's client game
does. Elite Force's player models are named `.mdr`; Reluva draws `.md3`
models only so far.

The player state differs from Quake III's from 48: gravity (800) and
speed (250) are at 56 and 60, after two words of Elite Force's own. The
word at 48 is a timer the server game sets to 200 when the player fires
and counts down without stopping at 0 (-1100 at 1.3 s into a level with
no firing); 52 stayed 0. The client game's phaser beam function (at
instruction 69445 in the retail cgame.qvm: it traces 2048 units from the
muzzle, then calls one of two beam functions, each called from nowhere
else) returns without drawing for the local player unless the snapshot's
word 48 is 0, so the original can't be sending it: its network protocol
sends only the fields in its table, the rest reaching the client game as
0. Reluva's snapshots zero it (`GameHost::kPlayerStateServerOnly`).
Which other fields the protocol leaves out isn't known yet.

Effects are scene entities of other types, seen with `reluva-tool
game-run <game> hm_dn1 -cgame -cgameframes 30 -fire` (and `-altfire`)
holding the phaser's fire against a wall (each field read off the
entities the client game added):

| Type | Seen as | Fields |
|---|---|---|
| 1 | alt fire's glow where it hits (`gfx/misc/sunny_flare`), projectiles in flight (the scavenger rifle's alt fire: some 300 in two seconds, no model) | a sprite facing the camera: its axes are left as cleared (each along x, y, z), so it can't lie in them; origin 68, shader 112, colour 116 (bytes r, g, b, a), rotation in degrees 132, radius 136. (First taken for a quad in its axes, which drew these edge-on to a player looking along y: invisible.) |
| 2 | sparks where the beam hits (`gfx/misc/sunny_flare`) | a quad in the entity's own axes: the first faces out of the wall hit (+y on a wall facing +y), the other two turned by the rotation at 132 (350 with sine 0.1736 in them); origin 68, shader 112, colour 116, radius 136 |
| 9 | the phaser's beam (`gfx/misc/phaser`), muzzle to wall | a line from the origin (68) to the old origin (84); width 132, 140 how often the texture repeats along it (5) |
| 11 | alt fire's beam (`gfx/effects/whitelaser`) | a line tapering from the width at 132 (1.3) to the one at 136 (13.2); 140 as type 9 |
| 12 | the I-MOD's beam (`gfx/misc/IMOD2`) | a cubic Bezier curve from the origin (68) through control points at 136 and 148 to the old origin (84); width 132 (4) |
| 13 | rings and blasts (`gfx/effects/prifle_altblast`, `gfx/misc/red_ring2`) | taken to be a ring or cone round the first axis (28): radii 132 and 136 (both growing as the colour fades: 20 and 14, then 160 and 107), height 140 (1, 6), 144 taken as the texture's repeats round it; 148 (15, 1.5) and 152 (1) not known |
| 8 | a mirror's or portal's camera (Quake III's portal surface entity, its type one on) | origin 68 and old origin 84 (the same for a mirror: seen as 2392 -936 52 twice), axes 28 (as the game set them), frame 80, old frame 96, skin number 104 (how a portal's camera turns) |
| 14 | the Dreadnought's electricity (`gfx/misc/dnBolt`) | taken to be a jagged line from the origin to the old origin: width 132 (0.5 to 15), jitter 136 (0.1 to 0.5), texture scale 140 (0.2) |

These came from `game-run ... -cgame -give -fire -weapon all` (and
`-altfire`), which gives the player every weapon (the game's `give all`,
with `sv_cheats` on) and fires each for two seconds. Dynamic lights
(call 43: a pointer to the origin, the intensity, which is how far the
light reaches, then red, green, blue from 0 to 1) come with most weapons
(radius 42 to 219 seen). The Play page draws them as Quake III does: on
the level's solid surfaces each pixel within reach is brightened by the
light's colour times its own (the light fading linearly to nothing at
the radius), and models within reach add 16 * radius^2 / distance^2
times the colour to their directed light, from the light's direction. Call 107 is `floor`: its one call site
(instruction 33934) passes one value and turns the float it returns into
a whole number (`CVFI`), as Quake III's client game numbers it; answered
with 0 before, effects that round a time or a count went wrong.

The Play page draws these as quads and ribbons with their shaders, the colour
at 116 in their vertices and for rgbGen and alphaGen entity; a line's
texture runs across it in s and along it in t (along it in s, the
phaser's beam showed stripes). Models with a custom shader (112) are
drawn with it in place of their own, in the entity's colour. Shader
animations start at the entity's shader time, taken to be Quake III's
128 (seconds of the level) when it holds a time that has come. Neither polygons nor
dynamic lights were added while firing the phaser.

Snapshots carry the area mask at 12 (32 bytes, a bit set for each area
not joined to the player's, from the eye taken 26 units above the
origin), as the original's server sends it; the client game copies it
into the view (refdef_t from 80, the end of the 112 bytes it clears),
and the renderer leaves out the leaves in those areas.

The Play page runs the server's frames first, up to a frame ahead of
the player's time, then gives the server the player's command stamped
with that time, which the client game then predicts from the snapshots:
as the original's client stamps commands with the snapshots' time, a
command never ahead of the server. Given first, up to a frame ahead,
with frames of 50 ms or more the client game stopped producing the
player's events (`game-run -cgame -give -events -weapon 7`: 31 fire
sounds in 3 s at 50 ms per command with the server first, none with the
command first).

Reluva's server runs a frame every 50 ms (Quake III's default `sv_fps
20`; it ran every 100 ms before, which made events and anything the
client game doesn't smooth itself move in coarse steps). Models are
drawn between their frame (80) and old frame (96), blended by the
back-lerp (100) on the GPU, as the client game asks.

Marks (scorches, holes) use call 27, Quake III's mark fragments:
`(numPoints, points, projection, maxPoints, pointBuffer, maxFragments,
fragmentBuffer)` -> how many fragments, each 8 bytes (first point, number
of points) into the points written (12 bytes each). Seen: 4 points (a
quad round the impact), up to 384 points. The client game then adds each
fragment as a polygon (call 42: shader, vertex count, vertices; taken to
be Quake III's 24-byte polyVert_t: position, texture coordinates,
colour). Reluva's answer (`marks.cpp`) is new code from that behaviour:
the world's surfaces (planar faces, triangle soups and patches,
tessellated as drawn; not those flagged sky, noimpact or nomarks: 0x4,
0x10, 0x20), each triangle facing against the projection clipped to the
polygon's edges swept along the projection and to its reach both ways.

The client game's sound calls are Quake III's, by what they're passed:
28 `S_StartSound(origin or 0, entityNum, channel, sfx)` (seen: an origin,
1022, 0, a sound), 29 `S_StartLocalSound(sfx, channel)`, 30
`S_ClearLoopingSounds()` once a frame, 31 `S_AddLoopingSound(entityNum,
origin, velocity, sfx)` (the phaser's hum: 0, the player's origin, the
zero vector, its sound), 32 `S_UpdateEntityPosition(entityNum, origin)`
(about 80 a frame), 33 `S_Respatialize(entityNum, origin, axes,
inWater)` (the player, the view's origin and axes), 34
`S_RegisterSound(name)`, 35 `S_StartBackgroundTrack(intro, loop)`.
