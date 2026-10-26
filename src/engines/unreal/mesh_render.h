// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Meshes on the GPU: every frame's vertices uploaded once (each vertex and
// texture coordinate pair once per frame), drawn at a frame, or between
// two, by offsetting into them. Shared by the Maps and Meshes pages.

#include "common/gfx3d.h"
#include "engines/unreal/mesh.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::unreal {

struct GpuMesh {
    gfx::MeshId mesh = 0;
    int32_t frameVertices = 0; // GPU vertices per frame
    int32_t frames = 0;
    struct Batch {
        uint32_t firstIndex = 0, indexCount = 0;
        int32_t textureIndex = 0;
        uint32_t polyFlags = 0;
    };
    std::vector<Batch> batches;
    // Frame 0's extent, after the mesh's own placement (Scale, Origin,
    // RotOrigin), in Unreal's axes.
    Vec3f center{};
    float radius = 1;
};

// What's done with an imported-model mesh's turn (Mesh::importAxes):
// nothing (the packed vertices as stored, as the game draws them, it
// seems: the float copy looks to be the modelling tool's Y-up space),
// the turn onto the float copy, or the opposite turn. For comparing.
enum class MeshTurn { None, FloatCopy, Reverse };
const char* meshTurnName(MeshTurn turn);

// Uploads a mesh; positions are placed by its Scale, Origin and RotOrigin.
Error uploadMesh(gfx::Renderer3D& renderer, const Mesh& mesh, GpuMesh& out, MeshTurn turn = MeshTurn::None);

// Where an actor puts its mesh: Location + PrePivot, turned by Rotation,
// scaled by DrawScale; then into the renderer's axes (Y negated).
gfx::Mat4 meshPlacement(const Vec3f& location, const Rotator& rotation, float drawScale, const Vec3f& prePivot);

// Which frames to draw: between two, `lerp` of the way to the second.
struct MeshPose {
    int32_t frame = 0, next = 0;
    float lerp = 0;
};
// An animation `time` seconds in (looping at its rate); a still frame
// when the rate is 0. Without the animation, frame 0.
MeshPose poseAt(const Mesh& mesh, const MeshAnimSeq* anim, float time, float startFraction = 0);

struct MeshLight {
    gfx::Vec3 ambient{1, 1, 1}, directed{}, direction{0, 0, 1}; // direction in the renderer's axes
};

// Draws it: `textures` by texture index (0: untextured), `exposure` as
// the Maps page's.
void drawMesh(gfx::Renderer3D& renderer, const GpuMesh& mesh, const gfx::Mat4& placement, const MeshPose& pose,
              const std::vector<gfx::TextureId>& textures, const MeshLight& light, float exposure);

} // namespace reluva::unreal
