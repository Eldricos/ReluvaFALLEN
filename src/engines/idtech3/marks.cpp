// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/marks.h"

#include "engines/idtech3/patch.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3f normalized(Vec3f v) {
    const float len = std::sqrt(dot(v, v));
    if (len > 1e-9f)
        for (float& c : v)
            c /= len;
    return v;
}

// A plane keeping the points with dot(normal, p) >= dist.
struct ClipPlane {
    Vec3f normal;
    float dist;
};

// Cuts `poly` down to the side of `plane` it keeps.
void clip(std::vector<Vec3f>& poly, const ClipPlane& plane) {
    std::vector<Vec3f> out;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec3f& a = poly[i];
        const Vec3f& b = poly[(i + 1) % poly.size()];
        const float da = dot(plane.normal, a) - plane.dist, db = dot(plane.normal, b) - plane.dist;
        if (da >= 0)
            out.push_back(a);
        if ((da >= 0) != (db >= 0)) {
            const float t = da / (da - db);
            out.push_back({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t});
        }
    }
    poly = std::move(out);
}

} // namespace

MarkSurfaces::MarkSurfaces(const Bsp& bsp, float subdivisions) {
    if (bsp.models.empty())
        return;
    const BspModel& world = bsp.models[0];
    std::vector<BspVertex> patchVerts;
    std::vector<uint32_t> patchIndices;
    for (int si = world.firstSurface; si < world.firstSurface + world.numSurfaces; ++si) {
        const BspSurface& s = bsp.surfaces[size_t(si)];
        if (s.type != SurfaceType::Planar && s.type != SurfaceType::TriangleSoup && s.type != SurfaceType::Patch)
            continue;
        if (s.shader >= 0 && size_t(s.shader) < bsp.shaders.size() &&
            (bsp.shaders[size_t(s.shader)].surfaceFlags &
             (surfaceflags::Sky | surfaceflags::NoImpact | surfaceflags::NoMarks)))
            continue;
        const BspVertex* v = &bsp.vertices[size_t(s.firstVertex)];
        const BspVertex* verts = v;
        const int32_t* idx = nullptr;
        std::vector<int32_t> patchIdx;
        size_t indexCount = 0;
        if (s.type == SurfaceType::Patch) {
            patchVerts.clear();
            patchIndices.clear();
            tessellatePatch(v, s.patchWidth, s.patchHeight, patchLevel(v, s.patchWidth, s.patchHeight, subdivisions),
                            patchVerts, patchIndices);
            verts = patchVerts.data();
            patchIdx.assign(patchIndices.begin(), patchIndices.end());
            idx = patchIdx.data();
            indexCount = patchIdx.size();
        } else {
            idx = &bsp.indices[size_t(s.firstIndex)];
            indexCount = size_t(s.numIndices);
        }
        Surface surface{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, triangles_.size(), 0};
        for (size_t i = 0; i + 2 < indexCount; i += 3) {
            Triangle t;
            Vec3f normalSum{};
            for (size_t k = 0; k < 3; ++k) {
                const BspVertex& bv = verts[idx[i + k]];
                t.corners[k] = bv.pos;
                for (size_t c = 0; c < 3; ++c) {
                    normalSum[c] += bv.normal[c];
                    surface.mins[c] = std::min(surface.mins[c], bv.pos[c]);
                    surface.maxs[c] = std::max(surface.maxs[c], bv.pos[c]);
                }
            }
            // The way it faces: its geometric normal, turned to agree with
            // the vertices' (the map's winding is the renderer's business).
            t.normal = normalized(cross(sub(t.corners[1], t.corners[0]), sub(t.corners[2], t.corners[0])));
            if (dot(t.normal, t.normal) < 0.5f)
                continue; // degenerate
            if (dot(t.normal, normalSum) < 0)
                for (float& c : t.normal)
                    c = -c;
            triangles_.push_back(t);
        }
        surface.count = triangles_.size() - surface.first;
        if (surface.count)
            surfaces_.push_back(surface);
    }
}

void MarkSurfaces::mark(const std::vector<Vec3f>& polygon, const Vec3f& projection, size_t maxPoints,
                        size_t maxFragments, std::vector<Vec3f>& points, std::vector<Fragment>& fragments) const {
    if (polygon.size() < 3)
        return;
    const float depth = std::sqrt(dot(projection, projection));
    if (depth < 1e-6f)
        return;
    const Vec3f dir = normalized(projection);
    // The volume the mark reaches: the polygon swept its projection's
    // length both ways.
    Vec3f mins{1e30f, 1e30f, 1e30f}, maxs{-1e30f, -1e30f, -1e30f}, center{};
    for (const Vec3f& p : polygon)
        for (size_t c = 0; c < 3; ++c) {
            mins[c] = std::min(mins[c], std::min(p[c] - projection[c], p[c] + projection[c]));
            maxs[c] = std::max(maxs[c], std::max(p[c] - projection[c], p[c] + projection[c]));
            center[c] += p[c] / float(polygon.size());
        }
    // Its sides: through each edge, along the projection, facing in; and
    // the two ends across it.
    std::vector<ClipPlane> planes;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const Vec3f& a = polygon[i];
        const Vec3f& b = polygon[(i + 1) % polygon.size()];
        Vec3f n = normalized(cross(dir, sub(b, a)));
        if (dot(n, n) < 0.5f)
            continue;
        if (dot(n, center) < dot(n, a))
            for (float& c : n)
                c = -c;
        planes.push_back({n, dot(n, a)});
    }
    const float mid = dot(dir, center);
    planes.push_back({dir, mid - depth});
    planes.push_back({{-dir[0], -dir[1], -dir[2]}, -(mid + depth)});

    std::vector<Vec3f> poly;
    for (const Surface& s : surfaces_) {
        bool outside = false;
        for (size_t c = 0; c < 3 && !outside; ++c)
            outside = s.maxs[c] < mins[c] || s.mins[c] > maxs[c];
        if (outside)
            continue;
        for (size_t ti = s.first; ti < s.first + s.count; ++ti) {
            const Triangle& t = triangles_[ti];
            // Only surfaces the mark comes at from the front.
            if (dot(t.normal, dir) > -0.1f)
                continue;
            poly.assign(t.corners.begin(), t.corners.end());
            for (const ClipPlane& plane : planes) {
                clip(poly, plane);
                if (poly.size() < 3)
                    break;
            }
            if (poly.size() < 3)
                continue;
            if (fragments.size() >= maxFragments || points.size() + poly.size() > maxPoints)
                return;
            fragments.push_back({int32_t(points.size()), int32_t(poly.size())});
            points.insert(points.end(), poly.begin(), poly.end());
        }
    }
}

} // namespace reluva::idtech3
