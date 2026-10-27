// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level running its scripts: its actors made into VM objects (each with
// its class's defaults and its own saved properties), the game type
// spawned, the start-up events sent in order, and the world ticked. The
// engine side scripts call into (spawning, destroying, iterating over the
// actors, timers) is vm_natives_engine.cpp. See docs/engines/unreal_vm.md.

#include "engines/unreal/actors.h"
#include "engines/unreal/vm.h"

#include <string>
#include <vector>

namespace reluva::unreal {

// The level's actor list (the Level object's Actors): object references,
// 0 for empty slots.
Error readLevelActors(const Package& map, std::vector<int32_t>& out);

class ScriptLevel {
public:
    ScriptLevel(Vm& vm, const Package& map);
    ~ScriptLevel();

    // Makes the level's actors.
    Error load();
    // Spawns the game type (the LevelInfo's DefaultGameType, else
    // `defaultGame`, "Package.Class") and sends every actor the start-up
    // events: the game's InitGame, then each actor's PreBeginPlay and
    // BeginPlay, then PostBeginPlay, then SetInitialState.
    void beginPlay(const std::string& defaultGame);
    // Moves the world on: each actor's timer, Tick and state code.
    void tick(float seconds);

    Vm& vm() { return vm_; }
    const Package& map() const { return map_; }
    VmObject* levelInfo() const { return levelInfo_; }
    VmObject* game() const { return game_; }
    const std::vector<VmObject*>& actors() const { return actors_; }
    float time() const { return time_; }

    // An actor of a class, as Actor.Spawn makes one: its owner, tag,
    // place; the start-up events sent. nullptr if the class isn't an
    // actor's.
    VmObject* spawn(VmClass* cls, VmObject* owner, NameId tag, const Value* location, const Value* rotation);
    void destroy(VmObject* actor);

private:
    void init(VmObject* actor); // the engine's own properties of a new actor

    Vm& vm_;
    const Package& map_;
    VmObject* levelInfo_ = nullptr;
    VmObject* game_ = nullptr;
    VmObject* levelObject_ = nullptr;
    std::vector<VmObject*> actors_;
    float time_ = 0;
};

// The game type a game's levels run with: its .ini's [Engine.Engine]
// DefaultGame ("DS9.DS9_GameInfo"), else Engine's GameInfo.
std::string defaultGameType(const PackageSet& packages);

// A level's start, run for what it changes: its scripts' start-up events
// and `seconds` of ticks, then each placed actor compared with how it was
// saved (destroyed, hidden, its draw type, its light's type and
// brightness). For drawing a level as it looks when play begins.
struct LevelStart {
    ScriptedActors changed;
    int removed = 0, hidden = 0, shown = 0, drawTypes = 0, lights = 0; // actors changed each way
    double milliseconds = 0;
    size_t errors = 0, missingNatives = 0;
    int accessedNone = 0;
};
Error runLevelStart(const PackageSet& packages, const Package& map, const std::string& defaultGame, float seconds,
                    LevelStart& out);

// The natives of Engine's Actor and its subclasses that a level needs
// (registered once per VM; they work on vm.level).
void registerEngineNatives(Vm& vm);

} // namespace reluva::unreal
