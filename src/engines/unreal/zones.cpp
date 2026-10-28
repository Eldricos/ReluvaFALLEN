// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/zones.h"

#include "engines/unreal/texture.h"

#include <algorithm>

namespace reluva::unreal {

namespace {

// How near a portal's box the eye can be and see all through it: walking
// through one, it's edge-on, its rectangle nothing, while the zone beyond
// fills half the view.
constexpr float kPortalMargin = 32;
// How thick a window's solid can be, to find what's beyond it.
constexpr float kWindowDepth = 512;

struct Rect {
    float x0 = 1, y0 = 1, x1 = -1, y1 = -1; // empty
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

Rect intersect(const Rect& a, const Rect& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

bool contains(const Rect& outer, const Rect& inner) {
    return outer.x0 <= inner.x0 && outer.y0 <= inner.y0 && outer.x1 >= inner.x1 && outer.y1 >= inner.y1;
}

// A polygon's rectangle on screen (-1 to 1 each way): its part in front
// of the eye, projected. Empty if none of it is.
Rect screenRect(const std::vector<Vec3f>& points, const std::array<float, 16>& m) {
    using Clip = std::array<float, 4>;
    std::vector<Clip> in, out;
    for (const Vec3f& p : points) {
        Clip c{};
        for (int row = 0; row < 4; ++row)
            c[size_t(row)] = m[size_t(row)] * p[0] + m[size_t(4 + row)] * p[1] + m[size_t(8 + row)] * p[2] +
                             m[size_t(12 + row)];
        in.push_back(c);
    }
    // Cut at w = kNear: what's behind the eye has no place on screen.
    constexpr float kNear = 0.01f;
    for (size_t i = 0; i < in.size(); ++i) {
        const Clip& a = in[i];
        const Clip& b = in[(i + 1) % in.size()];
        const float da = a[3] - kNear, db = b[3] - kNear;
        if (da >= 0)
            out.push_back(a);
        if ((da >= 0) != (db >= 0)) {
            const float t = da / (da - db);
            Clip c{};
            for (size_t k = 0; k < 4; ++k)
                c[k] = a[k] + (b[k] - a[k]) * t;
            out.push_back(c);
        }
    }
    Rect r;
    if (out.size() < 3)
        return r;
    r = {1e30f, 1e30f, -1e30f, -1e30f};
    for (const Clip& c : out) {
        const float x = c[0] / c[3], y = c[1] / c[3];
        r.x0 = std::min(r.x0, x);
        r.y0 = std::min(r.y0, y);
        r.x1 = std::max(r.x1, x);
        r.y1 = std::max(r.y1, y);
    }
    return intersect(r, Rect{-1, -1, 1, 1});
}

} // namespace

void ZoneVisibility::build(const Model& model) {
    nodes_.clear();
    portals_.clear();
    for (auto& list : byZone_)
        list.clear();
    nodes_.reserve(model.nodes.size());
    for (const BspNode& n : model.nodes) {
        Node node;
        node.plane = n.plane;
        node.front = n.front;
        node.back = n.back;
        node.zone[0] = n.zone[0];
        node.zone[1] = n.zone[1];
        node.leaf[0] = n.leaf[0];
        node.leaf[1] = n.leaf[1];
        nodes_.push_back(node);
    }
    auto add = [&](const BspNode& n, int a, int b) {
        Portal p;
        p.zone[0] = uint8_t(a & 63);
        p.zone[1] = uint8_t(b & 63);
        p.min = {1e30f, 1e30f, 1e30f};
        p.max = {-1e30f, -1e30f, -1e30f};
        for (int k = 0; k < n.vertexCount; ++k) {
            const size_t v = size_t(n.vertPool + k);
            if (v >= model.verts.size() || size_t(model.verts[v].point) >= model.points.size())
                return;
            const Vec3f& pt = model.points[size_t(model.verts[v].point)];
            p.points.push_back(pt);
            for (size_t c = 0; c < 3; ++c) {
                p.min[c] = std::min(p.min[c], pt[c]);
                p.max[c] = std::max(p.max[c], pt[c]);
            }
        }
        byZone_[p.zone[0]].push_back(uint32_t(portals_.size()));
        byZone_[p.zone[1]].push_back(uint32_t(portals_.size()));
        portals_.push_back(std::move(p));
    };
    for (const BspNode& n : model.nodes) {
        if (n.vertexCount < 3 || size_t(n.surf) >= model.surfs.size())
            continue;
        const uint32_t flags = model.surfs[size_t(n.surf)].polyFlags;
        // A portal: a polygon of a surface flagged as one, between two zones.
        if (flags & polyflag::Portal) {
            if ((n.zone[0] & 63) != (n.zone[1] & 63))
                add(n, n.zone[0], n.zone[1]);
            continue;
        }
        // A window: a solid surface seen through (glass, a grating, an
        // invisible wall) with another zone beyond its solid: found by
        // going through it until out of the solid again.
        if (!(flags & (polyflag::Translucent | polyflag::Masked | polyflag::Modulated | polyflag::Invisible)))
            continue;
        Vec3f centre{};
        int count = 0;
        for (int k = 0; k < n.vertexCount; ++k) {
            const size_t v = size_t(n.vertPool + k);
            if (v >= model.verts.size() || size_t(model.verts[v].point) >= model.points.size())
                break;
            const Vec3f& pt = model.points[size_t(model.verts[v].point)];
            for (size_t c = 0; c < 3; ++c)
                centre[c] += pt[c];
            ++count;
        }
        if (count < 3)
            continue;
        for (float& c : centre)
            c /= float(count);
        const int front = n.zone[1] & 63;
        for (float depth = 1; depth <= kWindowDepth; depth *= 1.5f) {
            const Vec3f at{centre[0] - n.plane[0] * depth, centre[1] - n.plane[1] * depth,
                           centre[2] - n.plane[2] * depth};
            bool solid = false;
            const int beyond = zoneAt(at, &solid);
            if (solid)
                continue;
            if (beyond != front)
                add(n, front, beyond);
            break;
        }
    }
}

int ZoneVisibility::zoneAt(const Vec3f& p, bool* solid) const {
    int zone = 0;
    bool inSolid = true;
    int32_t n = 0;
    for (size_t guard = 0; guard <= nodes_.size() && n >= 0 && size_t(n) < nodes_.size(); ++guard) {
        const Node& node = nodes_[size_t(n)];
        const bool front = node.plane[0] * p[0] + node.plane[1] * p[1] + node.plane[2] * p[2] - node.plane[3] >= 0;
        zone = node.zone[front ? 1 : 0];
        n = front ? node.front : node.back;
        if (n <= 0) {
            // No leaf there: inside solid.
            inSolid = node.leaf[front ? 1 : 0] < 0;
            break;
        }
    }
    if (solid)
        *solid = inSolid;
    return zone & 63;
}

uint64_t ZoneVisibility::visible(const Vec3f& eye, const std::array<float, 16>& clipFromWorld) const {
    if (portals_.empty())
        return ~uint64_t(0);
    bool solid = false;
    const int start = zoneAt(eye, &solid);
    if (solid) // outside the level: all of it in view
        return ~uint64_t(0);
    // Each zone's view so far (the rectangles it's seen through, together),
    // grown as more portals reach it; a zone goes round again when it grows.
    std::array<Rect, 64> view{};
    view[size_t(start)] = {-1, -1, 1, 1};
    uint64_t seen = uint64_t(1) << start, queued = seen;
    std::vector<int> queue{start};
    for (size_t step = 0; step < queue.size() && step < 4096; ++step) {
        const int z = queue[step];
        queued &= ~(uint64_t(1) << z);
        const Rect through = view[size_t(z)];
        for (const uint32_t i : byZone_[size_t(z)]) {
            const Portal& p = portals_[i];
            const int other = p.zone[0] == z ? p.zone[1] : p.zone[0];
            const bool near = eye[0] >= p.min[0] - kPortalMargin && eye[0] <= p.max[0] + kPortalMargin &&
                              eye[1] >= p.min[1] - kPortalMargin && eye[1] <= p.max[1] + kPortalMargin &&
                              eye[2] >= p.min[2] - kPortalMargin && eye[2] <= p.max[2] + kPortalMargin;
            const Rect r = near ? through : intersect(screenRect(p.points, clipFromWorld), through);
            if (r.empty())
                continue;
            Rect& v = view[size_t(other)];
            const uint64_t bit = uint64_t(1) << other;
            if (seen & bit) {
                if (contains(v, r))
                    continue;
                v = {std::min(v.x0, r.x0), std::min(v.y0, r.y0), std::max(v.x1, r.x1), std::max(v.y1, r.y1)};
            } else {
                v = r;
                seen |= bit;
            }
            if (!(queued & bit)) {
                queued |= bit;
                queue.push_back(other);
            }
        }
    }
    return seen;
}

} // namespace reluva::unreal
