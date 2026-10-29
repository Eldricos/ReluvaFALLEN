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
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

class LevelPlay {
public:
    static constexpr float kTick = 1.0f / 30; // seconds a tick moves the level on

    LevelPlay(const PackageSet& packages, const Package& map);
    // Starts the level and logs a player in (as the .ini's [URL] says), at
    // the start tagged `portal` if one is given; or, given a saved game of
    // this map, carries on from it. `saveFolder`: where its games are
    // saved and listed.
    Error start(const std::string& portal = {}, const std::filesystem::path& saved = {},
                const std::filesystem::path& saveFolder = {});

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
    // The HUD told of something (HudEvent: 1010, no game to quick load).
    void hudEvent(int code);

    // Sounds, for the page to play (level_audio.cpp): what the scripts
    // asked for since last time, where an actor is now (false once it's
    // gone), the actors' ambient sounds, and the music.
    std::vector<ScriptLevel::SoundRequest> takeSounds();
    bool locationOf(VmObject* actor, Vec3f& out);
    // Whether the level's walls (its BSP, not movers) stand between two
    // points, short of the last 24 units to `to`.
    bool wallBetween(const Vec3f& from, const Vec3f& to);
    struct Ambient {
        VmObject* actor = nullptr;
        ObjectRef sound;
        bool streamed = false; // one of the MP3s: music
        Vec3f at{};
        float radius = 0, volume = 0, pitch = 1;
    };
    void ambientSounds(std::vector<Ambient>& out);
    VmObject* currentMusic() { return level_->currentMusic(); }
    const ScriptLevel::Music& music() { return level_->music(); }
    void musicEnded(VmObject* sound) { level_->musicEnded(sound); }
    // The audio settings (the .ini's AudioDevice: MasterVolume,
    // SFXVolume, MusicVolume, SpeechVolume 0-255; UseStereo,
    // ReverseStereo) as the options leave them; the master volume as
    // SetMasterVolume has it for now.
    std::string audioSetting(const std::string& key);
    float masterVolume();
    bool takeKillSounds();

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
        // Tweening into it (animFrame below 0): what it's tweening from.
        std::string tweenFrom;
        float tweenFromFrame = 0;
        bool tweenFromLoop = false;
        uint8_t style = 1;
        float scaleGlow = 1;
        bool unlit = false;            // bUnlit: as bright as its ScaleGlow, not the light where it is
        bool renderInitial = false;    // bMultiMeshRenderInitial: drawn itself as well as its copies
        // The Fallen's layered animations on parts of it (C3DPlayAnim).
        struct Layer {
            std::string anim, bone;
            bool rest = false; // the part is all but the bone and those under it
            float frame = 0, weight = 0;
            std::array<bool, 3> clear{}; // its first bone keeps the actor's own yaw, pitch, roll
        };
        std::vector<Layer> layers;
        // Its face's channels away from the neutral face (C3DSetSkinAnim:
        // Blink, EyeBrows, Phonemes), seconds into each.
        struct Face {
            std::string anim;
            float seconds = 0, weight = 0;
        };
        std::vector<Face> faces;
        // What its scripts do to its bones on top (a back bent, a hand on a gun).
        LayeredPoser::Adjust adjust;
    };
    bool look(VmObject* actor, Look& out);
    // Whether an actor may be seen: not hidden, and in one of `zones` (a
    // bit for each of the level's zones seen) if its zone is known. Cheap,
    // to pass over the rest before look().
    bool mayBeSeen(VmObject* actor, uint64_t zones);
    // The lights drawing a corona (bCorona): where, its texture (the
    // actor's Skin, else its Texture), DrawScale, and its zone (-1 not
    // known). Hidden or not: a light actor is hidden, its corona isn't.
    struct Corona {
        VmObject* actor = nullptr;
        Vec3f location{};
        ObjectRef texture;
        float drawScale = 1;
        int zone = -1;
    };
    void coronas(std::vector<Corona>& out);
    // Its copies (The Fallen's MultiMesh: particles, sparks, a beam's
    // sections), each drawn as it is, in its place, turned and scaled
    // (AxisScale, along its own axes), glowing and in its style; with its
    // own texture, for sprites. False if it has no list of them (it's
    // drawn as itself); with one, it's drawn as its copies only, even none
    // yet, unless it asks to be drawn as well (renderInitial).
    struct Copy {
        Vec3f location{}, axisScale{1, 1, 1};
        Rotator rotation;
        float drawScale = 1, scaleGlow = 1;
        ObjectRef texture;
        uint8_t style = 1;
    };
    bool copies(VmObject* actor, std::vector<Copy>& out);
    // The decals stuck to the world (Decal.AttachDecal), and their size:
    // their texture's, by their scale.
    std::vector<ScriptLevel::Decal> decals() const;
    // It was drawn this frame (its LastRenderTime: The Fallen's heads and
    // faces only move on pawns drawn in the last few seconds).
    void drawn(VmObject* actor);
    // It's about to be drawn: C3DPreRender, as The Fallen's engine sends
    // (the player's weapon works out its tip and the crosshair).
    void preRender(VmObject* actor);

    Vm& vm() { return *vm_; }
    ScriptLevel& level() { return *level_; }
    VmObject* player() const { return player_; }
    // The live actor an object reference in the level's package names
    // (a placed light...); null if none or destroyed.
    VmObject* actorAt(int32_t ref);
    const Package& map() const { return map_; }
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
                 AnimSequence, AnimFrame, AnimRate, AnimLoop, Style, ScaleGlow, LastRenderTime, Unlit,
                 RenderInitial, Region, AmbientSoundField, SoundRadius, SoundVolume, SoundPitch, CoronaField, FieldCount };
    const std::array<int, FieldCount>& slotsOf(const VmClass* cls);
    int regionZoneField_ = -1; // ZoneNumber's place in PointRegion
    std::unordered_map<const VmClass*, std::array<int, FieldCount>> slots_;
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
