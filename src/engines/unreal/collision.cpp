// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/collision.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

constexpr uint8_t kNotCsg = 0x01; // a node from a non-solid brush: it doesn't divide solid from open
// A box counts as across a plane only this far past it: one just touching
// a wall (as a hit leaves it, 0.1 units off) slides along it without float
// noise putting it in.
constexpr float kTouch = 0.03f;

float distance(const BspNode& n, const Vec3f& p) {
    return n.plane[0] * p[0] + n.plane[1] * p[1] + n.plane[2] * p[2] - n.plane[3];
}

// How far a box reaches along a node's normal from its centre, less the
// touching tolerance (a line or point keeps none).
float reach(const BspNode& n, const Vec3f& e) {
    const float r = std::fabs(n.plane[0]) * e[0] + std::fabs(n.plane[1]) * e[1] + std::fabs(n.plane[2]) * e[2];
    return r > kTouch ? r - kTouch : r;
}

// Open space or solid, on a side of a node: crossing to the front of a
// CSG node's plane is open space, to its back solid; non-CSG nodes leave
// it as it was.
bool outsideAfter(bool outside, const BspNode& n, bool front) {
    const bool csg = !(n.flags & kNotCsg);
    return front ? (outside || csg) : (outside && !csg);
}

} // namespace

bool LevelCollision::solid(const Vec3f& p) const {
    if (model_.nodes.empty())
        return false;
    bool outside = model_.rootOutside;
    int32_t at = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        const BspNode& n = model_.nodes[size_t(at)];
        const bool front = distance(n, p) >= 0;
        outside = outsideAfter(outside, n, front);
        const int32_t child = front ? n.front : n.back;
        if (child <= 0 || size_t(child) >= model_.nodes.size())
            return !outside;
        at = child;
    }
    return false;
}

bool LevelCollision::overlaps(int32_t at, const Vec3f& c, const Vec3f& e, bool outside) const {
    const BspNode& n = model_.nodes[size_t(at)];
    const float d = distance(n, c), r = reach(n, e);
    for (int side = 0; side < 2; ++side) {
        const bool front = side == 0;
        if ((front && d <= -r) || (!front && d >= r))
            continue; // the box is wholly on the other side
        const bool out = outsideAfter(outside, n, front);
        const int32_t child = front ? n.front : n.back;
        if (child <= 0 || size_t(child) >= model_.nodes.size()) {
            if (!out)
                return true;
        } else if (overlaps(child, c, e, out)) {
            return true;
        }
    }
    return false;
}

bool LevelCollision::solid(const Vec3f& c, const Vec3f& e) const {
    if (model_.nodes.empty())
        return false;
    return overlaps(0, c, e, model_.rootOutside);
}

struct LevelCollision::Sweep {
    Vec3f start{}, end{}, extent{};
    float best = 2;      // the earliest time solid space was entered
    int bestEntry = 0;   // and across which plane: +(node+1) into its back, -(node+1) into its front
};

// The part of the move [f0, f1] on each side of a node's plane (the box
// partly on that side), each side's leaves or nodes visited in turn.
void LevelCollision::sweep(Sweep& s, int32_t at, float f0, float f1, bool outside, int entry) const {
    if (f0 >= s.best)
        return;
    const BspNode& n = model_.nodes[size_t(at)];
    const float d0 = distance(n, s.start), d1 = distance(n, s.end), r = reach(n, s.extent);
    const float dd = d1 - d0;
    for (int side = 0; side < 2; ++side) {
        const bool front = side == 0;
        // Where along the move the box is partly on this side:
        // front: d(f) > -r; back: d(f) < r, with d(f) = d0 + f * dd.
        float a = f0, b = f1;
        const float bound = front ? -r : r;
        if (std::fabs(dd) < 1e-9f) {
            if (front ? !(d0 > bound) : !(d0 < bound))
                continue;
        } else {
            const float cross = (bound - d0) / dd;
            const bool laterOnSide = front ? dd > 0 : dd < 0; // moving onto this side
            if (laterOnSide)
                a = std::max(a, cross);
            else
                b = std::min(b, cross);
            if (a > b)
                continue;
        }
        // Coming onto this side partway: this plane is where.
        const int sideEntry = a > f0 ? (front ? -(at + 1) : at + 1) : entry;
        const bool out = outsideAfter(outside, n, front);
        const int32_t child = front ? n.front : n.back;
        if (child <= 0 || size_t(child) >= model_.nodes.size()) {
            if (!out && a < s.best) {
                s.best = a;
                s.bestEntry = sideEntry;
            }
        } else {
            sweep(s, child, a, b, out, sideEntry);
        }
    }
}

LevelCollision::Hit LevelCollision::trace(const Vec3f& start, const Vec3f& end, const Vec3f& extent) const {
    Hit hit;
    hit.location = end;
    if (model_.nodes.empty())
        return hit;
    Sweep s;
    s.start = start;
    s.end = end;
    s.extent = extent;
    sweep(s, 0, 0, 1, model_.rootOutside, 0);
    if (s.best > 1)
        return hit;
    const Vec3f delta{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if (s.bestEntry == 0) {
        hit.startSolid = true;
        hit.time = 0;
        hit.location = start;
        return hit;
    }
    hit.node = std::abs(s.bestEntry) - 1;
    const BspNode& n = model_.nodes[size_t(hit.node)];
    const float sign = s.bestEntry > 0 ? 1.0f : -1.0f;
    hit.normal = {n.plane[0] * sign, n.plane[1] * sign, n.plane[2] * sign};
    // Stopped a little short of the surface (0.1 units from its plane, not
    // along the move: moving at a grazing angle, backing off along the
    // move hardly leaves it), so the box isn't left touching it.
    const float approach = -(hit.normal[0] * delta[0] + hit.normal[1] * delta[1] + hit.normal[2] * delta[2]);
    hit.time = length > 0 && approach > 0 ? std::max(0.0f, s.best - 0.1f / approach) : 0;
    hit.location = {start[0] + delta[0] * hit.time, start[1] + delta[1] * hit.time, start[2] + delta[2] * hit.time};
    return hit;
}

} // namespace reluva::unreal
