// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Collision against a level's geometry: its BSP says which space is solid
// (the brushes the level was carved from) and which open. Points are
// classified down the tree; boxes and lines are swept through it, each
// plane widened by the box's reach along its normal (without the bevel
// planes UE1 keeps in its leaf hulls, so a box can stop a little early
// at sharp outside corners). See docs/formats/unreal/collision.md.

#include "engines/unreal/model.h"

namespace reluva::unreal {

class LevelCollision {
public:
    explicit LevelCollision(const Model& model) : model_(model) {}

    // Whether a point is in solid space.
    bool solid(const Vec3f& point) const;
    // Whether a box (centre, half sizes) overlaps solid space.
    bool solid(const Vec3f& centre, const Vec3f& extent) const;

    struct Hit {
        float time = 1;    // 0-1 along the move where it stopped; 1: nothing hit
        Vec3f normal{};    // of the surface hit, facing back along the move
        Vec3f location{};  // where the box's centre stopped
        bool startSolid = false; // it started overlapping solid space
        bool hit() const { return time < 1 || startSolid; }
    };
    // Sweeps a box (half sizes `extent`; zero for a line) from start to end.
    Hit trace(const Vec3f& start, const Vec3f& end, const Vec3f& extent = {}) const;

private:
    struct Sweep;
    void sweep(Sweep& s, int32_t node, float f0, float f1, bool outside, int entry) const;
    bool overlaps(int32_t node, const Vec3f& c, const Vec3f& e, bool outside) const;

    const Model& model_;
};

} // namespace reluva::unreal
