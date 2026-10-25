// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// The light at a point, for lighting models: an ambient colour, a
// directed colour and the direction it comes from (towards the light),
// colours 0-255.
struct PointLight {
    std::array<float, 3> ambient{};
    std::array<float, 3> directed{};
    std::array<float, 3> direction{0, 0, 1};
};

// A map's light grid (see docs/formats/idtech3/bsp.md): light sampled on
// a regular grid over the world, by default every 64 x 64 x 128 units.
class LightGrid {
public:
    // `cellSize` from the worldspawn's "gridsize" key, if it has one. A
    // grid whose size doesn't match the world's bounds is left empty.
    void build(const Bsp& bsp, std::array<float, 3> cellSize = {64, 64, 128});
    bool empty() const { return data_.empty(); }

    // Blends the eight surrounding grid points, skipping those inside walls
    // (all black), as the original does; adds its minimum ambient light.
    // Without a grid: a fixed light from above.
    PointLight sample(const std::array<float, 3>& pos) const;

private:
    std::array<float, 3> origin_{}, size_{64, 64, 128};
    std::array<int, 3> count_{};
    std::vector<uint8_t> data_; // 8 bytes per point, colours already brightened
};

} // namespace reluva::idtech3
