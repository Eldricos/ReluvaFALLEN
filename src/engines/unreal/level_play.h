// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level being played: its scripts run live (vm_level.h) with a player
// logged in, ticked in fixed steps as real time passes, steered by the
// controls, seen from where the game's own PlayerCalcView puts the camera.
// The Maps page draws it; reluva-tool ue-play drives it without a window.
// See docs/engines/unreal_vm.md.

#include "engines/unreal/actors.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/vm.h"
#include "engines/unreal/vm_level.h"

#include <array>
#include <map>
#include <memory>
#include <string>

namespace reluva::unreal {

class LevelPlay {
public:
    static constexpr float kTick = 1.0f / 30; // seconds a tick moves the level on

    LevelPlay(const PackageSet& packages, const Package& map);
    // Starts the level and logs a player in (as the .ini's [URL] says).
    Error start();

    // Moves the level on by `seconds` of real time, in whole ticks: at
    // most `maxTicks` at once, so when the scripts can't keep up the game
    // runs slower than real time rather than falling ever further behind.
    void advance(float seconds, int maxTicks = 4);
    ScriptLevel::Controls& controls() { return level_->controls; }
    // A key bound to an exec function pressed: the player's Jump, Fire...
    void press(const char* exec);

    // Where the camera is, as the game puts it each frame
    // (PlayerCalcView): behind the player, or a cut scene's camera.
    struct View {
        Vec3f location{};
        Rotator rotation;
        float fov = 90;          // degrees across
        bool letterbox = false;  // the HUD's black bars (a cut scene)
        VmObject* actor = nullptr; // whose eyes, if anyone's
    };
    View view();

    // How an actor looks, as its properties say now.
    struct Look {
        Vec3f location{}, prePivot{};
        Rotator rotation;
        float drawScale = 1;
        int drawType = 0; // 1 sprite, 2 mesh, 3 brush...
        bool hidden = false;
        ObjectRef mesh, skin, texture;
        std::array<ObjectRef, 8> multiSkins{};
        std::string animSequence;
        float animFrame = 0, animRate = 0;
        uint8_t style = 1;
        float scaleGlow = 1;
    };
    bool look(VmObject* actor, Look& out);

    Vm& vm() { return *vm_; }
    ScriptLevel& level() { return *level_; }
    VmObject* player() const { return player_; }
    const std::string& loginError() const { return loginError_; }
    float time() const { return time_; }       // seconds played
    float behind() const { return behind_; }   // real seconds not yet played (it's slower than real time)
    int health();
    std::string playerState();

private:
    ObjectRef refOf(const Value* v) const;
    // Where each property look() reads is in a class's values, found
    // once per class (looking properties up by name for every actor every
    // frame took as long as the scripts' ticks).
    enum Field { Location, PrePivot, Rotation, DrawScale, DrawType, Hidden, MeshField, Skin, Texture, MultiSkins,
                 AnimSequence, AnimFrame, AnimRate, Style, ScaleGlow, FieldCount };
    std::map<const VmClass*, std::array<int, FieldCount>> slots_;
    std::array<NameId, FieldCount> names_{};

    const PackageSet& packages_;
    const Package& map_;
    std::unique_ptr<Vm> vm_;
    std::unique_ptr<ScriptLevel> level_;
    VmObject* player_ = nullptr;
    std::string loginError_;
    float time_ = 0, owed_ = 0, behind_ = 0;
};

} // namespace reluva::unreal
