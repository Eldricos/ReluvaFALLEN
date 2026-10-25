// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/arf.h"

#include <cstring>
#include <string>

namespace reluva::newworlds {

namespace {

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

float f32(const std::vector<uint8_t>& d, size_t o) {
    const uint32_t v = u32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

bool readTransform(const std::vector<uint8_t>& d, size_t at, PartTransform& out) {
    if (at > d.size() || d.size() - at < kTransformSize)
        return false;
    // The position chunk: u16 0x1c, u32 12.
    if (d[at + 40] != 0x1c || d[at + 41] != 0 || u32(d, at + 42) != 12)
        return false;
    for (int i = 0; i < 9; ++i)
        out.rotation[i] = f32(d, at + 4 * size_t(i));
    out.scale = f32(d, at + 36);
    out.position = {f32(d, at + 46), f32(d, at + 50), f32(d, at + 54)};
    return true;
}

Error parseArf(const std::vector<uint8_t>& data, std::vector<PartTransform>& out) {
    out.clear();
    if (data.size() < 4)
        return Error::fail("frame too small");
    const uint32_t count = u32(data, 0);
    if (count > 1024 || data.size() != 4 + size_t(count) * kTransformSize)
        return Error::fail("frame of " + std::to_string(data.size()) + " bytes doesn't hold " +
                           std::to_string(count) + " transforms");
    for (uint32_t i = 0; i < count; ++i) {
        PartTransform t;
        if (!readTransform(data, 4 + size_t(i) * kTransformSize, t))
            return Error::fail("transform " + std::to_string(i) + " has no position");
        out.push_back(t);
    }
    return Error::success();
}

} // namespace reluva::newworlds
