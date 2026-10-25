// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/economy.h"

#include <algorithm>
#include <cmath>

namespace reluva::newworlds {

namespace {

// Placeholders (game.md#placeholders), until measured in the game:
// - a building goes up in about 40 s (F1's vehicle yard: placed 6:40,
//   finished 7:22);
constexpr double kBuildSeconds = 40;
// - a mining station digs 1.5 ore a second (C5's two mines met its 200
//   dilithium and 1500 nitrium targets in about ten minutes);
constexpr float kMineRate = 1.5f;
// - the processor refines 3 ore a second, one material per ore;
constexpr float kRefineRate = 3;
// - a deposit block holds its map value (50-300) times 4 ore (the
//   tutorials' mines ran out after some eight minutes);
constexpr float kOrePerUnit = 4;
// - the crew grows by one every 30 s up to the capacity (F1: 20 to 40 in
//   18 minutes);
constexpr double kCrewSeconds = 30;
// - warnings repeat once a minute while they hold (K2's power messages at
//   8:14 and 9:14).
constexpr double kWarnEvery = 60;

} // namespace

const char* materialName(int m) {
    static const char* const kNames[] = {"DILITHIUM", "POLYCOMP", "THERMOCONC", "TRANSALUMIN", "NITRIUM", "DURANIUM"};
    return m >= 0 && m < kMaterials ? kNames[m] : "";
}

const char* oreName(int kind) {
    static const char* const kNames[] = {"dilithium", "talgonite", "silicon", "kelbonite", "magnesite", "dolamide"};
    return kind >= 1 && kind <= kOres ? kNames[kind - 1] : "";
}

const std::vector<BuildingType>& buildingTypes() {
    // Costs as the training missions' build menu shows them, in the
    // stock's order (Dilithium, Polycomp, Thermoconc, Transalumin,
    // Nitrium, Duranium; play-tutorials.md#build-costs-shown). The
    // accommodation block's isn't shown: a sickbay's, a guess. Power and
    // capacity are placeholders: the hub's generator covers a few
    // buildings (C6 starts at 31 % demand met with its hub alone loaded),
    // and each building adds a little capacity, accommodation most (F1's
    // capacity rose 50, 55, 65, 80, 90 as it built).
    static const std::vector<BuildingType> kTypes = {
        {"colony-hub", "COLONY HUB", {}, 60, 0, false, "-"},
        {"construction-yard", "CONSTRUCTION YARD", {0, 12, 89, 5, 50, 0}, -10, 5, false, "colony-hub"},
        {"vehicle-yard", "VEHICLE YARD", {0, 10, 90, 5, 0, 0}, -10, 5, false, "construction-yard"},
        {"power-generator", "POWER GENERATOR", {35, 1, 35, 0, 800, 0}, 60, 0, false, "construction-yard"},
        {"resource-processor", "RESOURCE PROCESSOR", {0, 4, 30, 2, 0, 0}, -10, 5, false, "construction-yard"},
        {"mining-station", "MINING STATION", {0, 14, 80, 10, 0, 140}, -15, 0, true, "resource-processor"},
        {"science-station", "SCIENCE STATION", {0, 4, 53, 17, 120, 0}, -10, 5, false, "construction-yard"},
        {"transporter-pad", "TRANSPORTER PAD", {0, 6, 32, 2, 0, 0}, -5, 10, false, "construction-yard"},
        {"sickbay", "SICKBAY", {0, 8, 48, 20, 0, 0}, -10, 5, false, "construction-yard"},
        {"accommodation", "ACCOMMODATION", {0, 8, 48, 20, 0, 0}, -5, 15, false, "construction-yard"},
        {"security-center", "SECURITY CENTER", {0, 10, 100, 10, 1200, 0}, -15, 5, false, "construction-yard"},
    };
    return kTypes;
}

const BuildingType* buildingType(std::string_view id) {
    for (const BuildingType& t : buildingTypes())
        if (t.id == id)
            return &t;
    return nullptr;
}

Economy::Economy(char side, Stock start, int crew, int capacity, ColonyMap map)
    : side_(side), stock_(start), crew_(crew), capacity_(capacity), map_(std::move(map)) {}

bool Economy::has(const std::string& type) const {
    return std::any_of(structures_.begin(), structures_.end(),
                       [&](const ColonyStructure& s) { return s.type == type && s.progress >= 1; });
}

bool Economy::available(const BuildingType& t) const { return t.needs != "-" && (t.needs.empty() || has(t.needs)); }

bool Economy::affordable(const BuildingType& t) const {
    for (int m = 0; m < kMaterials; ++m)
        if (stock_[size_t(m)] < t.cost[size_t(m)])
            return false;
    return true;
}

float Economy::powerSupply() const {
    float p = 0;
    for (const ColonyStructure& s : structures_)
        if (const BuildingType* t = buildingType(s.type); t && s.progress >= 1 && t->power > 0)
            p += t->power;
    return p;
}

float Economy::powerDemand() const {
    float p = 0;
    for (const ColonyStructure& s : structures_)
        if (const BuildingType* t = buildingType(s.type); t && s.progress >= 1 && t->power < 0 && !s.exhausted)
            p -= t->power;
    return p;
}

float Economy::demandMet() const {
    const float d = powerDemand();
    return d <= 0 ? 1 : std::min(1.0f, powerSupply() / d);
}

int Economy::addBuilt(const std::string& type, float x, float y) {
    ColonyStructure s;
    s.id = nextId_++;
    s.type = type;
    s.x = x, s.y = y;
    structures_.push_back(s);
    return s.id;
}

int Economy::order(const std::string& type, float x, float y, int tx, int ty) {
    const BuildingType* t = buildingType(type);
    if (!t || !available(*t))
        return 0;
    ColonyStructure s;
    s.id = nextId_++;
    s.type = type;
    s.x = x, s.y = y;
    s.progress = 0;
    s.started = false;
    if (t->onDeposit) {
        s.deposit = map_.depositKind ? map_.depositKind(tx, ty) : 0;
        s.blockX = tx / 2, s.blockY = ty / 2;
        if (s.deposit == 0)
            return 0;
    }
    structures_.push_back(s);
    return s.id;
}

std::vector<ScriptEvent> Economy::advance(double seconds) {
    std::vector<ScriptEvent> out;
    clock_ += seconds;
    const float met = demandMet();
    if (met < 1 && clock_ - lastPowerWarning_ >= kWarnEvery) {
        out.push_back({"power-low", {}});
        lastPowerWarning_ = clock_;
    }
    // Construction: paid for when it starts, then up over kBuildSeconds,
    // slower without full power.
    bool waiting = false;
    for (ColonyStructure& s : structures_) {
        if (s.progress >= 1)
            continue;
        const BuildingType* t = buildingType(s.type);
        if (!s.started) {
            if (!affordable(*t)) {
                waiting = true;
                continue;
            }
            for (int m = 0; m < kMaterials; ++m)
                stock_[size_t(m)] -= t->cost[size_t(m)];
            s.started = true;
        }
        s.progress = float(std::min(1.0, s.progress + seconds / kBuildSeconds * std::max(0.25f, met)));
        if (s.progress >= 1) {
            capacity_ += t->capacity;
            out.push_back({"built", {s.type}});
        }
    }
    if (waiting && clock_ - lastShortWarning_ >= kWarnEvery) {
        out.push_back({"short-resources", {}});
        lastShortWarning_ = clock_;
    }
    // Mining: each working station digs its block's ore for the processor.
    const bool processor = has("resource-processor");
    for (ColonyStructure& s : structures_) {
        if (s.type != "mining-station" || s.progress < 1 || s.exhausted || !processor)
            continue;
        auto it = std::find_if(mined_.begin(), mined_.end(), [&](const auto& m) { return m.first == std::pair{s.blockX, s.blockY}; });
        if (it == mined_.end()) {
            mined_.push_back({{s.blockX, s.blockY}, 0});
            it = mined_.end() - 1;
        }
        const float held = (map_.depositAmount ? float(map_.depositAmount(s.blockX * 2, s.blockY * 2)) : 100) * kOrePerUnit;
        const float take = std::min(kMineRate * met * float(seconds), held - it->second);
        it->second += take;
        ore_[size_t(s.deposit - 1)] += take;
        if (it->second >= held) {
            s.exhausted = true;
            out.push_back({"mine-exhausted", {}, s.blockY * 8 + 4, s.blockX * 8 + 4});
        }
    }
    // Refining: ore of kind k into material k-1, one for one (which ore
    // makes which material isn't known: a placeholder).
    if (processor) {
        float budget = kRefineRate * met * float(seconds);
        for (int k = 0; k < kOres && budget > 0; ++k) {
            const float use = std::min(budget, ore_[size_t(k)]);
            ore_[size_t(k)] -= use;
            budget -= use;
            made_[size_t(k)] += use;
            const int whole = int(made_[size_t(k)]);
            stock_[size_t(k)] += whole;
            made_[size_t(k)] -= float(whole);
        }
    }
    // The crew grows towards the capacity.
    crewTimer_ += seconds;
    while (crewTimer_ >= kCrewSeconds) {
        crewTimer_ -= kCrewSeconds;
        if (crew_ < capacity_)
            ++crew_;
    }
    return out;
}

} // namespace reluva::newworlds
