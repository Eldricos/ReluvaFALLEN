// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Compiled UnrealScript written out as text, close to the source it was
// compiled from: a class's declarations, then each function's and state's
// statements with their code offsets (jumps are shown as offsets, not
// rebuilt into if/while). For reading what the game's scripts do.

#include "engines/unreal/package_set.h"
#include "engines/unreal/script.h"

#include <map>
#include <string>

namespace reluva::unreal {

// The native functions the packages declare, by number (each declared
// once), for showing native calls by name and operators as operators.
struct NativeFunction {
    std::string name;   // the function's own name ("Not_PreBool")
    std::string symbol; // what scripts write: its friendly name ("!")
    uint32_t flags = 0; // funcflag::
    uint8_t precedence = 0;
    int params = 0;
};

class NativeTable {
public:
    explicit NativeTable(const PackageSet& packages);
    const NativeFunction* find(int native) const;
    size_t size() const { return natives_.size(); }

private:
    std::map<int, NativeFunction> natives_;
};

// A class (its declarations, functions, states and defaults), a struct, a
// function or a state, as text.
std::string scriptText(const PackageSet& packages, const Package& package, int exportIndex,
                       const NativeTable& natives);

} // namespace reluva::unreal
