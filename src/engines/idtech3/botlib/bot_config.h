// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/collision.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// The bots' configuration files, read with the bot scripts' preprocessor.
// Formats read from the GPL ioquake3 bot library (be_ai_weight.c,
// be_ai_goal.c, be_ai_weap.c, l_struct.c); docs/formats/idtech3/aas.md.

// Fuzzy weights (items' and weapons' worth to a bot): per name, nested
// switches on the bot's inventory,
//
//     weight "weapon_phaser" { switch (INVENTORY_PHASER) { case 1: return 0; default: return 50; } }
//
// a case applying while the inventory count is below its value.
struct FuzzySwitch {
    int index = 0;     // inventory slot tested
    int value = 0;     // applies below this (the default: always)
    float weight = 0;  // what it returns, unless it switches further
    std::unique_ptr<FuzzySwitch> child, next;
};
struct WeightConfig {
    std::string file;
    std::vector<std::pair<std::string, std::unique_ptr<FuzzySwitch>>> weights;
    // The weight's number for a name, -1 if none.
    int find(const std::string& name) const;
    // A weight's value for an inventory (counts by slot).
    float evaluate(int weight, const std::vector<int>& inventory) const;
};
std::unique_ptr<WeightConfig> readWeightConfig(const BotScript::Loader& load, const std::string& file,
                                               std::string* error = nullptr);

// Item descriptions (items.c): `iteminfo "classname" { name ... }`.
struct ItemInfo {
    std::string classname, name, model;
    int modelIndex = 0, type = 0, index = 0;
    float respawnTime = 0;
    Vec3f mins{}, maxs{};
};
bool readItemConfig(const BotScript::Loader& load, const std::string& file, std::vector<ItemInfo>& out,
                    std::string* error = nullptr);

// Weapon and projectile descriptions (weapons.c): `weaponinfo { ... }`
// and `projectileinfo { ... }`, each weapon with its projectile's.
struct ProjectileInfo {
    std::string name, model;
    int flags = 0, damage = 0, visDamage = 0, damageType = 0, healthInc = 0;
    float gravity = 0, radius = 0, push = 0, detonation = 0, bounce = 0, bounceFric = 0, bounceStop = 0;
};
struct WeaponInfo {
    bool valid = false;
    int number = 0;
    std::string name, model, projectile;
    int level = 0, weaponIndex = 0, flags = 0, numProjectiles = 0, ammoAmount = 0, ammoIndex = 0;
    float hspread = 0, vspread = 0, speed = 0, acceleration = 0, extraZVelocity = 0, activate = 0, reload = 0,
          spinUp = 0, spinDown = 0;
    Vec3f recoil{}, offset{}, angleOffset{};
    ProjectileInfo proj;
};
bool readWeaponConfig(const BotScript::Loader& load, const std::string& file, std::vector<WeaponInfo>& out,
                      std::string* error = nullptr);

} // namespace reluva::idtech3::botlib
