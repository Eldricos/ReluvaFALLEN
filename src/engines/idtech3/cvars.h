// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

class Qvm;

// Console variables: named strings the engine and the game code share
// (settings, game state). See docs/formats/idtech3/qvm.md.
class CvarStore {
public:
    // Flags as the game code passes them (the Quake III values).
    enum Flags : int32_t {
        Archive = 1,      // saved in the config file
        UserInfo = 2,
        ServerInfo = 4,
        SystemInfo = 8,
        Init = 16,        // only settable from the command line
        Latch = 32,       // takes effect on restart
        ReadOnly = 64,
        UserCreated = 128,
        Temp = 256,
        Cheat = 512,
        NoRestart = 1024,
    };

    struct Cvar {
        std::string name;
        std::string value;
        std::string defaultValue;
        int32_t flags = 0;
        int32_t modificationCount = 0;
        int32_t handle = 0; // index + 1 in registration order, for the game's vmCvar_t
    };

    // Creates the variable with `defaultValue` if it doesn't exist; an
    // existing one keeps its value and gains the flags.
    Cvar& create(std::string_view name, std::string_view defaultValue, int32_t flags);
    // Sets (creating it, flagged UserCreated, if needed). A ReadOnly or
    // Init variable is only changed with `force`.
    void set(std::string_view name, std::string_view value, bool force = false);

    const Cvar* find(std::string_view name) const;
    const Cvar* byHandle(int32_t handle) const;
    std::string string(std::string_view name) const; // "" if unset
    float value(std::string_view name) const;        // 0 if unset or not a number
    int32_t integer(std::string_view name) const;
    const std::vector<Cvar>& all() const { return cvars_; }

    // The game code's view of a variable, a vmCvar_t: handle,
    // modificationCount, value (float), integer, then the string, 256
    // bytes. Writes the variable `handle` into it at `address`.
    static constexpr int32_t kVmCvarSize = 16 + 256;
    void writeVmCvar(Qvm& vm, int32_t address, int32_t handle) const;
    // Rewrites the vmCvar_t at `address` if its variable changed since.
    void updateVmCvar(Qvm& vm, int32_t address) const;

private:
    Cvar* findMutable(std::string_view name);

    std::vector<Cvar> cvars_;
    std::map<std::string, size_t> byName_; // lower-case name -> index
};

} // namespace reluva::idtech3
