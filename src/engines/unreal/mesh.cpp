// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/mesh.h"

#include "engines/unreal/package_set.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
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
                     std::vector<Vec3f>& out, int& keys, bool& perFrame) {
    keys = 0;
    perFrame = false;
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
    // One array per frame, it seemed; they're really the clips' vertex
    // keys (see findClipTable), so as many as the frames needn't be them.
    perFrame = true;
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
// vertices: the block has a record per bone instead, a tree written depth
// first (see mesh.md):
//   u16 length and the bone's name; (n, n, 8) and n i32s;
//   (k, k, 128) and k float xyz, the bone's vertices in its own space;
//   (k, k, 128) and k float weights; (k, k, 128) and k i32s, which of the
//   frame's vertices each is; 12 floats, a 4x3 offset; an i32; 12 floats,
//   the bone's 4x3 placement in its parent's space (rows: X, Y, Z axes,
//   then the origin); an i32; (c, c, f) and c clips, each (n, n, f) and n
//   position keys (time, xyz), (m, m, f) and m rotation keys (time, 6
//   bytes), and 8 bytes; then (h, h, 4) and its h children's records.
struct BoneRecord {
    std::string name;
    int parent = -1;
    uint32_t vertices = 0;
    size_t positions = 0, weights = 0, indices = 0; // file offsets of the arrays
    std::array<float, 12> offset{}, placement{};
    uint32_t kind = 0, clips = 0;
    size_t clipsAt = 0; // the first clip
    struct Clip {
        size_t positions = 0, rotations = 0; // file offsets of the keys
        uint32_t nPositions = 0, nRotations = 0;
    };
    std::vector<Clip> clipList; // when they read
    size_t start = 0;   // where its record starts (its name)
    size_t end = 0;     // past its own fields (before its children); 0: its clips didn't read
};

struct BoneReader {
    const std::vector<uint8_t>& data;
    size_t base, from, to;
    size_t failedAt = 0; // where a record didn't hold together

    uint32_t u32(size_t at) const {
        const uint8_t* p = data.data() + (at - base);
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    float f32(size_t at) const {
        const uint32_t bits = u32(at);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    bool header(size_t at, uint32_t& n, uint32_t& flags) const {
        if (at < from || at + 12 > to)
            return false;
        n = u32(at);
        flags = u32(at + 8);
        return u32(at + 4) == n && n < 1000000;
    }
    bool name(size_t at, std::string& out) const {
        if (at < from || at + 2 > to)
            return false;
        // Some are empty (GrigariMarauder's), some end in a tab (VortaFeMale's "Box03").
        const size_t len = size_t(data[at - base]) | size_t(data[at - base + 1]) << 8;
        if (len > 64 || at + 2 + len > to)
            return false;
        out.assign(reinterpret_cast<const char*>(data.data() + (at - base) + 2), len);
        return std::all_of(out.begin(), out.end(), [](char c) { return c == '\t' || (c >= 32 && c < 127); });
    }

    // The first bone from `start`: a name just before an (n, n, 8) header
    // that the vertex arrays follow. 0 if there's none.
    size_t first(size_t start = 0) const {
        for (size_t at = std::max(start, from); at + 24 < to; ++at) {
            uint32_t n = 0, flags = 0, k = 0, kf = 0;
            if (!header(at, n, flags) || flags != 8 || n == 0 || n > 64)
                continue;
            if (!header(at + 12 + 4 * size_t(n), k, kf) || kf != 128)
                continue;
            for (size_t len = 1; len <= 64 && at >= std::max(start, from) + len + 2; ++len) {
                std::string s;
                if (name(at - 2 - len, s) && s.size() == len)
                    return at - 2 - len;
            }
        }
        return 0;
    }

    // A bone's own fields from `at` (its name). Its clips' keys are
    // stepped over too, to find its children; if they don't read, `end`
    // stays 0 and its children aren't known, but the bone still is.
    bool record(size_t at, BoneRecord& b) const {
        uint32_t n = 0, flags = 0;
        if (!name(at, b.name))
            return false;
        b.start = at;
        size_t p = at + 2 + b.name.size();
        if (!header(p, n, flags) || flags != 8 || n > 64)
            return false;
        p += 12 + 4 * size_t(n);
        // Keyframed meshes' bones (ARD_AmmoCrate's) keep weights and
        // indices but no positions, which their packed frames have: such a
        // bone places no vertices.
        size_t* arrays[3] = {&b.positions, &b.weights, &b.indices};
        uint32_t counts[3] = {};
        for (int a = 0; a < 3; ++a) {
            if (!header(p, counts[a], flags) || flags != 128 || (a == 2 && counts[2] != counts[1]))
                return false;
            *arrays[a] = p + 12;
            p += 12 + size_t(counts[a]) * (a == 0 ? 12 : 4);
        }
        b.vertices = counts[0] == counts[1] ? counts[0] : 0;
        if (p + 104 > to)
            return false;
        for (size_t c = 0; c < 12; ++c) {
            b.offset[c] = f32(p + 4 * c);
            b.placement[c] = f32(p + 52 + 4 * c);
            if (!sane(b.offset[c]) || !sane(b.placement[c]))
                return false;
        }
        p += 100;
        b.kind = u32(p);
        p += 4;
        b.end = 0;
        if (!header(p, b.clips, flags))
            return true;
        p += 12;
        b.clipsAt = p;
        std::vector<BoneRecord::Clip> clips;
        for (uint32_t c = 0; c < b.clips; ++c) {
            BoneRecord::Clip k;
            if (!header(p, k.nPositions, flags) || p + 12 + 16 * size_t(k.nPositions) > to)
                return true;
            k.positions = p + 12;
            p += 12 + 16 * size_t(k.nPositions);
            if (!header(p, k.nRotations, flags) || p + 12 + 10 * size_t(k.nRotations) + 8 > to)
                return true;
            k.rotations = p + 12;
            p += 12 + 10 * size_t(k.nRotations) + 8;
            clips.push_back(k);
        }
        b.clipList = std::move(clips);
        b.end = p;
        return true;
    }

    // Every bone, in file order, each found by its own fields; then their
    // parents, from each bone's children ((h, h, 4) after its clips, their
    // records following depth first, then the bone's last fields). A bone
    // whose clips or children aren't understood keeps its own children
    // unknown (they're placed as if at the root; failedAt says where).
    // Returns whether all were.
    bool all(std::vector<BoneRecord>& out) {
        out.clear();
        for (size_t at = first(); at; at = first(at + 1)) {
            BoneRecord b;
            if (!record(at, b))
                continue;
            out.push_back(b);
            at = b.indices; // past its name and arrays' headers
        }
        std::map<size_t, int> byStart;
        for (size_t i = 0; i < out.size(); ++i)
            byStart[out[i].start] = int(i);
        std::vector<bool> reached(out.size(), false);
        failedAt = 0;
        // Where bone i's subtree ends, 0 if that's not known.
        std::function<size_t(int, int)> subtree = [&](int i, int depth) -> size_t {
            reached[size_t(i)] = true;
            const size_t start = out[size_t(i)].start, end = out[size_t(i)].end;
            if (!end || depth > 256) {
                if (!failedAt)
                    failedAt = start;
                return 0;
            }
            uint32_t children = 0, flags = 0;
            size_t p = end;
            if (!header(p, children, flags) || flags != 4) {
                if (!failedAt)
                    failedAt = p;
                return 0;
            }
            p += 12;
            for (uint32_t c = 0; c < children; ++c) {
                auto it = byStart.find(p);
                if (it == byStart.end()) {
                    // A bone the search for names missed (an empty name).
                    BoneRecord nb;
                    if (!record(p, nb)) {
                        if (!failedAt)
                            failedAt = p;
                        return 0;
                    }
                    out.push_back(nb);
                    reached.push_back(false);
                    it = byStart.emplace(p, int(out.size() - 1)).first;
                }
                out[size_t(it->second)].parent = i;
                if ((p = subtree(it->second, depth + 1)) == 0)
                    return 0;
            }
            // After its children, the bone's last fields: a 4x3 matrix
            // (identity so far), an i32 7, 2 floats (+-1e38), 2 more, 6
            // (joint limits? +-30 degrees), then two arrays of i32s
            // (usually empty).
            if (p + 92 > to || u32(p + 48) != 7) {
                if (!failedAt)
                    failedAt = p;
                return 0;
            }
            p += 92;
            for (int a = 0; a < 2; ++a) {
                uint32_t n = 0;
                if (!header(p, n, flags) || p + 12 + 4 * size_t(n) > to) {
                    if (!failedAt)
                        failedAt = p;
                    return 0;
                }
                p += 12 + 4 * size_t(n);
            }
            return p;
        };
        for (size_t i = 0; i < out.size(); ++i)
            if (!reached[i])
                subtree(int(i), 0);
        // In file order, where parents come before their children (so a
        // bone's parent is always placed first).
        std::vector<size_t> order(out.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return out[a].start < out[b].start; });
        std::vector<int> newIndex(out.size());
        for (size_t i = 0; i < order.size(); ++i)
            newIndex[order[i]] = int(i);
        std::vector<BoneRecord> sorted;
        for (size_t i : order) {
            sorted.push_back(out[i]);
            if (sorted.back().parent >= 0)
                sorted.back().parent = newIndex[size_t(sorted.back().parent)];
        }
        out = std::move(sorted);
        return failedAt == 0;
    }
};

// 4x3 matrices as row vectors use them: p * m is p's rows X, Y, Z summed
// and the origin added.
using Mat43 = std::array<float, 12>;
Vec3f times(const Mat43& m, const Vec3f& v) {
    return {v[0] * m[0] + v[1] * m[3] + v[2] * m[6] + m[9], v[0] * m[1] + v[1] * m[4] + v[2] * m[7] + m[10],
            v[0] * m[2] + v[1] * m[5] + v[2] * m[8] + m[11]};
}
// a then b.
Mat43 then(const Mat43& a, const Mat43& b) {
    Mat43 out{};
    for (size_t row = 0; row < 4; ++row)
        for (size_t c = 0; c < 3; ++c) {
            float v = row == 3 ? b[9 + c] : 0;
            for (size_t k = 0; k < 3; ++k)
                v += a[row * 3 + k] * b[k * 3 + c];
            out[row * 3 + c] = v;
        }
    return out;
}

// A rotation key's 6 bytes: x, y and z as 11, 11 and 10 bits (0 to the
// top of each standing for -1 to 1, as packed vertices' are), then w - 1
// as a signed 16-bit fraction of 32767. Checked: x^2+y^2+z^2+w^2 comes to
// 1 on every key looked at, and a bone's keys turn it smoothly.
std::array<float, 4> rotationKey(const uint8_t* p) {
    const uint32_t xyz = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    const auto w = int16_t(uint16_t(p[4] | p[5] << 8));
    return {float(xyz & 0x7ff) / 2047 * 2 - 1, float((xyz >> 11) & 0x7ff) / 2047 * 2 - 1,
            float(xyz >> 22) / 1023 * 2 - 1, 1 + float(w) / 32767};
}

// A quaternion's turn as a row-vector matrix: the usual (column-vector)
// rotation matrix of it, taken as rows, which turns row vectors by its
// conjugate. That's how the keys are meant (below).
Mat43 rotation(std::array<float, 4> q) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n <= 0)
        return {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y),
            0, 0, 0};
}

std::array<float, 4> slerp(std::array<float, 4> a, std::array<float, 4> b, float t) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        for (float& c : b)
            c = -c;
        dot = -dot;
    }
    float wa = 1 - t, wb = t;
    if (dot < 0.9995f) {
        const float th = std::acos(std::min(dot, 1.0f)), s = std::sin(th);
        wa = std::sin((1 - t) * th) / s;
        wb = std::sin(t * th) / s;
    }
    return {a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb, a[3] * wa + b[3] * wb};
}

// The Fallen's skeletal animation: a clip per animation, in the order of
// their frames, each clip's keys in seconds at 30 frames a second (a clip
// with no keys after 0 is one frame). A bone at time t in a clip turns by
// its rotation key's conjugate, then by its placement's turn, from its
// placement's origin moved by its position key (in its parent's space).
// Checked on BajFanatic: in clip 0 at time 0 the bones put every vertex
// shared between bones in the same place to within 0.03 units (with the
// placements alone, 1.5), so the vertices are bound in that pose; clips
// match the animations' frame counts exactly (BajFanatic's 4 its 50
// frames, GrigSentryDrone's 30 its 1626).
struct BonePoser {
    const BoneReader& r;
    const std::vector<BoneRecord>& bones;
    // A clip's length in frames: its last key's time.
    int clipFrames(size_t c) const {
        float last = 0;
        for (const BoneRecord& b : bones) {
            if (c >= b.clipList.size())
                continue;
            const BoneRecord::Clip& k = b.clipList[c];
            if (k.nPositions)
                last = std::max(last, r.f32(k.positions + 16 * size_t(k.nPositions - 1)));
            if (k.nRotations)
                last = std::max(last, r.f32(k.rotations + 10 * size_t(k.nRotations - 1)));
        }
        return std::max(1, int(std::lround(last * 30)));
    }
    // Bone b's placement in its parent at time t of clip c.
    Mat43 local(const BoneRecord& b, size_t c, float t) const { return localWith(b, r, b, c, t); }

    // The same with another skeleton's keys for it (`keys`, read by
    // `kr`): an animation set's (below), the bones matched by name.
    static Mat43 localWith(const BoneRecord& b, const BoneReader& r, const BoneRecord& keys, size_t c, float t) {
        Mat43 m = b.placement;
        if (c >= keys.clipList.size())
            return m;
        const BoneRecord::Clip& k = keys.clipList[c];
        if (k.nRotations) {
            size_t i = 0;
            while (i + 1 < k.nRotations && r.f32(k.rotations + 10 * (i + 1)) <= t)
                ++i;
            auto q = rotationKey(r.data.data() + (k.rotations + 10 * i + 4 - r.base));
            if (i + 1 < k.nRotations) {
                const float t0 = r.f32(k.rotations + 10 * i), t1 = r.f32(k.rotations + 10 * (i + 1));
                if (t1 > t0 && t > t0)
                    q = slerp(q, rotationKey(r.data.data() + (k.rotations + 10 * (i + 1) + 4 - r.base)),
                              std::min(1.0f, (t - t0) / (t1 - t0)));
            }
            Mat43 turn = b.placement;
            turn[9] = turn[10] = turn[11] = 0;
            m = then(rotation(q), turn);
            m[9] = b.placement[9];
            m[10] = b.placement[10];
            m[11] = b.placement[11];
        }
        if (k.nPositions) {
            size_t i = 0;
            while (i + 1 < k.nPositions && r.f32(k.positions + 16 * (i + 1)) <= t)
                ++i;
            Vec3f d{r.f32(k.positions + 16 * i + 4), r.f32(k.positions + 16 * i + 8), r.f32(k.positions + 16 * i + 12)};
            if (i + 1 < k.nPositions) {
                const float t0 = r.f32(k.positions + 16 * i), t1 = r.f32(k.positions + 16 * (i + 1));
                if (t1 > t0 && t > t0) {
                    const float f = std::min(1.0f, (t - t0) / (t1 - t0));
                    for (size_t a = 0; a < 3; ++a)
                        d[a] += f * (r.f32(k.positions + 16 * (i + 1) + 4 + 4 * a) - d[a]);
                }
            }
            for (size_t a = 0; a < 3; ++a)
                m[9 + a] += sane(d[a]) ? d[a] : 0;
        }
        return m;
    }
};

// One frame of a skeleton's vertices into `out` (frameVerts of them, in
// packed units by Scale and Origin), each bone placed in its parent by
// `local(i)`. Returns how many vertices some bone placed.
template <class Local>
size_t poseVertices(const BoneReader& r, const std::vector<BoneRecord>& bones, size_t frame, const Vec3f& scale,
                    const Vec3f& origin, Vec3f* out, Local&& local) {
    std::vector<Vec3f> sum(frame, Vec3f{});
    std::vector<float> weight(frame, 0);
    std::vector<Mat43> world(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const BoneRecord& b = bones[i];
        const Mat43 m = local(i);
        world[i] = b.parent >= 0 ? then(m, world[size_t(b.parent)]) : m;
        for (uint32_t j = 0; j < b.vertices; ++j) {
            const size_t v = r.u32(b.indices + 4 * size_t(j));
            const float w = r.f32(b.weights + 4 * size_t(j));
            const Vec3f p{r.f32(b.positions + 12 * size_t(j)), r.f32(b.positions + 12 * size_t(j) + 4),
                          r.f32(b.positions + 12 * size_t(j) + 8)};
            if (v >= frame || !sane(w) || !sane(p[0]) || !sane(p[1]) || !sane(p[2]))
                continue;
            const Vec3f placed = times(world[i], p);
            for (size_t a = 0; a < 3; ++a)
                sum[v][a] += w * placed[a];
            weight[v] += w;
        }
    }
    size_t placed = 0;
    for (size_t v = 0; v < frame; ++v) {
        placed += weight[v] > 0;
        for (size_t a = 0; a < 3; ++a)
            out[v][a] = (weight[v] > 0 ? sum[v][a] / weight[v] : 0) / scale[a] + origin[a];
    }
    return placed;
}

// A vertex is the weighted sum of where its bones put it: p * the bone's
// placement * its parents', up to the root (as row vectors; KiraCellLight's
// one bone, "root", turns its float vertices into exactly its packed ones
// so). The "offset" matrix isn't part of it: with it, bones disagree
// about shared vertices by 5 units, without it by 1.5, and with the keys
// (above) by 0.03. Every frame posed so when the clips add up to the
// mesh's frames, else every frame in clip 0's first pose. Back into packed
// units by Scale and Origin. Returns the bones (0 if the block isn't a
// skeleton); `animated` says whether the frames were posed from the clips.
int poseFromBones(const std::vector<uint8_t>& data, size_t base, size_t from, size_t to, const MeshTail& t,
                  std::vector<Vec3f>& out, bool& animated) {
    BoneReader r{data, base, from, to};
    std::vector<BoneRecord> bones;
    r.all(bones);
    animated = false;
    if (bones.empty())
        return 0;
    const BonePoser poser{r, bones};
    // Which clip and time each frame is.
    const size_t clips = bones[0].clipList.size();
    bool clipsRead = clips > 0;
    for (const BoneRecord& b : bones)
        clipsRead = clipsRead && b.clipList.size() == clips;
    std::vector<std::pair<size_t, float>> frames;
    if (clipsRead)
        for (size_t c = 0; c < clips; ++c)
            for (int f = 0, n = poser.clipFrames(c); f < n; ++f)
                frames.push_back({c, float(f) / 30});
    animated = clipsRead && frames.size() == size_t(t.animFrames);
    const auto frame = size_t(t.frameVerts);
    out.assign(frame * size_t(t.animFrames), Vec3f{});
    const size_t posed = animated ? frames.size() : 1;
    for (size_t f = 0; f < posed; ++f) {
        const size_t placed = poseVertices(r, bones, frame, t.scale, t.origin, out.data() + f * frame, [&](size_t i) {
            return clipsRead ? poser.local(bones[i], animated ? frames[f].first : 0, animated ? frames[f].second : 0)
                             : bones[i].placement;
        });
        // Most vertices have to be placed, or this isn't the mesh's skeleton.
        if (f == 0 && placed * 10 < frame * 9)
            return 0;
    }
    for (size_t f = posed; f < size_t(t.animFrames); ++f)
        std::copy(out.begin(), out.begin() + std::ptrdiff_t(frame), out.begin() + std::ptrdiff_t(f * frame));
    return int(bones.size());
}

// The Fallen's clips by name: before the bones, a table of the mesh's own
// animations, (c, c, 4) and c x (u16 length and name, f32 seconds, i32 2,
// u16 length and group), and after it another (c, c, 4) and c runs of
// vertex keys for them (most empty): (k, k, 5) and k x (f32 time,
// (n, n, 1) and n packed vertices (FrameVerts), (m, m, 64) and m x 12
// bytes, a 24-byte bounding box). A clip with vertex keys is that vertex
// animation (an ammo crate, a creature, a face's Blink or Phonemes); the
// others are the bones' clip of the same index. Checked: ARD_TortureMachine's
// 10 runs last its 10 animations' 400 frames; Quark's 5 clips are
// BarFidget, BaseReference, Blink, Eyebrows and Phonemes, the last three
// with vertex keys (2, 3 and 8), the bones holding still there.
struct NamedClip {
    std::string name;
    std::vector<std::pair<float, size_t>> keys; // time, file offset of its packed vertices
};

std::vector<NamedClip> findClipTable(const BoneReader& r, size_t frameVerts) {
    auto header = [&](size_t at, uint32_t flags, uint32_t& n) {
        uint32_t f = 0;
        return r.header(at, n, f) && f == flags;
    };
    for (size_t at = r.from; at + 12 < r.to; ++at) {
        uint32_t c = 0;
        if (!header(at, 4, c) || c == 0 || c > 4096)
            continue;
        // The names.
        std::vector<NamedClip> clips(c);
        size_t p = at + 12;
        bool ok = true;
        for (NamedClip& clip : clips) {
            std::string group;
            if (!r.name(p, clip.name) || clip.name.empty() || p + 2 + clip.name.size() + 8 > r.to) {
                ok = false;
                break;
            }
            p += 2 + clip.name.size();
            const float seconds = r.f32(p);
            p += 8;
            if (!sane(seconds) || seconds < 0 || !r.name(p, group)) {
                ok = false;
                break;
            }
            p += 2 + group.size();
        }
        if (!ok)
            continue;
        // Its vertex keys: the next (c, c, 4) whose runs read.
        for (size_t q = p; q + 12 < r.to; ++q) {
            uint32_t c2 = 0;
            if (!header(q, 4, c2) || c2 != c)
                continue;
            size_t k = q + 12;
            bool runs = true;
            for (NamedClip& clip : clips) {
                clip.keys.clear();
                uint32_t n = 0;
                if (!header(k, 5, n)) {
                    runs = false;
                    break;
                }
                k += 12;
                for (uint32_t j = 0; j < n && runs; ++j) {
                    uint32_t v = 0, m = 0;
                    if (k + 4 > r.to || !header(k + 4, 1, v) || v != frameVerts) {
                        runs = false;
                        break;
                    }
                    clip.keys.push_back({r.f32(k), k + 16});
                    k += 16 + 4 * size_t(v);
                    if (!header(k, 64, m) || k + 12 + 12 * size_t(m) + 24 > r.to) {
                        runs = false;
                        break;
                    }
                    k += 12 + 12 * size_t(m) + 24;
                }
            }
            if (runs)
                return clips;
        }
        // The names without vertex keys (an animation set's).
        for (NamedClip& clip : clips)
            clip.keys.clear();
        return clips;
    }
    return {};
}

// Fills the frames of a mesh's own animations from its named clips: each
// frame at its time in the clip (30 a second), from the clip's vertex keys
// (between two, blended) or else from its bones. Returns the frames filled.
int framesFromClips(const std::vector<uint8_t>& data, size_t base, size_t from, size_t to, Mesh& mesh,
                    int& bonesUsed) {
    bonesUsed = 0;
    const BoneReader r{data, base, from, to};
    const std::vector<NamedClip> clips = findClipTable(r, size_t(mesh.frameVerts));
    if (clips.empty())
        return 0;
    std::vector<BoneRecord> bones;
    BoneReader(r).all(bones);
    bool bonesUsable = !bones.empty();
    for (const BoneRecord& b : bones)
        bonesUsable = bonesUsable && b.clipList.size() == clips.size();
    const BonePoser poser{r, bones};
    const auto frame = size_t(mesh.frameVerts);
    int filled = 0;
    std::vector<Vec3f> pose(frame);
    for (const MeshAnimSeq& a : mesh.anims) {
        if (a.startFrame < 0 || a.numFrames <= 0 || a.startFrame + a.numFrames > mesh.animFrames)
            continue;
        size_t c = 0;
        while (c < clips.size() && fs::toLower(clips[c].name) != fs::toLower(a.name))
            ++c;
        if (c == clips.size())
            continue;
        const NamedClip& clip = clips[c];
        for (int f = 0; f < a.numFrames; ++f) {
            const float t = float(f) / 30;
            Vec3f* out = mesh.verts.data() + size_t(a.startFrame + f) * frame;
            if (!clip.keys.empty()) {
                size_t i = 0;
                while (i + 1 < clip.keys.size() && clip.keys[i + 1].first <= t)
                    ++i;
                float blend = 0;
                if (i + 1 < clip.keys.size() && clip.keys[i + 1].first > clip.keys[i].first && t > clip.keys[i].first)
                    blend = std::min(1.0f, (t - clip.keys[i].first) / (clip.keys[i + 1].first - clip.keys[i].first));
                for (size_t v = 0; v < frame; ++v) {
                    Vec3f p = unpackVertex(r.u32(clip.keys[i].second + 4 * v));
                    if (blend > 0) {
                        const Vec3f q = unpackVertex(r.u32(clip.keys[i + 1].second + 4 * v));
                        for (size_t k = 0; k < 3; ++k)
                            p[k] += blend * (q[k] - p[k]);
                    }
                    out[v] = p;
                }
                ++filled;
            } else if (bonesUsable &&
                       poseVertices(r, bones, frame, mesh.scale, mesh.origin, pose.data(),
                                    [&](size_t i) { return poser.local(bones[i], c, t); }) * 10 >= frame * 9) {
                std::copy(pose.begin(), pose.end(), out);
                ++filled;
                // One bone is a prop (a chair's "root"), not a character.
                if (bones.size() > 1)
                    bonesUsed = int(bones.size());
            }
        }
    }
    return filled;
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
            MeshAnimSeq::Notify note;
            note.time = r.f32();
            note.function = package.name(r.index());
            a.notifies.push_back(std::move(note));
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
    size_t blockEnd = 0;     // where an imported-model block ends
    bool vertexKeys = false; // its vertices were the clips' keys, not every frame

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
        blockEnd = 0;
        for (size_t at = end; at-- > afterTextures && !found;) {
            Reader c = r;
            c.seek(at);
            MeshTail candidate;
            if (readTail(c, ver, out.lod, candidate) || c.remaining() != 0 || !plausible(candidate, out.lod))
                continue;
            std::vector<Vec3f> verts;
            int keys = 0, bones = 0;
            bool animated = false, perFrame = false;
            const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
            // The standard vertices, and a few bytes before the standard
            // fields (Shared.u's glass and debris fragments have an i32 0).
            if (!out.verts.empty() && out.verts.size() == size_t(candidate.frameVerts) * size_t(candidate.animFrames)) {
                tail = std::move(candidate);
                out.importBlock = at - afterTextures;
                blockEnd = at;
                found = true;
                break;
            }
            if (!findVertexBlock(data, base, afterTextures, at, candidate, verts, keys, perFrame) &&
                (bones = poseFromBones(data, base, afterTextures, at, candidate, verts, animated)) == 0)
                continue;
            // Keyframed (some frames of vertices only): its bones, if they
            // animate every frame, do better.
            if (keys) {
                std::vector<Vec3f> posed;
                bool all = false;
                if (const int n = poseFromBones(data, base, afterTextures, at, candidate, posed, all); n && all) {
                    verts = std::move(posed);
                    bones = n;
                    animated = true;
                    keys = 0;
                }
            }
            out.bones = bones;
            out.bonesAnimated = animated;
            tail = std::move(candidate);
            out.verts = std::move(verts);
            out.keyFrames = keys;
            vertexKeys = perFrame && bones == 0;
            out.vertexBytes = 4;
            out.importBlock = at - afterTextures;
            blockEnd = at;
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
    // The Fallen's clips by name: every frame of the mesh's own animations
    // from their vertex keys or bones (better than what's above where the
    // vertices were only some keyframes).
    if (blockEnd && (out.keyFrames || out.bones || vertexKeys || std::getenv("RELUVA_CLIPS_ALWAYS"))) {
        const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
        int bonesUsed = 0;
        if (const int filled = framesFromClips(data, base, afterTextures, blockEnd, out, bonesUsed); filled > 0) {
            out.framesFromClips = filled;
            out.keyFrames = 0;
            // A skeleton that places the mesh (so it can take shared animations).
            if (bonesUsed && !out.bones) {
                out.bones = bonesUsed;
                out.bonesAnimated = true;
            }
        }
    }

    // Animations whose frames run past the mesh's are left out (The
    // Fallen's JadziaDax has a test one like that), not the whole mesh.
    // The Fallen's characters list many of those: another mesh's.
    for (const MeshAnimSeq& a : out.anims)
        if (a.startFrame < 0 || a.numFrames < 0 || a.startFrame + a.numFrames > out.animFrames)
            out.otherAnims.push_back(a);
    const size_t anims = out.anims.size();
    std::erase_if(out.anims, [&](const MeshAnimSeq& a) {
        return a.startFrame < 0 || a.numFrames < 0 || a.startFrame + a.numFrames > out.animFrames;
    });
    out.droppedAnims = int(anims - out.anims.size());
    return Error::success();
}

struct AnimationSet::Impl {
    std::vector<uint8_t> data;
    size_t base = 0, from = 0, to = 0;
    std::vector<BoneRecord> bones;
    std::vector<int> clipStart, clipFrames; // per clip
    std::vector<std::string> clipNames;     // lower case, if it names them
    BoneReader reader() const { return BoneReader{data, base, from, to}; }
};

AnimationSet::AnimationSet() = default;
AnimationSet::~AnimationSet() = default;
AnimationSet::AnimationSet(AnimationSet&&) noexcept = default;
AnimationSet& AnimationSet::operator=(AnimationSet&&) noexcept = default;

int AnimationSet::frames() const {
    return impl && !impl->clipStart.empty() ? impl->clipStart.back() + impl->clipFrames.back() : 0;
}

int AnimationSet::clipFor(int32_t startFrame, int32_t numFrames, std::string_view name) const {
    if (!impl)
        return -1;
    const auto it = std::lower_bound(impl->clipStart.begin(), impl->clipStart.end(), startFrame);
    if (it == impl->clipStart.end() || *it != startFrame)
        return -1;
    const auto c = size_t(it - impl->clipStart.begin());
    if (impl->clipFrames[c] != numFrames)
        return -1;
    if (!name.empty() && c < impl->clipNames.size() && impl->clipNames[c] != fs::toLower(name))
        return -1;
    return int(c);
}

namespace {

// An export's data after its properties, and the skeleton in it.
Error readBones(const Package& package, int index, std::vector<uint8_t>& data, size_t& from,
                std::vector<BoneRecord>& bones) {
    if (Error err = package.readExport(index, data))
        return err;
    const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
    Reader r(data, base);
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    from = r.pos();
    BoneReader br{data, base, from, base + data.size()};
    br.all(bones);
    if (bones.empty())
        return Error::fail(package.path(index + 1) + ": no skeleton");
    return Error::success();
}

} // namespace

Error readAnimationSet(const Package& package, int index, AnimationSet& out) {
    auto impl = std::make_unique<AnimationSet::Impl>();
    if (Error err = readBones(package, index, impl->data, impl->from, impl->bones))
        return err;
    impl->base = size_t(package.exports()[size_t(index)].serialOffset);
    impl->to = impl->base + impl->data.size();
    const size_t clips = impl->bones[0].clipList.size();
    for (const BoneRecord& b : impl->bones)
        if (b.clipList.size() != clips)
            return Error::fail(package.path(index + 1) + ": its bones' clips don't agree");
    const BoneReader r = impl->reader();
    const BonePoser poser{r, impl->bones};
    int start = 0;
    for (size_t c = 0; c < clips; ++c) {
        impl->clipStart.push_back(start);
        impl->clipFrames.push_back(poser.clipFrames(c));
        start += impl->clipFrames.back();
    }
    if (clips == 0)
        return Error::fail(package.path(index + 1) + ": no clips");
    for (const NamedClip& c : findClipTable(r, 0))
        impl->clipNames.push_back(fs::toLower(c.name));
    if (impl->clipNames.size() != clips)
        impl->clipNames.clear();
    out.impl = std::move(impl);
    return Error::success();
}

Error borrowAnimation(const Package& package, int index, const AnimationSet& set, int clip, const MeshAnimSeq& seq,
                      Mesh& mesh) {
    if (!set.impl || clip < 0 || size_t(clip) >= set.impl->clipFrames.size())
        return Error::fail("no such clip");
    std::vector<uint8_t> data;
    size_t from = 0;
    std::vector<BoneRecord> bones;
    if (Error err = readBones(package, index, data, from, bones))
        return err;
    const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
    const BoneReader r{data, base, from, base + data.size()};
    const BoneReader sr = set.impl->reader();
    // The set's bone for each of the mesh's, by name.
    std::vector<const BoneRecord*> keys(bones.size(), nullptr);
    size_t matched = 0;
    for (size_t i = 0; i < bones.size(); ++i)
        for (const BoneRecord& sb : set.impl->bones)
            if (sb.name == bones[i].name) {
                keys[i] = &sb;
                ++matched;
                break;
            }
    if (matched * 2 < bones.size())
        return Error::fail(package.path(index + 1) + ": its bones aren't the set's");
    const auto frame = size_t(mesh.frameVerts);
    const int n = set.impl->clipFrames[size_t(clip)];
    const size_t first = mesh.verts.size();
    mesh.verts.resize(first + frame * size_t(n));
    for (int f = 0; f < n; ++f)
        poseVertices(r, bones, frame, mesh.scale, mesh.origin, mesh.verts.data() + first + size_t(f) * frame,
                     [&](size_t i) {
                         return keys[i] ? BonePoser::localWith(bones[i], sr, *keys[i], size_t(clip), float(f) / 30)
                                        : bones[i].placement;
                     });
    MeshAnimSeq a = seq;
    a.startFrame = mesh.animFrames;
    a.numFrames = n;
    mesh.anims.push_back(a);
    mesh.animFrames += n;
    return Error::success();
}

std::vector<AnimationSet> findAnimationSets(const PackageSet& packages) {
    std::vector<AnimationSet> out;
    for (const std::string& name : packages.names(".u")) {
        const Package* p = packages.get(name);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i) {
            if (fs::toLower(p->className(int32_t(i) + 1)) != "mesh")
                continue;
            AnimationSet set;
            if (readAnimationSet(*p, int(i), set))
                continue;
            uint32_t vertices = 0;
            for (const BoneRecord& b : set.impl->bones)
                vertices += b.vertices;
            if (vertices == 0)
                out.push_back(std::move(set));
        }
    }
    return out;
}

bool borrowNamedAnimation(const Package& package, int index, const std::vector<AnimationSet>& sets,
                          std::string_view name, Mesh& mesh) {
    if (!mesh.bones || mesh.findAnim(name))
        return false;
    const std::string wanted = fs::toLower(name);
    for (const MeshAnimSeq& seq : mesh.otherAnims) {
        if (fs::toLower(seq.name) != wanted)
            continue;
        for (const AnimationSet& set : sets)
            if (const int clip = set.clipFor(seq.startFrame, seq.numFrames, seq.name); clip >= 0) {
                const MeshAnimSeq copy = seq; // mesh.otherAnims may not survive the borrowing
                return !borrowAnimation(package, index, set, clip, copy, mesh);
            }
        return false;
    }
    return false;
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
    const size_t base = size_t(package.exports()[size_t(index)].serialOffset);
    size_t start = base;
    {
        Reader r(data, base);
        PropertyList props;
        if (beginObject(package, index, r, props))
            return out;
        start = r.pos();
    }
    BoneReader r{data, base, start, base + data.size()};
    const size_t first = r.first();
    if (!first)
        return out;
    std::vector<BoneRecord> bones;
    const bool whole = r.all(bones);
    out.push_back("Bones (offsets from the properties' end; the first at +" + std::to_string(first - start) + "):");
    std::vector<int> depth(bones.size(), 0);
    char line[512];
    int shown = 0;
    for (size_t i = 0; i < bones.size(); ++i) {
        const BoneRecord& b = bones[i];
        depth[i] = b.parent >= 0 ? depth[size_t(b.parent)] + 1 : 1;
        const int d = depth[i] * 2;
        std::snprintf(line, sizeof line, "%*s%s: %u vertices, origin (%g %g %g), kind %u, %u clips", d, "",
                      b.name.c_str(), b.vertices, double(b.placement[9]), double(b.placement[10]),
                      double(b.placement[11]), b.kind, b.clips);
        out.push_back(line);
        if (!b.end) {
            out.push_back(std::string(size_t(d) + 2, ' ') + "its clips aren't understood, so neither are its children");
            continue;
        }
        std::string keys;
        size_t p = b.clipsAt;
        for (uint32_t c = 0; c < b.clips; ++c) {
            const uint32_t np = r.u32(p);
            const size_t pos = p + 12;
            p = pos + 16 * size_t(np);
            const uint32_t nr = r.u32(p);
            const size_t rot = p + 12;
            p = rot + 10 * size_t(nr) + 8;
            if (c < 6) {
                std::snprintf(line, sizeof line, " %u/%u", np, nr);
                keys += line;
            }
            // A few clips' keys, decoded, for checking.
            if (nr >= 3 && shown < 3) {
                ++shown;
                out.push_back(std::string(size_t(d) + 4, ' ') + "clip " + std::to_string(c) +
                              " rotation keys (time: x y z w):");
                for (uint32_t j = 0; j < nr && j < 12; ++j) {
                    const auto q = rotationKey(data.data() + (rot + 10 * size_t(j) + 4 - base));
                    std::snprintf(line, sizeof line, "%*s%.4f: %.3f %.3f %.3f %.3f", d + 6, "",
                                  double(r.f32(rot + 10 * size_t(j))), double(q[0]), double(q[1]), double(q[2]),
                                  double(q[3]));
                    out.push_back(line);
                }
            }
        }
        out.push_back(std::string(size_t(d) + 2, ' ') + "keys (position/rotation) of the first clips:" + keys);
    }
    if (whole) {
        out.push_back(std::to_string(bones.size()) + " bones, all in one tree");
    } else {
        int w = std::snprintf(line, sizeof line,
                              "%zu bones; at +%zu not understood (some bones' parents not known, placed from the "
                              "root):",
                              bones.size(), r.failedAt - start);
        for (size_t k = 0; k < 64 && r.failedAt + k < base + data.size(); ++k)
            w += std::snprintf(line + w, sizeof line - size_t(w), " %02x", data[r.failedAt + k - base]);
        out.push_back(line);
    }
    return out;
}

} // namespace reluva::unreal
