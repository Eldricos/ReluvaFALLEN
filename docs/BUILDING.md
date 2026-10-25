# Building Reluva

You don't need to download or install SDL3 or Dear ImGui yourself. CMake
fetches the exact versions Reluva uses and builds them along with the project
(the first configure takes a few minutes). You only need a compiler, CMake,
Git, and on Linux the system headers SDL builds against.

## Linux

### 1. Install the tools and SDL's build dependencies

**Ubuntu / Debian / Mint / Pop!_OS** (tested on Ubuntu 24.04):

```sh
sudo apt install build-essential git cmake ninja-build pkg-config \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
  libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
  libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev liburing-dev
```

**Fedora:**

```sh
sudo dnf install gcc-c++ git cmake ninja-build \
  alsa-lib-devel pulseaudio-libs-devel pipewire-devel pipewire-jack-audio-connection-kit-devel \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel \
  libXScrnSaver-devel libxkbcommon-devel libdrm-devel mesa-libgbm-devel \
  mesa-libGL-devel mesa-libGLES-devel mesa-libEGL-devel vulkan-devel \
  wayland-devel wayland-protocols-devel libdecor-devel dbus-devel ibus-devel systemd-devel liburing-devel
```

**Arch / Manjaro / SteamOS:**

```sh
sudo pacman -S base-devel git cmake ninja alsa-lib jack libpulse pipewire sndio \
  libx11 libxcursor libxext libxinerama libxkbcommon libxrandr libxrender libxss libxtst \
  mesa vulkan-headers wayland wayland-protocols libdecor ibus libusb hidapi
```

The Fedora and Arch lists follow SDL's own
[Linux build notes](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies).
Check there if a package name has changed.

### 2. Build and run

```sh
git clone https://github.com/Eldricos/Reluva.git
cd Reluva
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/reluva_tests      # unit tests
./build/reluva            # launcher
./build/reluva testbed    # test pattern: Esc to exit, Space for a tone
```

For debugging, use a separate folder: `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`.

Run everything from the top `Reluva` folder; there's no need to `cd build`.
Linux only runs programs from the current folder when the path is given, hence
the `./build/` prefix. The same goes for the developer tool, for example with
the game installed in `games/newworlds`:

```sh
git pull && cmake --build build                         # update and rebuild
./build/reluva-tool cat-list games/newworlds/rommisc.cat
./build/reluva-tool geo-survey games/newworlds/*.cat
```

At the end of the configure step, CMake prints a summary of SDL's features.
`SDL_X11`, `SDL_WAYLAND` and at least one of `SDL_PIPEWIRE`, `SDL_PULSEAUDIO` or
`SDL_ALSA` should say `ON`. If one says `OFF`, its `-dev` package is missing.
Install it, delete `build/`, and configure again.

**Engines** can be left out of the build with CMake options:
`RELUVA_ENGINE_IDTECH3` (Elite Force, on by default) and
`RELUVA_ENGINE_NEWWORLDS` (Star Trek: New Worlds, **off** by default for now,
because two of its files hold string literals longer than Visual Studio
accepts). To build New Worlds with GCC or Clang:
`cmake -S . -B build -DRELUVA_ENGINE_NEWWORLDS=ON`. In Visual Studio the options
are under *Project > CMake Settings*.

**3D rendering** (the Elite Force World page) uses SDL's GPU API: Vulkan on
Linux and Windows, Metal on macOS. Nothing extra is needed to build it. To run
it, the graphics driver must support Vulkan, as current drivers for AMD, Intel
and NVIDIA do (on Debian/Ubuntu, Mesa's is `mesa-vulkan-drivers`). Without
one, Reluva falls back to SDL's usual renderer: everything else works and the
World page says 3D isn't available. Setting `SDL_RENDER_DRIVER` to another
renderer (e.g. `opengl`) forces that fallback.

The shaders are compiled into `src/backends/sdl3/shaders/compiled.h`, which is
committed. Only after changing `draw.vert` or `draw.frag` run
`src/backends/sdl3/shaders/build.sh`, which needs `glslang-tools` and
`spirv-cross`.

**IDE (optional):** VS Code with the *CMake Tools* and *C/C++* extensions opens
the folder directly. CLion and Qt Creator also open `CMakeLists.txt` directly.

## Windows (Visual Studio 2022)

### 1. Install Visual Studio

1. Run the [Visual Studio 2022 installer](https://visualstudio.microsoft.com/vs/)
   (Community edition is free).
2. Tick the **Desktop development with C++** workload. Its default components
   already include **C++ CMake tools for Windows** and a **Windows SDK**, which
   are the two you need.
3. Under **Individual components**, also tick **Git for Windows**, unless you
   already have Git installed.

### 2. Open the project

1. Start Visual Studio and choose **Clone a repository**.
   Paste `https://github.com/Eldricos/Reluva.git`, pick a folder, and click **Clone**.
   (If you already cloned it, use **File > Open > Folder...** instead.)
2. Visual Studio detects `CMakeLists.txt` and configures the project
   automatically. The first time, it downloads and builds SDL3 and ImGui. Follow
   the progress in the **Output** window (choose "CMake" in its dropdown) and
   wait for *"CMake generation finished."*

### 3. Build and run

1. In the toolbar, the configuration dropdown shows **x64-Debug** by default.
   Choose **x64-Release** when you want a fast build.
2. In the **Select Startup Item** dropdown (next to the green ▶ button), choose
   **reluva.exe**.
3. Press **F5** to build and run under the debugger, or **Ctrl+F5** to run
   without it.

A console window opens next to the game window and shows Reluva's log
messages, which is useful during development.

**Passing command-line arguments** (e.g. to start the testbed directly): in
Solution Explorer, right-click `CMakeLists.txt` → **Add Debug Configuration**
(or **Debug and Launch Settings**) → **reluva.exe**. In the `launch.vs.json`
file that opens, add an `args` line to the `reluva.exe` entry:

```json
"args": [ "testbed" ]
```

**Unit tests:** pick **reluva_tests.exe** as the startup item and run it, or
use **Test > Test Explorer**.

### Command-line alternative

From the **x64 Native Tools Command Prompt for VS 2022** (in the Start menu):

```bat
git clone https://github.com/Eldricos/Reluva.git
cd Reluva
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\reluva.exe
```

With this generator, choose Debug or Release with `--config` when building
(`CMAKE_BUILD_TYPE` is ignored). `cmake --open build` opens the generated `.sln`.

## Troubleshooting

- **The first configure fails while downloading:** CMake fetches SDL and ImGui
  from GitHub, so it needs internet access the first time. Later builds reuse
  the downloaded copies.
- **"Error: generator : Ninja / Does not match the generator used previously"**
  The `build` folder was first configured with a different generator (e.g.
  without `-G Ninja`). A build folder keeps the generator it started with, so
  delete it (`rm -rf build`) and configure again, or use a new folder name.
- **Linux: CMake error "Couldn't find dependency package for ..."** A `-dev`
  package from step 1 is missing. The error names the feature, e.g. `XCURSOR`
  → `libxcursor-dev`.
- **Using a system-installed SDL3 instead:** configure with
  `-DRELUVA_USE_SYSTEM_SDL=ON`. CMake then needs to find SDL3's CMake package
  (e.g. via `CMAKE_PREFIX_PATH`).
- **Strange errors after changing dependencies or compilers:** delete the
  `build` folder (in Visual Studio: **Project > Delete Cache and Reconfigure**)
  and configure again.
