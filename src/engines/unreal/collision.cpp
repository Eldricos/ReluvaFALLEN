// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/collision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
// CSG node's plane is open space, to its back solid; other nodes leave it
// as it was. A CSG node has vertices and neither flag 0x01 (not CSG) nor
// 0x20 (as the engine's collision checks it).
bool isCsg(const BspNode& n) { return n.vertexCount > 0 && !(n.flags & (kNotCsg | 0x20)); }
bool outsideAfter(bool outside, const BspNode& n, bool front) {
    const bool csg = isCsg(n);
    return front ? (outside || csg) : (outside && !csg);
}

// The box's reach along a normal (the engine's push-out), exact.
float pushOut(const Vec3f& n, const Vec3f& e) {
    return std::fabs(n[0] * e[0]) + std::fabs(n[1] * e[1]) + std::fabs(n[2] * e[2]);
}
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
constexpr float kWiden = 1.1f; // the walk's box, a little bigger than the actor's

} // namespace

bool LevelCollision::csg(const BspNode& n) const { return isCsg(n); }

bool LevelCollision::hull(int32_t node, std::vector<Plane>& planes) const {
    planes.clear();
    if (node < 0 || size_t(node) >= model_.nodes.size())
        return false;
    const int32_t at = model_.nodes[size_t(node)].collisionBound;
    const auto& h = model_.leafHulls;
    if (at < 0 || size_t(at) >= h.size())
        return false;
    size_t i = size_t(at);
    std::vector<uint8_t> signs; // per hull plane: its normal's sign on each axis (1 -, 2 + for X; 4, 8 Y; 16, 32 Z)
    for (; i < h.size() && h[i] != -1 && planes.size() < 64; ++i) {
        const int32_t index = h[i] & ~0x40000000;
        if (index < 0 || size_t(index) >= model_.nodes.size())
            return false;
        const auto& p = model_.nodes[size_t(index)].plane;
        const float s = (h[i] & 0x40000000) ? -1.0f : 1.0f;
        planes.push_back({{p[0] * s, p[1] * s, p[2] * s}, p[3] * s, index});
        uint8_t m = 0;
        for (int k = 0; k < 3; ++k)
            m |= uint8_t((planes.back().n[size_t(k)] < 0 ? 1 : planes.back().n[size_t(k)] > 0 ? 2 : 0) << (2 * k));
        signs.push_back(m);
    }
    while (i < h.size() && h[i] != -1)
        ++i;
    if (i + 6 >= h.size())
        return false;
    float box[6];
    for (int k = 0; k < 6; ++k) {
        const int32_t bits = h[i + 1 + size_t(k)];
        std::memcpy(&box[k], &bits, 4);
    }
    const size_t hullPlanes = planes.size();
    // The bounding box's faces, 0.1 out.
    for (int k = 0; k < 3; ++k) {
        Vec3f lo{}, hi{};
        lo[size_t(k)] = -1;
        hi[size_t(k)] = 1;
        planes.push_back({lo, 0.1f - box[k], -1});
        planes.push_back({hi, box[3 + k] + 0.1f, -1});
    }
    // Bevels along the edges between two of its planes that face opposite
    // ways along an axis but the same way across it: a plane through
    // their line of meeting, square to that axis, facing as the first.
    for (size_t a = 0; a < hullPlanes; ++a)
        for (size_t b = 0; b < hullPlanes; ++b) {
            const uint8_t both = signs[a] | signs[b];
            for (int k = 0; k < 3; ++k) {
                if (((both >> (2 * k)) & 3) != 3)
                    continue;
                Vec3f axis{};
                axis[size_t(k)] = 1;
                const Vec3f& na = planes[a].n;
                const Vec3f& nb = planes[b].n;
                if (dot(cross(axis, na), cross(axis, nb)) <= 0.001f)
                    continue;
                // Their line: along na x nb, through a point on both.
                const Vec3f dir = cross(na, nb);
                const float dd = dot(dir, dir);
                if (dd < 1e-12f)
                    continue;
                const Vec3f t1 = cross(nb, dir), t2 = cross(dir, na);
                const Vec3f point{(planes[a].w * t1[0] + planes[b].w * t2[0]) / dd,
                                  (planes[a].w * t1[1] + planes[b].w * t2[1]) / dd,
                                  (planes[a].w * t1[2] + planes[b].w * t2[2]) / dd};
                Vec3f n = cross(axis, dir);
                const float len = std::sqrt(dot(n, n));
                if (len < 1e-9f)
                    continue;
                n = {n[0] / len, n[1] / len, n[2] / len};
                if (dot(n, na) < 0)
                    n = {-n[0], -n[1], -n[2]};
                planes.push_back({n, dot(n, point), -1});
            }
        }
    return true;
}

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
    const Vec3f wide{e[0] * kWiden, e[1] * kWiden, e[2] * kWiden};
    const bool box = e[0] > 0 || e[1] > 0 || e[2] > 0;
    const float d = distance(n, c), r = box ? pushOut({n.plane[0], n.plane[1], n.plane[2]}, wide) : 0;
    for (int side = 0; side < 2; ++side) {
        const bool front = side == 0;
        if (box ? ((front && d <= -r) || (!front && d >= r)) : ((front && d < 0) || (!front && d >= 0)))
            continue; // the box is wholly on the other side
        const bool out = outsideAfter(outside, n, front);
        const int32_t child = front ? n.front : n.back;
        if (child <= 0 || size_t(child) >= model_.nodes.size()) {
            if (!out && (!box || hullOverlaps(at, c, e)))
                return true;
        } else if (overlaps(child, c, e, out)) {
            return true;
        }
    }
    return false;
}

const std::vector<LevelCollision::Plane>* LevelCollision::hullOf(int32_t node) const {
    if (node < 0 || size_t(node) >= model_.nodes.size())
        return nullptr;
    if (hullState_.size() != model_.nodes.size()) {
        hullState_.assign(model_.nodes.size(), 0);
        hulls_.assign(model_.nodes.size(), {});
    }
    uint8_t& state = hullState_[size_t(node)];
    if (state == 0)
        state = hull(node, hulls_[size_t(node)]) ? 1 : 2;
    return state == 1 ? &hulls_[size_t(node)] : nullptr;
}

bool LevelCollision::hullOverlaps(int32_t node, const Vec3f& c, const Vec3f& e) const {
    const std::vector<Plane>* planes = hullOf(node);
    if (!planes)
        return false; // no hull: nothing there to hit
    for (const Plane& p : *planes)
        if (dot(p.n, c) - p.w >= pushOut(p.n, e))
            return false;
    return true;
}

bool LevelCollision::solid(const Vec3f& c, const Vec3f& e) const {
    if (model_.nodes.empty())
        return false;
    return overlaps(0, c, e, model_.rootOutside);
}

struct LevelCollision::Sweep {
    Vec3f start{}, end{}, extent{};
    Vec3f hullNormal{}; // the hull plane hit, when bestEntry says so
    int32_t hullNode = -1;
    float best = 2;      // the earliest time solid space was entered
    int bestEntry = 0;   // and across which plane: +(node+1) into its back, -(node+1) into its front
};

// The part of the move [f0, f1] on each side of a node's plane (the box
// partly on that side), each side's leaves or nodes visited in turn.
void LevelCollision::sweep(Sweep& s, int32_t at, float f0, float f1, bool outside, int entry) const {
    if (f0 >= s.best)
        return;
    const BspNode& n = model_.nodes[size_t(at)];
    const bool box = s.extent[0] > 0 || s.extent[1] > 0 || s.extent[2] > 0;
    const Vec3f wide{s.extent[0] * kWiden, s.extent[1] * kWiden, s.extent[2] * kWiden};
    const float d0 = distance(n, s.start), d1 = distance(n, s.end),
                r = box ? pushOut({n.plane[0], n.plane[1], n.plane[2]}, wide) : reach(n, s.extent);
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
            if (!out && box)
                clipHull(s, at);
            else if (!out && a < s.best) {
                s.best = a;
                s.bestEntry = sideEntry;
            }
        } else {
            sweep(s, child, a, b, out, sideEntry);
        }
    }
}

// The move clipped against a node's hull: when the box enters the last of
// its planes and leaves the first; a hit if it enters (after -1) before
// it leaves, the plane entered last the one hit. A hull it starts in and
// never enters doesn't stop it, so it can move out.
void LevelCollision::clipHull(Sweep& s, int32_t node) const {
    const std::vector<Plane>* hullPlanes = hullOf(node);
    if (!hullPlanes)
        return;
    const std::vector<Plane>& planes = *hullPlanes;
    float t0 = -1, t1 = std::min(1.0f, s.best);
    const Plane* entered = nullptr;
    for (const Plane& p : planes) {
        const float push = pushOut(p.n, s.extent);
        const float ds = dot(p.n, s.start) - p.w, de = dot(p.n, s.end) - p.w;
        const float rate = ds - de;
        if (rate > 1e-5f) { // moving in
            const float t = (ds > 0 && ds < push ? 0.0f : ds - push) / rate;
            if (t > t0) {
                t0 = t;
                entered = &p;
            }
        } else if (rate < -1e-5f) { // moving out
            t1 = std::min(t1, (ds - push) / rate);
        } else if (ds > push && de > push) {
            return; // alongside it, outside
        }
        if (!(t0 < t1))
            return;
    }
    if (t0 <= -1 || !(t0 < t1) || t1 <= 0 || !entered)
        return;
    const float t = std::max(0.0f, t0);
    if (t < s.best) {
        s.best = t;
        s.hullNormal = entered->n;
        s.hullNode = entered->node >= 0 ? entered->node : node;
        s.bestEntry = 0x7fffffff; // from a hull
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
    if (s.bestEntry == 0x7fffffff) {
        hit.node = s.hullNode;
        hit.normal = s.hullNormal;
    } else {
        hit.node = std::abs(s.bestEntry) - 1;
        const BspNode& n = model_.nodes[size_t(hit.node)];
        const float sign = s.bestEntry > 0 ? 1.0f : -1.0f;
        hit.normal = {n.plane[0] * sign, n.plane[1] * sign, n.plane[2] * sign};
    }
    // Stopped a little short of the surface (0.1 units from its plane, not
    // along the move: moving at a grazing angle, backing off along the
    // move hardly leaves it), so the box isn't left touching it.
    const float approach = -(hit.normal[0] * delta[0] + hit.normal[1] * delta[1] + hit.normal[2] * delta[2]);
    hit.time = length > 0 && approach > 0 ? std::max(0.0f, s.best - 0.1f / approach) : 0;
    hit.location = {start[0] + delta[0] * hit.time, start[1] + delta[1] * hit.time, start[2] + delta[2] * hit.time};
    return hit;
}

} // namespace reluva::unreal
