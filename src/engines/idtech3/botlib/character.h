// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// A bot's character (botfiles/bots/<name>_c.c): numbered characteristics
// (its name, aim, aggression, reaction time...), each an integer, a float
// or a string, given per skill level:
//
//     skill 1 { CHARACTERISTIC_NAME "Munro" CHARACTERISTIC_ATTACK_SKILL 0.3 ... }
//
// How characters are chosen, completed and blended between skills was
// read from the GPL ioquake3 bot library (be_ai_char.c); new code.
struct BotCharacter {
    static constexpr int kCount = 80;
    struct Value {
        enum class Type { None, Integer, Float, String } type = Type::None;
        int integer = 0;
        float number = 0;
        std::string text;
    };
    std::string file;
    float skill = 0;
    std::array<Value, kCount> values;
};

class BotCharacters {
public:
    // Reads `file` from the scripts with `load`; nullptr when it has no
    // such skill (`skill` < 0: the first it has) or can't be read.
    static std::unique_ptr<BotCharacter> read(const BotScript::Loader& load, const std::string& file, int skill,
                                              std::string* error = nullptr);

    // The original's choice: the skill clamped to 1..5; skills 1, 4 and 5
    // read as they are (or the nearest the file has, or the default
    // character's), others blended between 1 and 4 or 4 and 5; what the
    // file leaves out taken from the default character
    // (bots/default_c.c). A handle from 1, 0 if none could be read.
    int load(const BotScript::Loader& load, const std::string& file, float skill, std::string* log = nullptr);
    void free(int handle);
    const BotCharacter* get(int handle) const;

    float floatValue(int handle, int index) const;
    float boundedFloat(int handle, int index, float min, float max) const;
    int integer(int handle, int index) const;
    int boundedInteger(int handle, int index, int min, int max) const;
    std::string string(int handle, int index) const;

private:
    std::unique_ptr<BotCharacter> loadSkill(const BotScript::Loader& load, const std::string& file, int skill,
                                            std::string* log);
    std::vector<std::unique_ptr<BotCharacter>> characters_; // by handle - 1
};

} // namespace reluva::idtech3::botlib
