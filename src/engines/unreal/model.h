// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Models (Model objects): BSP geometry, a level's world and each brush's.
// A level's is the one with nodes; its polygons are the nodes' vertex
// lists, each drawn with its surface's texture and flags. See
// docs/formats/unreal/model.md.

#include "engines/unreal/package.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::unreal {

using Vec3f = std::array<float, 3>;

struct BspNode {
    std::array<float, 4> plane{}; // normal and distance
    uint64_t zoneMask = 0;
    uint8_t flags = 0;
    int32_t vertPool = 0, surf = 0, back = 0, front = 0, planeNode = 0;
    int32_t collisionBound = 0, renderBound = 0;
    uint8_t zone[2] = {};
    uint8_t vertexCount = 0;
    int32_t leaf[2] = {};
};

struct BspSurf {
    int32_t texture = 0; // object reference
    uint32_t polyFlags = 0;
    int32_t base = 0, normal = 0, textureU = 0, textureV = 0; // into points / vectors
    int32_t lightMap = 0, brushPoly = 0;
    int16_t panU = 0, panV = 0;
    int32_t actor = 0; // object reference: the brush it came from
};

struct BspVert {
    int32_t point = 0, side = 0;
};

struct Zone {
    int32_t actor = 0; // object reference: the ZoneInfo (or LevelInfo for zone 0)
    uint64_t connectivity = 0, visibility = 0;
};

// Where a surface's lightmap is: a grid of uClamp x vClamp sample points
// across the surface, `uScale` x `vScale` texture units apart, starting
// at `pan`; for each light that reaches it, one bit per sample (lit or
// in shadow) from `dataOffset` in the model's light bits. See model.md.
struct LightMapIndex {
    int32_t dataOffset = 0;
    Vec3f pan{};
    int32_t uClamp = 0, vClamp = 0;
    float uScale = 0, vScale = 0;
    int32_t lightActors = -1; // into Model::lights: the lights, up to a 0
};

struct Leaf {
    int32_t zone = 0, permeating = 0, volumetric = 0;
    uint64_t visibleZones = 0;
};

struct Model {
    Vec3f boundsMin{}, boundsMax{};
    std::vector<Vec3f> vectors, points;
    std::vector<BspNode> nodes;
    std::vector<BspSurf> surfs;
    std::vector<BspVert> verts;
    int32_t sharedSides = 0, zoneCount = 0;

    // After the vertices: what lighting needs. If these don't read (a
    // layout this reader doesn't know), the geometry still does and
    // `lightingError` says why.
    std::vector<Zone> zones;
    int32_t polys = 0; // object reference
    std::vector<LightMapIndex> lightMaps;
    std::vector<uint8_t> lightBits;
    std::vector<Leaf> leaves;
    std::vector<int32_t> lights; // object references: light actors, lists ending in 0
    bool rootOutside = true;     // whether what's beyond the BSP's root is open space
    size_t trailingBytes = 0;    // left over after the last field read
    std::string lightingError;
    bool hasLighting() const { return lightingError.empty(); }
    // Read without validating: why the BSP (nodes, surfaces, vertices)
    // didn't hold together and was dropped.
    std::string geometryError;
};

// Reads a model: its geometry (an error if that fails), then its zones,
// lightmaps and lights (into lightingError if those fail). Without
// `validate`, a BSP whose indices don't hold together is dropped (see
// geometryError) rather than failing, for when only what follows is
// wanted (a mover's Polys: one of The Fallen's movers has such a brush).
Error readModel(const Package& package, int exportIndex, Model& out, bool validate = true);

// The model a level is drawn from: of the package's models, the one with
// the most nodes (brushes' own models have none). -1 if there's none.
int findLevelModel(const Package& package, Model* out = nullptr, std::string* error = nullptr);

// A level's polygons as triangles, grouped by texture and flags. Positions
// are the level's own (Unreal's axes: X forward, Y right, Z up); texture
// coordinates are in texels, for dividing by the texture's size.
struct Polygon {
    int32_t surf = 0;
    std::vector<Vec3f> points;
    Vec3f normal{};
    std::vector<std::array<float, 2>> texels;
};
std::vector<Polygon> levelPolygons(const Model& model);

} // namespace reluva::unreal
