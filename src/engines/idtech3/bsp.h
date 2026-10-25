// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// A compiled map (.bsp, "IBSP" version 46). Field names follow the lumps
// described in docs/formats/idtech3/bsp.md; all values are little-endian
// in the file and converted on load.

struct BspShader {
    std::string name;
    int32_t surfaceFlags = 0;
    int32_t contentFlags = 0;
};

struct BspPlane {
    std::array<float, 3> normal{};
    float dist = 0;
};

struct BspNode {
    int32_t plane = 0;
    std::array<int32_t, 2> children{}; // negative: -(leaf + 1)
    std::array<int32_t, 3> mins{}, maxs{};
};

struct BspLeaf {
    int32_t cluster = 0; // -1: outside the map
    int32_t area = 0;
    std::array<int32_t, 3> mins{}, maxs{};
    int32_t firstLeafSurface = 0, numLeafSurfaces = 0;
    int32_t firstLeafBrush = 0, numLeafBrushes = 0;
};

// Model 0 is the world; the others are brush entities' ("*1", "*2"...).
struct BspModel {
    std::array<float, 3> mins{}, maxs{};
    int32_t firstSurface = 0, numSurfaces = 0;
    int32_t firstBrush = 0, numBrushes = 0;
};

struct BspBrush {
    int32_t firstSide = 0, numSides = 0;
    int32_t shader = 0;
};

struct BspBrushSide {
    int32_t plane = 0;
    int32_t shader = 0;
};

struct BspVertex {
    std::array<float, 3> pos{};
    std::array<float, 2> st{};       // texture coordinates
    std::array<float, 2> lightmap{}; // lightmap coordinates
    std::array<float, 3> normal{};
    std::array<uint8_t, 4> color{};
};

struct BspFog {
    std::string shader;
    int32_t brush = 0;
    int32_t visibleSide = 0; // -1: none
};

enum class SurfaceType : int32_t { Bad = 0, Planar = 1, Patch = 2, TriangleSoup = 3, Flare = 4 };

struct BspSurface {
    int32_t shader = 0;
    int32_t fog = 0; // -1: none
    SurfaceType type = SurfaceType::Bad;
    int32_t firstVertex = 0, numVertices = 0;
    int32_t firstIndex = 0, numIndices = 0;
    int32_t lightmap = 0; // -1: none
    std::array<int32_t, 2> lightmapPos{}, lightmapSize{};
    std::array<float, 3> lightmapOrigin{};
    std::array<std::array<float, 3>, 3> lightmapVecs{}; // s, t, normal; patches: bounds, bounds, normal
    int32_t patchWidth = 0, patchHeight = 0;           // patch control points
};

constexpr int kLightmapSize = 128; // lightmaps are 128x128 RGB

struct Bsp {
    int32_t version = 0;
    std::string entities; // the entity lump's text, see entities.h
    std::vector<BspShader> shaders;
    std::vector<BspPlane> planes;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leafs;
    std::vector<int32_t> leafSurfaces;
    std::vector<int32_t> leafBrushes;
    std::vector<BspModel> models;
    std::vector<BspBrush> brushes;
    std::vector<BspBrushSide> brushSides;
    std::vector<BspVertex> vertices;
    std::vector<int32_t> indices; // relative to a surface's firstVertex
    std::vector<BspFog> fogs;
    std::vector<BspSurface> surfaces;
    std::vector<uint8_t> lightmaps; // kLightmapSize^2 * 3 bytes each
    std::vector<uint8_t> lightGrid; // 8 bytes per point, see bsp.md
    int32_t visClusters = 0, visClusterBytes = 0;
    std::vector<uint8_t> visData; // visClusters * visClusterBytes bits

    // Values the original engine tolerates and parseBsp() corrected, as
    // described in bsp.md: how many surfaces had each.
    struct Fixes {
        int lightmaps = 0; // lightmap past the end: now -1 (vertex lighting)
        int fogs = 0;      // fog that doesn't exist: now -1
        int flares = 0;    // flare or Bad surface with vertices or indices: now none
        int types = 0;     // unknown surface type: now Bad
        int total() const { return lightmaps + fogs + flares + types; }
    } fixes;

    size_t lightmapCount() const { return lightmaps.size() / (kLightmapSize * kLightmapSize * 3); }
    // Whether cluster `to` can be seen from cluster `from`. Without vis data
    // (or for clusters outside the map) everything is visible.
    bool clusterVisible(int from, int to) const;
};

// Parse a whole .bsp file. Checks the header, that every lump lies inside
// the file and holds whole records, and that every index between lumps is
// in range, so the rest of the engine can trust the result. Out-of-range
// values the original engine tolerates are corrected instead (see `fixes`);
// errors name the record, the field and its value.
Error parseBsp(const std::vector<uint8_t>& data, Bsp& out);

} // namespace reluva::idtech3
