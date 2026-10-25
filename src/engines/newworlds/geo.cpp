// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/geo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace reluva::newworlds {

namespace {

constexpr size_t kCornerSize = 32;
constexpr float kUvScale = 1.0f / 65536.0f;
constexpr uint32_t kMaxCount = 1u << 20;
constexpr uint32_t kMaxParts = 256;
constexpr uint32_t kMaxPartValue = 255; // sanity limit for a part's node number

struct Reader {
    const std::vector<uint8_t>& d;
    bool has(size_t at, size_t bytes) const { return at <= d.size() && bytes <= d.size() - at; }
    uint32_t u32(size_t o) const {
        return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    }
    float f32(size_t o) const {
        const uint32_t v = u32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    GeoModel::Vec3 vec3(size_t o) const { return {f32(o), f32(o + 4), f32(o + 8)}; }
};

std::string hex(size_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%zx", v);
    return buf;
}

bool isUnit(const GeoModel::Vec3& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return std::fabs(len - 1.0f) < 0.01f;
}

} // namespace

Error parseGeo(const std::vector<uint8_t>& data, GeoModel& out) {
    out = GeoModel{};
    const Reader r{data};
    if (!r.has(0, 16))
        return Error::fail("model too small");
    // The file starts with face 0: its header is the file header, whose
    // second value is the face count. Every face is u32 ?, u32 ?, u32 corner
    // count, u32 part, then 32-byte corners: u32 vertex, f32 u, f32 v,
    // 20 bytes. A face's first value is the previous face's part.
    const uint32_t faceCount = r.u32(4);
    if (faceCount == 0 || faceCount > kMaxCount)
        return Error::fail("model header face count out of range");
    uint32_t partCount = 1;
    size_t p = 0;
    for (uint32_t i = 0; i < faceCount; ++i) {
        if (!r.has(p, 16))
            return Error::fail("model ends inside face " + std::to_string(i));
        GeoModel::Face face;
        face.groupA = i ? r.u32(p) : 0;
        face.groupB = i ? r.u32(p + 4) : 0;
        const uint32_t n = r.u32(p + 8);
        face.part = r.u32(p + 12);
        if (n < 3 || n > 256 || face.part >= kMaxParts || !r.has(p + 16, size_t(n) * kCornerSize))
            return Error::fail("face " + std::to_string(i) + " at " + hex(p) + " has a bad header (" +
                               std::to_string(r.u32(p)) + ", " + std::to_string(r.u32(p + 4)) + ", " +
                               std::to_string(n) + ", " + std::to_string(r.u32(p + 12)) + ")");
        p += 16;
        for (uint32_t k = 0; k < n; ++k, p += kCornerSize)
            face.corners.push_back({r.u32(p), r.f32(p + 4) * kUvScale, r.f32(p + 8) * kUvScale,
                                    {r.u32(p + 12), r.u32(p + 16), r.u32(p + 20), r.u32(p + 24), r.u32(p + 28)}});
        partCount = std::max(partCount, face.part + 1);
        out.faces.push_back(std::move(face));
    }

    // Bounds: u32 last face's part, u32 ?, u32 part count, u32 attachment
    // count, then centre x, y and radius.
    if (!r.has(p, 28) || r.u32(p) != out.faces.back().part || r.u32(p + 8) != partCount)
        return Error::fail("no bounds block after the faces at " + hex(p) + " (" + std::to_string(r.u32(p)) +
                           ", " + std::to_string(r.u32(p + 4)) + ", " + std::to_string(r.u32(p + 8)) + ", " +
                           std::to_string(r.u32(p + 12)) + "; " + std::to_string(partCount) + " parts)");
    out.centreX = r.f32(p + 16);
    out.centreY = r.f32(p + 20);
    out.radius = r.f32(p + 24);
    p += 28;

    // The attachment block: u32 node count, u32 named-place count, u32 ?,
    // then 58 bytes per attachment (40 bytes, probably its orientation; a
    // chunk: u16 0x1c, u32 12, the position). 58 isn't a multiple of 4, so
    // what follows is often unaligned. The part data starts u32 size, 1, part
    // count, then the first part: 0, vertex count, the vertices and a list of
    // unit normals.
    const uint32_t attachCount = r.u32(p - 16);
    auto isPartData = [&](size_t q) {
        if (!r.has(q, 20) || r.u32(q + 4) != 1 || r.u32(q + 8) != partCount || r.u32(q + 12) != 0)
            return false;
        const uint32_t n = r.u32(q + 16);
        const size_t after = q + 20 + size_t(n) * 12;
        return n > 0 && n <= kMaxCount && r.has(after, 20) && r.u32(after) == 1 && r.u32(after + 4) > 0 &&
               isUnit(r.vec3(after + 8));
    };
    size_t partData = 0;
    const size_t expected = p + 12 + size_t(attachCount) * 58;
    if (attachCount <= kMaxCount && r.has(p, 12) && isPartData(expected)) {
        out.nodeCount = r.u32(p);
        out.placeCount = r.u32(p + 4);
        for (uint32_t i = 0; i < attachCount; ++i) {
            const size_t a = p + 12 + size_t(i) * 58;
            GeoModel::Attachment at{};
            for (int k = 0; k < 10; ++k)
                at.frame[k] = r.f32(a + 4 * size_t(k));
            at.position = r.vec3(a + 46);
            out.attachments.push_back(at);
        }
        partData = expected;
    } else {
        // Not the usual layout: search for the part data byte by byte.
        for (size_t q = p; r.has(q, 20) && !partData; ++q)
            if (isPartData(q))
                partData = q;
    }
    p = partData + 12;

    // Each part: u32 ? (0, or 1 in ptank_l's part 1), u32 vertex count, the
    // vertices, its normal lists and its edges; parts other than the last end
    // with a u32 0.
    for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex) {
        const std::string where = "part " + std::to_string(partIndex) + " at " + hex(p);
        if (partIndex > 0) {
            if (!r.has(p, 4) || r.u32(p) != 0)
                return Error::fail("no end marker before " + where);
            p += 4;
        }
        if (!r.has(p, 8) || r.u32(p) > kMaxPartValue)
            return Error::fail(where + " starts with " + std::to_string(r.has(p, 4) ? r.u32(p) : 0));
        GeoModel::Part part;
        part.node = r.u32(p);
        const uint32_t vertexCount = r.u32(p + 4);
        p += 8;
        if (vertexCount > kMaxCount || !r.has(p, size_t(vertexCount) * 12))
            return Error::fail(where + " has a bad vertex count");
        for (uint32_t i = 0; i < vertexCount; ++i, p += 12)
            part.vertices.push_back(r.vec3(p));

        // Normal lists: u32 1, u32 count, count unit vectors; repeated.
        while (r.has(p, 20) && r.u32(p) == 1 && r.u32(p + 4) > 0 && r.u32(p + 4) <= kMaxCount &&
               isUnit(r.vec3(p + 8))) {
            const uint32_t n = r.u32(p + 4);
            if (!r.has(p + 8, size_t(n) * 12))
                return Error::fail("model ends inside a normal list of part " + std::to_string(partIndex));
            std::vector<GeoModel::Vec3> list;
            for (uint32_t i = 0; i < n; ++i)
                list.push_back(r.vec3(p + 8 + size_t(i) * 12));
            part.normalLists.push_back(std::move(list));
            p += 8 + size_t(n) * 12;
        }

        // Edges: u32 0 (none, as in terrain tiles) or u32 1, u32 group count,
        // then per group u32 count and count x (u32 part, v0, v1, faceA, faceB).
        if (!r.has(p, 4) || r.u32(p) > 1)
            return Error::fail("edge list flag of part " + std::to_string(partIndex) + " not found at " + hex(p));
        if (r.u32(p) == 1) {
            if (!r.has(p, 8))
                return Error::fail("model ends at an edge list");
            const uint32_t groups = r.u32(p + 4);
            p += 8;
            for (uint32_t g = 0; g < groups; ++g) {
                if (!r.has(p, 4))
                    return Error::fail("model ends at an edge group");
                const uint32_t n = r.u32(p);
                p += 4;
                if (n > kMaxCount || !r.has(p, size_t(n) * 20))
                    return Error::fail("model ends inside an edge group");
                for (uint32_t i = 0; i < n; ++i, p += 20) {
                    if (r.u32(p) != partIndex)
                        return Error::fail("an edge at " + hex(p) + " belongs to part " + std::to_string(r.u32(p)) +
                                           ", expected " + std::to_string(partIndex));
                    part.edges.push_back({r.u32(p + 4), r.u32(p + 8), r.u32(p + 12), r.u32(p + 16)});
                }
            }
        } else {
            p += 4;
        }
        out.parts.push_back(std::move(part));
    }

    // Consistency: every index in range, every edge's vertices on its faces.
    for (const auto& face : out.faces)
        for (const auto& c : face.corners)
            if (c.vertex >= out.parts[face.part].vertices.size())
                return Error::fail("a face of part " + std::to_string(face.part) + " uses vertex " +
                                   std::to_string(c.vertex) + " of " +
                                   std::to_string(out.parts[face.part].vertices.size()));
    auto onFace = [&](uint32_t vertex, uint32_t face) {
        for (const auto& c : out.faces[face].corners)
            if (c.vertex == vertex)
                return true;
        return false;
    };
    for (size_t pi = 0; pi < out.parts.size(); ++pi) {
        const auto& part = out.parts[pi];
        for (size_t i = 0; i < part.edges.size(); ++i) {
            const auto& e = part.edges[i];
            const std::string name = "edge " + std::to_string(i) + " of part " + std::to_string(pi);
            if (e.v0 >= part.vertices.size() || e.v1 >= part.vertices.size() || e.faceA >= out.faces.size() ||
                e.faceB >= out.faces.size() || out.faces[e.faceA].part != pi || out.faces[e.faceB].part != pi)
                return Error::fail(name + " is out of range");
            if (!onFace(e.v0, e.faceA) || !onFace(e.v1, e.faceA) || !onFace(e.v0, e.faceB) || !onFace(e.v1, e.faceB))
                return Error::fail(name + " doesn't lie on its faces");
        }
    }
    return Error::success();
}

// Edge heights at 5 points along each side, from the model's vertices that
// lie on the side (linear in between), after the tile's rotation.
TileSides tileSides(const GeoModel& geo, int rotation) {
    TileSides e;
    std::vector<std::pair<float, float>> side[4]; // (position along the side, height)
    for (const auto& part : geo.parts)
        for (const auto& v : part.vertices) {
            float x = v.x, y = v.y;
            for (int k = 0; k < rotation; ++k) {
                const float t = x;
                x = -y;
                y = t;
            }
            const float h = kGroundTileSize / 2;
            if (std::fabs(x - h) < 2)
                side[0].push_back({y, v.z});
            if (std::fabs(y - h) < 2)
                side[1].push_back({x, v.z});
            if (std::fabs(x + h) < 2)
                side[2].push_back({y, v.z});
            if (std::fabs(y + h) < 2)
                side[3].push_back({x, v.z});
        }
    for (int s = 0; s < 4; ++s) {
        auto& pts = side[s];
        if (pts.empty())
            continue;
        std::sort(pts.begin(), pts.end());
        e.present[s] = true;
        for (int k = 0; k < 5; ++k) {
            const float t = -kGroundTileSize / 2 + float(k) * kGroundTileSize / 4;
            float z = pts.front().second;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                if (t >= pts[i].first && t <= pts[i + 1].first) {
                    const float span = pts[i + 1].first - pts[i].first;
                    z = span > 0 ? pts[i].second + (pts[i + 1].second - pts[i].second) * (t - pts[i].first) / span
                                 : pts[i].second;
                    break;
                }
            if (t > pts.back().first)
                z = pts.back().second;
            e.z[s][k] = z;
        }
    }
    return e;
}

} // namespace reluva::newworlds
