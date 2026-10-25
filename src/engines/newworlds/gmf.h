// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::newworlds {

// A .gmf map (chunk tag 0x11). See docs/formats/newworlds/gmf.md; much of the
// format is still unknown and is skipped.
//
// A map is a grid of blocks; a block is 2 x 2 tiles; a tile is a ground model
// from the planet's archive (8192 x 8192 units).
struct GmfMap {
    static constexpr int kLayers = 5;
    struct Tile {
        uint32_t model = 0;    // index of its ground model among the archive's .geo files
        uint32_t flags = 0;    // bits 14-15: rotation; others unknown
        int rotation = 0;      // quarter turns anticlockwise (bits 14-15 of flags)
        // Bit 24 of flags: set only on hole_02 in icewrld, whose texture is
        // transparent around the hole, so presumably "draw ground under it".
        bool underlay = false;
        // Layers of terrain codes: 8 x 8 cells each, row by row from the
        // south, columns from the west (after the tile's rotation); 0 for
        // none, otherwise an index into the map's colour table. The first row
        // and column use other codes than the rest. What the layers are for
        // (not textures: hole, ramp and concrete tiles share the same ones)
        // isn't known.
        std::array<uint8_t, kLayers * 64> layers{};
    };
    struct Cell {
        uint16_t block = 0; // which block
        // Bits 12-15: a resource deposit's kind (1-6; 0 none; the third u16's
        // bits 12-15 always repeat it). Bits 0-11: 50-300 in steps of 12.5,
        // spread inside deposits (an amount?) and following the terrain
        // elsewhere (unknown).
        uint16_t value = 0;
        int deposit() const { return value >> 12; }
    };

    // An object placed on the map, from a grid cell's list: a u32 whose bits
    // 0-3 and 4-7 are its position, 8-13 unknown (evenly spread), 14-15 a
    // variant (unknown), 16-18 its type, 19-22 a turn, 23 always set.
    struct Scenery {
        uint16_t bx = 0, by = 0; // its grid cell (block)
        uint8_t x = 0, y = 0;    // within the block, on a 16 x 16 grid in map orientation
        uint8_t type = 0;        // icewrld: 1 trees, 0 rocks?, 2 bushes?, 4 frn_spec? (guesses)
        uint8_t turn = 0;        // sixteenths of a turn, presumably anticlockwise
        uint8_t variant = 0;     // bits 14-15
        uint8_t extra = 0;       // bits 8-13
    };

    int width = 0, height = 0;                 // in blocks
    std::vector<Tile> tiles;
    std::vector<std::array<uint32_t, 4>> blocks; // four tile numbers each
    std::vector<Cell> cells;                   // width x height, row by row
    std::vector<Scenery> scenery;              // cell by cell, row by row
    // The colour table at the end of the file (the same in every icewrld
    // map), one RGB colour per terrain code; code 0 is magenta.
    std::vector<std::array<uint8_t, 3>> palette;
};

Error parseGmf(const std::vector<uint8_t>& data, GmfMap& out);

// The tile record at a tile position (x east, y north, from the map's
// south-west corner, 2 tiles per block), or -1 outside the map.
int tileAt(const GmfMap& map, int tx, int ty);

// How a map's model numbers count its archive's .geo files (full names, in
// archive order): returns, per model number, an index into geoNames, or
// kNoModel for a number with no model. Not archive order (which differs per
// planet) but one list of fixed places for all six planets (see
// docs/formats/newworlds/gmf.md): base_01-16, hole_01, base_21, base_22,
// base_20, base_18, crack_03-06, -, crack_08, brid_01-02, ramp, concrete,
// hole_03, hole_02, concact, base_19, base_23-26, base_17, -, then the
// planets' own models at 41-54 (balls, water, mountains, arch and needle,
// caves or lava fall, finger). A missing model leaves its place empty. Then
// the lod/ models, then any other main models (tree, rock, ...), both in
// archive order. "base" is the family numbered 01-26 (ice, alpin, des, vol,
// red, alien).
constexpr size_t kNoModel = size_t(-1);
std::vector<size_t> tileModelOrder(const std::vector<std::string>& geoNames);

} // namespace reluva::newworlds
