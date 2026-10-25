// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' weapons: the weapon descriptions (weapons.c) and choosing the
// one worth most by each bot's weapon weights. Behaviour read from the
// GPL ioquake3 bot library (be_ai_weap.c); new code.

#include "engines/idtech3/botlib/bot_library.h"

namespace reluva::idtech3::botlib {

int BotLibrary::allocWeaponState() { return alloc(weaponStates_); }

void BotLibrary::freeWeaponState(int handle) {
    if (state(weaponStates_, handle))
        weaponStates_[size_t(handle) - 1].reset();
}

int BotLibrary::loadWeaponWeights(int handle, const std::string& file) {
    WeaponState* ws = state(weaponStates_, handle);
    if (!ws)
        return blerr::CannotLoadWeaponWeights;
    loadConfigs();
    ws->weights = weights(file);
    if (!ws->weights)
        return blerr::CannotLoadWeaponWeights;
    if (weapons_.empty())
        return blerr::CannotLoadWeaponConfig;
    ws->weightIndex.clear();
    for (const WeaponInfo& w : weapons_)
        ws->weightIndex.push_back(w.valid ? ws->weights->find(w.name) : -1);
    return blerr::None;
}

int BotLibrary::chooseBestFightWeapon(int handle, const std::vector<int>& inventory) const {
    const WeaponState* ws = state(weaponStates_, handle);
    if (!ws || !ws->weights)
        return 0;
    int best = 0;
    float bestWeight = 0;
    for (size_t i = 0; i < weapons_.size() && i < ws->weightIndex.size(); ++i) {
        if (!weapons_[i].valid || ws->weightIndex[i] < 0)
            continue;
        const float w = ws->weights->evaluate(ws->weightIndex[i], inventory);
        if (w > bestWeight) {
            bestWeight = w;
            best = int(i);
        }
    }
    return best;
}

const WeaponInfo* BotLibrary::weaponInfo(int weapon) const {
    if (weapon <= 0 || size_t(weapon) >= weapons_.size())
        return nullptr;
    return &weapons_[size_t(weapon)];
}

} // namespace reluva::idtech3::botlib
