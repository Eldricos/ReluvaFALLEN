// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Collision against a level's geometry, as Engine.dll's UModel PointCheck
// and LineCheck do it: the BSP walked with each plane widened by the box's
// reach (times 1.1) to the solid leaves it touches, and each of those
// tested against the convex hull stored for its node, its planes, its
// bounding box and bevels along its edges, at the box's true size. Lines
// and points: the BSP alone. See docs/formats/unreal/collision.md.

#include "engines/unreal/model.h"

#include <vector>

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
        int32_t node = -1;       // the node whose plane it stopped at (its coplanar ones share it)
        bool hit() const { return time < 1 || startSolid; }
    };
    // Sweeps a box (half sizes `extent`; zero for a line) from start to end.
    Hit trace(const Vec3f& start, const Vec3f& end, const Vec3f& extent = {}) const;

private:
    struct Sweep;
    struct Plane {
        Vec3f n{};
        float w = 0;
        int32_t node = -1; // the node it came from, -1 for the box's faces and bevels
    };
    void sweep(Sweep& s, int32_t node, float f0, float f1, bool outside, int entry) const;
    bool overlaps(int32_t node, const Vec3f& c, const Vec3f& e, bool outside) const;
    bool csg(const BspNode& n) const;
    bool hullOverlaps(int32_t node, const Vec3f& c, const Vec3f& e) const;
    void clipHull(Sweep& s, int32_t node) const;
    // A node's collision hull: its planes, its bounding box's faces (0.1
    // out) and the bevels along its edges; false if it has none.
    bool hull(int32_t node, std::vector<Plane>& planes) const;
    // Each node's hull, built once when first asked for.
    const std::vector<Plane>* hullOf(int32_t node) const;
    mutable std::vector<std::vector<Plane>> hulls_;
    mutable std::vector<uint8_t> hullState_; // 0 not built, 1 built, 2 none

    const Model& model_;
};

} // namespace reluva::unreal
