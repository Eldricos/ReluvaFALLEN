# Reluva

**R**eimplemented **E**ngines for **L**egacy, **U**nsupported & **V**intage **A**dventures.

A framework for running classic games natively on modern systems by
reimplementing their engines, in the spirit of ScummVM. Games are not
included: you point Reluva at your own installed copy.

## Layout

```
src/common/        platform-independent core: System interface, events,
                   surfaces, config, case-insensitive file lookup, detection,
                   text and a built-in font; archives (folders, .zip/.pk3) and
                   search sets; image decoding (TGA, JPEG, PNG, BMP); the 3D
                   rendering interface (gfx3d.h)
  audio/           sound decoders and the mixer, shared by every engine: WAV (PCM, IMA ADPCM), MP3
third_party/       outside code Reluva builds in, each with its licence (minimp3, stb_image)
src/backends/sdl3/ the only code that touches SDL (window, rendering, input, audio),
                   including 3D on SDL's GPU API and its shaders
src/launcher/      Dear ImGui game list: add / remove / start games
src/engines/       one directory per engine, plus registry.cpp listing them
  testbed/         built-in test "game" that exercises the backend
  newworlds/       Star Trek: New Worlds: readers for its archives and their
                   files; currently runs as a data viewer
  idtech3/         games on the Quake III Arena engine, starting with Star Trek:
                   Voyager - Elite Force: search path, shader scripts, BSP
                   maps; currently runs as a data viewer (docs/engines/idtech3.md)
src/tools/         reluva-tool, utilities for studying game data
tests/             unit tests for common/ and the engines
```

Engines only use `common/` and `engines/engine.h`. They never include SDL.

## Building

Needs CMake 3.24+, Git and a C++20 compiler. SDL3 and Dear ImGui are
downloaded and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/reluva
```

Engines are CMake options (`RELUVA_ENGINE_IDTECH3` on, `RELUVA_ENGINE_NEWWORLDS`
off by default for now); see the build notes.

See **[docs/BUILDING.md](docs/BUILDING.md)** for step-by-step instructions for
Linux (with each distro's packages) and Windows with Visual Studio 2022.

## Usage

- `reluva` opens the launcher. *Add Game...* scans a folder for supported games.
- `reluva <target>` starts a configured game directly. `reluva testbed`
  runs the test pattern.
- `reluva --hash <files...>` prints detection signatures for an engine's table.
- `reluva-tool` has developer tools for studying game data: `hexdump`,
  `cat-probe`, `cat-list` and `cat-extract` (New Worlds archives).
- In game: **Ctrl+F5** returns to the launcher, **Alt+Enter** toggles fullscreen.
- New Worlds currently opens as a data viewer. **Tab** switches between missions, models,
  textures, maps, sounds, strings and fonts, **Left/Right** step through them and **F** cycles filters.
  Missions (level.dat's list) show their map with the briefing over it: **Up/Down** turn its
  pages, **B** hides it, **J** picks the side of a joint mission, **Space** plays the mission's
  voice clip, **N/P** the next or previous one, **M** the planet's music, **Enter** plays the
  briefing step by step (**Enter** again skips a step, **Backspace** stops), **X** steps through
  the mission's script (its events, messages and objectives; **Z** stops), **I** shows the game's
  interface over the map in the side's style (the script's messages and objectives in it), **C**
  shows or hides the colony (its hub where the recordings put it, growing as the script builds)
  and **V** looks at it, and **F3** plays on the map with the game's own camera and controls
  (select and move the colony's starting units, **B** to build, the mission's script running;
  see `docs/formats/newworlds/game.md`); the map's
  keys work too. For
  models, drag with the mouse (or **W/A/S/D**) to rotate, use the wheel (or
  **+/-**) to zoom, **Space** to spin, **P** to show one part at a time, **L**
  for outlines, **U** for texture-coordinate checks, **T** to turn textures off, **G** to change how alpha layers are used (by default glow in `_g` textures, transparency in the others), **H** for smooth or flat shading, **M** for the model's points (from its .mdl), **N** to select the next one and **K** to play its animations (or sweep its turret)
  (glow, ignored, transparency), **C** to
  colour faces by texture number, **B** to show back faces
  and **R** to reset the view. Maps are built from their ground tiles: drag to
  rotate, use the wheel to zoom, **W/A/S/D** to move, **Q/E** to turn,
  **Left/Right** for the next map, **T** for textures, **G** to colour the ground by its terrain codes or show resource deposits, **O** for the placed trees and rocks, **H** for smooth or flat shading, **K** for
  the walls between levels and **L** for outlines. For sounds, **Space** (or
  **Enter**) plays or stops the selected one, **A** plays each as it's selected and **F**
  lists every copy of the sounds the archives store more than once. For strings,
  **Left/Right** picks a table, **Up/Down**, **PageUp/PageDown** and **Home/End**
  scroll, **+/-** change the text size and **F** switches to the game's fonts.
  For fonts, **Left/Right** picks one, **+/-** change the size and **B** outlines
  the glyphs' boxes.

- Elite Force (id Tech 3) also opens as a data viewer, starting on its maps in 3D.
  **Tab** switches between the 3D world, models, wireframe maps, textures and files. In the world,
  **Left/Right** picks a map, drag to look, **W/A/S/D** move, **Space/C** (or **E/Q**) go up
  and down, **Shift** goes faster, **R** returns to the start and **I** shows or hides the
  info; it needs a GPU with Vulkan (Linux, Windows) or Metal (macOS). Models placed by the
  map's entities show too. The models page shows every model, players put together:
  **Left/Right** step, drag to turn, the wheel zooms, **A** animates, **,/.** step frames.
  For wireframe maps,
  **Left/Right** picks one,
  drag (or **Shift+arrows**) rotates, the wheel (or **+/-**) zooms, **W/A/S/D/Q/E** move,
  **P** shows or hides patches, **I** the info and **R** resets the view. For textures,
  **Left/Right** step through them (**PageUp/PageDown** by 10). The files page lists the
  search path and which archive each file comes from.

The config is saved as `reluva.ini` in SDL's preferences folder
(`~/.local/share/reluva/reluva/` on Linux, `%APPDATA%\reluva\reluva\` on Windows).

## Adding an engine

1. Create `src/engines/<name>/` with an `Engine` and a `MetaEngine` (copy `testbed`).
2. Add a detection table built with `reluva --hash`.
3. Register its accessor in `src/engines/registry.cpp` and add the source to
   `CMakeLists.txt`.

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: only clean-room work is accepted.

## License

Reluva is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE).

Reluva doesn't include any game data. Games and their assets remain the
property of their respective owners.
