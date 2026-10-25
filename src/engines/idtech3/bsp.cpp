// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/bsp.h"

#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kVersion = 46;
constexpr int kLumps = 17;
constexpr size_t kHeaderSize = 8 + kLumps * 8;

enum Lump {
    Entities, Shaders, Planes, Nodes, Leafs, LeafSurfaces, LeafBrushes, Models,
    Brushes, BrushSides, Vertices, Indices, Fogs, Surfaces, Lightmaps, LightGrid, Visibility,
};

const char* const kLumpNames[kLumps] = {
    "entities", "shaders", "planes", "nodes", "leafs", "leaf surfaces", "leaf brushes", "models",
    "brushes", "brush sides", "vertices", "indices", "fogs", "surfaces", "lightmaps", "light grid", "visibility",
};

// Reads little-endian fields out of one record.
class Fields {
public:
    explicit Fields(const uint8_t* p) : p_(p) {}
    int32_t i32() {
        const uint32_t v = uint32_t(p_[0]) | uint32_t(p_[1]) << 8 | uint32_t(p_[2]) << 16 | uint32_t(p_[3]) << 24;
        p_ += 4;
        return static_cast<int32_t>(v);
    }
    float f32() {
        const uint32_t bits = static_cast<uint32_t>(i32());
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    uint8_t u8() { return *p_++; }
    // A fixed-size, NUL-padded name.
    std::string name(size_t size) {
        size_t len = 0;
        while (len < size && p_[len])
            ++len;
        std::string s(reinterpret_cast<const char*>(p_), len);
        p_ += size;
        return s;
    }
    template <size_t N> void i32s(std::array<int32_t, N>& a) {
        for (auto& v : a)
            v = i32();
    }
    template <size_t N> void f32s(std::array<float, N>& a) {
        for (auto& v : a)
            v = f32();
    }

private:
    const uint8_t* p_;
};

struct LumpRange {
    size_t offset = 0, size = 0;
};

// Reads every record of a lump with `parse`, after checking its size is a
// whole number of records.
template <typename T, typename Parse>
Error readRecords(const std::vector<uint8_t>& data, const LumpRange& lump, int index, size_t recordSize,
                  std::vector<T>& out, Parse parse) {
    if (lump.size % recordSize)
        return Error::fail(std::string("BSP ") + kLumpNames[index] + " lump is " + std::to_string(lump.size) +
                           " bytes, not a multiple of " + std::to_string(recordSize));
    out.resize(lump.size / recordSize);
    for (size_t i = 0; i < out.size(); ++i) {
        Fields f(data.data() + lump.offset + i * recordSize);
        parse(f, out[i]);
    }
    return Error::success();
}

bool inRange(int64_t first, int64_t count, size_t size) {
    return first >= 0 && count >= 0 && first + count <= static_cast<int64_t>(size);
}

bool validIndex(int64_t i, size_t size) { return i >= 0 && i < static_cast<int64_t>(size); }

std::string num(int64_t v) { return std::to_string(v); }

// "BSP surface 12: shader 40 (of 38)"
Error bad(const std::string& what, size_t i, const std::string& detail) {
    return Error::fail("BSP " + what + " " + std::to_string(i) + ": " + detail);
}
std::string range(const char* field, int64_t first, int64_t count, size_t size) {
    return std::string(field) + " " + num(first) + "+" + num(count) + " (of " + num(int64_t(size)) + ")";
}
std::string index(const char* field, int64_t v, size_t size) {
    return std::string(field) + " " + num(v) + " (of " + num(int64_t(size)) + ")";
}

// Values the original engine tolerates, made consistent so the checks
// below and the rest of the engine needn't special-case them. See bsp.md.
void normalize(Bsp& b) {
    const size_t lightmaps = b.lightmapCount();
    for (BspSurface& s : b.surfaces) {
        // A lightmap past the end: lit by its vertex colours instead.
        if (s.lightmap >= 0 && size_t(s.lightmap) >= lightmaps) {
            s.lightmap = -1;
            ++b.fixes.lightmaps;
        }
        // A fog that doesn't exist: no fog.
        if (s.fog < -1 || (s.fog >= 0 && size_t(s.fog) >= b.fogs.size())) {
            s.fog = -1;
            ++b.fixes.fogs;
        }
        // A type we don't know: kept as Bad, which nothing draws.
        const int32_t type = static_cast<int32_t>(s.type);
        if (type < 0 || type > 4) {
            s.type = SurfaceType::Bad;
            ++b.fixes.types;
        }
        // Bad surfaces and flares use neither vertices nor indices.
        if (s.type == SurfaceType::Bad && (s.numVertices || s.numIndices)) {
            s.firstVertex = s.numVertices = s.firstIndex = s.numIndices = 0;
            ++b.fixes.flares;
        }
        if (s.type == SurfaceType::Flare && (s.numVertices || s.numIndices)) {
            s.firstVertex = s.numVertices = s.firstIndex = s.numIndices = 0;
            ++b.fixes.flares;
        }
    }
}

Error validate(const Bsp& b) {
    for (size_t i = 0; i < b.nodes.size(); ++i) {
        const BspNode& n = b.nodes[i];
        if (!validIndex(n.plane, b.planes.size()))
            return bad("node", i, index("plane", n.plane, b.planes.size()));
        for (int32_t c : n.children) {
            if (c >= 0 && !validIndex(c, b.nodes.size()))
                return bad("node", i, index("child node", c, b.nodes.size()));
            if (c < 0 && !validIndex(-int64_t(c) - 1, b.leafs.size()))
                return bad("node", i, index("child leaf", -int64_t(c) - 1, b.leafs.size()));
        }
    }
    for (size_t i = 0; i < b.leafs.size(); ++i) {
        const BspLeaf& l = b.leafs[i];
        if (!inRange(l.firstLeafSurface, l.numLeafSurfaces, b.leafSurfaces.size()))
            return bad("leaf", i, range("leaf surfaces", l.firstLeafSurface, l.numLeafSurfaces, b.leafSurfaces.size()));
        if (!inRange(l.firstLeafBrush, l.numLeafBrushes, b.leafBrushes.size()))
            return bad("leaf", i, range("leaf brushes", l.firstLeafBrush, l.numLeafBrushes, b.leafBrushes.size()));
        if (b.visClusterBytes && l.cluster >= b.visClusters)
            return bad("leaf", i, index("cluster", l.cluster, size_t(b.visClusters)));
    }
    for (size_t i = 0; i < b.leafSurfaces.size(); ++i)
        if (!validIndex(b.leafSurfaces[i], b.surfaces.size()))
            return bad("leaf surface", i, index("surface", b.leafSurfaces[i], b.surfaces.size()));
    for (size_t i = 0; i < b.leafBrushes.size(); ++i)
        if (!validIndex(b.leafBrushes[i], b.brushes.size()))
            return bad("leaf brush", i, index("brush", b.leafBrushes[i], b.brushes.size()));
    for (size_t i = 0; i < b.models.size(); ++i) {
        const BspModel& m = b.models[i];
        if (!inRange(m.firstSurface, m.numSurfaces, b.surfaces.size()))
            return bad("model", i, range("surfaces", m.firstSurface, m.numSurfaces, b.surfaces.size()));
        if (!inRange(m.firstBrush, m.numBrushes, b.brushes.size()))
            return bad("model", i, range("brushes", m.firstBrush, m.numBrushes, b.brushes.size()));
    }
    for (size_t i = 0; i < b.brushes.size(); ++i) {
        const BspBrush& br = b.brushes[i];
        if (!inRange(br.firstSide, br.numSides, b.brushSides.size()))
            return bad("brush", i, range("sides", br.firstSide, br.numSides, b.brushSides.size()));
        if (!validIndex(br.shader, b.shaders.size()))
            return bad("brush", i, index("shader", br.shader, b.shaders.size()));
    }
    for (size_t i = 0; i < b.brushSides.size(); ++i) {
        if (!validIndex(b.brushSides[i].plane, b.planes.size()))
            return bad("brush side", i, index("plane", b.brushSides[i].plane, b.planes.size()));
        if (!validIndex(b.brushSides[i].shader, b.shaders.size()))
            return bad("brush side", i, index("shader", b.brushSides[i].shader, b.shaders.size()));
    }
    for (size_t i = 0; i < b.fogs.size(); ++i)
        if (!validIndex(b.fogs[i].brush, b.brushes.size()))
            return bad("fog", i, index("brush", b.fogs[i].brush, b.brushes.size()));
    for (size_t i = 0; i < b.surfaces.size(); ++i) {
        const BspSurface& s = b.surfaces[i];
        if (!validIndex(s.shader, b.shaders.size()))
            return bad("surface", i, index("shader", s.shader, b.shaders.size()));
        if (!inRange(s.firstVertex, s.numVertices, b.vertices.size()))
            return bad("surface", i, range("vertices", s.firstVertex, s.numVertices, b.vertices.size()));
        if (!inRange(s.firstIndex, s.numIndices, b.indices.size()))
            return bad("surface", i, range("indices", s.firstIndex, s.numIndices, b.indices.size()));
        for (int32_t k = 0; k < s.numIndices; ++k) {
            const int32_t v = b.indices[size_t(s.firstIndex + k)];
            if (!validIndex(v, size_t(s.numVertices)))
                return bad("surface", i, "index " + num(k) + " is vertex " + num(v) + " (of " + num(s.numVertices) + ")");
        }
        if (s.type == SurfaceType::Patch &&
            (s.patchWidth < 1 || s.patchHeight < 1 || int64_t(s.patchWidth) * s.patchHeight != s.numVertices))
            return bad("surface", i, "patch " + num(s.patchWidth) + "x" + num(s.patchHeight) + " but " +
                                         num(s.numVertices) + " vertices");
    }
    return Error::success();
}

} // namespace

bool Bsp::clusterVisible(int from, int to) const {
    if (visData.empty() || from < 0 || to < 0 || from >= visClusters || to >= visClusters)
        return true;
    const uint8_t byte = visData[size_t(from) * size_t(visClusterBytes) + size_t(to >> 3)];
    return (byte >> (to & 7)) & 1;
}

Error parseBsp(const std::vector<uint8_t>& data, Bsp& out) {
    out = Bsp{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "IBSP", 4) != 0)
        return Error::fail("Not an IBSP map");
    Fields header(data.data() + 4);
    out.version = header.i32();
    if (out.version != kVersion)
        return Error::fail("Unsupported BSP version " + std::to_string(out.version) + " (expected 46)");
    LumpRange lumps[kLumps];
    for (int i = 0; i < kLumps; ++i) {
        const int32_t offset = header.i32(), size = header.i32();
        if (offset < 0 || size < 0 || size_t(offset) + size_t(size) > data.size())
            return Error::fail(std::string("BSP ") + kLumpNames[i] + " lump lies outside the file");
        lumps[i] = {size_t(offset), size_t(size)};
    }

    out.entities.assign(reinterpret_cast<const char*>(data.data() + lumps[Entities].offset), lumps[Entities].size);
    if (const size_t nul = out.entities.find('\0'); nul != std::string::npos)
        out.entities.resize(nul);

    Error err;
    auto check = [&](Error e) {
        if (e && !err)
            err = std::move(e);
    };
    check(readRecords(data, lumps[Shaders], Shaders, 72, out.shaders, [](Fields& f, BspShader& s) {
        s.name = f.name(64);
        s.surfaceFlags = f.i32();
        s.contentFlags = f.i32();
    }));
    check(readRecords(data, lumps[Planes], Planes, 16, out.planes, [](Fields& f, BspPlane& p) {
        f.f32s(p.normal);
        p.dist = f.f32();
    }));
    check(readRecords(data, lumps[Nodes], Nodes, 36, out.nodes, [](Fields& f, BspNode& n) {
        n.plane = f.i32();
        f.i32s(n.children);
        f.i32s(n.mins);
        f.i32s(n.maxs);
    }));
    check(readRecords(data, lumps[Leafs], Leafs, 48, out.leafs, [](Fields& f, BspLeaf& l) {
        l.cluster = f.i32();
        l.area = f.i32();
        f.i32s(l.mins);
        f.i32s(l.maxs);
        l.firstLeafSurface = f.i32();
        l.numLeafSurfaces = f.i32();
        l.firstLeafBrush = f.i32();
        l.numLeafBrushes = f.i32();
    }));
    auto readInt = [](Fields& f, int32_t& v) { v = f.i32(); };
    check(readRecords(data, lumps[LeafSurfaces], LeafSurfaces, 4, out.leafSurfaces, readInt));
    check(readRecords(data, lumps[LeafBrushes], LeafBrushes, 4, out.leafBrushes, readInt));
    check(readRecords(data, lumps[Models], Models, 40, out.models, [](Fields& f, BspModel& m) {
        f.f32s(m.mins);
        f.f32s(m.maxs);
        m.firstSurface = f.i32();
        m.numSurfaces = f.i32();
        m.firstBrush = f.i32();
        m.numBrushes = f.i32();
    }));
    check(readRecords(data, lumps[Brushes], Brushes, 12, out.brushes, [](Fields& f, BspBrush& b) {
        b.firstSide = f.i32();
        b.numSides = f.i32();
        b.shader = f.i32();
    }));
    check(readRecords(data, lumps[BrushSides], BrushSides, 8, out.brushSides, [](Fields& f, BspBrushSide& s) {
        s.plane = f.i32();
        s.shader = f.i32();
    }));
    check(readRecords(data, lumps[Vertices], Vertices, 44, out.vertices, [](Fields& f, BspVertex& v) {
        f.f32s(v.pos);
        f.f32s(v.st);
        f.f32s(v.lightmap);
        f.f32s(v.normal);
        for (auto& c : v.color)
            c = f.u8();
    }));
    check(readRecords(data, lumps[Indices], Indices, 4, out.indices, readInt));
    check(readRecords(data, lumps[Fogs], Fogs, 72, out.fogs, [](Fields& f, BspFog& g) {
        g.shader = f.name(64);
        g.brush = f.i32();
        g.visibleSide = f.i32();
    }));
    check(readRecords(data, lumps[Surfaces], Surfaces, 104, out.surfaces, [](Fields& f, BspSurface& s) {
        s.shader = f.i32();
        s.fog = f.i32();
        s.type = static_cast<SurfaceType>(f.i32());
        s.firstVertex = f.i32();
        s.numVertices = f.i32();
        s.firstIndex = f.i32();
        s.numIndices = f.i32();
        s.lightmap = f.i32();
        f.i32s(s.lightmapPos);
        f.i32s(s.lightmapSize);
        f.f32s(s.lightmapOrigin);
        for (auto& v : s.lightmapVecs)
            f.f32s(v);
        s.patchWidth = f.i32();
        s.patchHeight = f.i32();
    }));
    if (err)
        return err;

    constexpr size_t kLightmapBytes = kLightmapSize * kLightmapSize * 3;
    if (lumps[Lightmaps].size % kLightmapBytes)
        return Error::fail("BSP lightmaps lump is not a whole number of 128x128 lightmaps");
    auto copyLump = [&](const LumpRange& l, std::vector<uint8_t>& v) {
        v.assign(data.begin() + std::ptrdiff_t(l.offset), data.begin() + std::ptrdiff_t(l.offset + l.size));
    };
    copyLump(lumps[Lightmaps], out.lightmaps);
    copyLump(lumps[LightGrid], out.lightGrid);

    const LumpRange& vis = lumps[Visibility];
    if (vis.size) {
        if (vis.size < 8)
            return Error::fail("BSP visibility lump is too small");
        Fields f(data.data() + vis.offset);
        out.visClusters = f.i32();
        out.visClusterBytes = f.i32();
        if (out.visClusters < 0 || out.visClusterBytes < 0 || out.visClusterBytes < (out.visClusters + 7) / 8 ||
            int64_t(out.visClusters) * out.visClusterBytes > int64_t(vis.size - 8))
            return Error::fail("BSP visibility lump is damaged");
        out.visData.assign(data.begin() + std::ptrdiff_t(vis.offset + 8),
                           data.begin() + std::ptrdiff_t(vis.offset + 8 + size_t(out.visClusters) * out.visClusterBytes));
    }
    normalize(out);
    return validate(out);
}

} // namespace reluva::idtech3
