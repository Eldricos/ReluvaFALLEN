// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::newworlds {

// A mission's colony, laid out on its map from what recordings of play show
// (docs/formats/newworlds/colony.md). The game keeps its starting colonies
// in STNW.exe, not the maps, so this is an approximation: the hub where the
// recordings place the colony, and each building the mission's script
// builds in the next free block around it.

// Where a side's colony starts in a mission, as a map position (eighths of
// a block, row first); a < 0 if no recording gives it.
struct ColonySite {
    int a = -1, b = -1;
    const char* source = ""; // how it's known
};
ColonySite colonySite(std::string_view shortName, char side);

// A building's model for a side, by the name the mission scripts use
// ("vehicle-yard"); empty if none is known. matched: whether recordings
// matched it (play-f1.md, play-k1.md) rather than its name alone.
std::string buildingModel(std::string_view building, char side, bool* matched = nullptr);

// What the layout needs to know of the map: its size in blocks, a block's
// resource deposit (0 for none), the ground's height at a map position
// (NaN where there's no ground), and a model's size across x and y in
// model units (a block is 16384).
struct ColonyGround {
    int width = 0, height = 0;
    std::function<int(int bx, int by)> deposit;
    std::function<float(int a, int b)> heightAt;
    std::function<std::pair<float, float>(const std::string& model)> size;
};

struct ColonyBuilding {
    std::string name, model;
    int a = 0, b = 0; // its middle
};

// Buildings on the map's tile grid (half a block, 4 map positions), each
// over as many tiles as its model needs, close around the hub. A tile is
// free if nothing stands on it, its ground is flat and near the hub's
// height, and its block has no deposit; mining stations instead stand on
// the nearest deposit.
class ColonyLayout {
public:
    ColonyLayout(char side, ColonyGround ground) : side_(side), ground_(std::move(ground)) {}
    // Puts the hub near the site, where there's room around it. False if
    // there's nowhere.
    bool start(int a, int b);
    // Adds a building; false if the side has no model for it (or it's a
    // unit), it upgrades a building that isn't there, or there's no room.
    bool add(std::string_view building);
    // For the player placing a building (not an upgrade): its footprint
    // centred as near a map position as the tile grid allows, its middle
    // there (a, b), and whether it fits (free, flat ground near the hub's
    // height; a mine's middle on a deposit). False if the side has no
    // model for it.
    bool spotAt(std::string_view building, int a, int b, int& ma, int& mb, bool& fits) const;
    // Adds it there if it fits.
    bool addAt(std::string_view building, int a, int b);
    const std::vector<ColonyBuilding>& buildings() const { return buildings_; }
    // The concrete the colony starts on, as tile positions (x east, y
    // north): its ground within two tiles of the hub. Recordings show the
    // starting colony on concrete tiles and buildings added later on the
    // bare ground (colony.md); how far the game's pads reach isn't known.
    const std::vector<std::pair<int, int>>& pad() const { return pad_; }

private:
    struct Rect {
        int x, y, w, h; // in tiles
    };
    bool tileFree(int tx, int ty, bool mine) const;   // groundFits, and nothing stands there
    // On the map, flat (within flatness), near the hub's height.
    bool groundFits(int tx, int ty, bool mine, float flatness) const;
    bool fits(const Rect& r, bool mine) const;
    bool spotRect(std::string_view building, int a, int b, std::string& model, Rect& r) const;
    Rect footprint(const std::string& model, int tx, int ty) const;
    void place(std::string_view name, const std::string& model, const Rect& r);

    char side_;
    ColonyGround ground_;
    std::vector<ColonyBuilding> buildings_;
    std::vector<Rect> taken_;
    std::vector<std::pair<int, int>> pad_;
    float hubHeight_ = 0;
    bool heightSet_ = false; // tiles must be near hubHeight_
};

} // namespace reluva::newworlds
