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
// nothing (the packed vertices as stored), the turn onto the float copy,
// or the opposite turn. The opposite is what stands The Fallen's chairs
// and people up (CM05_Kira); the others are kept for comparing.
enum class MeshTurn { None, FloatCopy, Reverse };
const char* meshTurnName(MeshTurn turn);

// Uploads a mesh; positions are placed by its Scale, Origin and RotOrigin.
// Imported meshes are also turned about Z by `yawQuarters` quarter turns
// (each 16384 of yaw): with only the turn above, The Fallen's face their
// right; 3 (three quarters, i.e. a quarter turn back) faces them ahead.
constexpr int kImportYawQuarters = 3;
Error uploadMesh(gfx::Renderer3D& renderer, const Mesh& mesh, GpuMesh& out, MeshTurn turn = MeshTurn::Reverse,
                 int yawQuarters = kImportYawQuarters);

// Where a mesh's packed vertex is, as uploadMesh places it (by Scale and
// Origin, the import turn, RotOrigin), before the actor's own placement;
// a vector in its float copy's space (its imported-model block's: type 2
// attachment points') turned the same way; and one already in the
// skeleton's units (Scale applied, from the origin: the precomputed
// places). Unreal's axes.
Vec3f placeMeshVertex(const Mesh& mesh, const Vec3f& packed, MeshTurn turn = MeshTurn::Reverse,
                      int yawQuarters = kImportYawQuarters);
Vec3f placeMeshFloatVector(const Mesh& mesh, const Vec3f& v, MeshTurn turn = MeshTurn::Reverse,
                           int yawQuarters = kImportYawQuarters);
Vec3f placeMeshScaled(const Mesh& mesh, const Vec3f& v, MeshTurn turn = MeshTurn::Reverse,
                      int yawQuarters = kImportYawQuarters);

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
// An animation at `fraction` through it (an actor's AnimFrame: below 0
// while tweening into it, drawn at its first frame); a loop goes on from
// its last frame back to its first.
MeshPose poseAtFraction(const Mesh& mesh, const MeshAnimSeq* anim, float fraction, bool loop);

struct MeshLight {
    gfx::Vec3 ambient{1, 1, 1}, directed{}, direction{0, 0, 1}; // direction in the renderer's axes
};

// Draws it: `textures` by texture index (0: untextured), `exposure` as
// the Maps page's; `style` the actor's (actors.h's kStyle...: translucent,
// modulated and masked apply to the whole mesh, as its polygons' own
// flags do to them).
void drawMesh(gfx::Renderer3D& renderer, const GpuMesh& mesh, const gfx::Mat4& placement, const MeshPose& pose,
              const std::vector<gfx::TextureId>& textures, const MeshLight& light, float exposure,
              uint8_t style = 1);

} // namespace reluva::unreal
