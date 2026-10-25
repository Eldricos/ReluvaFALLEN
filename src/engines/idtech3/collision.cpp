// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/collision.h"
#include "engines/idtech3/patch.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

// Moves stop this far short of what they hit, so the next move doesn't
// start inside it (the original's value).
constexpr float kClipEpsilon = 0.125f;

float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

// A sweep of a box centred on the line start-end (the box made symmetric,
// the line moved to its centre), and what it hit so far.
struct CollisionWorld::Work {
    Vec3f start{}, end{};
    Vec3f extents{}; // half the box's size
    Vec3f centre{};  // the box's centre relative to the traced point
    int32_t mask = 0;
    TraceResult result;
    uint32_t id = 0;

    // How far the box reaches beyond its centre along a plane's normal.
    float offset(const Vec3f& n) const {
        return std::abs(n[0]) * extents[0] + std::abs(n[1]) * extents[1] + std::abs(n[2]) * extents[2];
    }
};

CollisionWorld::CollisionWorld(const Bsp& bsp)
    : bsp_(bsp), checked_(bsp.brushes.size(), 0), checkedPatches_(bsp.surfaces.size(), 0) {
    buildPatches();
}

void CollisionWorld::buildPatches() {
    // Each solid patch (its shader's contents not empty) cut into
    // triangles, finer the more its control points bend, each triangle
    // kept as planes. A curve cut into n pieces strays from its chords by
    // about its bend / (2 n^2): twice the square root of the largest bend
    // in units (2 to 16 cuts per 3x3 block) keeps that under a tenth of a
    // unit, so sliding along a pillar doesn't bump over its facets.
    patchOf_.assign(bsp_.surfaces.size(), -1);
    auto sub = [](const Vec3f& a, const Vec3f& b) { return Vec3f{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto cross = [](const Vec3f& a, const Vec3f& b) {
        return Vec3f{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    for (size_t s = 0; s < bsp_.surfaces.size(); ++s) {
        const BspSurface& surf = bsp_.surfaces[s];
        if (surf.type != SurfaceType::Patch || surf.patchWidth < 3 || surf.patchHeight < 3)
            continue;
        if (surf.shader < 0 || size_t(surf.shader) >= bsp_.shaders.size())
            continue;
        const BspShader& shader = bsp_.shaders[size_t(surf.shader)];
        if (!shader.contentFlags)
            continue;
        const BspVertex* control = &bsp_.vertices[size_t(surf.firstVertex)];
        const int w = surf.patchWidth, h = surf.patchHeight;
        // Within a sixteenth of a unit of round (2 * sqrt(bend) cuts).
        const int level = patchLevel(control, w, h, 0.125f);
        std::vector<BspVertex> verts;
        std::vector<uint32_t> indices;
        tessellatePatch(control, w, h, level, verts, indices);
        PatchCollide pc;
        pc.contents = shader.contentFlags;
        pc.surfaceFlags = shader.surfaceFlags;
        pc.firstFacet = facets_.size();
        pc.mins = {1e9f, 1e9f, 1e9f};
        pc.maxs = {-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const Vec3f p[3] = {verts[indices[i]].pos, verts[indices[i + 1]].pos, verts[indices[i + 2]].pos};
            Vec3f n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
            const float len = std::sqrt(dot(n, n));
            if (len < 1e-4f)
                continue; // degenerate
            for (float& c : n)
                c /= len;
            Facet f;
            // Its plane both ways, half a unit thick, so even a point
            // can't pass between them.
            const float d = dot(n, p[0]);
            f.add({n, d + 0.5f});
            f.add({{-n[0], -n[1], -n[2]}, -d + 0.5f});
            // Its edges, facing out, a tenth of a unit wide of them: the
            // edge planes lean with the triangle, so where two meet at an
            // angle a thin wedge would be left between them on the inside
            // of the bend (half a unit deep, as the triangles are thick).
            // Cut as finely as above, neighbours meet at small angles, so
            // that covers it, and the ridge it leaves on the outside of a
            // curve is about a hundredth of a unit: nothing to catch on.
            for (int e = 0; e < 3; ++e) {
                const Vec3f& a = p[e];
                const Vec3f& b = p[(e + 1) % 3];
                Vec3f en = cross(sub(b, a), n);
                const float el = std::sqrt(dot(en, en));
                if (el < 1e-6f)
                    continue;
                for (float& c : en)
                    c /= el;
                if (dot(en, p[(e + 2) % 3]) - dot(en, a) > 0)
                    for (float& c : en)
                        c = -c;
                f.add({en, dot(en, a) + 0.1f});
            }
            // And its bounding box, so a box's corners don't get past
            // its edges' planes.
            for (int axis = 0; axis < 3; ++axis) {
                const float lo = std::min({p[0][size_t(axis)], p[1][size_t(axis)], p[2][size_t(axis)]});
                const float hi = std::max({p[0][size_t(axis)], p[1][size_t(axis)], p[2][size_t(axis)]});
                f.mins[size_t(axis)] = lo - 0.5f;
                f.maxs[size_t(axis)] = hi + 0.5f;
                BspPlane up, down;
                up.normal[size_t(axis)] = 1;
                up.dist = hi + 0.5f;
                down.normal[size_t(axis)] = -1;
                down.dist = -(lo - 0.5f);
                f.add(up);
                f.add(down);
                pc.mins[size_t(axis)] = std::min(pc.mins[size_t(axis)], f.mins[size_t(axis)]);
                pc.maxs[size_t(axis)] = std::max(pc.maxs[size_t(axis)], f.maxs[size_t(axis)]);
            }
            facets_.push_back(std::move(f));
        }
        pc.numFacets = facets_.size() - pc.firstFacet;
        // The facets in runs under boxes, and the runs likewise.
        auto bound = [](FacetGroup& g, const Vec3f& mins, const Vec3f& maxs) {
            for (size_t k = 0; k < 3; ++k) {
                g.mins[k] = std::min(g.mins[k], mins[k]);
                g.maxs[k] = std::max(g.maxs[k], maxs[k]);
            }
        };
        const size_t firstRun = runs_.size();
        for (size_t f = pc.firstFacet; f < pc.firstFacet + pc.numFacets; f += kFacetRun) {
            FacetGroup run{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, f,
                           std::min(kFacetRun, pc.firstFacet + pc.numFacets - f)};
            for (size_t i = run.first; i < run.first + run.count; ++i)
                bound(run, facets_[i].mins, facets_[i].maxs);
            runs_.push_back(run);
        }
        pc.firstGroup = groups_.size();
        for (size_t r = firstRun; r < runs_.size(); r += kFacetRun) {
            FacetGroup group{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, r, std::min(kFacetRun, runs_.size() - r)};
            for (size_t i = group.first; i < group.first + group.count; ++i)
                bound(group, runs_[i].mins, runs_[i].maxs);
            groups_.push_back(group);
        }
        pc.numGroups = groups_.size() - pc.firstGroup;
        if (pc.numFacets) {
            patchOf_[s] = int(patches_.size());
            patches_.push_back(pc);
        }
    }
}

void CollisionWorld::tracePatch(Work& w, int surface) const {
    const int index = patchOf_[size_t(surface)];
    if (index < 0)
        return;
    const PatchCollide& pc = patches_[size_t(index)];
    if (!(pc.contents & w.mask))
        return;
    // The swept box's bounds, to skip what it can't reach.
    Vec3f lo{}, hi{};
    for (size_t i = 0; i < 3; ++i) {
        lo[i] = std::min(w.start[i], w.end[i]) - w.extents[i] - 1;
        hi[i] = std::max(w.start[i], w.end[i]) + w.extents[i] + 1;
    }
    auto apart = [&](const Vec3f& mins, const Vec3f& maxs) {
        for (size_t i = 0; i < 3; ++i)
            if (mins[i] > hi[i] || maxs[i] < lo[i])
                return true;
        return false;
    };
    if (apart(pc.mins, pc.maxs))
        return;
    // Each facet as a brush's sides (traceBrush).
    auto testFacet = [&](const Facet& facet) {
        float enter = -1, leave = 1;
        const BspPlane* hitPlane = nullptr;
        bool startsOut = false;
        for (int pi = 0; pi < facet.planeCount; ++pi) {
            const BspPlane& p = facet.planes[size_t(pi)];
            const float dist = p.dist + w.offset(p.normal);
            const float d1 = dot(w.start, p.normal) - dist;
            const float d2 = dot(w.end, p.normal) - dist;
            startsOut |= d1 > 0;
            if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1))
                return; // outside it all the way
            if (d1 <= 0 && d2 <= 0)
                continue;
            if (d1 > d2) {
                const float frac = (d1 - kClipEpsilon) / (d1 - d2);
                if (frac > enter) {
                    enter = frac;
                    hitPlane = &p;
                }
            } else {
                leave = std::min(leave, (d1 + kClipEpsilon) / (d1 - d2));
            }
        }
        // Starting inside a patch (standing on one's edge): not held there,
        // as the original's patches only stop what enters them.
        if (!startsOut)
            return;
        if (enter < leave && enter > -1 && enter < w.result.fraction && hitPlane) {
            w.result.fraction = std::max(enter, 0.0f);
            w.result.normal = hitPlane->normal;
            w.result.dist = hitPlane->dist;
            w.result.surfaceFlags = pc.surfaceFlags;
            w.result.contents = pc.contents;
        }
    };
    for (size_t g = pc.firstGroup; g < pc.firstGroup + pc.numGroups; ++g) {
        const FacetGroup& group = groups_[g];
        if (apart(group.mins, group.maxs))
            continue;
        for (size_t r = group.first; r < group.first + group.count; ++r) {
            const FacetGroup& run = runs_[r];
            if (apart(run.mins, run.maxs))
                continue;
            for (size_t f = run.first; f < run.first + run.count; ++f)
                if (!apart(facets_[f].mins, facets_[f].maxs))
                    testFacet(facets_[f]);
        }
    }
}

int32_t CollisionWorld::brushContents(int brush) const {
    const int shader = bsp_.brushes[size_t(brush)].shader;
    return shader >= 0 && size_t(shader) < bsp_.shaders.size() ? bsp_.shaders[size_t(shader)].contentFlags : 0;
}

void CollisionWorld::traceBrush(Work& w, int brush) const {
    const BspBrush& b = bsp_.brushes[size_t(brush)];
    if (b.numSides <= 0 || !(brushContents(brush) & w.mask))
        return;
    // Clip the line against each side's plane, pushed out by the box:
    // the line is inside the brush between the last plane it enters
    // and the first it leaves.
    float enter = -1, leave = 1;
    const BspBrushSide* hitSide = nullptr;
    bool startsOut = false, endsOut = false;
    for (int s = 0; s < b.numSides; ++s) {
        const BspBrushSide& side = bsp_.brushSides[size_t(b.firstSide + s)];
        const BspPlane& p = bsp_.planes[size_t(side.plane)];
        const float dist = p.dist + w.offset(p.normal);
        const float d1 = dot(w.start, p.normal) - dist;
        const float d2 = dot(w.end, p.normal) - dist;
        startsOut |= d1 > 0;
        endsOut |= d2 > 0;
        // Wholly in front of this plane: outside the brush.
        if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1))
            return;
        if (d1 <= 0 && d2 <= 0)
            continue;
        if (d1 > d2) { // entering
            const float f = (d1 - kClipEpsilon) / (d1 - d2);
            if (f > enter) {
                enter = f;
                hitSide = &side;
            }
        } else { // leaving
            const float f = (d1 + kClipEpsilon) / (d1 - d2);
            leave = std::min(leave, f);
        }
    }
    if (!startsOut) {
        w.result.startSolid = true;
        if (!endsOut) {
            w.result.allSolid = true;
            w.result.fraction = 0;
            w.result.contents = brushContents(brush);
        }
        return;
    }
    if (enter < leave && enter > -1 && enter < w.result.fraction && hitSide) {
        const BspPlane& p = bsp_.planes[size_t(hitSide->plane)];
        w.result.fraction = std::max(enter, 0.0f);
        w.result.normal = p.normal;
        w.result.dist = p.dist;
        const int shader = hitSide->shader >= 0 ? hitSide->shader : b.shader;
        w.result.surfaceFlags =
            shader >= 0 && size_t(shader) < bsp_.shaders.size() ? bsp_.shaders[size_t(shader)].surfaceFlags : 0;
        w.result.contents = brushContents(brush);
    }
}

void CollisionWorld::traceNode(Work& w, int node) const {
    while (node >= 0) {
        const BspNode& n = bsp_.nodes[size_t(node)];
        const BspPlane& p = bsp_.planes[size_t(n.plane)];
        const float offset = w.offset(p.normal) + 1; // a little more, for the clip epsilon
        const float d1 = dot(w.start, p.normal) - p.dist;
        const float d2 = dot(w.end, p.normal) - p.dist;
        if (d1 >= offset && d2 >= offset) {
            node = n.children[0];
        } else if (d1 < -offset && d2 < -offset) {
            node = n.children[1];
        } else {
            // The swept box crosses the plane: both sides.
            traceNode(w, n.children[0]);
            if (w.result.allSolid)
                return;
            node = n.children[1];
        }
    }
    const BspLeaf& leaf = bsp_.leafs[size_t(-1 - node)];
    for (int i = 0; i < leaf.numLeafBrushes; ++i) {
        const int brush = bsp_.leafBrushes[size_t(leaf.firstLeafBrush + i)];
        if (checked_[size_t(brush)] == w.id)
            continue; // already tested from another leaf
        checked_[size_t(brush)] = w.id;
        traceBrush(w, brush);
        if (w.result.allSolid)
            return;
    }
    if (patches_.empty())
        return;
    for (int i = 0; i < leaf.numLeafSurfaces; ++i) {
        const int surface = bsp_.leafSurfaces[size_t(leaf.firstLeafSurface + i)];
        if (surface < 0 || size_t(surface) >= patchOf_.size() || patchOf_[size_t(surface)] < 0 ||
            checkedPatches_[size_t(surface)] == w.id)
            continue;
        checkedPatches_[size_t(surface)] = w.id;
        tracePatch(w, surface);
    }
}

TraceResult CollisionWorld::trace(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                  int model, int32_t mask) const {
    Work w;
    for (int i = 0; i < 3; ++i) {
        w.centre[size_t(i)] = (mins[size_t(i)] + maxs[size_t(i)]) * 0.5f;
        w.extents[size_t(i)] = (maxs[size_t(i)] - mins[size_t(i)]) * 0.5f;
        w.start[size_t(i)] = start[size_t(i)] + w.centre[size_t(i)];
        w.end[size_t(i)] = end[size_t(i)] + w.centre[size_t(i)];
    }
    w.mask = mask;
    w.id = ++traceCount_;
    if (w.id == 0) { // wrapped: forget what was tested
        std::fill(checked_.begin(), checked_.end(), 0);
        std::fill(checkedPatches_.begin(), checkedPatches_.end(), 0);
        w.id = ++traceCount_;
    }
    if (model >= 0 && size_t(model) < bsp_.models.size()) {
        if (model == 0 && !bsp_.nodes.empty()) {
            traceNode(w, 0);
        } else {
            const BspModel& m = bsp_.models[size_t(model)];
            for (int i = 0; i < m.numBrushes && !w.result.allSolid; ++i)
                traceBrush(w, m.firstBrush + i);
            for (int i = 0; i < m.numSurfaces && !w.result.allSolid; ++i)
                if (const int surface = m.firstSurface + i; surface >= 0 && size_t(surface) < patchOf_.size())
                    tracePatch(w, surface);
        }
    }
    TraceResult r = w.result;
    for (int i = 0; i < 3; ++i)
        r.endPos[size_t(i)] = start[size_t(i)] + r.fraction * (end[size_t(i)] - start[size_t(i)]);
    return r;
}

TraceResult CollisionWorld::traceBox(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                     const Vec3f& boxMins, const Vec3f& boxMaxs, int32_t boxContents) {
    // The box as a brush of six axial planes, in a world of its own.
    Bsp bsp;
    bsp.shaders.push_back({"box", 0, boxContents});
    for (int axis = 0; axis < 3; ++axis) {
        BspPlane hi, lo;
        hi.normal[size_t(axis)] = 1;
        hi.dist = boxMaxs[size_t(axis)];
        lo.normal[size_t(axis)] = -1;
        lo.dist = -boxMins[size_t(axis)];
        bsp.planes.push_back(hi);
        bsp.planes.push_back(lo);
    }
    for (int i = 0; i < 6; ++i)
        bsp.brushSides.push_back({i, 0});
    bsp.brushes.push_back({0, 6, 0});
    BspModel model;
    model.numBrushes = 1;
    bsp.models.push_back(model); // model 0, without nodes: its brushes directly
    const CollisionWorld world(bsp);
    return world.trace(start, end, mins, maxs, 0, boxContents ? boxContents : -1);
}

int CollisionWorld::leafAt(const Vec3f& point) const {
    if (bsp_.nodes.empty())
        return bsp_.leafs.empty() ? -1 : 0;
    int node = 0;
    while (node >= 0) {
        const BspNode& n = bsp_.nodes[size_t(node)];
        const BspPlane& p = bsp_.planes[size_t(n.plane)];
        node = n.children[dot(point, p.normal) - p.dist >= 0 ? 0 : 1];
    }
    return -1 - node;
}

void CollisionWorld::boxLeafs(const Vec3f& mins, const Vec3f& maxs, std::vector<int>& leafs) const {
    if (bsp_.nodes.empty()) {
        if (!bsp_.leafs.empty())
            leafs.push_back(0);
        return;
    }
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        if (node < 0) {
            leafs.push_back(-1 - node);
            continue;
        }
        const BspNode& n = bsp_.nodes[size_t(node)];
        const BspPlane& p = bsp_.planes[size_t(n.plane)];
        // The box's nearest and farthest corners from the plane.
        float lo = -p.dist, hi = -p.dist;
        for (size_t k = 0; k < 3; ++k) {
            lo += p.normal[k] * (p.normal[k] >= 0 ? mins[k] : maxs[k]);
            hi += p.normal[k] * (p.normal[k] >= 0 ? maxs[k] : mins[k]);
        }
        if (hi >= 0)
            stack.push_back(n.children[0]);
        if (lo < 0)
            stack.push_back(n.children[1]);
    }
}

int CollisionWorld::areaCount() const {
    int count = 0;
    for (const BspLeaf& l : bsp_.leafs)
        count = std::max(count, l.area + 1);
    return count;
}

AreaPortals::AreaPortals(int areaCount)
    : count_(std::max(areaCount, 0)), open_(size_t(count_) * size_t(count_), 0), group_(size_t(count_), 0) {
    flood();
}

void AreaPortals::adjust(int area1, int area2, bool open) {
    if (area1 < 0 || area2 < 0 || area1 >= count_ || area2 >= count_)
        return;
    int& a = open_[size_t(area1) * size_t(count_) + size_t(area2)];
    int& b = open_[size_t(area2) * size_t(count_) + size_t(area1)];
    if (open) {
        ++a;
        ++b;
    } else if (a > 0) { // the original stops on closing a closed one
        --a;
        --b;
    }
    flood();
}

bool AreaPortals::connected(int area1, int area2) const {
    if (area1 < 0 || area2 < 0 || area1 >= count_ || area2 >= count_)
        return false;
    return group_[size_t(area1)] == group_[size_t(area2)];
}

void AreaPortals::flood() {
    std::fill(group_.begin(), group_.end(), -1);
    for (int start = 0; start < count_; ++start) {
        if (group_[size_t(start)] >= 0)
            continue;
        std::vector<int> stack{start};
        group_[size_t(start)] = start;
        while (!stack.empty()) {
            const int area = stack.back();
            stack.pop_back();
            for (int other = 0; other < count_; ++other)
                if (group_[size_t(other)] < 0 && open_[size_t(area) * size_t(count_) + size_t(other)] > 0) {
                    group_[size_t(other)] = start;
                    stack.push_back(other);
                }
        }
    }
}

int32_t CollisionWorld::pointContents(const Vec3f& point, int model) const {
    auto inside = [&](int brush) {
        const BspBrush& b = bsp_.brushes[size_t(brush)];
        for (int s = 0; s < b.numSides; ++s) {
            const BspPlane& p = bsp_.planes[size_t(bsp_.brushSides[size_t(b.firstSide + s)].plane)];
            if (dot(point, p.normal) - p.dist > 0)
                return false;
        }
        return b.numSides > 0;
    };
    int32_t result = 0;
    if (model < 0 || size_t(model) >= bsp_.models.size())
        return 0;
    if (model == 0 && !bsp_.nodes.empty()) {
        const int leaf = leafAt(point);
        if (leaf < 0)
            return 0;
        const BspLeaf& l = bsp_.leafs[size_t(leaf)];
        for (int i = 0; i < l.numLeafBrushes; ++i) {
            const int brush = bsp_.leafBrushes[size_t(l.firstLeafBrush + i)];
            if (inside(brush))
                result |= brushContents(brush);
        }
    } else {
        const BspModel& m = bsp_.models[size_t(model)];
        for (int i = 0; i < m.numBrushes; ++i)
            if (inside(m.firstBrush + i))
                result |= brushContents(m.firstBrush + i);
    }
    return result;
}

bool CollisionWorld::modelBounds(int model, Vec3f& mins, Vec3f& maxs) const {
    if (model < 0 || size_t(model) >= bsp_.models.size())
        return false;
    mins = bsp_.models[size_t(model)].mins;
    maxs = bsp_.models[size_t(model)].maxs;
    return true;
}

} // namespace reluva::idtech3
