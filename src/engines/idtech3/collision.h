// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

using Vec3f = std::array<float, 3>;

// Content flags (the shaders' surfaceparms, as the games use them).
namespace contents {
constexpr int32_t Solid = 1;
constexpr int32_t Lava = 8, Slime = 16, Water = 32, Fog = 64;
constexpr int32_t PlayerClip = 0x10000, MonsterClip = 0x20000;
constexpr int32_t Body = 0x2000000, Corpse = 0x4000000, Trigger = 0x40000000;
} // namespace contents

struct TraceResult {
    bool allSolid = false;   // the whole move is inside something solid
    bool startSolid = false; // it starts inside something solid
    float fraction = 1;      // how far it got: 1 = all the way
    Vec3f endPos{};
    Vec3f normal{};          // the plane hit
    float dist = 0;
    int32_t surfaceFlags = 0;
    int32_t contents = 0;    // of what was hit
};

// A map's solid parts for moving things through: a box (or a point) swept
// along a line is stopped by the brushes it meets, each a convex volume
// bounded by planes, found through the BSP tree. Brushes count when their
// contents share a bit with the mask. Curved surfaces (patches) collide
// too: each is cut into triangles once, each triangle a thin convex
// volume (its plane both ways, its edges, and its bounding box), found
// through the same leaves. See docs/formats/idtech3/bsp.md.
class CollisionWorld {
public:
    explicit CollisionWorld(const Bsp& bsp);

    // Model 0 is the world, others brush entities' (*1, *2...), traced
    // in their own coordinates. mins/maxs give the box around the moving
    // point (zeros for a point).
    TraceResult trace(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs, int model,
                      int32_t mask) const;
    // The contents of everything at a point.
    int32_t pointContents(const Vec3f& point, int model) const;
    // The leaf a point is in, for visibility.
    int leafAt(const Vec3f& point) const;
    // The leaves a box touches (inside or on their edges).
    void boxLeafs(const Vec3f& mins, const Vec3f& maxs, std::vector<int>& leafs) const;
    // The area a leaf is in (-1: none, as solid leaves).
    int leafArea(int leaf) const {
        return leaf >= 0 && size_t(leaf) < bsp_.leafs.size() ? bsp_.leafs[size_t(leaf)].area : -1;
    }
    // How many areas the map is cut into (by its area portals).
    int areaCount() const;
    bool modelBounds(int model, Vec3f& mins, Vec3f& maxs) const;
    int modelCount() const { return int(bsp_.models.size()); }
    const Bsp& bsp() const { return bsp_; }

    // A box of given contents, for entities that aren't brush models;
    // traced in the same way.
    static TraceResult traceBox(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                const Vec3f& boxMins, const Vec3f& boxMaxs, int32_t boxContents);

private:
    struct Work; // one trace's state
    void traceNode(Work& w, int node) const;
    void traceBrush(Work& w, int brush) const;
    void tracePatch(Work& w, int surface) const;
    int32_t brushContents(int brush) const;
    void buildPatches();

    // A triangle of a patch, as planes bounding it.
    // (Its planes kept in place: a trace near a curve tests many facets.)
    struct Facet {
        std::array<BspPlane, 11> planes{}; // its plane both ways, 3 edges, 6 box sides
        int planeCount = 0;
        Vec3f mins{}, maxs{};
        void add(const BspPlane& p) { planes[size_t(planeCount++)] = p; }
    };
    // Neighbouring facets (as tessellated, a run along a row) under one
    // box, and runs of those under another, so a trace skips most of a
    // big curve at once.
    struct FacetGroup {
        Vec3f mins{}, maxs{};
        size_t first = 0, count = 0; // facets, or (for the outer level) runs
    };
    static constexpr size_t kFacetRun = 16;
    struct PatchCollide {
        int32_t contents = 0, surfaceFlags = 0;
        Vec3f mins{}, maxs{};
        size_t firstFacet = 0, numFacets = 0;
        size_t firstGroup = 0, numGroups = 0; // outer groups, in groups_
    };
    std::vector<FacetGroup> runs_;   // facets, kFacetRun at most each
    std::vector<FacetGroup> groups_; // runs, kFacetRun at most each

    const Bsp& bsp_;
    mutable std::vector<uint32_t> checked_;        // per brush: the trace that last tested it
    mutable std::vector<uint32_t> checkedPatches_; // per surface, likewise
    mutable uint32_t traceCount_ = 0;
    std::vector<Facet> facets_;
    std::vector<int> patchOf_; // per surface: its PatchCollide, -1 none
    std::vector<PatchCollide> patches_;
};

// Which of a map's areas join up, as the original keeps it: areas meet
// only through area portals (where doors stand), each closed until the
// game opens it (as many times as it's opened, less times closed); areas
// joined through open ones, step by step, are connected.
class AreaPortals {
public:
    explicit AreaPortals(int areaCount);
    // The portal between two areas opened, or closed again.
    void adjust(int area1, int area2, bool open);
    // Whether the areas join up (never for -1).
    bool connected(int area1, int area2) const;

private:
    void flood();
    int count_ = 0;
    std::vector<int> open_;  // count_ x count_: times each portal is open
    std::vector<int> group_; // by area: areas with the same are connected
};

} // namespace reluva::idtech3
