// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Bot characters. See character.h.

#include "engines/idtech3/botlib/character.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3::botlib {

namespace {
const char* const kDefaultCharacter = "bots/default_c.c";
} // namespace

std::unique_ptr<BotCharacter> BotCharacters::read(const BotScript::Loader& load, const std::string& file, int skill,
                                                  std::string* error) {
    BotScript script(load);
    if (!script.open(file)) {
        if (error)
            *error = script.error();
        return nullptr;
    }
    auto ch = std::make_unique<BotCharacter>();
    ch->file = file;
    bool found = false;
    BotToken t;
    while (script.next(t)) {
        if (!t.is("skill")) {
            script.fail("unknown definition " + t.text);
            break;
        }
        double level = 0;
        if (!script.readNumber(level) || !script.expect("{"))
            break;
        if (skill >= 0 && int(level) != skill) {
            if (!script.skipBlock())
                break;
            continue;
        }
        // Index and value pairs up to the closing brace.
        ch->skill = float(int(level));
        while (script.next(t) && !t.is("}")) {
            if (t.type != BotToken::Type::Number || t.isFloat) {
                script.fail("expected an integer index, found " + t.text);
                break;
            }
            const int index = int(t.number);
            if (index < 0 || index >= BotCharacter::kCount) {
                script.fail("characteristic index out of range");
                break;
            }
            BotCharacter::Value& v = ch->values[size_t(index)];
            if (v.type != BotCharacter::Value::Type::None) {
                script.fail("characteristic " + std::to_string(index) + " given twice");
                break;
            }
            BotToken value;
            if (!script.next(value)) {
                script.fail("characteristic without a value");
                break;
            }
            double sign = 1;
            if (value.is("-")) {
                sign = -1;
                script.next(value);
            }
            if (value.type == BotToken::Type::Number && value.isFloat) {
                v.type = BotCharacter::Value::Type::Float;
                v.number = float(sign * value.number);
            } else if (value.type == BotToken::Type::Number) {
                v.type = BotCharacter::Value::Type::Integer;
                v.integer = int(sign * value.number);
            } else if (value.type == BotToken::Type::String) {
                v.type = BotCharacter::Value::Type::String;
                v.text = value.text;
            } else {
                script.fail("expected an integer, float or string, found " + value.text);
                break;
            }
        }
        found = script.error().empty();
        break;
    }
    if (!script.error().empty()) {
        if (error)
            *error = script.error();
        return nullptr;
    }
    if (!found) {
        if (error)
            *error = file + ": no skill " + std::to_string(skill);
        return nullptr;
    }
    return ch;
}

std::unique_ptr<BotCharacter> BotCharacters::loadSkill(const BotScript::Loader& load, const std::string& file,
                                                       int skill, std::string* log) {
    // The skill from the file, else from the default character, else the
    // file's first, else the default's first; then what's missing taken
    // from the default character at that skill.
    std::string error;
    std::unique_ptr<BotCharacter> ch;
    for (const auto& [f, s] : {std::make_pair(file, skill), std::make_pair(std::string(kDefaultCharacter), skill),
                               std::make_pair(file, -1), std::make_pair(std::string(kDefaultCharacter), -1)}) {
        if ((ch = read(load, f, s, &error)))
            break;
        if (log)
            *log += error + "\n";
    }
    if (!ch)
        return nullptr;
    if (const auto def = read(load, kDefaultCharacter, skill)) {
        for (int i = 0; i < BotCharacter::kCount; ++i)
            if (ch->values[size_t(i)].type == BotCharacter::Value::Type::None)
                ch->values[size_t(i)] = def->values[size_t(i)];
    }
    return ch;
}

int BotCharacters::load(const BotScript::Loader& load, const std::string& file, float skill, std::string* log) {
    skill = std::clamp(skill, 1.0f, 5.0f);
    std::unique_ptr<BotCharacter> result;
    if (skill == 1 || skill == 4 || skill == 5) {
        result = loadSkill(load, file, int(skill), log);
    } else {
        const int lo = skill < 4 ? 1 : 4, hi = skill < 4 ? 4 : 5;
        auto a = loadSkill(load, file, lo, log);
        if (!a)
            return 0;
        auto b = loadSkill(load, file, hi, log);
        if (!b || b->skill == a->skill) {
            result = std::move(a);
        } else {
            // Floats blended by the skill; the rest from the lower.
            result = std::make_unique<BotCharacter>(*a);
            result->skill = skill;
            const float scale = (skill - a->skill) / (b->skill - a->skill);
            for (size_t i = 0; i < size_t(BotCharacter::kCount); ++i) {
                const auto& va = a->values[i];
                const auto& vb = b->values[i];
                if (va.type == BotCharacter::Value::Type::Float && vb.type == BotCharacter::Value::Type::Float)
                    result->values[i].number = va.number + (vb.number - va.number) * scale;
            }
        }
    }
    if (!result)
        return 0;
    for (size_t i = 0; i < characters_.size(); ++i)
        if (!characters_[i]) {
            characters_[i] = std::move(result);
            return int(i) + 1;
        }
    characters_.push_back(std::move(result));
    return int(characters_.size());
}

void BotCharacters::free(int handle) {
    if (handle >= 1 && size_t(handle) <= characters_.size())
        characters_[size_t(handle) - 1].reset();
}

const BotCharacter* BotCharacters::get(int handle) const {
    if (handle < 1 || size_t(handle) > characters_.size())
        return nullptr;
    return characters_[size_t(handle) - 1].get();
}

float BotCharacters::floatValue(int handle, int index) const {
    const BotCharacter* ch = get(handle);
    if (!ch || index < 0 || index >= BotCharacter::kCount)
        return 0;
    const auto& v = ch->values[size_t(index)];
    return v.type == BotCharacter::Value::Type::Integer ? float(v.integer)
           : v.type == BotCharacter::Value::Type::Float ? v.number
                                                         : 0;
}

float BotCharacters::boundedFloat(int handle, int index, float min, float max) const {
    return std::clamp(floatValue(handle, index), min, max);
}

int BotCharacters::integer(int handle, int index) const {
    const BotCharacter* ch = get(handle);
    if (!ch || index < 0 || index >= BotCharacter::kCount)
        return 0;
    const auto& v = ch->values[size_t(index)];
    return v.type == BotCharacter::Value::Type::Integer ? v.integer
           : v.type == BotCharacter::Value::Type::Float ? int(v.number)
                                                         : 0;
}

int BotCharacters::boundedInteger(int handle, int index, int min, int max) const {
    return std::clamp(integer(handle, index), min, max);
}

std::string BotCharacters::string(int handle, int index) const {
    const BotCharacter* ch = get(handle);
    if (!ch || index < 0 || index >= BotCharacter::kCount)
        return {};
    const auto& v = ch->values[size_t(index)];
    return v.type == BotCharacter::Value::Type::String ? v.text : std::string();
}

} // namespace reluva::idtech3::botlib
