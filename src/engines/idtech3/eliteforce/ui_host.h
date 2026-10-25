// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/scene.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/vm_services.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's menus: vm/ui.qvm run with the engine side of its system
// calls, in Elite Force's numbering, as worked out in
// docs/formats/idtech3/eliteforce-syscalls.md. Drawing is recorded, not
// done: each frame's pictures are collected for whatever shows them.
class UiHost {
public:
    // vmMain's commands (Quake III's order; GetApiVersion and Init are
    // confirmed on Elite Force's ui.qvm).
    enum Command : int32_t {
        GetApiVersion = 0,
        Init = 1,
        Shutdown = 2,
        KeyEvent = 3,      // (key, down)
        MouseEvent = 4,    // (dx, dy)
        Refresh = 5,       // (time in ms)
        IsFullscreen = 6,
        SetActiveMenu = 7, // (menu: 0 none, 1 main, ...)
        ConsoleCommand = 8,
        DrawConnectScreen = 9,
    };
    static constexpr int32_t kApiVersion = 4; // what Elite Force's ui.qvm reports

    // One picture: a part (s1,t1)-(s2,t2) of a shader stretched over a
    // screen rectangle, tinted by the colour set before it.
    using Picture = eliteforce::Picture;

    UiHost(const SearchSet& files, CvarStore& cvars);

    // Reads vm/ui.qvm and checks its API version.
    Error load();
    // vmMain(command, args...).
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);
    Error call(int32_t command, const std::vector<int32_t>& args = {}) {
        int32_t ignored;
        return call(command, args, ignored);
    }

    // One frame of the menus at `time` (ms), the way the original engine
    // runs them while not connected to a game: if no menu covers the
    // screen (one just closed itself, say), the main menu opens; then
    // they draw (pictures()).
    Error refresh(int32_t time);

    // What GetGlconfig reports, in pixels.
    void setScreenSize(int width, int height) {
        width_ = width;
        height_ = height;
    }
    // Where in glconfig_t Quake III's 17 words (maxTextureSize ...
    // vidWidth, vidHeight ...) start: 5124 in Elite Force, after a word of
    // its own (docs/formats/idtech3/eliteforce-syscalls.md). For trying
    // other layouts.
    void setGlconfigWords(int32_t offset) { glconfigWords_ = offset; }
    // Where the CD key the menus take (call 57) is kept; none: it's only
    // remembered while running.
    void setKeyFile(std::filesystem::path path) { keyFile_ = std::move(path); }
    // The clock Milliseconds reports.
    void setTime(int32_t ms) { time_ = ms; }

    Qvm& vm() { return vm_; }
    const std::vector<Picture>& pictures() const { return pictures_; }
    // The 3D scenes drawn among them (the player model in Player
    // settings...), each over the pictures before it.
    const Scene& scene() const { return scene_; }
    // Forgets the last frame's pictures and scenes, before the next.
    void clearPictures() {
        pictures_.clear();
        scene_ = Scene{};
    }
    const HandleTable& shaders() const { return shaders_; }
    const HandleTable& models() const { return models_; }
    const HandleTable& skins() const { return skins_; }
    const HandleTable& sounds() const { return sounds_; }
    // Text the menus print; by default, kept in log().
    std::function<void(const std::string&)> print;
    const std::string& log() const { return log_; }
    // Commands the menus run (Cmd_ExecuteText), in order.
    const std::vector<std::string>& commands() const { return commands_; }
    // Calls not implemented yet: number -> how many times.
    const std::map<int, int>& unhandled() const { return unhandled_; }
    // The last system calls made (oldest first), with their first
    // arguments and any text they point at, for reports when the menus
    // stop. The CD key is left out.
    std::vector<std::string> recentCalls() const;
    // S_StartLocalSound: a registered sound to play now, not placed in the
    // world, on a channel (a new sound on the same channel, other than 0,
    // replaces the one there). Unset: sounds are only registered.
    std::function<void(const std::string& sound, int channel)> startLocalSound;
    // What keys do, which the Controls menu shows and changes; none if not set.
    KeyBindings* bindings = nullptr;
    // Called by every system call before it's handled, for logging.
    std::function<void(int number, const int32_t* args)> trace;

private:
    int32_t systemCall(Qvm& vm, int number, const int32_t* args);
    const std::string& storedCdKey();
    static bool cdKeyLooksValid(const std::string& key);
    void writeGlconfig(int32_t address);
    void writeClientState(int32_t address);

    const SearchSet& files_;
    CvarStore& cvars_;
    Qvm vm_;
    VmFiles vmFiles_;
    HandleTable shaders_, models_, skins_, sounds_;
    std::vector<Picture> pictures_;
    Scene scene_;
    std::map<int32_t, std::shared_ptr<const Md3Model>> md3s_; // by model handle, for tags
    std::array<float, 4> color_{1, 1, 1, 1};
    std::vector<std::string> commands_;
    std::map<int, int> unhandled_;
    std::string log_;
    int width_ = 640, height_ = 480;
    int32_t time_ = 0;
    int32_t glconfigWords_ = 5124;
    int32_t catcher_ = 0;
    bool overstrike_ = false;
    std::filesystem::path keyFile_;
    std::string cdKey_;
    bool cdKeyRead_ = false;
    std::map<int32_t, std::pair<std::array<float, 3>, std::array<float, 3>>> modelBounds_; // by handle
    bool mainMenuNotFullscreen_ = false; // IsFullscreen seems to mean something else
    struct Recent {
        int number = -1;
        int32_t args[4] = {};
        std::string text; // what args[0] or args[1] point at, if text
    };
    std::array<Recent, 64> recent_{};
    size_t recentNext_ = 0;
};

} // namespace reluva::idtech3::eliteforce
