// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/newworlds/geo.h"

#include <cstdint>
#include <vector>

namespace reluva::newworlds {

// Where an animated part is: a rotation and a position in the model's space.
// The same 58-byte record is used by .arf frames, a .mdl's reference pose
// and a .geo's attachments: 9 f32 rotation (row by row), f32 scale (1 in
// every file), then a chunk: u16 0x1c, u32 12, 3 f32 position.
struct PartTransform {
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float scale = 1;
    GeoModel::Vec3 position{0, 0, 0};
};

constexpr size_t kTransformSize = 58;

// Reads the record at data[at]; false if it isn't one (no position chunk).
bool readTransform(const std::vector<uint8_t>& data, size_t at, PartTransform& out);

// An .arf (chunk tag 0x25): one animation frame, u32 count and a transform
// per animated part, in the order of the .mdl's animated list
// (geo/kln/conbee00.arf ... conbee15.arf; geo/fed/fcmwalk0.arf ...).
Error parseArf(const std::vector<uint8_t>& data, std::vector<PartTransform>& out);

} // namespace reluva::newworlds
