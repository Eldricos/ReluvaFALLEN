// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Meshes (Mesh and LodMesh objects): vertex-animated models, every frame
// keeping every vertex's position. Characters, items and decorations.
// See docs/formats/unreal/mesh.md.

#include "engines/unreal/model.h"
#include "engines/unreal/properties.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::unreal {

struct MeshAnimSeq {
    std::string name, group;
    int32_t startFrame = 0, numFrames = 0;
    float rate = 0; // frames per second
};

// A triangle: three of a frame's vertices, each corner's texture
// coordinates (0-1), which of the mesh's textures it uses, and its flags.
struct MeshTri {
    std::array<int32_t, 3> vertex{};
    std::array<std::array<float, 2>, 3> uv{};
    int32_t textureIndex = 0;
    uint32_t polyFlags = 0;
};

struct Mesh {
    bool lod = false;           // a LodMesh
    int32_t frameVerts = 0;     // vertices per frame
    int32_t animFrames = 0;
    std::vector<Vec3f> verts;   // animFrames x frameVerts, as stored (before Scale and Origin)
    int vertexBytes = 4;        // how each vertex was stored: 4 (packed 11/11/10 bits) or 8
    std::vector<MeshTri> tris;  // vertex indices into one frame
    std::vector<MeshAnimSeq> anims;
    std::vector<int32_t> textures; // object references, by texture index
    Vec3f scale{1, 1, 1}, origin{};
    Rotator rotOrigin;
    size_t trailingBytes = 0; // left after the last field read
    // The Fallen's layout: how many bytes of imported-model block (holding
    // the vertices) came between the textures and the standard fields; 0
    // for the standard layout.
    size_t importBlock = 0;
    // Its float copy of the vertices shows they're turned from how the
    // packed ones lie: that turn (rows: output axes), applied after Scale
    // and Origin and before RotOrigin. None for the standard layout.
    std::optional<std::array<Vec3f, 3>> importAxes;
    // Without a float copy to fit (most keep per-vertex weights where
    // KiraCellLight keeps its copy), the turn most of them show is taken.
    bool importAxesGuessed = false;
    // The Fallen's keyframed meshes: how many frames of vertices were
    // found, fewer than animFrames; the first is used for every frame
    // until the rest is worked out. 0: every frame was found.
    int keyFrames = 0;
    int droppedAnims = 0; // animations left out, their frames past the mesh's

    const MeshAnimSeq* findAnim(std::string_view name) const;
};

// Reads a Mesh or LodMesh export.
Error readMesh(const Package& package, int exportIndex, Mesh& out);

bool isMeshClass(std::string_view className);

// For working out a mesh's layout (reluva-tool ue-probe): the arrays
// that look like an imported-model block's ((n, n, flags) headers), and
// every place the standard tail could start and hold together.
std::vector<std::string> describeMeshLayout(const Package& package, int exportIndex);

} // namespace reluva::unreal
