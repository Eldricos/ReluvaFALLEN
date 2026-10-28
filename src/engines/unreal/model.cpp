// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/model.h"

#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include <algorithm>

#include <cmath>

namespace reluva::unreal {

namespace {

Vec3f readVector(Reader& r) {
    Vec3f v;
    for (float& f : v)
        f = r.f32();
    return v;
}

// An array's count, checked against what's left (each element taking at
// least `minSize` bytes) before anything is allocated for it.
bool arrayCount(Reader& r, size_t minSize, size_t& count) {
    const int32_t n = r.index();
    if (!r.ok() || n < 0 || size_t(n) * minSize > r.remaining()) {
        r.fail();
        return false;
    }
    count = size_t(n);
    return true;
}

Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Everything after the vertices. Brushes' models stop early (no nodes, so
// nothing to light); that isn't an error.
void readLighting(Reader& r, Model& out) {
    auto fail = [&](const std::string& why) {
        out.zones.clear();
        out.lightMaps.clear();
        out.lightBits.clear();
        out.leaves.clear();
        out.lights.clear();
        out.lightingError = why + " (at file offset " + std::to_string(r.pos()) + ")";
    };
    if (out.zoneCount < 0 || out.zoneCount > 64)
        return fail("zone count " + std::to_string(out.zoneCount));
    out.zones.resize(size_t(out.zoneCount));
    for (Zone& z : out.zones) {
        z.actor = r.index();
        z.connectivity = r.u64();
        z.visibility = r.u64();
    }
    out.polys = r.index();
    size_t n = 0;
    // Light map index: i32 + 3 floats + 2 compact + 2 floats + i32, 30 bytes at least.
    if (!r.ok() || !arrayCount(r, 30, n))
        return fail("zones or the light map count run past the end");
    out.lightMaps.resize(n);
    for (LightMapIndex& m : out.lightMaps) {
        m.dataOffset = r.i32();
        m.pan = readVector(r);
        m.uClamp = r.index();
        m.vClamp = r.index();
        m.uScale = r.f32();
        m.vScale = r.f32();
        m.lightActors = r.i32();
    }
    if (!r.ok() || !arrayCount(r, 1, n))
        return fail("light maps run past the end");
    out.lightBits.resize(n);
    r.bytes(out.lightBits.data(), n);
    if (!r.ok() || !arrayCount(r, 25, n))
        return fail("light bits run past the end");
    r.skip(n * 25); // bounds: boxes
    if (!r.ok() || !arrayCount(r, 4, n))
        return fail("bounds run past the end");
    r.skip(n * 4); // leaf hulls: planes' node indices
    if (!r.ok() || !arrayCount(r, 11, n))
        return fail("leaf hulls run past the end");
    out.leaves.resize(n);
    for (Leaf& l : out.leaves) {
        l.zone = r.index();
        l.permeating = r.index();
        l.volumetric = r.index();
        l.visibleZones = r.u64();
    }
    if (!r.ok() || !arrayCount(r, 1, n))
        return fail("leaves run past the end");
    out.lights.resize(n);
    for (int32_t& l : out.lights)
        l = r.index();
    out.rootOutside = r.i32() != 0;
    r.i32(); // linked
    if (!r.ok())
        return fail("lights run past the end");
    out.trailingBytes = r.remaining();

    // As with the geometry, the indices have to hold together.
    for (const LightMapIndex& m : out.lightMaps) {
        if (m.uClamp < 0 || m.vClamp < 0 || m.uClamp > 4096 || m.vClamp > 4096 || m.dataOffset < 0)
            return fail("a light map's size or data offset is out of range");
        if (m.lightActors < -1 || (m.lightActors >= 0 && size_t(m.lightActors) >= out.lights.size()))
            return fail("a light map's lights are out of range");
        // Each light in its list has a bitmap of rows padded to bytes.
        size_t count = 0;
        for (size_t i = size_t(std::max(m.lightActors, 0)); m.lightActors >= 0 && i < out.lights.size() && out.lights[i]; ++i)
            ++count;
        const size_t bytes = size_t((m.uClamp + 7) / 8) * size_t(m.vClamp) * count;
        if (size_t(m.dataOffset) + bytes > out.lightBits.size())
            return fail("a light map's bits run past the light bits");
    }
    for (const BspSurf& s : out.surfs)
        if (s.lightMap >= 0 && size_t(s.lightMap) >= out.lightMaps.size())
            return fail("a surface's light map is out of range");
}

} // namespace

Error readModel(const Package& package, int index, Model& out, bool validate) {
    out = Model{};
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const Export& e = package.exports()[size_t(index)];
    const std::string what = package.path(index + 1);
    Reader r(data, size_t(e.serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;

    // Bounds: a box (min, max, whether set) and a sphere (centre, and from
    // version 62 its radius).
    out.boundsMin = readVector(r);
    out.boundsMax = readVector(r);
    r.u8();
    readVector(r);
    if (package.version() > 61)
        r.f32();

    size_t n = 0;
    if (arrayCount(r, 12, n)) {
        out.vectors.resize(n);
        for (Vec3f& v : out.vectors)
            v = readVector(r);
    }
    if (arrayCount(r, 12, n)) {
        out.points.resize(n);
        for (Vec3f& v : out.points)
            v = readVector(r);
    }
    if (!r.ok())
        return Error::fail(what + ": vectors or points run past the end");

    // Nodes: at least 16 (plane) + 8 (zone mask) + 1 + 7 compact indices
    // + 3 bytes + 8 (leaves) bytes each.
    if (arrayCount(r, 43, n)) {
        out.nodes.resize(n);
        for (BspNode& node : out.nodes) {
            for (float& f : node.plane)
                f = r.f32();
            node.zoneMask = r.u64();
            node.flags = r.u8();
            node.vertPool = r.index();
            node.surf = r.index();
            node.back = r.index();
            node.front = r.index();
            node.planeNode = r.index();
            node.collisionBound = r.index();
            node.renderBound = r.index();
            node.zone[0] = r.u8();
            node.zone[1] = r.u8();
            node.vertexCount = r.u8();
            node.leaf[0] = r.i32();
            node.leaf[1] = r.i32();
        }
    }
    if (!r.ok())
        return Error::fail(what + ": nodes run past the end");

    if (arrayCount(r, 14, n)) {
        out.surfs.resize(n);
        for (BspSurf& s : out.surfs) {
            s.texture = r.index();
            s.polyFlags = r.u32();
            s.base = r.index();
            s.normal = r.index();
            s.textureU = r.index();
            s.textureV = r.index();
            s.lightMap = r.index();
            s.brushPoly = r.index();
            s.panU = r.i16();
            s.panV = r.i16();
            s.actor = r.index();
        }
    }
    if (!r.ok())
        return Error::fail(what + ": surfaces run past the end");

    if (arrayCount(r, 2, n)) {
        out.verts.resize(n);
        for (BspVert& v : out.verts) {
            v.point = r.index();
            v.side = r.index();
        }
    }
    out.sharedSides = r.i32();
    out.zoneCount = r.i32();
    if (!r.ok())
        return Error::fail(what + ": vertices run past the end");
    const size_t afterVerts = r.pos();

    // Check the indices hold together, which they won't if the layout
    // above is off for this game's version.
    auto check = [&]() -> std::string {
        const auto points = int32_t(out.points.size()), vectors = int32_t(out.vectors.size());
        for (const BspNode& node : out.nodes) {
            if (node.vertexCount == 0)
                continue;
            if (node.surf < 0 || size_t(node.surf) >= out.surfs.size() || node.vertPool < 0 ||
                size_t(node.vertPool) + node.vertexCount > out.verts.size())
                return "a node's surface or vertices are out of range";
            for (int k = 0; k < node.vertexCount; ++k) {
                const int32_t p = out.verts[size_t(node.vertPool + k)].point;
                if (p < 0 || p >= points)
                    return "a vertex's point is out of range";
            }
        }
        for (const BspSurf& s : out.surfs)
            if (s.base < 0 || s.base >= points || s.normal < 0 || s.normal >= vectors || s.textureU < 0 ||
                s.textureU >= vectors || s.textureV < 0 || s.textureV >= vectors)
                return "a surface's vectors are out of range";
        return {};
    };
    if (std::string bad = check(); !bad.empty()) {
        if (validate)
            return Error::fail(what + ": " + bad);
        // Wanted for what follows (a brush's Polys): the BSP is dropped
        // so nothing draws from it.
        out.geometryError = bad;
        out.nodes.clear();
        out.surfs.clear();
        out.verts.clear();
    }

    r.seek(afterVerts);
    readLighting(r, out);
    return Error::success();
}

int findLevelModel(const Package& package, Model* out, std::string* error) {
    int best = -1;
    size_t bestNodes = 0;
    Model model;
    for (int i : package.exportsOfClass("Model")) {
        if (Error err = readModel(package, i, model)) {
            if (error && error->empty())
                *error = err.message;
            continue;
        }
        if (model.nodes.size() > bestNodes) {
            best = i;
            bestNodes = model.nodes.size();
            if (out)
                *out = std::move(model);
        }
    }
    return best;
}

std::vector<Polygon> levelPolygons(const Model& model) {
    std::vector<Polygon> polys;
    for (const BspNode& node : model.nodes) {
        if (node.vertexCount < 3)
            continue;
        const BspSurf& s = model.surfs[size_t(node.surf)];
        if (s.polyFlags & (polyflag::Invisible | polyflag::Portal))
            continue;
        Polygon p;
        p.surf = node.surf;
        p.normal = model.vectors[size_t(s.normal)];
        p.zones = uint64_t(1) << (node.zone[1] & 63);
        if (node.zone[1] == 0 || (s.polyFlags & (polyflag::TwoSided | polyflag::NotSolid | polyflag::Semisolid)))
            p.zones |= uint64_t(1) << (node.zone[0] & 63);
        const Vec3f& base = model.points[size_t(s.base)];
        const Vec3f& tu = model.vectors[size_t(s.textureU)];
        const Vec3f& tv = model.vectors[size_t(s.textureV)];
        for (int k = 0; k < node.vertexCount; ++k) {
            const Vec3f& pt = model.points[size_t(model.verts[size_t(node.vertPool + k)].point)];
            const Vec3f d = sub(pt, base);
            p.points.push_back(pt);
            p.texels.push_back({dot(d, tu) + float(s.panU), dot(d, tv) + float(s.panV)});
        }
        polys.push_back(std::move(p));
    }
    return polys;
}

} // namespace reluva::unreal
