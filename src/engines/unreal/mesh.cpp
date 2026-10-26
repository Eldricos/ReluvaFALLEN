// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/mesh.h"

#include "common/fs.h"

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
// least `minSize` bytes).
bool count(Reader& r, size_t minSize, size_t& n) {
    const int32_t c = r.index();
    if (!r.ok() || c < 0 || size_t(c) * minSize > r.remaining()) {
        r.fail();
        return false;
    }
    n = size_t(c);
    return true;
}

// A lazy array: from version 63 the file offset just past it comes first.
// Returns that offset (0 before 63) and the count.
bool lazyCount(Reader& r, int version, size_t minSize, size_t& end, size_t& n) {
    end = version >= 63 ? size_t(uint32_t(r.i32())) : 0;
    return count(r, minSize, n);
}

void skipArray(Reader& r, size_t elementSize) {
    size_t n = 0;
    if (count(r, elementSize, n))
        r.skip(n * elementSize);
}

void skipLazy(Reader& r, int version, size_t elementSize) {
    size_t end = 0, n = 0;
    if (!lazyCount(r, version, elementSize, end, n))
        return;
    if (end)
        r.seek(end);
    else
        r.skip(n * elementSize);
}

// What follows the textures: per-frame bounds, the frame counts and
// placement, and for a LodMesh its faces, wedges and materials (as
// triangles).
struct MeshTail {
    int32_t frameVerts = 0, animFrames = 0;
    Vec3f scale{}, origin{};
    Rotator rotOrigin;
    int32_t modelVerts = 0, specialVerts = 0;
    std::vector<MeshTri> tris;
};

Error readTail(Reader& r, int ver, bool lod, MeshTail& out) {
    auto fail = [&](const char* why) { return Error::fail(why); };
    skipArray(r, 25); // per-frame bounding boxes
    skipArray(r, 16); // and spheres
    out.frameVerts = r.i32();
    out.animFrames = r.i32();
    r.u32(); // AND of the triangles' flags
    r.u32(); // OR
    out.scale = readVector(r);
    out.origin = readVector(r);
    out.rotOrigin.pitch = r.i32();
    out.rotOrigin.yaw = r.i32();
    out.rotOrigin.roll = r.i32();
    r.i32(); // current polygon
    r.i32(); // current vertex
    if (ver == 65)
        r.f32();
    else if (ver >= 66)
        skipArray(r, 4); // texture levels of detail
    if (!r.ok())
        return fail("the mesh's fields run past the end");
    if (!lod)
        return Error::success();

    skipArray(r, 2); // collapse point thus
    skipArray(r, 2); // face level
    size_t faces = 0;
    if (!count(r, 8, faces))
        return fail("faces run past the end");
    std::vector<std::array<uint16_t, 4>> faceList(faces);
    for (auto& f : faceList)
        for (uint16_t& v : f)
            v = r.u16();
    skipArray(r, 2); // collapse wedge thus
    size_t wedges = 0;
    if (!count(r, 4, wedges))
        return fail("wedges run past the end");
    struct Wedge {
        uint16_t vertex;
        float u, v;
    };
    std::vector<Wedge> wedgeList(wedges);
    for (Wedge& w : wedgeList) {
        w.vertex = r.u16();
        w.u = float(r.u8()) / 256;
        w.v = float(r.u8()) / 256;
    }
    size_t materials = 0;
    if (!count(r, 8, materials))
        return fail("materials run past the end");
    std::vector<std::pair<uint32_t, int32_t>> materialList(materials);
    for (auto& m : materialList) {
        m.first = r.u32();
        m.second = r.i32();
    }
    skipArray(r, 8); // special faces (weapon triangles)
    out.modelVerts = r.i32();
    out.specialVerts = r.i32();
    r.skip(4 * 6);   // level-of-detail settings
    skipArray(r, 2); // remap anim verts
    r.i32();         // old frame verts
    if (!r.ok())
        return fail("the level-of-detail fields run past the end");
    // Faces through wedges to vertices; a frame's special vertices (for
    // the weapon triangle) come before the model's.
    for (const auto& f : faceList) {
        MeshTri t;
        if (f[3] >= materialList.size())
            return fail("a face's material is out of range");
        for (size_t k = 0; k < 3; ++k) {
            if (f[k] >= wedgeList.size())
                return fail("a face's wedge is out of range");
            const Wedge& w = wedgeList[f[k]];
            t.vertex[k] = int32_t(w.vertex) + out.specialVerts;
            t.uv[k] = {w.u, w.v};
        }
        t.polyFlags = materialList[f[3]].first;
        t.textureIndex = materialList[f[3]].second;
        out.tris.push_back(t);
    }
    return Error::success();
}

bool sane(float f) { return std::isfinite(f) && std::fabs(f) < 1e6f; }

// Whether a tail read from somewhere holds together: frame counts,
// placement, and for a LodMesh the vertex counts.
bool plausible(const MeshTail& t, bool lod) {
    if (t.frameVerts <= 0 || t.animFrames <= 0 || int64_t(t.frameVerts) * t.animFrames > 10000000)
        return false;
    for (float f : t.scale)
        if (!sane(f) || std::fabs(f) < 1e-6f)
            return false;
    for (float f : t.origin)
        if (!sane(f))
            return false;
    if (lod && (t.modelVerts + t.specialVerts != t.frameVerts || t.tris.empty()))
        return false;
    return true;
}

Vec3f unpackVertex(uint32_t p) {
    return {float(int32_t(p << 21) >> 21), float(int32_t(p << 10) >> 21), float(int32_t(p) >> 22)};
}

// The vertices in an imported-model block (data[from, to), file offsets):
// an array of (i32 count, i32 count again, i32 1) and that many packed
// vertices, of all frames at once, or else one such array per frame.
bool findVertexBlock(const std::vector<uint8_t>& data, size_t base, size_t from, size_t to, const MeshTail& t,
                     std::vector<Vec3f>& out) {
    const auto frame = uint32_t(t.frameVerts), total = uint32_t(t.frameVerts) * uint32_t(t.animFrames);
    auto u32At = [&](size_t at) {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    auto block = [&](size_t at, uint32_t n) {
        return at + 12 + size_t(n) * 4 <= to && u32At(at) == n && u32At(at + 4) == n && u32At(at + 8) == 1;
    };
    auto take = [&](size_t at, uint32_t n) {
        for (uint32_t k = 0; k < n; ++k)
            out.push_back(unpackVertex(u32At(at + 12 + 4 * size_t(k))));
    };
    for (size_t at = from; at + 12 <= to; ++at)
        if (block(at, total)) {
            take(at, total);
            return true;
        }
    if (t.animFrames > 1)
        for (size_t at = from; at + 12 <= to; ++at) {
            if (!block(at, frame))
                continue;
            take(at, frame);
            at += 12 + size_t(frame) * 4 - 1;
            if (out.size() == total)
                return true;
        }
    out.clear();
    return false;
}

} // namespace

bool isMeshClass(std::string_view className) {
    const std::string c = fs::toLower(className);
    return c == "mesh" || c == "lodmesh";
}

const MeshAnimSeq* Mesh::findAnim(std::string_view name) const {
    const std::string wanted = fs::toLower(name);
    for (const MeshAnimSeq& a : anims)
        if (fs::toLower(a.name) == wanted)
            return &a;
    return nullptr;
}

Error readMesh(const Package& package, int index, Mesh& out) {
    out = Mesh{};
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const std::string what = package.path(index + 1);
    const int ver = package.version();
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    out.lod = fs::toLower(package.className(index + 1)) == "lodmesh";
    auto fail = [&](const std::string& why) {
        return Error::fail(what + ": " + why + " (at file offset " + std::to_string(r.pos()) + ")");
    };

    // As any primitive: bounding box and sphere.
    r.skip(25);
    r.skip(ver > 61 ? 16 : 12);

    // Vertices, all frames'. Their size is told by the lazy array's end:
    // 4 bytes (11, 11 and 10 bits) as usual, or 8 (16 bits each).
    size_t end = 0, n = 0;
    if (!lazyCount(r, ver, 4, end, n))
        return fail("vertices run past the end");
    if (n && end > r.pos() && (end - r.pos()) % n == 0)
        out.vertexBytes = int((end - r.pos()) / n);
    if (out.vertexBytes != 4 && out.vertexBytes != 8)
        return fail(std::to_string(out.vertexBytes) + "-byte vertices");
    out.verts.resize(n);
    for (Vec3f& v : out.verts) {
        if (out.vertexBytes == 4) {
            const uint32_t p = r.u32();
            v = {float(int32_t(p << 21) >> 21), float(int32_t(p << 10) >> 21), float(int32_t(p) >> 22)};
        } else {
            v[0] = float(r.i16());
            v[1] = float(r.i16());
            v[2] = float(r.i16());
            r.skip(2);
        }
    }
    if (!r.ok())
        return fail("vertices run past the end");

    // Triangles (a LodMesh keeps its own below, these are empty).
    if (!lazyCount(r, ver, 20, end, n))
        return fail("triangles run past the end");
    if (end && n && end - r.pos() != n * 20)
        return fail("triangles of " + std::to_string((end - r.pos()) / n) + " bytes");
    out.tris.resize(n);
    for (MeshTri& t : out.tris) {
        for (int32_t& v : t.vertex)
            v = r.u16();
        for (auto& uv : t.uv) {
            uv[0] = float(r.u8()) / 256;
            uv[1] = float(r.u8()) / 256;
        }
        t.polyFlags = r.u32();
        t.textureIndex = r.i32();
    }

    // Animation sequences, each with its notifications (time, function).
    if (!count(r, 16, n))
        return fail("animation sequences run past the end");
    out.anims.resize(n);
    for (MeshAnimSeq& a : out.anims) {
        a.name = package.name(r.index());
        a.group = package.name(r.index());
        a.startFrame = r.i32();
        a.numFrames = r.i32();
        size_t notifies = 0;
        if (!count(r, 5, notifies))
            return fail("animation notifications run past the end");
        for (size_t k = 0; k < notifies; ++k) {
            r.f32();
            r.index();
        }
        a.rate = r.f32();
    }
    if (!r.ok())
        return fail("animation sequences run past the end");

    skipLazy(r, ver, 8); // connects: per vertex, its triangles
    r.skip(25);          // bounding box
    r.skip(ver > 61 ? 16 : 12);
    skipLazy(r, ver, 4); // vertex links
    if (!count(r, 1, n))
        return fail("textures run past the end");
    out.textures.resize(n);
    for (int32_t& t : out.textures)
        t = r.index();
    const size_t afterTextures = r.pos();

    // The standard layout: the tail right here, the vertices read above.
    MeshTail tail;
    Reader t = r;
    Error err = readTail(t, ver, out.lod, tail);
    if (!err && (out.verts.empty() || size_t(tail.frameVerts) * size_t(tail.animFrames) != out.verts.size()))
        err = Error::fail("no vertices where expected");
    if (!err) {
        out.trailingBytes = t.remaining();
    } else {
        // The Fallen's layout: an imported-model block before the tail,
        // holding the vertices. Its tail is where the standard fields
        // read from to exactly the end and agree with themselves, found
        // from the end back (the block can be large; the tail is near the end).
        const size_t end = r.end();
        bool found = false;
        for (size_t at = end; at-- > afterTextures && !found;) {
            Reader c = r;
            c.seek(at);
            MeshTail candidate;
            if (readTail(c, ver, out.lod, candidate) || c.remaining() != 0 || !plausible(candidate, out.lod))
                continue;
            std::vector<Vec3f> verts;
            if (!findVertexBlock(data, size_t(package.exports()[size_t(index)].serialOffset), afterTextures, at,
                                 candidate, verts))
                continue;
            tail = std::move(candidate);
            out.verts = std::move(verts);
            out.vertexBytes = 4;
            out.importBlock = at - afterTextures;
            found = true;
        }
        if (!found)
            return fail("neither the standard layout (" + err.message + ") nor an imported-model block before the "
                        "standard fields fits");
    }
    out.frameVerts = tail.frameVerts;
    out.animFrames = tail.animFrames;
    out.scale = tail.scale;
    out.origin = tail.origin;
    out.rotOrigin = tail.rotOrigin;
    if (out.lod)
        out.tris = std::move(tail.tris);
    else if (out.tris.empty())
        return fail("its triangles aren't in the standard place (a Mesh with an imported-model block isn't read yet)");

    // The pieces have to agree, which they won't if a layout above is off.
    if (out.frameVerts <= 0 || out.animFrames <= 0 ||
        size_t(out.frameVerts) * size_t(out.animFrames) != out.verts.size())
        return fail(std::to_string(out.verts.size()) + " vertices for " + std::to_string(out.animFrames) +
                    " frames of " + std::to_string(out.frameVerts));
    for (const MeshTri& t : out.tris)
        for (int32_t v : t.vertex)
            if (v < 0 || v >= out.frameVerts)
                return fail("a triangle's vertex is out of range");
    for (const MeshAnimSeq& a : out.anims)
        if (a.startFrame < 0 || a.numFrames < 0 || a.startFrame + a.numFrames > out.animFrames)
            return fail("animation " + a.name + "'s frames are out of range");
    return Error::success();
}

} // namespace reluva::unreal
