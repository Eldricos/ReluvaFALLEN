// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Which of a level's zones the camera can see. The zones' stored
// visibility (Zone::visibility, Leaf::visibleZones) has every bit set in
// all of The Fallen's maps, so it says nothing; as UE1 did, this works it
// out each frame from the zone portals: from the camera's zone, through
// each portal it can see into the zone beyond, the view narrowed to the
// portal's rectangle on screen as it goes. See
// docs/formats/unreal/model.md.

#include "engines/unreal/model.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::unreal {

class ZoneVisibility {
public:
    void build(const Model& model);
    // The zone a point is in (0 with no BSP); `solid` set if it's inside
    // solid, in no leaf.
    int zoneAt(const Vec3f& p, bool* solid = nullptr) const;
    // Bit z set for each zone seen from `eye` with `clipFromWorld` (Unreal
    // coordinates to clip space, column-major): every bit when the level
    // has no portals or the eye is in solid, outside it.
    uint64_t visible(const Vec3f& eye, const std::array<float, 16>& clipFromWorld) const;
    size_t portalCount() const { return portals_.size(); }

private:
    struct Node {
        std::array<float, 4> plane{};
        int32_t front = 0, back = 0;
        uint8_t zone[2] = {};
        int32_t leaf[2] = {};
    };
    std::vector<Node> nodes_;
    struct Portal {
        uint8_t zone[2] = {};
        std::vector<Vec3f> points;
        Vec3f min{}, max{};
    };
    std::vector<Portal> portals_;
    std::array<std::vector<uint32_t>, 64> byZone_; // the portals on each zone
};

} // namespace reluva::unreal
