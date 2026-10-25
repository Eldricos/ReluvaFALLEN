// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/md3.h"

#include "common/fs.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kVersion = 15;
constexpr size_t kHeaderSize = 108, kFrameSize = 56, kTagSize = 112, kSurfaceSize = 108, kShaderSize = 68;
constexpr float kXyzScale = 1.0f / 64;
constexpr float kTau = 6.28318530718f;

class Reader {
public:
    Reader(const std::vector<uint8_t>& d, size_t at) : d_(d), at_(at) {}
    int32_t i32() {
        const uint8_t* p = &d_[at_];
        at_ += 4;
        return static_cast<int32_t>(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                                    uint32_t(p[3]) << 24);
    }
    int16_t i16() {
        const uint8_t* p = &d_[at_];
        at_ += 2;
        return static_cast<int16_t>(uint16_t(p[0] | p[1] << 8));
    }
    float f32() {
        const uint32_t bits = static_cast<uint32_t>(i32());
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    Vec3 vec() {
        Vec3 v;
        for (float& f : v)
            f = f32();
        return v;
    }
    std::string name(size_t size) {
        size_t len = 0;
        while (len < size && d_[at_ + len])
            ++len;
        std::string s(reinterpret_cast<const char*>(&d_[at_]), len);
        at_ += size;
        return s;
    }

private:
    const std::vector<uint8_t>& d_;
    size_t at_;
};

bool fits(size_t size, int64_t offset, int64_t count, size_t record) {
    return offset >= 0 && count >= 0 && uint64_t(offset) + uint64_t(count) * record <= size;
}

} // namespace

const Md3Tag* Md3Model::tag(int frame, std::string_view tagName) const {
    if (frame < 0 || frame >= int(frames.size()))
        return nullptr;
    for (int i = 0; i < tagCount; ++i) {
        const Md3Tag& t = tags[size_t(frame * tagCount + i)];
        if (t.name == tagName)
            return &t;
    }
    return nullptr;
}

Error parseMd3(const std::vector<uint8_t>& data, Md3Model& out) {
    out = Md3Model{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "IDP3", 4) != 0)
        return Error::fail("Not an MD3 model");
    Reader h(data, 4);
    if (const int32_t version = h.i32(); version != kVersion)
        return Error::fail("Unsupported MD3 version " + std::to_string(version));
    out.name = h.name(64);
    h.i32(); // flags
    const int32_t numFrames = h.i32(), numTags = h.i32(), numSurfaces = h.i32();
    h.i32(); // skins, unused
    const int32_t ofsFrames = h.i32(), ofsTags = h.i32(), ofsSurfaces = h.i32();
    if (numFrames < 1 || numTags < 0 || numSurfaces < 0 || !fits(data.size(), ofsFrames, numFrames, kFrameSize) ||
        !fits(data.size(), ofsTags, int64_t(numFrames) * numTags, kTagSize))
        return Error::fail("MD3 frames or tags lie outside the file");

    Reader fr(data, size_t(ofsFrames));
    for (int i = 0; i < numFrames; ++i) {
        Md3Frame f;
        f.mins = fr.vec();
        f.maxs = fr.vec();
        f.origin = fr.vec();
        f.radius = fr.f32();
        f.name = fr.name(16);
        out.frames.push_back(f);
    }
    out.tagCount = numTags;
    Reader tr(data, size_t(ofsTags));
    for (int i = 0; i < numFrames * numTags; ++i) {
        Md3Tag t;
        t.name = tr.name(64);
        t.origin = tr.vec();
        for (Vec3& a : t.axis)
            a = tr.vec();
        out.tags.push_back(t);
    }

    int64_t at = ofsSurfaces;
    for (int s = 0; s < numSurfaces; ++s) {
        const std::string where = "MD3 surface " + std::to_string(s);
        if (!fits(data.size(), at, 1, kSurfaceSize) || std::memcmp(&data[size_t(at)], "IDP3", 4) != 0)
            return Error::fail(where + " lies outside the file or is damaged");
        Reader sr(data, size_t(at) + 4);
        Md3Surface surf;
        surf.name = sr.name(64);
        sr.i32(); // flags
        const int32_t sFrames = sr.i32(), numShaders = sr.i32(), numVerts = sr.i32(), numTris = sr.i32();
        const int32_t ofsTris = sr.i32(), ofsShaders = sr.i32(), ofsSt = sr.i32(), ofsXyz = sr.i32(),
                      ofsEnd = sr.i32();
        if (sFrames != numFrames)
            return Error::fail(where + " has " + std::to_string(sFrames) + " frames, the model " +
                               std::to_string(numFrames));
        if (numVerts < 0 || numTris < 0 || numShaders < 0 || ofsEnd <= 0 ||
            !fits(data.size(), at + ofsShaders, numShaders, kShaderSize) ||
            !fits(data.size(), at + ofsTris, numTris, 12) || !fits(data.size(), at + ofsSt, numVerts, 8) ||
            !fits(data.size(), at + ofsXyz, int64_t(numVerts) * numFrames, 8) ||
            !fits(data.size(), at, ofsEnd, 1))
            return Error::fail(where + " has parts outside the file");

        Reader shr(data, size_t(at + ofsShaders));
        for (int i = 0; i < numShaders; ++i) {
            surf.shaders.push_back(shr.name(64));
            shr.i32(); // index, for the original's own use
        }
        Reader trr(data, size_t(at + ofsTris));
        for (int i = 0; i < numTris * 3; ++i) {
            const int32_t v = trr.i32();
            if (v < 0 || v >= numVerts)
                return Error::fail(where + ": triangle " + std::to_string(i / 3) + " uses vertex " +
                                   std::to_string(v) + " (of " + std::to_string(numVerts) + ")");
            surf.indices.push_back(uint32_t(v));
        }
        Reader str(data, size_t(at + ofsSt));
        for (int i = 0; i < numVerts; ++i) {
            const float u = str.f32();
            surf.st.push_back({u, str.f32()});
        }
        // Positions in 1/64 units; normals as two angles in a byte each:
        // latitude (high byte) around Z, longitude (low byte) from +Z.
        surf.vertexCount = numVerts;
        Reader xr(data, size_t(at + ofsXyz));
        for (int i = 0; i < numVerts * numFrames; ++i) {
            Md3Vertex v;
            for (float& c : v.pos)
                c = xr.i16() * kXyzScale;
            const uint16_t n = static_cast<uint16_t>(xr.i16());
            const float lat = float((n >> 8) & 0xff) * kTau / 256, lng = float(n & 0xff) * kTau / 256;
            v.normal = {std::cos(lat) * std::sin(lng), std::sin(lat) * std::sin(lng), std::cos(lng)};
            surf.vertices.push_back(v);
        }
        out.surfaces.push_back(std::move(surf));
        at += ofsEnd;
    }
    return Error::success();
}

std::map<std::string, std::string> parseSkin(std::string_view text) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string line(text.substr(start, end - start));
        start = end + 1;
        // Trim spaces, tabs and carriage returns around both halves.
        auto trim = [](std::string s) {
            const size_t a = s.find_first_not_of(" \t\r\"");
            const size_t b = s.find_last_not_of(" \t\r\"");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        const size_t comma = line.find(',');
        if (comma == std::string::npos)
            continue;
        const std::string surface = fs::toLower(trim(line.substr(0, comma)));
        const std::string shader = trim(line.substr(comma + 1));
        if (surface.empty() || shader.empty() || surface.rfind("tag_", 0) == 0)
            continue;
        out[surface] = shader;
    }
    return out;
}

} // namespace reluva::idtech3
