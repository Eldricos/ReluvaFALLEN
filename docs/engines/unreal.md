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
| Packages | header, name, import and export tables, the older layouts (before versions 64 and 68) as well as later ones; only The Fallen's, versions 71-73, checked on real files ([package.md](../formats/unreal/package.md)); found by name through the `.ini`'s `[Core.System] Paths` (`package_set.cpp`); imports resolved to the package that defines them |
| Properties | tagged properties of every type, state frames ([package.md](../formats/unreal/package.md)) |
| Textures | P8 with palettes (masked colour 0), DXT1, RGB8, RGBA8; all The Fallen's stored textures decode. Procedural ones play: fire (with its lathes), water, wave (lit as bumps), wet, ice, and The Fallen's FLC ones ([texture.md](../formats/unreal/texture.md)); how the fire, water and wave ones look is Reluva's reading of their settings, not yet compared with the original running |
| Sounds | the embedded WAV, played through `common/audio`; The Fallen's music streamed from `mp3/` in place of its placeholder Sounds ([sound.md](../formats/unreal/sound.md)) |
| Music | The Fallen has no music modules: its music is the MP3s (above), which play. Other UE1 games' modules (`.umx`) are extracted (`ue-extract`) but not played: there's no tracker player yet |
| Levels | all 74 of The Fallen's maps read and draw. The level's BSP model: points, vectors, nodes, surfaces, vertices ([model.md](../formats/unreal/model.md)); polygons drawn textured, with masked, translucent and modulated surfaces, and only the zones seen through the zone portals. Lit by lightmaps worked out from the shadow bits and light actors, with zone ambient ([lighting.md](../formats/unreal/lighting.md)): lights' effects change their brightness (flicker, pulse, waver), scripted lights follow their scripts, lit actors light what's near them as they move, and movers are lit where they are. Movers (doors, lifts) from their own brushes, moved by their scripts, and the sky from the level's sky zone ([actors.md](../formats/unreal/actors.md)). Lightmaps, movers and the sky not yet compared with the original running; the patterns light effects draw across surfaces aren't drawn |
| Meshes | all The Fallen's Mesh and LodMesh exports read (455 and 6), with their animations ([mesh.md](../formats/unreal/mesh.md)): vertex animation, and characters posed from their skeletons with their own clips and the shared animation sets; layers on parts of the body, faces (blink, brows, mouth), tweens from the pose showing, and the scripts' bends and IK; attachment points, where held things go, checked against the meshes' own precomputed places (`reluva-tool ue-points`). Lit where they stand ([actors.md](../formats/unreal/actors.md)); a Meshes page |
| Viewer | Maps (3D, fly camera from the PlayerStart; Enter plays the level), Meshes, Textures, Sounds, Packages (tables and properties) |
| UnrealScript | classes, states, functions, properties, enums, constants and their bytecode read (every one of The Fallen's), and written out as text (`ue-script`) ([script.md](../formats/unreal/script.md)). A VM runs them: classes loaded, objects, the interpreter with states, latent code and timers, and every native the game's scripts call, Core's, Engine's and The Fallen's own; every map runs (`ue-run`) ([unreal_vm.md](unreal_vm.md)) |
| Game | started: the level's scripts run live with the player logged in, moving and colliding against the level, steered from the keyboard and mouse and seen from the game's own camera, cut scenes included (the Maps page, Enter; [unreal_vm.md](unreal_vm.md)). Actors' animations play on the scripts' timing, with their notifications. The AI finds its way along the level's paths; the characters' lines play from the dialogue tables ([dialogue.md](../formats/unreal/dialogue.md)), shown as they're said: cut scenes, hails over the communicator (requests for supplies, reports, signing off) and the AI's own lines (spotting the player, losing them, chatter); the documents and the level's objectives and title read from `int/` ([int.md](../formats/unreal/int.md)). The game's own HUD is drawn (its panels, numbers, communicator; [font.md](../formats/unreal/font.md)), and its menus work: the title, credits, main menu and info pad, New Game travelling to the opening, the UWindow options window with the mouse; games saved and loaded (Reluva's own files); sound: effects heard from where they are, ambient sounds, the music (the MP3s) and the dialogue's voices; the effects: particles, sparks, electric arcs, beams, rain and snow, decals. No native a map calls is left unwritten; The Fallen's own physics modes work: the player climbs ladders, hangs from ledges and pulls up onto them, and each kind of effect moves its particles |

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
./build/reluva-tool ue-points "/path/to/DS9TheFallen"     # attachment points against the meshes' precomputed places
./build/reluva-tool ue-extract "/path/to/DS9TheFallen" <package> <export>   # a texture (.tga), sound or music
./build/reluva --hash "/path/to/DS9TheFallen/System/DS9.exe" ".../System/Engine.u"
```

and fixing what `ue-check` reports: the version/licensee pair The Fallen
uses, and any place its layouts differ from stock UE1 (licensees often
added fields).

## Next steps

What's left is mostly checking, a few things whose cause wasn't found,
some data not read yet, and speed:

1. **Compare with the original running.** Nothing has been compared side
   by side with The Fallen running: the lightmaps, movers and the sky on
   its maps; how the fire, water and wave textures look; the effects
   (swarms, lava, water rings, scripted paths); the AI's own lines'
   timing. Several readings are Reluva's own until then (the bone axis
   order and IK natives in [unreal_vm.md](unreal_vm.md), the lathe and
   wave lighting in [texture.md](../formats/unreal/texture.md)).
2. **Rendering and sound.** The patterns light effects draw across
   surfaces (a torch's waver, water's shimmer); sounds muffled by walls.
3. **Animation.** `FireRunRGunTipLoc`: the running-right shot's gun tip
   is 10 to 17 units below the meshes' own precomputed place on every
   character, where the other shots fit within a few units
   ([mesh.md](../formats/unreal/mesh.md)).
4. **Play-through checks.** The supply beam-down hasn't been played end
   to end.
5. **Data not read yet.** The rest of the maps' strings in `int/` (their
   actors' messages and tricorder readings; the dialogue tables,
   documents, objectives and titles are read), the `System/*.int`
   localisation, and `movies/intro.avi` (the copy worked from doesn't
   have it).
6. **Speed.** M03_KiraL2 runs 20 seconds of play in about 11 of CPU;
   the biggest maps to be measured again.
7. **Other UE1 games.** A tracker player for music modules, and the
   layouts other licensees changed, once there's a second game to check
   against.

The game's own gaps, which nothing here can fill: no lip-sync data, and
no dialogue tables for mission 11, mission 12 or the training.
