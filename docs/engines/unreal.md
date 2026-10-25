# The Unreal Engine 1 engine

Engine id `unreal`, in `src/engines/unreal/`. First game: Star Trek: Deep
Space Nine - The Fallen (`ds9fallen`, The Collective, 2000).

## One engine for the family

Like `idtech3`, this engine is meant for every game on Epic's first Unreal
engine. What they share (packages, properties, textures, sounds, BSP
levels, and later the UnrealScript virtual machine) is written once;
what a game changes is keyed off its `GameInfo` in `games.cpp`, with
game-specific code in a subfolder (`src/engines/unreal/fallen/`) when
there is some. A folder holding `System/Core.u` and `System/Engine.u`
that isn't a known game is offered as "Unreal Engine 1 game
(unidentified)" (`ue1`), so its data can be looked at too.

## Clean room

The formats are written up in [docs/formats/unreal/](../formats/unreal/)
from public community documentation of the UE1 package format (the notes
written alongside community package tools, and modding wikis), and are
being checked against The Fallen's own files. There is an open-source
clean-room reimplementation of UE1; it may be consulted as a pointer to
where to look, but none of its code is copied or translated, and
findings are confirmed against the game's data with `reluva-tool` before
they go in. Never use Epic's licensee source code (leaked or otherwise).

## Where it stands

| Part | State |
|---|---|
| Detection | The Fallen by file names (`System/DS9.ini` or `System/DS9.exe`, with `Core.u` and `Engine.u`), shown as "unknown release" until hashes from owned copies are added (`reluva --hash`); any other UE1 game as unidentified |
| Packages | header, name, import and export tables, any version 61-69 ([package.md](../formats/unreal/package.md)); found by name through the `.ini`'s `[Core.System] Paths` (`package_set.cpp`); imports resolved to the package that defines them |
| Properties | tagged properties of every type, state frames ([package.md](../formats/unreal/package.md)) |
| Textures | P8 with palettes (masked colour 0), DXT1, RGB8, RGBA8 ([texture.md](../formats/unreal/texture.md)) |
| Sounds | the embedded WAV, played through `common/audio` ([sound.md](../formats/unreal/sound.md)) |
| Music | the embedded module is extracted (`ue-extract`); not played (no tracker player yet) |
| Levels | the level's BSP model: points, vectors, nodes, surfaces, vertices ([model.md](../formats/unreal/model.md)); polygons drawn textured, with masked, translucent and modulated surfaces. Not yet: lightmaps, zones, movers and other brushes, actors' meshes, the sky |
| Viewer | Maps (3D, fly camera from the PlayerStart), Textures, Sounds, Packages (tables and properties) |
| Game | not started: UnrealScript bytecode VM, actors, collision, input, HUD/menus |

**Not yet checked against the retail data.** Everything above is tested
on packages built by `tests/test_unreal.cpp`, not on The Fallen's files.
The first job with the game installed is:

```sh
./build/reluva-tool ue-check "/path/to/DS9TheFallen"      # every package, texture, sound, level
./build/reluva-tool ue-info  "/path/to/DS9TheFallen/System/Engine.u"
./build/reluva-tool ue-props "/path/to/DS9TheFallen/Maps/<map>.unr" <export>
./build/reluva --hash "/path/to/DS9TheFallen/System/DS9.exe" ".../System/Engine.u"
```

and fixing what `ue-check` reports: the version/licensee pair The Fallen
uses, and any place its layouts differ from stock UE1 (licensees often
added fields).

## Next steps

1. **Check against The Fallen's data** (above); add its detection hashes.
2. **Levels, continued**: lightmaps (the model's LightMap and LightBits
   arrays and the level's light actors), zones and the sky zone, brush
   actors (movers) drawn from their own models (Polys), visibility by
   zone portals.
3. **Meshes**: vertex meshes (`Mesh`/`LodMesh`) for actors, and The
   Fallen's character models, whatever format they turn out to be.
4. **UnrealScript**: classes, functions and states from `.u` packages;
   a bytecode interpreter; the native functions the game's script calls
   (Core, Engine, then the game's own). This is the bulk of running the
   game, as it was for the QVMs of Elite Force.
5. **Game loop**: actors ticking, collision against the BSP, the player
   pawn, input bindings from `DS9.ini`/`User.ini`, the HUD and menus
   (UWindow or The Fallen's own), audio by actors, music (a module
   player in `common/audio`).
