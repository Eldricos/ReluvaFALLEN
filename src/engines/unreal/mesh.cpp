// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/mesh.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>

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
// Many of The Fallen's animated meshes keep fewer such frames than they
// have (keyframes, between other arrays not worked out yet): then the
// first is used for every frame, and `keys` says how many were found
// (0 when every frame was).
bool findVertexBlock(const std::vector<uint8_t>& data, size_t base, size_t from, size_t to, const MeshTail& t,
                     std::vector<Vec3f>& out, int& keys) {
    keys = 0;
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
    for (size_t at = from; at + 12 <= to; ++at) {
        if (!block(at, frame))
            continue;
        take(at, frame);
        at += 12 + size_t(frame) * 4 - 1;
        if (out.size() == total)
            return true;
    }
    if (out.empty())
        return false;
    keys = int(out.size() / frame);
    out.resize(frame);
    for (int32_t f = 1; f < t.animFrames; ++f)
        out.insert(out.end(), out.begin(), out.begin() + frame);
    return true;
}

// Which way the vertices go: the block keeps them a second time as
// floats, placed (`(n, n, 128)` and n x 3 floats, n = FrameVerts). Of the
// 24 axis-aligned turns, the one that takes the first frame's packed
// vertices (by Scale and Origin) closest onto those, if close; both
// about their middles, as the float copy can be moved too.
std::optional<std::array<Vec3f, 3>> importAxes(const std::vector<uint8_t>& data, size_t base, size_t from,
                                               size_t to, const Mesh& mesh, const MeshTail& t) {
    const auto n = uint32_t(t.frameVerts);
    auto u32At = [&](size_t at) {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    auto f32At = [&](size_t at) {
        const uint32_t bits = u32At(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    // The first frame, by Scale and Origin, and its extent.
    std::vector<Vec3f> scaled(n);
    float extent = 0;
    for (uint32_t k = 0; k < n; ++k) {
        for (size_t a = 0; a < 3; ++a) {
            scaled[k][a] = (mesh.verts[k][a] - t.origin[a]) * t.scale[a];
            extent = std::max(extent, std::fabs(scaled[k][a]));
        }
    }
    if (extent <= 0)
        return std::nullopt;
    // Compared about their middles: the float copy can be moved as well.
    Vec3f mid{};
    for (const Vec3f& v : scaled)
        for (size_t a = 0; a < 3; ++a)
            mid[a] += v[a] / float(n);
    extent = 0;
    for (Vec3f& v : scaled)
        for (size_t a = 0; a < 3; ++a) {
            v[a] -= mid[a];
            extent = std::max(extent, std::fabs(v[a]));
        }
    if (extent <= 0)
        return std::nullopt;
    // The 24 turns: each output axis takes an input axis, signed, with
    // determinant +1.
    std::vector<std::array<Vec3f, 3>> turns;
    const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& p : perms)
        for (int signs = 0; signs < 8; ++signs) {
            std::array<Vec3f, 3> m{};
            for (size_t row = 0; row < 3; ++row)
                m[row][size_t(p[row])] = (signs >> row) & 1 ? -1.0f : 1.0f;
            const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                              m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                              m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            if (det > 0)
                turns.push_back(m);
        }
    std::optional<std::array<Vec3f, 3>> best;
    float bestError = 0;
    for (size_t at = from; at + 12 + size_t(n) * 12 <= to; ++at) {
        if (u32At(at) != n || u32At(at + 4) != n || u32At(at + 8) != 128)
            continue;
        Vec3f floatMid{};
        for (uint32_t k = 0; k < n; ++k)
            for (size_t a = 0; a < 3; ++a)
                floatMid[a] += f32At(at + 12 + 12 * size_t(k) + 4 * a) / float(n);
        for (const auto& m : turns) {
            float err = 0;
            for (uint32_t k = 0; k < n && std::isfinite(err); ++k)
                for (size_t row = 0; row < 3; ++row) {
                    const float want = f32At(at + 12 + 12 * size_t(k) + 4 * row) - floatMid[row];
                    const float got = m[row][0] * scaled[k][0] + m[row][1] * scaled[k][1] + m[row][2] * scaled[k][2];
                    err += (want - got) * (want - got);
                }
            if (!std::isfinite(err))
                break;
            if (!best || err < bestError) {
                best = m;
                bestError = err;
            }
        }
    }
    // Close: within 5% of the mesh's size on average (the packed vertices
    // are rounded to whole units).
    if (!best || std::sqrt(bestError / float(3 * n)) > 0.05f * extent)
        return std::nullopt;
    return best;
}

// Meshes built on a skeleton (The Fallen's characters) keep no packed
// vertices: the block has a record per bone instead (see mesh.md):
//   (n, n, 8) and n i32s; (k, k, 128) and k float xyz, the bone's
//   vertices in its own space; (k, k, 128) and k float weights; (k, k,
//   128) and k i32s, which of the frame's vertices each is; 12 floats, a
//   4x3 offset; an i32; 12 floats, the bone's 4x3 placement (rows: X, Y,
//   Z axes, then the origin); then its animation keys, not read yet.
// A vertex is the weighted sum of where its bones put it (p * offset *
// placement, as row vectors: KiraCellLight's one bone, "root", turns its
// float vertices into exactly its packed ones so). That pose, by Scale
// and Origin back into packed units, for every frame. Returns the bones.
int poseFromBones(const std::vector<uint8_t>& data, size_t base, size_t from, size_t to, const MeshTail& t,
                  std::vector<Vec3f>& out) {
    auto u32At = [&](size_t at) {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    auto f32At = [&](size_t at) {
        const uint32_t bits = u32At(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto header = [&](size_t at, uint32_t flags, uint32_t& n) {
        if (at + 12 > to)
            return false;
        n = u32At(at);
        return u32At(at + 4) == n && u32At(at + 8) == flags;
    };
    const auto frame = size_t(t.frameVerts);
    std::vector<Vec3f> sum(frame, Vec3f{});
    std::vector<float> weight(frame, 0);
    int bones = 0;
    for (size_t at = from; at + 12 <= to; ++at) {
        uint32_t n = 0, k = 0, k2 = 0, k3 = 0;
        if (!header(at, 8, n) || n == 0 || n > 64)
            continue;
        size_t p = at + 12 + 4 * size_t(n);
        if (!header(p, 128, k) || k > frame)
            continue;
        const size_t positions = p + 12;
        p = positions + 12 * size_t(k);
        if (!header(p, 128, k2) || k2 != k)
            continue;
        const size_t weights = p + 12;
        p = weights + 4 * size_t(k);
        if (!header(p, 128, k3) || k3 != k)
            continue;
        const size_t indices = p + 12;
        p = indices + 4 * size_t(k);
        if (p + 100 > to)
            continue;
        float offset[12], place[12];
        bool ok = true;
        for (size_t c = 0; c < 12; ++c) {
            offset[c] = f32At(p + 4 * c);
            place[c] = f32At(p + 52 + 4 * c);
            ok = ok && sane(offset[c]) && sane(place[c]);
        }
        for (uint32_t j = 0; j < k && ok; ++j)
            ok = u32At(indices + 4 * size_t(j)) < frame;
        if (!ok)
            continue;
        auto times = [](const float* m, const Vec3f& v) {
            return Vec3f{v[0] * m[0] + v[1] * m[3] + v[2] * m[6] + m[9], v[0] * m[1] + v[1] * m[4] + v[2] * m[7] + m[10],
                         v[0] * m[2] + v[1] * m[5] + v[2] * m[8] + m[11]};
        };
        for (uint32_t j = 0; j < k; ++j) {
            const Vec3f local{f32At(positions + 12 * size_t(j)), f32At(positions + 12 * size_t(j) + 4),
                              f32At(positions + 12 * size_t(j) + 8)};
            const float w = f32At(weights + 4 * size_t(j));
            if (!sane(w) || !sane(local[0]) || !sane(local[1]) || !sane(local[2]))
                continue;
            const Vec3f placed = times(place, times(offset, local));
            const size_t v = u32At(indices + 4 * size_t(j));
            for (size_t a = 0; a < 3; ++a)
                sum[v][a] += w * placed[a];
            weight[v] += w;
        }
        ++bones;
        at = p + 99;
    }
    // Most vertices have to be placed, or this isn't a skeleton.
    size_t placed = 0;
    for (float w : weight)
        placed += w > 0;
    if (bones == 0 || placed * 10 < frame * 9)
        return 0;
    out.assign(frame * size_t(t.animFrames), Vec3f{});
    for (size_t v = 0; v < frame; ++v)
        for (size_t a = 0; a < 3; ++a)
            out[v][a] = (weight[v] > 0 ? sum[v][a] / weight[v] : 0) / t.scale[a] + t.origin[a];
    for (size_t f = 1; f < size_t(t.animFrames); ++f)
        std::copy(out.begin(), out.begin() + std::ptrdiff_t(frame), out.begin() + std::ptrdiff_t(f * frame));
    return bones;
}

// The turn most of The Fallen's imported meshes' float copies show
// (159 of 180 that have one); for those without one to fit.
constexpr std::array<Vec3f, 3> kUsualImportAxes{Vec3f{1, 0, 0}, Vec3f{0, 0, 1}, Vec3f{0, -1, 0}};

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
            int keys = 0, bones = 0;
            const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
            if (!findVertexBlock(data, base, afterTextures, at, candidate, verts, keys) &&
                (bones = poseFromBones(data, base, afterTextures, at, candidate, verts)) == 0)
                continue;
            out.bones = bones;
            tail = std::move(candidate);
            out.verts = std::move(verts);
            out.keyFrames = keys;
            out.vertexBytes = 4;
            out.importBlock = at - afterTextures;
            out.importAxes = importAxes(data, size_t(package.exports()[size_t(index)].serialOffset), afterTextures, at,
                                        out, tail);
            if (!out.importAxes) {
                out.importAxes = kUsualImportAxes;
                out.importAxesGuessed = !bones;
            }
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
    // Animations whose frames run past the mesh's are left out (The
    // Fallen's JadziaDax has a test one like that), not the whole mesh.
    const size_t anims = out.anims.size();
    std::erase_if(out.anims, [&](const MeshAnimSeq& a) {
        return a.startFrame < 0 || a.numFrames < 0 || a.startFrame + a.numFrames > out.animFrames;
    });
    out.droppedAnims = int(anims - out.anims.size());
    return Error::success();
}

std::vector<std::string> describeMeshLayout(const Package& package, int index) {
    std::vector<std::string> out;
    std::vector<uint8_t> data;
    if (package.readExport(index, data))
        return out;
    const size_t base = size_t(package.exports()[size_t(index)].serialOffset), end = base + data.size();
    auto u32At = [&](size_t at) {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    Reader r(data, base);
    PropertyList props;
    if (beginObject(package, index, r, props))
        return out;
    const size_t start = r.pos();
    char line[160];
    out.push_back("Array headers (i32 n, i32 n, i32 flags; n 1-1000000), offsets from the properties' end:");
    auto header = [&](size_t at) {
        const uint32_t n = u32At(at);
        return at + 12 <= end && n != 0 && n <= 1000000 && u32At(at + 4) == n && u32At(at + 8) <= 0xffff;
    };
    for (size_t at = start; at + 12 <= end; ++at) {
        // A header seen a byte early (its count shifted up) is left out.
        if (!header(at) || header(at + 1))
            continue;
        std::snprintf(line, sizeof line, "  +%zu: n %u, flags %u", at - start, u32At(at), u32At(at + 8));
        out.push_back(line);
    }
    out.push_back("Places the standard tail reads from to the end, holding together:");
    const bool lod = fs::toLower(package.className(index + 1)) == "lodmesh";
    for (size_t at = start; at < end; ++at) {
        Reader c = r;
        c.seek(at);
        MeshTail t;
        if (readTail(c, package.version(), lod, t) || c.remaining() != 0 || !plausible(t, lod))
            continue;
        std::snprintf(line, sizeof line, "  +%zu: %d vertices x %d frames, scale %g %g %g, %zu triangles", at - start,
                      t.frameVerts, t.animFrames, double(t.scale[0]), double(t.scale[1]), double(t.scale[2]),
                      t.tris.size());
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> describeBones(const Package& package, int index) {
    std::vector<std::string> out;
    std::vector<uint8_t> data;
    if (package.readExport(index, data))
        return out;
    const size_t base = size_t(package.exports()[size_t(index)].serialOffset), end = base + data.size();
    size_t start = base;
    {
        Reader r(data, base);
        PropertyList props;
        if (beginObject(package, index, r, props))
            return out;
        start = r.pos();
    }
    auto u32At = [&](size_t at) {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    };
    auto f32At = [&](size_t at) {
        const uint32_t bits = u32At(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto header = [&](size_t at, uint32_t& n, uint32_t& flags) {
        if (at + 12 > end)
            return false;
        n = u32At(at);
        flags = u32At(at + 8);
        return u32At(at + 4) == n && n < 100000;
    };
    // A name: u16 length and that many printable characters.
    auto nameAt = [&](size_t at, std::string& name) {
        if (at + 2 > end)
            return false;
        const size_t len = size_t(data[at - base]) | size_t(data[at - base + 1]) << 8;
        if (len == 0 || len > 64 || at + 2 + len > end)
            return false;
        name.assign(reinterpret_cast<const char*>(data.data() + (at - base) + 2), len);
        return std::all_of(name.begin(), name.end(), [](char c) { return c >= 32 && c < 127; });
    };
    // The first bone: a name just before an (n, n, 8) header that the
    // vertex arrays follow.
    size_t first = 0;
    for (size_t at = start; at + 24 < end && !first; ++at) {
        uint32_t n = 0, flags = 0, k = 0, kf = 0;
        if (!header(at, n, flags) || flags != 8 || n == 0 || n > 64)
            continue;
        if (!header(at + 12 + 4 * size_t(n), k, kf) || kf != 128)
            continue;
        for (size_t len = 1; len <= 64 && len + 2 <= at - start; ++len) {
            std::string name;
            if (nameAt(at - 2 - len, name) && name.size() == len) {
                first = at - 2 - len;
                break;
            }
        }
    }
    if (!first)
        return out;
    char line[256];
    int bones = 0, shownKeys = 0;
    // One bone and its children, depth first; returns where it ends, 0 if
    // it doesn't hold together.
    std::function<size_t(size_t, int)> bone = [&](size_t at, int depth) -> size_t {
        std::string name;
        if (!nameAt(at, name))
            return 0;
        size_t p = at + 2 + name.size();
        uint32_t n = 0, flags = 0;
        if (!header(p, n, flags) || flags != 8)
            return 0;
        p += 12 + 4 * size_t(n);
        uint32_t k = 0;
        for (int a = 0; a < 3; ++a) {
            uint32_t ka = 0;
            if (!header(p, ka, flags) || flags != 128 || (a && ka != k))
                return 0;
            k = ka;
            p += 12 + size_t(k) * (a == 0 ? 12 : 4);
        }
        if (p + 104 > end)
            return 0;
        const size_t place = p + 52;
        p += 100;
        const uint32_t kind = u32At(p);
        p += 4;
        uint32_t clips = 0, clipFlags = 0;
        if (!header(p, clips, clipFlags))
            return 0;
        p += 12;
        std::snprintf(line, sizeof line, "%*s%s: %u vertices, origin (%g %g %g), kind %u, %u clips:", depth * 2, "",
                      name.c_str(), k, double(f32At(place + 36)), double(f32At(place + 40)), double(f32At(place + 44)),
                      kind, clips);
        out.push_back(line);
        std::string keys;
        for (uint32_t c = 0; c < clips; ++c) {
            uint32_t np = 0, nr = 0, fp = 0, fr = 0;
            if (!header(p, np, fp) || p + 12 + 16 * size_t(np) > end)
                return 0;
            const size_t pos = p + 12;
            p = pos + 16 * size_t(np);
            if (!header(p, nr, fr) || p + 12 + 10 * size_t(nr) + 8 > end)
                return 0;
            const size_t rot = p + 12;
            p = rot + 10 * size_t(nr) + 8;
            if (c < 6) {
                std::snprintf(line, sizeof line, " %u/%u", np, nr);
                keys += line;
                if (np)
                    keys += "(to " + std::to_string(f32At(pos + 16 * size_t(np - 1))).substr(0, 5) + "s)";
            }
            // The first clips with several rotation keys: their bytes, for
            // working out the encoding.
            if (nr >= 3 && shownKeys < 3) {
                ++shownKeys;
                out.push_back(std::string(size_t(depth) * 2 + 4, ' ') + "clip " + std::to_string(c) +
                              " rotation keys (time: bytes):");
                for (uint32_t j = 0; j < nr && j < 24; ++j) {
                    const size_t e = rot + 10 * size_t(j);
                    int w = std::snprintf(line, sizeof line, "%*s%.4f:", depth * 2 + 6, "", double(f32At(e)));
                    for (size_t b = 4; b < 10; ++b)
                        w += std::snprintf(line + w, sizeof line - size_t(w), " %02x", data[e + b - base]);
                    out.push_back(line);
                }
            }
        }
        out.push_back(std::string(size_t(depth) * 2 + 2, ' ') + "keys (position/rotation) of the first clips:" + keys);
        ++bones;
        uint32_t children = 0, childFlags = 0;
        if (!header(p, children, childFlags) || childFlags != 4)
            return p; // the last bone (or something else follows)
        p += 12;
        for (uint32_t ch = 0; ch < children; ++ch) {
            const size_t next = bone(p, depth + 1);
            if (!next)
                return 0;
            p = next;
        }
        return p;
    };
    out.push_back("Bones (offsets from the properties' end: first at +" + std::to_string(first - start) + "):");
    const size_t stop = bone(first, 1);
    out.push_back(std::to_string(bones) + " bones read" +
                  (stop ? ", ending at +" + std::to_string(stop - start) : std::string(", then something not understood")));
    return out;
}

} // namespace reluva::unreal
