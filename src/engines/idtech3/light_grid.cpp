// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The grid's layout and how the original samples it were read from the
// GPL Quake III Arena renderer (tr_bsp.c R_LoadLightGrid, tr_light.c);
// docs/formats/idtech3/bsp.md describes it.

#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/world_renderer.h" // brightenLight

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr float kMinimumAmbient = 32;

} // namespace

void LightGrid::build(const Bsp& bsp, std::array<float, 3> cellSize) {
    data_.clear();
    if (bsp.models.empty() || bsp.lightGrid.empty())
        return;
    size_ = cellSize;
    // Grid points on multiples of the cell size inside the world's bounds.
    for (int i = 0; i < 3; ++i) {
        const size_t k = size_t(i);
        if (size_[k] <= 0)
            return;
        origin_[k] = size_[k] * std::ceil(bsp.models[0].mins[k] / size_[k]);
        const float top = size_[k] * std::floor(bsp.models[0].maxs[k] / size_[k]);
        count_[k] = int((top - origin_[k]) / size_[k]) + 1;
        if (count_[k] < 1)
            return;
    }
    if (size_t(count_[0]) * size_t(count_[1]) * size_t(count_[2]) * 8 != bsp.lightGrid.size())
        return;
    data_ = bsp.lightGrid;
    for (size_t i = 0; i < data_.size(); i += 8) {
        brightenLight(&data_[i]);
        brightenLight(&data_[i + 3]);
    }
}

PointLight LightGrid::sample(const std::array<float, 3>& pos) const {
    PointLight light;
    if (data_.empty()) {
        light.ambient = {150 + kMinimumAmbient, 150 + kMinimumAmbient, 150 + kMinimumAmbient};
        light.directed = {150, 150, 150};
        const float n = std::sqrt(0.45f * 0.45f + 0.3f * 0.3f + 0.9f * 0.9f);
        light.direction = {0.45f / n, 0.3f / n, 0.9f / n};
        return light;
    }
    int cell[3];
    float frac[3];
    for (int i = 0; i < 3; ++i) {
        const float v = (pos[size_t(i)] - origin_[size_t(i)]) / size_[size_t(i)];
        const float f = std::floor(v);
        frac[i] = v - f;
        cell[i] = std::clamp(int(f), 0, count_[size_t(i)] - 1);
    }
    const size_t step[3] = {8, 8 * size_t(count_[0]), 8 * size_t(count_[0]) * size_t(count_[1])};
    float total = 0;
    std::array<float, 3> dir{};
    for (int corner = 0; corner < 8; ++corner) {
        float weight = 1;
        size_t at = size_t(cell[0]) * step[0] + size_t(cell[1]) * step[1] + size_t(cell[2]) * step[2];
        bool inside = true;
        for (int j = 0; j < 3; ++j) {
            if (corner & (1 << j)) {
                if (cell[j] + 1 > count_[size_t(j)] - 1) {
                    inside = false; // past the grid's edge
                    break;
                }
                weight *= frac[j];
                at += step[j];
            } else {
                weight *= 1 - frac[j];
            }
        }
        if (!inside)
            continue;
        const uint8_t* p = &data_[at];
        if (p[0] + p[1] + p[2] == 0)
            continue; // inside a wall
        total += weight;
        for (int c = 0; c < 3; ++c) {
            light.ambient[size_t(c)] += weight * p[c];
            light.directed[size_t(c)] += weight * p[3 + c];
        }
        // Direction as two angles: longitude (byte 6) from +Z, latitude
        // (byte 7) around it.
        const float lat = float(p[7]) * kTau / 256, lng = float(p[6]) * kTau / 256;
        const std::array<float, 3> n{std::cos(lat) * std::sin(lng), std::sin(lat) * std::sin(lng), std::cos(lng)};
        for (int c = 0; c < 3; ++c)
            dir[size_t(c)] += weight * n[size_t(c)];
    }
    if (total > 0 && total < 0.99f)
        for (int c = 0; c < 3; ++c) {
            light.ambient[size_t(c)] /= total;
            light.directed[size_t(c)] /= total;
        }
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 0)
        light.direction = {dir[0] / len, dir[1] / len, dir[2] / len};
    for (float& a : light.ambient)
        a = std::min(255.0f, a + kMinimumAmbient);
    return light;
}

} // namespace reluva::idtech3
