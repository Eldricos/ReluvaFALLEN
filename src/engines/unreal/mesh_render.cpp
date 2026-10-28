// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/mesh_render.h"

#include "engines/unreal/actors.h"
#include "engines/unreal/texture.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace reluva::unreal {

namespace {

Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

gfx::Mat4 translation(const Vec3f& t) {
    gfx::Mat4 m = gfx::identity();
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}

gfx::Mat4 scaling(const Vec3f& s) {
    gfx::Mat4 m = gfx::identity();
    m[0] = s[0];
    m[5] = s[1];
    m[10] = s[2];
    return m;
}

gfx::Mat4 rotation(const Rotator& r) {
    const std::array<Vec3f, 3> axes = rotationAxes(r);
    gfx::Mat4 m = gfx::identity();
    for (size_t c = 0; c < 3; ++c)
        for (size_t k = 0; k < 3; ++k)
            m[c * 4 + k] = axes[c][k];
    return m;
}

// The surface flags that change how a triangle is drawn.
constexpr uint32_t kDrawFlags = polyflag::Masked | polyflag::Translucent | polyflag::Modulated | polyflag::Unlit;

} // namespace

const char* meshTurnName(MeshTurn turn) {
    switch (turn) {
    case MeshTurn::FloatCopy: return "turned onto their float copy";
    case MeshTurn::Reverse: return "turned the opposite way";
    default: return "as stored";
    }
}

namespace {

// A vector already by Scale (Origin taken off, for a point) on through the
// mesh's turn (imported meshes: as `turn` says, then the quarter turns of
// yaw) and RotOrigin.
Vec3f turnScaled(const Mesh& mesh, Vec3f s, MeshTurn turn, int yawQuarters) {
    if (mesh.importAxes && turn != MeshTurn::None) {
        // The opposite turn is the transpose (the turn's rows as columns).
        const auto& m = *mesh.importAxes;
        const bool t = turn == MeshTurn::Reverse;
        auto at = [&](size_t row, size_t col) { return t ? m[col][row] : m[row][col]; };
        s = {at(0, 0) * s[0] + at(0, 1) * s[1] + at(0, 2) * s[2], at(1, 0) * s[0] + at(1, 1) * s[1] + at(1, 2) * s[2],
             at(2, 0) * s[0] + at(2, 1) * s[1] + at(2, 2) * s[2]};
        // A quarter turn of yaw takes X to Y: (x, y) to (-y, x).
        for (int q = 0; q < (yawQuarters % 4 + 4) % 4; ++q)
            s = {-s[1], s[0], s[2]};
    }
    const std::array<Vec3f, 3> axes = rotationAxes(mesh.rotOrigin);
    Vec3f p{};
    for (size_t k = 0; k < 3; ++k)
        p[k] = axes[0][k] * s[0] + axes[1][k] * s[1] + axes[2][k] * s[2];
    return p;
}

} // namespace

Vec3f placeMeshVertex(const Mesh& mesh, const Vec3f& v, MeshTurn turn, int yawQuarters) {
    return turnScaled(mesh,
                      {(v[0] - mesh.origin[0]) * mesh.scale[0], (v[1] - mesh.origin[1]) * mesh.scale[1],
                       (v[2] - mesh.origin[2]) * mesh.scale[2]},
                      turn, yawQuarters);
}

Vec3f placeMeshScaled(const Mesh& mesh, const Vec3f& v, MeshTurn turn, int yawQuarters) {
    return turnScaled(mesh, v, turn, yawQuarters);
}

Error buildMeshTopology(const Mesh& mesh, MeshTopology& out) {
    out = MeshTopology{};
    // Each (vertex, texture coordinates) pair once; triangles by texture and flags.
    std::map<std::tuple<int32_t, float, float>, uint32_t> cornerOf;
    std::map<std::pair<int32_t, uint32_t>, std::vector<uint32_t>> groups;
    for (const MeshTri& t : mesh.tris) {
        if (t.polyFlags & polyflag::Invisible)
            continue;
        auto& list = groups[{t.textureIndex, t.polyFlags & kDrawFlags}];
        for (size_t k = 0; k < 3; ++k) {
            const auto key = std::make_tuple(t.vertex[k], t.uv[k][0], t.uv[k][1]);
            auto it = cornerOf.find(key);
            if (it == cornerOf.end()) {
                it = cornerOf.emplace(key, uint32_t(out.corners.size())).first;
                out.corners.push_back({t.vertex[k], t.uv[k]});
            }
            list.push_back(it->second);
        }
    }
    for (const auto& [key, list] : groups) {
        GpuMesh::Batch b;
        b.textureIndex = key.first;
        b.polyFlags = key.second;
        b.firstIndex = uint32_t(out.indices.size());
        b.indexCount = uint32_t(list.size());
        out.indices.insert(out.indices.end(), list.begin(), list.end());
        out.batches.push_back(b);
    }
    if (out.indices.empty())
        return Error::fail("the mesh has no visible triangles");
    return Error::success();
}

Error uploadMesh(gfx::Renderer3D& renderer, const Mesh& mesh, GpuMesh& out, MeshTurn turn, int yawQuarters) {
    MeshTopology topology;
    if (Error err = buildMeshTopology(mesh, topology))
        return err;
    return uploadMesh(renderer, mesh, topology, out, turn, yawQuarters);
}

Error uploadMesh(gfx::Renderer3D& renderer, const Mesh& mesh, const MeshTopology& topology, GpuMesh& out,
                 MeshTurn turn, int yawQuarters) {
    out = GpuMesh{};
    // The mesh's own placement: less Origin, scaled by Scale, turned by RotOrigin.
    auto place = [&](const Vec3f& v) { return placeMeshVertex(mesh, v, turn, yawQuarters); };
    const auto& corners = topology.corners;
    const auto& indices = topology.indices;
    out.batches = topology.batches;
    out.frameVertices = int32_t(corners.size());
    out.frames = mesh.animFrames;
    std::vector<gfx::Vertex> vertices;
    vertices.reserve(size_t(out.frameVertices) * size_t(out.frames));
    std::vector<Vec3f> positions(size_t(mesh.frameVerts)), normals(size_t(mesh.frameVerts));
    // The triangles' winding isn't certain, so which way their normals
    // point is decided by the mesh itself: outwards on the whole (frame 0).
    float outward = 1;
    for (int32_t f = 0; f < mesh.animFrames; ++f) {
        for (int32_t v = 0; v < mesh.frameVerts; ++v)
            positions[size_t(v)] = place(mesh.verts[size_t(f) * size_t(mesh.frameVerts) + size_t(v)]);
        // Smooth normals: each vertex's triangles' normals, by their area.
        std::fill(normals.begin(), normals.end(), Vec3f{});
        for (const MeshTri& t : mesh.tris) {
            const Vec3f& a = positions[size_t(t.vertex[0])];
            const Vec3f n = cross(sub(positions[size_t(t.vertex[1])], a), sub(positions[size_t(t.vertex[2])], a));
            for (int32_t v : t.vertex)
                for (size_t k = 0; k < 3; ++k)
                    normals[size_t(v)][k] += n[k];
        }
        if (f == 0) {
            Vec3f mid{};
            for (const Vec3f& p : positions)
                for (size_t k = 0; k < 3; ++k)
                    mid[k] += p[k] / float(positions.size());
            float sum = 0;
            for (size_t v = 0; v < positions.size(); ++v) {
                const Vec3f d = sub(positions[v], mid);
                sum += d[0] * normals[v][0] + d[1] * normals[v][1] + d[2] * normals[v][2];
            }
            outward = sum < 0 ? -1.0f : 1.0f;
        }
        for (const auto& [vertex, uv] : corners) {
            gfx::Vertex gv{};
            const Vec3f& p = positions[size_t(vertex)];
            std::copy(p.begin(), p.end(), gv.pos);
            gv.uv[0] = uv[0];
            gv.uv[1] = uv[1];
            const Vec3f& n = normals[size_t(vertex)];
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (size_t k = 0; k < 3; ++k)
                gv.normal[k] = len > 0 ? outward * n[k] / len : 0;
            gv.color[0] = gv.color[1] = gv.color[2] = gv.color[3] = 255;
            vertices.push_back(gv);
        }
        if (f == 0) {
            Vec3f lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (const Vec3f& p : positions)
                for (size_t k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], p[k]);
                    hi[k] = std::max(hi[k], p[k]);
                }
            for (size_t k = 0; k < 3; ++k)
                out.center[k] = (lo[k] + hi[k]) / 2;
            const Vec3f half = sub(hi, out.center);
            out.radius = std::max(1.0f, std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]));
        }
    }
    return renderer.createMesh(vertices, indices, out.mesh);
}

gfx::Mat4 meshPlacement(const Vec3f& location, const Rotator& rot, float drawScale, const Vec3f& prePivot) {
    const gfx::Mat4 flip = scaling({1, -1, 1});
    gfx::Mat4 m = gfx::multiply(flip, translation({location[0] + prePivot[0], location[1] + prePivot[1],
                                                   location[2] + prePivot[2]}));
    m = gfx::multiply(m, rotation(rot));
    return gfx::multiply(m, scaling({drawScale, drawScale, drawScale}));
}

// RotOrigin's turn R (its rows where each axis goes), then
// R diag(scale) R^T: scaled along the axes before it, turned back.
gfx::Mat4 meshAxisScaling(const Mesh& mesh, const Vec3f& scale) {
    const std::array<Vec3f, 3> axes = rotationAxes(mesh.rotOrigin);
    gfx::Mat4 m = scaling({1, 1, 1});
    for (size_t col = 0; col < 3; ++col)
        for (size_t row = 0; row < 3; ++row) {
            float v = 0;
            for (size_t k = 0; k < 3; ++k)
                v += axes[k][row] * scale[k] * axes[k][col];
            m[col * 4 + row] = v;
        }
    return m;
}

MeshPose poseAt(const Mesh& mesh, const MeshAnimSeq* anim, float time, float startFraction) {
    MeshPose p;
    if (!anim || anim->numFrames <= 0 || mesh.animFrames <= 0)
        return p;
    const float n = float(anim->numFrames);
    float t = startFraction * n;
    if (anim->rate > 0)
        t += time * anim->rate;
    const float whole = std::floor(t);
    const int32_t f = int32_t(whole) % anim->numFrames;
    p.frame = anim->startFrame + f;
    p.next = anim->rate > 0 ? anim->startFrame + (f + 1) % anim->numFrames : p.frame;
    p.lerp = anim->rate > 0 ? t - whole : 0;
    return p;
}

MeshPose poseAtFraction(const Mesh& mesh, const MeshAnimSeq* anim, float fraction, bool loop) {
    MeshPose p;
    if (!anim || anim->numFrames <= 0 || mesh.animFrames <= 0)
        return p;
    const float t = std::max(0.0f, fraction) * float(anim->numFrames);
    const float whole = std::floor(t);
    const int32_t f = std::min(int32_t(whole), anim->numFrames - 1);
    p.frame = anim->startFrame + f;
    p.next = f + 1 < anim->numFrames ? p.frame + 1 : loop ? anim->startFrame : p.frame;
    p.lerp = p.next == p.frame ? 0 : t - whole;
    return p;
}

void drawMesh(gfx::Renderer3D& renderer, const GpuMesh& mesh, const gfx::Mat4& placement, const MeshPose& pose,
              const std::vector<gfx::TextureId>& textures, const MeshLight& light, float exposure, uint8_t style) {
    for (const GpuMesh::Batch& batch : mesh.batches) {
        GpuMesh::Batch b = batch;
        if (style == kStyleTranslucent)
            b.polyFlags |= polyflag::Translucent;
        else if (style == kStyleModulated)
            b.polyFlags |= polyflag::Modulated;
        else if (style == kStyleMasked)
            b.polyFlags |= polyflag::Masked;
        gfx::DrawCall call;
        call.mesh = mesh.mesh;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.model = placement;
        // Between two frames: the later's vertices, blended back towards the earlier's.
        call.vertexOffset = pose.next * mesh.frameVertices;
        call.oldVertexOffset = pose.frame * mesh.frameVertices;
        call.backLerp = pose.next == pose.frame ? 0 : 1 - pose.lerp;
        call.texture = b.textureIndex >= 0 && size_t(b.textureIndex) < textures.size()
                           ? textures[size_t(b.textureIndex)]
                           : 0;
        call.state.cull = gfx::Cull::None;
        call.color = {exposure, exposure, exposure, 1};
        if (b.polyFlags & polyflag::Unlit) {
            call.rgbSource = gfx::ColorSource::Constant;
        } else {
            call.rgbSource = gfx::ColorSource::Lighting;
            call.ambientLight = light.ambient;
            call.directedLight = light.directed;
            call.lightDirection = light.direction;
        }
        if (b.polyFlags & polyflag::Masked)
            call.state.alphaTest = gfx::AlphaTest::Ge128;
        if (b.polyFlags & polyflag::Translucent) {
            call.state.src = gfx::Blend::One;
            call.state.dst = gfx::Blend::OneMinusSrcColor;
            call.state.depthWrite = false;
        } else if (b.polyFlags & polyflag::Modulated) {
            call.state.src = gfx::Blend::DstColor;
            call.state.dst = gfx::Blend::SrcColor;
            call.state.depthWrite = false;
            call.rgbSource = gfx::ColorSource::Constant;
            call.color = {1, 1, 1, 1};
        }
        renderer.draw(call);
    }
}

} // namespace reluva::unreal
