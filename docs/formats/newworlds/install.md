# Star Trek: New Worlds: install layout

Source: directory listings of a clean install of the English Windows retail
release, and of the same install with an unofficial fan patch applied
(compared file by file, October 2026).

## Identification

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `STNW.exe` | 7,143,424 | `e7bfa98b3f045471789d84089f4b8eca` |
| `glblgame.cat` | 6,883,984 | `9b5c322aef388f8e58e72b81bd9a91ad` |
| `rommisc.cat` | 309,162 | `ed1ff0cd696da786cf19656551647c43` |

`STNW.exe` and `glblgame.cat` are identical in the clean and fan-patched installs.

## Game data

- `*.cat`: archives (see [cat.md](cat.md)). All of these have been read:
  - per side: `fed` (Federation), `kli` (Klingon), `rom` (Romulan), each with
    `actor`, `build`, `gui`, `misc`; and `for` (the native races: Hubrin,
    Metar, Taubat, one Orion unit) with `actor`, `build`, `misc` but no `gui`
  - per planet type: `alnwrld`, `alpwrld`, `deswrld`, `icewrld`, `redwrld`, `vlcwrld`
  - shared: `glblgame`, `glblgui`, `misc`

  24 in all, every one in the install.
- `game.ini`, `level.dat`, `olevel.dat`, `kapla.rse`: the screen size and side numbers, and the mission lists; see [level.md](level.md).
- `Media/`: standard MP3 and WAV files (voice per faction in `FedVO`,
  `KlingVO`, `RomVO`; `Music`; `Misc` sound effects and tutorial voice).
- `SFC2.BIK` plus the movies on the CD: Bink video. A clean install doesn't
  copy the `MOVIES` folder to the hard disk.

## Not needed by Reluva

Miles Sound System (`Mss32.dll`; its plugins: `*.flt` filters, `*.asi`
codecs such as the MP3 decoder for `Media/`, `*.m3d` 3D-sound providers), electronic
registration (`EReg*`, `Reg*`, `ERHttp*`), the defunct Mplayer online service
(`mplaynow*`, `MPLAYNOW/`, `NewWrlds.mpi`), `kapromo.exe` (promo trailer),
`DX7Aeng.exe` (DirectX 7 installer), and `BINKPLAY.EXE`.

## Fan patch differences

Changed: `fedactor.cat`, `foractor.cat`, `kliactor.cat`, `romactor.cat`,
`rombuild.cat`, `alpwrld.cat`, `game.ini`, `level.dat`, `olevel.dat`,
`kapla.rse`, `Readme.txt`, `BINKPLAY.EXE`, `Media/StarTrek.wav`.
Added: `vars.ini`, `screen.png`. The executable is unchanged.

## `Readme.txt`

The retail readme (Windows-1252 text) describes the game's behaviour,
which the data files don't:

- **Difficulty is the race played:** Klingon novice, Federation average,
  Romulan expert. The tutorials are in the Single Player Campaign menu.
- **Hot keys:** Tab full-screen tricorder; Space jump to the last reported
  event; Home the colony hub; V vehicle menu (with a vehicle yard), B
  building menu, C construction queue; Shift+V / Shift+B cycle units /
  structures; F1 full 3D camera, F2 isometric, F3 close-up isometric, Q cycle
  cameras; Z / X record / recall a camera position; A, T, S, P select all
  APCs, tanks, science vehicles, photon artillery; Ctrl+1-5 set a group, 1-5
  select it, Shift+1-5 jump to it; Esc quit to the menu; Pause.
- **Isometric camera:** left click selects, right click cancels, double
  left click (with units selected) opens the Playbook (orders); the pointer
  at the screen edges or the arrow keys scroll; Ctrl+left / right click and
  numpad 8 / 2 zoom; numpad 4 / 6 rotate.
- **Full 3D camera:** "associated" with a unit or structure after a double
  left click (orbits and follows it) or free (right click); double right
  click on the terrain or the tricorder jumps there; arrows move and strafe;
  numpad 4 / 6 rotate, 5 reverse, 8 / 2 zoom, 7 / 1 raise / lower, 9 / 3 or
  PageUp / PageDown pitch.
- **Multiplayer** (DirectPlay over TCP/IP, IPX or modem): 2 or 3 players, up
  to 6 in co-op; won by destroying the opponents' colony hubs, making their
  objectives impossible, or completing all objectives first. The campaign
  levels are the eight joint missions (Into the Arena ... The Lost Tribes);
  the head-to-head levels the six D maps, named as in `kapla.rse` (Lupercal
  Jade ... Pellion Red; see [level.md](level.md)), so that file is the
  shipped mission list's naming.

