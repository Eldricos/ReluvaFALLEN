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
#include <vector>

namespace reluva::unreal {

class LevelPlay {
public:
    static constexpr float kTick = 1.0f / 30; // seconds a tick moves the level on

    LevelPlay(const PackageSet& packages, const Package& map);
    // Starts the level and logs a player in (as the .ini's [URL] says), at
    // the start tagged `portal` if one is given.
    Error start(const std::string& portal = {});

    // Moves the level on by `seconds` of real time, in whole ticks: at
    // most `maxTicks` at once, so when the scripts can't keep up the game
    // runs slower than real time rather than falling ever further behind.
    void advance(float seconds, int maxTicks = 4);
    ScriptLevel::Controls& controls() { return level_->controls; }
    // A key bound to an exec function pressed: the player's Jump, Fire...
    void press(const char* exec);
    // A key pressed or let go ("2", "F1"), as the player's .ini binds it
    // ([Engine.Input] in User.ini, else DefUser.ini: "2=Rifle"): the exec
    // functions it names, the buttons it holds. False if it's bound to none.
    bool pressKey(const std::string& key, bool down = true);
    // A key going down or up as the engine first hands it on: to the
    // console's KeyEvent (`inputKey` an EInputKey: 13 Enter, 27 Escape, 37
    // to 40 the arrows, 0x70 F1...). The Fallen's console gives the arrows
    // and Enter to the HUD while its menus show. True if it took the key;
    // otherwise it's the bindings' (pressKey).
    bool consoleKey(int inputKey, bool down);
    // The game paused (Level.Pauser set: the menus).
    bool paused() const;
    // The game's UWindow windows open (the options): the console's
    // Root.bUWindowActive; the mouse is theirs then.
    bool windowsOpen() const;
    // Where the mouse is (in the screen's pixels) for those windows: the
    // console's MouseX and MouseY.
    void consoleMouse(float x, float y);
    // The game's menu showing (the player's bShowMenu): the keys are all
    // the game's then.
    bool menuShowing() const;
    // Where the game asked to travel ("CM01a_SW/CM01_A_Sisko01": a map,
    // then the start to begin at), once: empty after.
    std::string takeTravel();
    // The game asked to quit (its menu's Quit).
    bool quitAsked() const;

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
        bool animLoop = false;
        uint8_t style = 1;
        float scaleGlow = 1;
        // The Fallen's layered animations on parts of it (C3DPlayAnim).
        struct Layer {
            std::string anim, bone;
            bool rest = false; // the part is all but the bone and those under it
            float frame = 0, weight = 0;
        };
        std::vector<Layer> layers;
        // What its scripts do to its bones on top (a back bent, a hand on a gun).
        LayeredPoser::Adjust adjust;
    };
    bool look(VmObject* actor, Look& out);
    // It was drawn this frame (its LastRenderTime: The Fallen's heads and
    // faces only move on pawns drawn in the last few seconds).
    void drawn(VmObject* actor);
    // It's about to be drawn: C3DPreRender, as The Fallen's engine sends
    // (the player's weapon works out its tip and the crosshair).
    void preRender(VmObject* actor);

    Vm& vm() { return *vm_; }
    ScriptLevel& level() { return *level_; }
    VmObject* player() const { return player_; }
    const std::string& loginError() const { return loginError_; }
    float time() const { return time_; }       // seconds played
    float behind() const { return behind_; }   // real seconds not yet played (it's slower than real time)
    int health();
    // The level's title (from its .int, as CTVE_Document sets it) and its
    // objectives that are active now (LevelInfo's Objective_State 0).
    std::string title();
    std::vector<std::string> activeObjectives();
    std::string playerState();

private:
    ObjectRef refOf(const Value* v) const;
    std::string inputSetting(const std::string& key) const;
    std::map<std::string, std::string> aliases_; // the input aliases, by name (lower case)
    // Where each property look() reads is in a class's values, found
    // once per class (looking properties up by name for every actor every
    // frame took as long as the scripts' ticks).
    enum Field { Location, PrePivot, Rotation, DrawScale, DrawType, Hidden, MeshField, Skin, Texture, MultiSkins,
                 AnimSequence, AnimFrame, AnimRate, AnimLoop, Style, ScaleGlow, LastRenderTime, FieldCount };
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
