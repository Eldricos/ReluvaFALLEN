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
| Detection | The Fallen: one Windows release by `DS9.exe` and `Engine.u`'s hashes; others by file names (`System/DS9.ini` or `System/DS9.exe`, with `Core.u` and `Engine.u`), shown as "unknown release"; any other UE1 game as unidentified |
| Packages | header, name, import and export tables, any version 61-69 ([package.md](../formats/unreal/package.md)); found by name through the `.ini`'s `[Core.System] Paths` (`package_set.cpp`); imports resolved to the package that defines them |
| Properties | tagged properties of every type, state frames ([package.md](../formats/unreal/package.md)) |
| Textures | P8 with palettes (masked colour 0), DXT1, RGB8, RGBA8; all The Fallen's stored textures decode. Procedural ones play: fire, water, wet, ice, and The Fallen's FLC ones ([texture.md](../formats/unreal/texture.md)) |
| Sounds | the embedded WAV, played through `common/audio`; The Fallen's music streamed from `mp3/` in place of its placeholder Sounds ([sound.md](../formats/unreal/sound.md)) |
| Music | the embedded module is extracted (`ue-extract`); not played (no tracker player yet) |
| Levels | all 74 of The Fallen's maps read and draw. The level's BSP model: points, vectors, nodes, surfaces, vertices ([model.md](../formats/unreal/model.md)); polygons drawn textured, with masked, translucent and modulated surfaces. Lit by lightmaps worked out from the shadow bits and light actors, with zone ambient ([lighting.md](../formats/unreal/lighting.md)); not yet checked on The Fallen's maps. Movers (doors, lifts) from their own brushes, moved through their keyframes on request, and the sky from the level's sky zone ([actors.md](../formats/unreal/actors.md)); not yet checked on The Fallen's maps. Lights pulse, flicker and blink; sprites drawn |
| Meshes | Mesh and LodMesh read, with their animations ([mesh.md](../formats/unreal/mesh.md)); actors drawn as meshes placed in the Maps page, with their classes' defaults ([actors.md](../formats/unreal/actors.md)); a Meshes page; not yet checked on The Fallen's |
| Viewer | Maps (3D, fly camera from the PlayerStart; Enter plays the level), Meshes, Textures, Sounds, Packages (tables and properties) |
| UnrealScript | classes, states, functions, properties, enums, constants and their bytecode read (every one of The Fallen's), and written out as text (`ue-script`) ([script.md](../formats/unreal/script.md)). A VM runs them: classes loaded, objects, the interpreter with states, latent code and timers, Core's natives and the Engine ones a level's start needs; every map's start runs (`ue-run`), and the Maps page draws levels as their scripts leave them ([unreal_vm.md](unreal_vm.md)) |
| Game | started: the level's scripts run live with the player logged in, moving and colliding against the level, steered from the keyboard and mouse and seen from the game's own camera, cut scenes included (the Maps page, Enter; [unreal_vm.md](unreal_vm.md)). Actors' animations play on the scripts' timing, with their notifications. The AI finds its way along the level's paths; the characters' lines play from the dialogue tables ([dialogue.md](../formats/unreal/dialogue.md)), shown as they're said; the documents and the level's objectives and title read from `int/` ([int.md](../formats/unreal/int.md)). The game's own HUD is drawn (its panels, numbers, communicator; [font.md](../formats/unreal/font.md)), and its menus work: the title, credits, main menu and info pad, New Game travelling to the opening, the UWindow options window with the mouse. Not yet: sounds, saving and loading |

**First check against the retail data** (October 2026): all 175 packages
open (versions 71-73), all 3801 palettes and 2868 of 2930 sounds decode.
A second run, after fixes, read all 74 maps (`.dsm`; the largest,
M03_KiraL2, 25619 BSP nodes) and they draw in the Maps page; 182
textures are procedural with no stored pixels (now shown as stand-ins,
[texture.md](../formats/unreal/texture.md)) and the 62 sounds that fail
are empty placeholders for the music in the install's `mp3` folder
([sound.md](../formats/unreal/sound.md)). To
check again:

```sh
./build/reluva-tool ue-check "/path/to/DS9TheFallen"      # every package, texture, sound, level
./build/reluva-tool ue-info  "/path/to/DS9TheFallen/System/Engine.u"
./build/reluva-tool ue-props "/path/to/DS9TheFallen/Maps/<map>.unr" <export>
./build/reluva-tool ue-script "/path/to/DS9TheFallen" <class | Class.Function | -all <dir>>
./build/reluva-tool ue-run    "/path/to/DS9TheFallen" <map | -all> [seconds]
./build/reluva-tool ue-play   "/path/to/DS9TheFallen" <map> play wait 20 hold w 2 shot frame.ppm
./build/reluva --hash "/path/to/DS9TheFallen/System/DS9.exe" ".../System/Engine.u"
```

and fixing what `ue-check` reports: the version/licensee pair The Fallen
uses, and any place its layouts differ from stock UE1 (licensees often
added fields).

## Next steps

1. **Check against The Fallen's data**: done for packages, textures,
   sounds and levels' geometry; one release's hashes added. Procedural
   textures' effects (FireTexture's flames, Wet/Wave/Water ripples) to
   be worked out from the game running. Other data the install has, not
   read yet: `movies/intro.avi`, the rest of the maps' strings in `int/`
   (their actors' messages and tricorder readings; the dialogue tables,
   documents, objectives and titles are read), the `System/*.int`
   localisation.
2. **Levels, continued**: check the lightmaps on The Fallen's maps and
   tune them against the original ([lighting.md](../formats/unreal/lighting.md));
   lights' effects; check movers and the sky on The Fallen's maps; movers
   lit as they move; visibility by zone portals.
3. **Meshes**: check them on The Fallen's data; sprites for actors drawn
   as sprites; meshes lit as the original lights them.
4. **UnrealScript**: classes, functions and states from `.u` packages
   (done, [script.md](../formats/unreal/script.md)); a bytecode interpreter
   (running levels' start, [unreal_vm.md](unreal_vm.md)); next the player
   logging in, then the native functions `ue-run` lists as missing
   (Engine's movement and collision, then the game's own). This is the
   bulk of running the game, as it was for the QVMs of Elite Force.
5. **Game loop**: actors ticking, collision against the BSP, the player
   pawn, input bindings from `DS9.ini`/`User.ini`, the HUD and menus
   (UWindow or The Fallen's own), audio by actors, music (a module
   player in `common/audio`).
