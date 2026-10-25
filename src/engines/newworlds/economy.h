// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/mission_script.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::newworlds {

// A colony's economy while playing: its stock, buildings going up, power,
// mines and the resource processor. Built from what recordings show
// (docs/formats/newworlds/game.md#the-colony): the costs are the training
// missions' build menu's; rates and amounts not yet measured in the game
// are named placeholders (vm-checklist.md).

// The colony's six materials, in the NEW WORLDS panel's order.
enum Material { Dilithium, Polycomp, Thermoconc, Transalumin, Nitrium, Duranium, kMaterials };
using Stock = std::array<int, kMaterials>;
const char* materialName(int m); // "DILITHIUM", ...

// The six raw ores, by deposit kind 1-6 (gmf.md#resource-deposits).
constexpr int kOres = 6;
const char* oreName(int kind); // "dilithium", "talgonite", ... (kind 1-6)

struct BuildingType {
    std::string id;    // the mission scripts' name: "construction-yard"
    std::string panel; // the selection panel's: "CONSTRUCTION YARD"
    Stock cost{};      // what it takes from the stock when building starts
    float power = 0;   // supply (positive) or demand (negative), in power units
    int capacity = 0;  // colony capacity it adds once built
    bool onDeposit = false; // a mining station: stands on a deposit
    std::string needs;      // a building that must stand first ("" none)
};
// The types the build menu offers (the training missions' Federation
// list; the other sides are taken to cost the same until seen).
const std::vector<BuildingType>& buildingTypes();
const BuildingType* buildingType(std::string_view id);

// The ground the colony stands on: a tile's deposit kind (0 none) and how
// much ore its block holds (drawn down by mining; shared by the block's
// tiles), as the map gives them.
struct ColonyMap {
    std::function<int(int tx, int ty)> depositKind;
    std::function<int(int tx, int ty)> depositAmount;
};

struct ColonyStructure {
    int id = 0;
    std::string type;
    float x = 0, y = 0;     // its middle, world units
    float progress = 1;     // 0 when ordered, 1 built
    bool started = true;    // its cost paid (it waits for materials if not)
    int deposit = 0;        // a mine's ore kind
    int blockX = -1, blockY = -1; // a mine's deposit block
    bool exhausted = false;
};

class Economy {
public:
    Economy(char side, Stock start, int crew, int capacity, ColonyMap map);

    const Stock& stock() const { return stock_; }
    int crew() const { return crew_; }
    int capacity() const { return capacity_; }
    float powerSupply() const;
    float powerDemand() const;
    float demandMet() const; // 0-1
    const std::vector<ColonyStructure>& structures() const { return structures_; }
    const std::array<float, kOres>& oreStock() const { return ore_; } // raw ore waiting at the processor

    // A building already standing (the starting colony).
    int addBuilt(const std::string& type, float x, float y);
    // Whether the menu offers a type now (what it needs stands).
    bool available(const BuildingType& t) const;
    bool affordable(const BuildingType& t) const;
    // Orders a building at a spot (its middle). Its cost is taken when work
    // starts; without enough materials it waits (and the colony hears so).
    // tx, ty: the tile under its middle, for a mine's deposit. 0 if the
    // type isn't available.
    int order(const std::string& type, float x, float y, int tx, int ty);

    // Moves time on: construction, power, mining and refining. Returns what
    // happened, as the mission scripts' events (built <type>, power-low,
    // short-resources, mine-exhausted at its position).
    std::vector<ScriptEvent> advance(double seconds);

private:
    bool has(const std::string& type) const;

    char side_;
    Stock stock_;
    int crew_, capacity_;
    ColonyMap map_;
    std::vector<ColonyStructure> structures_;
    std::array<float, kOres> ore_{};
    std::array<float, kMaterials> made_{}; // fractions of materials refined
    std::vector<std::pair<std::pair<int, int>, float>> mined_; // blocks' ore taken so far
    double clock_ = 0, lastPowerWarning_ = -1e9, lastShortWarning_ = -1e9, crewTimer_ = 0;
    int nextId_ = 1;
};

} // namespace reluva::newworlds
