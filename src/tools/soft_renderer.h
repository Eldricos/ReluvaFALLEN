// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A software gfx::Renderer3D for reluva-tool: draws a page's frames into
// memory so they can be saved and looked at without a GPU (on a machine
// with none, or in a test). It does what a level's drawing needs to be
// recognisable: triangles clipped at the near plane, textured (bilinear,
// as the GPU draws them, or nearest where the texture asks; wrapping or
// clamped) and coloured as the draw call says (its
// constant, the vertices' colours, or its lighting), depth tested,
// alpha tested and blended, with the overlay on top. Not done: deforms,
// texture coordinate rows and turbulence, fog, dynamic lights, stencil,
// mirroring, environment and vector texture coordinates.

#include "common/gfx3d.h"
#include "common/types.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::tools {

class SoftRenderer3D : public gfx::Renderer3D {
public:
    SoftRenderer3D(int width, int height);

    Error createTexture(int width, int height, const uint8_t* rgba, const gfx::TextureParams& params,
                        gfx::TextureId& out) override;
    void destroyTexture(gfx::TextureId id) override;
    Error updateTextures(const std::vector<gfx::TextureRegion>& regions) override;
    Error createMesh(const std::vector<gfx::Vertex>& vertices, const std::vector<uint32_t>& indices,
                     gfx::MeshId& out) override;
    Error createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t>& indices, gfx::MeshId& out) override;
    Error setMeshIndices(gfx::MeshId id, const std::vector<uint32_t>& indices) override;
    void destroyMesh(gfx::MeshId id) override;

    int width() const override { return w_; }
    int height() const override { return h_; }
    std::string deviceName() const override { return "software (reluva-tool)"; }

    void beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3& eye, Color clear) override;
    void draw(const gfx::DrawCall& call) override;
    void setView(const gfx::Mat4& viewProj, const gfx::Vec3& eye) override;
    void setDynamicLights(const std::vector<gfx::DynamicLight>& lights) override {
        lights_.assign(lights.begin(), lights.begin() + std::ptrdiff_t(std::min<size_t>(lights.size(), gfx::kMaxDynamicLights)));
    }
    void setViewport(int x, int y, int w, int h) override;
    void setDepthRange(float maxDepth) override { depthRange_ = maxDepth; }
    void clearDepth() override;
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float gamma) override { gamma_ = gamma; }
    void endFrame(const Surface* overlay) override;

    // Whether draws are done (when only some frames are wanted, the rest
    // cost nothing).
    bool drawing = true;
    // The last finished frame (RGBA32), and how many frames there were.
    const Surface& frame() const { return frame_; }
    int frames() const { return frames_; }
    // Draw calls and triangles drawn in the last frame.
    int lastDraws() const { return lastDraws_; }
    int lastTriangles() const { return lastTriangles_; }

private:
    std::vector<gfx::DynamicLight> lights_;
    struct Texture {
        int w = 0, h = 0;
        bool clamp = false, nearest = false;
        std::vector<uint8_t> rgba;
    };
    struct Mesh {
        gfx::MeshId vertexMesh = 0; // whose vertices (itself, unless made by createIndexedMesh)
        std::vector<gfx::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    int w_, h_;
    Surface color_, frame_;
    std::vector<float> depth_;
    std::map<gfx::TextureId, Texture> textures_;
    std::map<gfx::MeshId, Mesh> meshes_;
    uint32_t nextId_ = 1;
    gfx::Mat4 viewProj_ = gfx::identity();
    gfx::Vec3 eye_{};
    int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0; // the viewport
    float depthRange_ = 1, gamma_ = 1;
    int frames_ = 0, draws_ = 0, triangles_ = 0, lastDraws_ = 0, lastTriangles_ = 0;
};

} // namespace reluva::tools
