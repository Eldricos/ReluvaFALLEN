cmake_minimum_required(VERSION 3.24)
project(reluva VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RELUVA_USE_SYSTEM_SDL "Use an installed SDL3 instead of building it" OFF)
option(RELUVA_BUILD_TESTS "Build unit tests" ON)
# Engines can be left out of the build, like ScummVM's. New Worlds is off
# by default for now: two of its source files hold string literals longer
# than Visual Studio accepts (error C2026).
option(RELUVA_ENGINE_NEWWORLDS "Build the Star Trek: New Worlds engine" OFF)
option(RELUVA_ENGINE_IDTECH3 "Build the id Tech 3 engine (Star Trek: Voyager - Elite Force)" ON)

include(FetchContent)

# --- SDL3 -------------------------------------------------------------------
if(RELUVA_USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED CONFIG)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.14
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- Dear ImGui (no CMake support upstream, so build it here) -----------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- Warnings -----------------------------------------------------------------
add_library(reluva_warnings INTERFACE)
if(MSVC)
    target_compile_options(reluva_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(reluva_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# --- common: platform-independent core, no SDL allowed here -------------------
add_library(reluva_common STATIC
    src/common/config.cpp
    src/common/fs.cpp
    src/common/md5.cpp
    src/common/archive.cpp
    src/common/zip.cpp
    src/common/image.cpp
    src/common/gfx3d.cpp
    src/common/detection.cpp
    src/common/audio/decode.cpp
    src/common/audio/mixer.cpp
    src/common/audio/mp3.cpp
    src/common/audio/wav.cpp
    src/common/font.cpp
    src/common/text.cpp)
target_include_directories(reluva_common PUBLIC src)
# Third-party headers (third_party/, see each README) as system includes, so
# our warnings don't apply to them.
target_include_directories(reluva_common SYSTEM PRIVATE third_party/minimp3 third_party/stb)
target_link_libraries(reluva_common PRIVATE reluva_warnings)

# --- engines: talk only to common/ --------------------------------------------
add_library(reluva_engines STATIC
    src/engines/registry.cpp
    src/engines/testbed/testbed.cpp)
if(RELUVA_ENGINE_NEWWORLDS)
    target_sources(reluva_engines PRIVATE
        src/engines/newworlds/newworlds.cpp
        src/engines/newworlds/cat_archive.cpp
        src/engines/newworlds/string_table.cpp
        src/engines/newworlds/texture.cpp
        src/engines/newworlds/geo.cpp
        src/engines/newworlds/gmf.cpp
        src/engines/newworlds/model_textures.cpp
        src/engines/newworlds/ground_textures.cpp
        src/engines/newworlds/mdl.cpp
        src/engines/newworlds/arf.cpp
        src/engines/newworlds/map_viewer.cpp
        src/engines/newworlds/mission_viewer.cpp
        src/engines/newworlds/briefing.cpp
        src/engines/newworlds/mission_script.cpp
        src/engines/newworlds/mission_scripts.cpp
        src/engines/newworlds/game_ui.cpp
        src/engines/newworlds/colony.cpp
        src/engines/newworlds/model_render.cpp
        src/engines/newworlds/star_system.cpp
        src/engines/newworlds/world.cpp
        src/engines/newworlds/model_pose.cpp
        src/engines/newworlds/economy.cpp
        src/engines/newworlds/model_viewer.cpp
        src/engines/newworlds/texture_viewer.cpp
        src/engines/newworlds/sound_viewer.cpp
        src/engines/newworlds/string_viewer.cpp
        src/engines/newworlds/fif.cpp
        src/engines/newworlds/font_viewer.cpp
        src/engines/newworlds/levels.cpp)
    target_compile_definitions(reluva_engines PUBLIC RELUVA_ENGINE_NEWWORLDS)
endif()
if(RELUVA_ENGINE_IDTECH3)
    target_sources(reluva_engines PRIVATE
        src/engines/idtech3/idtech3.cpp
        src/engines/idtech3/games.cpp
        src/engines/idtech3/filesystem.cpp
        src/engines/idtech3/lexer.cpp
        src/engines/idtech3/marks.cpp
        src/engines/idtech3/mdr.cpp
        src/engines/idtech3/botlib/aas.cpp
        src/engines/idtech3/botlib/bot_chat.cpp
        src/engines/idtech3/botlib/bot_config.cpp
        src/engines/idtech3/botlib/bot_goals.cpp
        src/engines/idtech3/botlib/bot_library.cpp
        src/engines/idtech3/botlib/bot_move.cpp
        src/engines/idtech3/botlib/bot_weapons.cpp
        src/engines/idtech3/botlib/bot_script.cpp
        src/engines/idtech3/botlib/character.cpp
        src/engines/idtech3/player_animation.cpp
        src/engines/idtech3/sound_system.cpp
        src/engines/idtech3/entities.cpp
        src/engines/idtech3/shader.cpp
        src/engines/idtech3/bindings.cpp
        src/engines/idtech3/bsp.cpp
        src/engines/idtech3/viewers.cpp
        src/engines/idtech3/map_viewer.cpp
        src/engines/idtech3/patch.cpp
        src/engines/idtech3/world_renderer.cpp
    src/engines/idtech3/shader_cache.cpp
    src/engines/idtech3/md3.cpp
    src/engines/idtech3/light_grid.cpp
    src/engines/idtech3/model_cache.cpp
    src/engines/idtech3/model_viewer.cpp
    src/engines/idtech3/menu_viewer.cpp
    src/engines/idtech3/play_viewer.cpp
    src/engines/idtech3/scene_drawer.cpp
    src/engines/idtech3/qvm.cpp
    src/engines/idtech3/cvars.cpp
    src/engines/idtech3/sounds.cpp
    src/engines/idtech3/collision.cpp
    src/engines/idtech3/vm_services.cpp
    src/engines/idtech3/eliteforce/ui_host.cpp
    src/engines/idtech3/eliteforce/game_host.cpp
    src/engines/idtech3/eliteforce/game_bots.cpp
    src/engines/idtech3/eliteforce/cgame_host.cpp
    src/engines/idtech3/eliteforce/scene.cpp
        src/engines/idtech3/world_viewer.cpp)
    target_compile_definitions(reluva_engines PUBLIC RELUVA_ENGINE_IDTECH3)
endif()
target_link_libraries(reluva_engines PUBLIC reluva_common PRIVATE reluva_warnings)

# --- executable: SDL3 backend + ImGui launcher --------------------------------
add_executable(reluva
    src/main.cpp
    src/backends/sdl3/sdl_system.cpp
    src/backends/sdl3/sdl_gpu.cpp
    src/launcher/launcher.cpp)
target_link_libraries(reluva PRIVATE reluva_engines imgui SDL3::SDL3 reluva_warnings)

# --- developer tools -----------------------------------------------------------
add_executable(reluva-tool src/tools/reluva_tool.cpp)
target_link_libraries(reluva-tool PRIVATE reluva_engines reluva_warnings)

# --- tests ----------------------------------------------------------------------
if(RELUVA_BUILD_TESTS)
    enable_testing()
    add_executable(reluva_tests tests/test_common.cpp)
    target_link_libraries(reluva_tests PRIVATE reluva_common reluva_warnings)
    target_include_directories(reluva_tests PRIVATE tests)
    add_test(NAME common COMMAND reluva_tests)

    if(RELUVA_ENGINE_NEWWORLDS)
        add_executable(reluva_newworlds_tests tests/test_newworlds_cat.cpp)
        target_link_libraries(reluva_newworlds_tests PRIVATE reluva_engines reluva_warnings)
        add_test(NAME newworlds_cat COMMAND reluva_newworlds_tests)
    endif()

    if(RELUVA_ENGINE_IDTECH3)
        add_executable(reluva_idtech3_tests tests/test_idtech3.cpp)
        target_link_libraries(reluva_idtech3_tests PRIVATE reluva_engines reluva_warnings)
        target_include_directories(reluva_idtech3_tests PRIVATE tests)
        add_test(NAME idtech3 COMMAND reluva_idtech3_tests)
    endif()
endif()
