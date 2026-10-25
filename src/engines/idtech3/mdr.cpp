// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// .mdr models (Raven's skeletal format, "RDM5" version 2: Elite Force's
// player models). The layout, the packed bones and how a vertex is placed
// by its bones were read from the GPL ioquake3 renderer (qfiles.h,
// tr_model.c, tr_animation.c); docs/formats/idtech3/mdr.md describes
// them. This is new code written from that description.
//
// The model is posed here, once for each frame, into the same form as an
// .md3's (each frame's vertex positions and normals, each frame's tags),
// so everything that draws or attaches to .md3 models takes these too.

#include "engines/idtech3/md3.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kVersion = 2;
constexpr size_t kHeaderSize = 104;
constexpr size_t kFrameHeader = 40;    // bounds, local origin, radius
constexpr size_t kFrameNameSize = 16;  // after them, in frames that aren't packed
constexpr size_t kBoneSize = 48;       // 3 x 4 floats
constexpr size_t kPackedBoneSize = 24; // 12 16-bit numbers
constexpr size_t kLodSize = 12, kSurfaceSize = 168, kVertexHeader = 24, kWeightSize = 20, kTagSize = 36;
constexpr int kMaxBones = 128;

uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}
int32_t i32(const std::vector<uint8_t>& d, size_t at) { return static_cast<int32_t>(u32(d, at)); }
float f32(const std::vector<uint8_t>& d, size_t at) {
    const uint32_t bits = u32(d, at);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint16_t u16(const std::vector<uint8_t>& d, size_t at) { return uint16_t(d[at] | d[at + 1] << 8); }
Vec3 vec(const std::vector<uint8_t>& d, size_t at) { return {f32(d, at), f32(d, at + 4), f32(d, at + 8)}; }
std::string name(const std::vector<uint8_t>& d, size_t at, size_t size) {
    size_t len = 0;
    while (len < size && d[at + len])
        ++len;
    return std::string(reinterpret_cast<const char*>(&d[at]), len);
}
bool inside(size_t size, int64_t at, int64_t length) { return at >= 0 && length >= 0 && uint64_t(at + length) <= size; }

// A bone: rows of a 3 x 4 matrix, the rotation in the first three columns
// and the position in the fourth.
using Bone = std::array<std::array<float, 4>, 3>;

// A packed bone: the position's x, y, z as 16-bit numbers biased by 32768
// in 64ths of a unit, then the rotation's rows likewise in 32766ths.
Bone unpack(const std::vector<uint8_t>& d, size_t at) {
    Bone b{};
    for (int r = 0; r < 3; ++r)
        b[size_t(r)][3] = float(int(u16(d, at + 2 * size_t(r))) - 32768) / 64.0f;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            b[size_t(r)][size_t(c)] = float(int(u16(d, at + 6 + 6 * size_t(r) + 2 * size_t(c))) - 32768) / 32766.0f;
    return b;
}

} // namespace

Error parseMdr(const std::vector<uint8_t>& data, Md3Model& out, int lod) {
    out = Md3Model{};
    const size_t size = data.size();
    if (size < kHeaderSize || std::memcmp(data.data(), "RDM5", 4) != 0)
        return Error::fail("Not an MDR model");
    if (i32(data, 4) != kVersion)
        return Error::fail("Unsupported MDR version " + std::to_string(i32(data, 4)));
    out.name = name(data, 8, 64);
    const int32_t numFrames = i32(data, 72), numBones = i32(data, 76), ofsFrames = i32(data, 80);
    const int32_t numLods = i32(data, 84), ofsLods = i32(data, 88), numTags = i32(data, 92), ofsTags = i32(data, 96);
    if (numFrames < 1 || numBones < 1 || numBones > kMaxBones || numLods < 1)
        return Error::fail("MDR model with bad counts");

    // The frames: their bones, packed when the offset is negative.
    const bool packed = ofsFrames < 0;
    const int64_t framesAt = packed ? -int64_t(ofsFrames) : ofsFrames;
    const size_t frameSize =
        packed ? kFrameHeader + size_t(numBones) * kPackedBoneSize
               : kFrameHeader + kFrameNameSize + size_t(numBones) * kBoneSize;
    if (!inside(size, framesAt, int64_t(frameSize) * numFrames))
        return Error::fail("MDR frames outside the file");
    std::vector<Bone> bones(size_t(numFrames) * size_t(numBones));
    for (int32_t f = 0; f < numFrames; ++f) {
        const size_t at = size_t(framesAt) + size_t(f) * frameSize;
        Md3Frame frame;
        frame.mins = vec(data, at);
        frame.maxs = vec(data, at + 12);
        frame.origin = vec(data, at + 24);
        frame.radius = f32(data, at + 36);
        size_t boneAt = at + kFrameHeader;
        if (!packed) {
            frame.name = name(data, boneAt, kFrameNameSize);
            boneAt += kFrameNameSize;
        }
        out.frames.push_back(frame);
        for (int32_t b = 0; b < numBones; ++b) {
            Bone& bone = bones[size_t(f) * size_t(numBones) + size_t(b)];
            if (packed) {
                bone = unpack(data, boneAt + size_t(b) * kPackedBoneSize);
            } else {
                for (size_t r = 0; r < 3; ++r)
                    for (size_t c = 0; c < 4; ++c)
                        bone[r][c] = f32(data, boneAt + size_t(b) * kBoneSize + (r * 4 + c) * 4);
            }
        }
    }

    // The level of detail's surfaces (0 the most detailed; the levels
    // follow each other, each giving where it ends). Each vertex is
    // placed by its weights: the sum of each bone's matrix times the
    // weight's offset, times the weight; its normal likewise, rotated only.
    int64_t lodAt = ofsLods;
    if (!inside(size, lodAt, kLodSize))
        return Error::fail("MDR levels of detail outside the file");
    for (int l = 0; l < std::min(lod, numLods - 1); ++l) {
        const int64_t next = lodAt + i32(data, size_t(lodAt) + 8);
        if (next <= lodAt || !inside(size, next, kLodSize))
            break; // the last good one
        lodAt = next;
    }
    const int32_t numSurfaces = i32(data, size_t(lodAt));
    int64_t surfaceAt = lodAt + i32(data, size_t(lodAt) + 4);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (!inside(size, surfaceAt, kSurfaceSize))
            return Error::fail("MDR surface outside the file");
        const size_t at = size_t(surfaceAt);
        Md3Surface surface;
        surface.name = fs::toLower(name(data, at + 4, 64));
        surface.shaders.push_back(name(data, at + 68, 64));
        const int32_t numVerts = i32(data, at + 140), ofsVerts = i32(data, at + 144);
        const int32_t numTriangles = i32(data, at + 148), ofsTriangles = i32(data, at + 152);
        const int32_t ofsEnd = i32(data, at + 164);
        if (numVerts < 0 || numTriangles < 0 || ofsEnd <= 0)
            return Error::fail("MDR surface with bad counts");
        if (!inside(size, surfaceAt + ofsTriangles, int64_t(numTriangles) * 12))
            return Error::fail("MDR triangles outside the file");
        for (int32_t t = 0; t < numTriangles; ++t)
            for (size_t k = 0; k < 3; ++k) {
                const int32_t index = i32(data, at + size_t(ofsTriangles) + size_t(t) * 12 + k * 4);
                if (index < 0 || index >= numVerts)
                    return Error::fail("MDR triangle with a vertex out of range");
                surface.indices.push_back(uint32_t(index));
            }
        // The vertices' weights, then each frame's positions from them.
        struct Weight {
            int32_t bone;
            float weight;
            Vec3 offset;
        };
        std::vector<std::vector<Weight>> weights(static_cast<size_t>(numVerts));
        std::vector<Vec3> normals(static_cast<size_t>(numVerts));
        int64_t vertexAt = surfaceAt + ofsVerts;
        for (int32_t v = 0; v < numVerts; ++v) {
            if (!inside(size, vertexAt, kVertexHeader))
                return Error::fail("MDR vertex outside the file");
            const size_t va = size_t(vertexAt);
            normals[size_t(v)] = vec(data, va);
            surface.st.push_back({f32(data, va + 12), f32(data, va + 16)});
            const int32_t numWeights = i32(data, va + 20);
            if (numWeights < 0 || !inside(size, vertexAt + int64_t(kVertexHeader), int64_t(numWeights) * kWeightSize))
                return Error::fail("MDR vertex weights outside the file");
            for (int32_t w = 0; w < numWeights; ++w) {
                const size_t wa = va + kVertexHeader + size_t(w) * kWeightSize;
                const Weight weight{i32(data, wa), f32(data, wa + 4), vec(data, wa + 8)};
                if (weight.bone < 0 || weight.bone >= numBones)
                    return Error::fail("MDR weight on a bone out of range");
                weights[size_t(v)].push_back(weight);
            }
            vertexAt += int64_t(kVertexHeader) + int64_t(numWeights) * int64_t(kWeightSize);
        }
        surface.vertexCount = numVerts;
        surface.vertices.resize(size_t(numFrames) * size_t(numVerts));
        for (int32_t f = 0; f < numFrames; ++f)
            for (int32_t v = 0; v < numVerts; ++v) {
                Md3Vertex& out = surface.vertices[size_t(f) * size_t(numVerts) + size_t(v)];
                for (const Weight& w : weights[size_t(v)]) {
                    const Bone& b = bones[size_t(f) * size_t(numBones) + size_t(w.bone)];
                    const Vec3& n = normals[size_t(v)];
                    for (size_t r = 0; r < 3; ++r) {
                        out.pos[r] += w.weight * (b[r][0] * w.offset[0] + b[r][1] * w.offset[1] +
                                                  b[r][2] * w.offset[2] + b[r][3]);
                        out.normal[r] += w.weight * (b[r][0] * n[0] + b[r][1] * n[1] + b[r][2] * n[2]);
                    }
                }
                const float len = std::sqrt(out.normal[0] * out.normal[0] + out.normal[1] * out.normal[1] +
                                            out.normal[2] * out.normal[2]);
                if (len > 1e-6f)
                    for (float& c : out.normal)
                        c /= len;
            }
        out.surfaces.push_back(std::move(surface));
        surfaceAt += ofsEnd;
    }

    // Tags: bones by name, each frame its bone's axes (the matrix's
    // columns) and position.
    if (numTags < 0 || !inside(size, ofsTags, int64_t(numTags) * int64_t(kTagSize)))
        return Error::fail("MDR tags outside the file");
    out.tagCount = numTags;
    for (int32_t f = 0; f < numFrames; ++f)
        for (int32_t t = 0; t < numTags; ++t) {
            const size_t ta = size_t(ofsTags) + size_t(t) * kTagSize;
            const int32_t boneIndex = i32(data, ta);
            Md3Tag tag;
            tag.name = name(data, ta + 4, 32);
            if (boneIndex >= 0 && boneIndex < numBones) {
                const Bone& b = bones[size_t(f) * size_t(numBones) + size_t(boneIndex)];
                for (size_t axis = 0; axis < 3; ++axis)
                    for (size_t k = 0; k < 3; ++k)
                        tag.axis[axis][k] = b[k][axis];
                tag.origin = {b[0][3], b[1][3], b[2][3]};
            }
            out.tags.push_back(tag);
        }
    return Error::success();
}

Error parseModel(const std::vector<uint8_t>& data, Md3Model& out, int lod) {
    if (data.size() >= 4 && std::memcmp(data.data(), "RDM5", 4) == 0)
        return parseMdr(data, out, lod);
    return parseMd3(data, out);
}

} // namespace reluva::idtech3
