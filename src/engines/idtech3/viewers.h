// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The id Tech 3 engine's data viewer: pages showing what a game's files
// hold, while the renderer and game code are written. Tab switches pages,
// Esc returns to the launcher.
//
// World:    the map drawn by the GPU renderer. Left/Right pick a map; drag
//           to look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift
//           to go faster, R back to the start point, I to show or hide
//           the info.
// Models:   every .md3 and .mdr; players (lower, upper and head in one folder) put
//           together. Left/Right step, PageUp/PageDown jump 10; drag to
//           turn, wheel or +/- to zoom, A to animate, ,/. to step frames,
//           [ and ] to step through a player's animations (from its
//           animation.cfg, legs and torso paired as the game plays them),
//           T to place a player's torso on its legs' tag or as in its
//           file, R to reset the view, I to show or hide the info.
// Files:    Up/Down, PageUp/PageDown, Home/End scroll the search path's files.
// Textures: Left/Right step, PageUp/PageDown jump 10, Home/End.
// Menu:     the game's own menus (Elite Force: vm/ui.qvm) running; keys,
//           typing and the mouse go to them (their own pointer follows
//           the mouse, the system's is hidden), Ctrl+Tab and Ctrl+Esc
//           leave, Ctrl+R starts them again.
// Play:     a Holomatch level run by the game's own code (Elite Force:
//           vm/qagame.qvm and vm/cgame.qvm) with you as the player:
//           keys do what they're bound to (the game's default.cfg, then
//           the menus' Controls, kept in reluva.cfg in the save folder),
//           the mouse looks (it's held by the window); Ctrl+PageUp and
//           Ctrl+PageDown pick a map, Ctrl+I shows the info, Ctrl+R
//           restarts, Esc goes back to the menus (Ctrl+Esc to the
//           launcher). ~ opens the console (lines starting / are
//           commands, others are said); messagemode (T) types a chat
//           line. Starting
//           a level from the menus (Engage) comes here.
// Maps:     Left/Right pick a map; drag (or arrow keys with Shift) to rotate,
//           wheel or +/- to zoom, W/A/S/D/Q/E to move, R to reset the view,
//           P to show or hide patches, I to show or hide the info.

#include "engines/idtech3/bindings.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/scene_drawer.h"
#include "engines/idtech3/shader_cache.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/world_renderer.h"

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/image.h"
#include "common/system.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <filesystem>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3 {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const ShaderLibrary& shaders;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
    std::filesystem::path savePath;           // where this game's own files go (GameTarget::savePath)
    audio::Mixer* mixer = nullptr;            // sound output; none if the system has no audio
    // The console variables, one set for every page that runs game code,
    // as the original has one: what's chosen in the menus applies to the
    // level started from them.
    CvarStore cvars;
    KeyBindings bindings;
    // A page asking for another to be shown (by name), as when the menus
    // start a level on the Play page; `requestMap` is the level.
    std::string requestPage, requestMap;
    // Commands for the level's server after it starts ("addbot munro 3"...).
    std::vector<std::string> requestCommands;
};

// The settings: the game's default.cfg, then the player's own (Reluva's
// config file in the save folder), and writing that file back with the
// bindings and the settings to keep (Archive cvars), if any changed.
void loadConfig(ViewerContext& ctx);
void saveConfig(ViewerContext& ctx);
// The brightness setting (r_gamma, the menus' brightness slider; 1 unless
// set), which 3D pages show their frames with.
float brightness(const ViewerContext& ctx);

// The video settings the original's Video and Graphics menus set (Quake
// III's cvars), as Reluva applies them; a setting not made counts as the
// original's default, but the size, which is Reluva's 960x720 until a
// mode is chosen.
struct VideoSettings {
    int width = 960, height = 720; // r_mode (r_customwidth/height for -1)
    bool fullscreen = false;       // r_fullscreen
    int picmip = 0;                // r_picmip: textures halved this many times (texture detail)
    // r_texturemode: GL_{NEAREST,LINEAR}[_MIPMAP_{NEAREST,LINEAR}]
    bool nearest = false, mipmaps = true, mipNearest = true;
    bool anisotropic = false;  // r_ext_texture_filter_anisotropic
    bool vertexLight = false;  // r_vertexLight: the level lit by its vertices, not its lightmaps
    float subdivisions = 4;    // r_subdivisions: how far curves may stray from round, in units (geometric detail)
    int lodBias = 0;           // r_lodBias: models' lower details used (geometric detail)
    bool fastSky = false;      // r_fastsky: the sky left plain
};
VideoSettings videoSettings(const CvarStore& cvars);
// Gives the caches the settings that change how things load (call before
// loading); a null cache is skipped.
void applyVideoSettings(const VideoSettings& v, ShaderCache& shaders, WorldRenderer* world, ModelCache* models);

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0;   // pages with nothing to show are skipped
    virtual void activate() = 0;        // redraw and retitle after switching here
    virtual void deactivate() {}        // before switching away
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    // Pages that draw with System::init3D; the engine puts the 2D
    // framebuffer back when leaving one.
    virtual bool uses3D() const { return false; }
    // Whether Esc and Tab (which otherwise leave and switch pages) go to
    // the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();

    struct Row {
        std::string name;
        std::string source; // the archive's file name
    };
    std::vector<Row> files_;
    int top_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return names_.size(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;

private:
    void show(int index);

    std::vector<std::string> names_;
    int index_ = 0;
};

class WorldViewer : public Page {
public:
    explicit WorldViewer(ViewerContext& ctx);
    ~WorldViewer() override;
    const char* name() const override { return "World"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    void load(int index);
    void toStart();
    void drawOverlay(uint64_t nowMs);

    std::vector<std::string> names_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr; // valid while this page is shown
    std::unique_ptr<ShaderCache> shaders_;  // the next three draw through it
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<ModelCache> models_;
    LightGrid grid_;
    // Models the map's entities name, lit by the light grid where they stand.
    struct Placed {
        int model = -1;
        gfx::Mat4 transform{};
        PointLight light;
    };
    std::vector<Placed> placed_;
    Surface overlay_;
    std::string error_;
    std::vector<std::string> info_;
    std::array<float, 3> start_{};
    float startYaw_ = 0;

    // Fly camera, in the map's units (Z up); yaw 0 looks along +X.
    std::array<float, 3> pos_{};
    float yaw_ = 0, pitch_ = 0;
    bool held_[8] = {}; // forward, back, left, right, up, down, fast, (unused)
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    bool showInfo_ = true;
    uint64_t lastMs_ = 0;
    uint64_t loadedMs_ = 0; // shader animations run from when the map was loaded
    uint64_t fpsStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
    // Per second: average milliseconds per frame spent issuing draws, and
    // presenting (which includes waiting for vsync).
    double drawSum_ = 0, presentSum_ = 0;
    float drawMs_ = 0, presentMs_ = 0;
    std::string device_;
};

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return entries_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    struct Entry {
        std::string name; // the .md3, or the player's folder
        bool player = false;
    };
    void show(int index);
    void resetView();

    std::vector<Entry> entries_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_;
    Surface overlay_;
    std::vector<std::string> info_;
    std::string error_;
    int model_ = -1;                     // a single model
    int lower_ = -1, upper_ = -1, head_ = -1; // a player's parts
    const Skin* skins_[3] = {};
    std::array<float, 3> center_{};
    float radius_ = 32;
    float yaw_ = 0, pitch_ = 0, distance_ = 100;
    bool dragging_ = false, animate_ = false, showInfo_ = true;
    bool torsoOnTag_ = true; // a player's torso placed on the legs' tag_torso (T toggles)
    int lastX_ = 0, lastY_ = 0;
    int frame_ = 0; // when not animating
    uint64_t startMs_ = 0, lastMs_ = 0;
    // A player's animations (animation.cfg) and the pair playing.
    std::vector<PlayerAnimation> anims_;
    int legsAnim_ = 0, torsoAnim_ = 0;
    float animTime_ = 0; // seconds into them
    void nextAnimation(int step);
};

class MenuViewer : public Page {
public:
    explicit MenuViewer(ViewerContext& ctx);
    ~MenuViewer() override;
    const char* name() const override { return "Menu"; }
    size_t count() const override { return available_ ? 1 : 0; }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }
    bool takesKey(const Event& ev) const override;

private:
    void start();
    void stop(const Error& err);
    void pinCursor();
    void syncCursor();
    void findCursor();
    void playSound(const std::string& name, int channel);
    void checkCommands();

    bool available_ = false;
    gfx::Renderer3D* renderer_ = nullptr;
    int frameW_ = 960, frameH_ = 720; // r_mode
    bool restart_ = false;            // the menus ran vid_restart
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_;   // the models the menus show
    std::unique_ptr<SceneDrawer> scene_;   // their pictures and scenes
    CvarStore& cvars_ = ctx_.cvars;
    std::unique_ptr<eliteforce::UiHost> ui_; // kept while other pages are shown
    size_t commandsSeen_ = 0;                // of ui_->commands()
    std::string lastCommand_;                // shown for a while, to see what buttons do
    uint64_t lastCommandMs_ = 0;
    SoundLibrary sounds_{ctx_.files};
    std::map<int, audio::SoundHandle> channels_; // the sound on each of the menus' channels
    Surface overlay_;
    std::string error_;
    std::string reportPath_; // where the details of the last stop were written
    uint64_t startMs_ = 0, shownMs_ = 0;
    // The mouse, and the menus' own pointer as last drawn (both in frame
    // pixels); the pointer's picture is drawn offset from its point.
    bool haveMouse_ = false, haveCursor_ = false, calibrate_ = false;
    int mouseX_ = 0, mouseY_ = 0;
    float cursorX_ = 0, cursorY_ = 0;
    float cursorOffsetX_ = 0, cursorOffsetY_ = 0;
    float measuredX_ = 0, measuredY_ = 0; // where its picture last put it
    float pendingX_ = 0, pendingY_ = 0;   // the steps sent since, in pixels expected
    float gainX_ = 1, gainY_ = 1;         // pixels moved per pixel expected
};

// The code points in UTF-8 text (typed characters).
std::vector<uint32_t> decodeUtf8(const char* s);

class PlayViewer : public Page {
public:
    explicit PlayViewer(ViewerContext& ctx);
    ~PlayViewer() override;
    const char* name() const override { return "Play"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }
    bool takesKey(const Event& ev) const override;

private:
    void start(int index);
    void stop(const std::string& why);
    void runGame(uint64_t nowMs);
    void drawLoadingScreen();
    bool loadingFailed_ = false; // the client game couldn't draw its loading screen
    void sendServerCommands();
    void consoleCommand(const std::string& text);
    static bool isButton(const std::string& name);
    void runBinding(const std::string& line, int32_t key, bool down);
    bool held(const char* button) const;
    void stopSounds();
    void drawOverlay(uint64_t nowMs, bool haveView);
    // What the games print: kept for the console, and the client game's
    // (chat, kills, joins) shown at the top for a few seconds, as the
    // original's console does with its last lines.
    void addPrint(const std::string& text, bool notify);
    std::deque<std::pair<uint64_t, std::string>> notify_;
    std::string printPartial_[2]; // a line not yet ended, per game
    std::deque<std::string> consoleLines_;
    // Typing: the console (~) or a chat line (messagemode, T).
    enum class Typing { None, Console, Say, SayTeam, Tell };
    Typing typing_ = Typing::None;
    std::string field_;
    size_t cursor_ = 0;
    int32_t tellClient_ = -1;
    uint32_t swallowChar_ = 0;    // the character of the key that opened the typing
    uint32_t bindingKeyChar_ = 0; // the character of the key whose binding is running
    int consoleScroll_ = 0;    // lines back from the newest
    std::vector<std::string> history_;
    size_t historyPos_ = 0;
    void openTyping(Typing t, int32_t client = -1, uint32_t key = 0);
    void closeTyping();
    void typingEvent(const Event& ev);
    void submitTyping();
    void drawColoured(int x, int y, const std::string& line);

    std::vector<std::string> names_; // maps
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    int frameW_ = 960, frameH_ = 720; // r_mode
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<ModelCache> models_;
    LightGrid grid_;
    std::unique_ptr<SceneDrawer> scene_; // the client game's scenes and pictures
    // What the player's view drew this frame, for the info line.
    struct Shown {
        size_t models = 0, effects = 0, lights = 0;
        int other = 0; // entities not drawn (kinds not done yet)
        std::set<std::string> unloadable;
    };
    Shown shown_;
    struct Timing {
        float server = 0, client = 0, draw = 0; // ms, the last frame's
    };
    Timing timing_;
    struct FrameNote {
        uint64_t ms = 0;
        int dt = 0;
        bool command = false;
        int snapshots = 0;
        int32_t events = 0;
    };
    std::deque<FrameNote> recentFrames_; // the last two seconds'
    SoundLibrary soundLibrary_{ctx_.files};
    std::unique_ptr<SoundSystem> soundSystem_; // while a level runs, with audio output
    CvarStore& cvars_ = ctx_.cvars;
    std::unique_ptr<eliteforce::GameHost> game_;
    std::unique_ptr<eliteforce::CgameHost> cgame_;
    Surface overlay_;
    std::string error_, notice_;
    bool showInfo_ = true;
    uint64_t shownMs_ = 0, lastMs_ = 0;
    int32_t clientTime_ = 0; // the player's time in the level, ms
    // The player's view angles in degrees (pitch counts downwards), and
    // what they hold.
    float yaw_ = 0, pitch_ = 0;
    float mouseX_ = 0, mouseY_ = 0;         // moved since the last frame
    float lastMouseX_ = 0, lastMouseY_ = 0; // the last frame's, for m_filter
    std::map<std::string, std::set<int32_t>> buttons_; // the engine's +buttons ("forward"...) and the keys holding each
    gfx::Vec3 eye_{};
};

class MapViewer : public Page {
public:
    explicit MapViewer(ViewerContext& ctx);
    const char* name() const override { return "Maps"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Edge {
        std::array<float, 3> a, b;
        Color color;
        bool patch;
    };
    // The orbit camera's basis, rebuilt by render().
    struct Camera {
        std::array<float, 3> pos{}, right{}, up{}, forward{};
        float cx = 0, cy = 0, focal = 0;
    };

    void load(int index);
    void resetView();
    void render();
    void drawLine3d(const std::array<float, 3>& a, const std::array<float, 3>& b, Color c);
    void drawLine2d(float x0, float y0, float x1, float y1, Color c);

    std::vector<std::string> names_;
    int index_ = 0;
    Bsp bsp_;
    std::string error_; // why the map couldn't be shown
    std::vector<std::string> info_;
    std::vector<Edge> edges_;
    std::array<float, 3> mins_{}, maxs_{};

    // Orbit camera around `target_`.
    std::array<float, 3> target_{};
    float yaw_ = 0, pitch_ = 0, distance_ = 0;
    bool showPatches_ = true, showInfo_ = true;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    bool dirty_ = true;
    Camera cam_;
};

} // namespace reluva::idtech3
