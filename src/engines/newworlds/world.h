// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace reluva::newworlds {

// The game being played on a map: its units, where they can go, and the
// player's selection and battle groups. Positions are in world units, x
// east and y north as the map viewer draws the map (a tile is 8192, a
// block 16384; a map position <a:b> is y = a / 8 blocks, x = b / 8). Only
// what recordings of play show is modelled (docs/formats/newworlds/play.md
// and play-tutorials.md); figures not yet measured in the game are named
// placeholders (vm-checklist.md).

// The ground units drive over: per tile, its movement class (the tile
// flags' bits 16-17, one value per ground model: 0 for holes, water_01
// and rock formations, which nothing in the recordings crosses; gmf.md),
// and the ground's height anywhere.
struct Terrain {
    int width = 0, height = 0; // in tiles
    std::vector<int> moveClass; // width x height, row by row from the south
    std::function<float(float x, float y)> heightAt;
};

// A tile can be entered if its class isn't 0 and the ground doesn't climb
// more than this between it and its neighbour (a level is about 1840
// units; cliffs between levels are walls).
constexpr float kMaxStep = 1500;
constexpr float kTile = 8192;

// What a unit is. speed: world units a second (placeholder until measured).
struct UnitKind {
    std::string name;  // as the selection panel names it ("GROUND SCOUT")
    std::string model; // "geo/fed/ffs.geo"
    float speed = 0;
};

// The side's ground scout: the four vehicles each recorded colony starts
// with are of one kind (F1, R1; which kind isn't known).
UnitKind groundScout(char side);

struct Unit {
    int id = 0;
    char side = 'F';
    UnitKind kind;
    float x = 0, y = 0;
    float heading = 0; // radians anticlockwise from east
    std::vector<std::pair<float, float>> path; // waypoints still to reach
    bool selected = false;
    bool moving() const { return !path.empty(); }
};

class World {
public:
    explicit World(Terrain terrain);

    const Terrain& terrain() const { return terrain_; }
    bool passable(int tx, int ty) const;
    // Whether a unit can step from one tile to a neighbour (8 ways; a
    // diagonal needs both tiles beside it open).
    bool canStep(int tx, int ty, int dx, int dy) const;
    // A path of tile centres from one point to another (A*), the last one
    // the goal itself; empty if there's none. The goal moves to the
    // nearest open tile if it is closed.
    std::vector<std::pair<float, float>> findPath(float x0, float y0, float x1, float y1) const;

    int addUnit(char side, const UnitKind& kind, float x, float y, float heading = 0);
    const std::vector<Unit>& units() const { return units_; }
    Unit* unit(int id);

    // Selection, as the game's: a click selects one unit (or none), a box
    // the units inside it; right-click drops it.
    void select(const std::vector<int>& ids);
    void clearSelection();
    std::vector<int> selection() const;
    // Orders the selected units to a point: each finds its own path to a
    // spot round it, so they don't all pile onto one.
    void moveSelected(float x, float y);

    // Battle groups 1-9: Ctrl+N makes the selection group N, N selects it.
    void setGroup(int n);
    bool selectGroup(int n);
    // The middle of group N (or of the selection, for n = 0); false if empty.
    bool groupCentre(int n, float& x, float& y) const;

    // Moves time on; true if anything moved.
    bool advance(double seconds);

private:
    int tileOf(float v) const { return int(v / kTile); }

    Terrain terrain_;
    std::vector<Unit> units_;
    std::array<std::vector<int>, 10> groups_;
    int nextId_ = 1;
};

} // namespace reluva::newworlds
