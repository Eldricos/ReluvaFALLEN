// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/shader_cache.h"

#include "common/gfx3d.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// Draws a map's surfaces (every model's: the world's and brush entities')
// with their shaders through a ShaderCache: sorted by the shaders' sort
// order, the sky box and cloud layers behind, fog passes over fogged
// surfaces. See docs/engines/idtech3.md for what's approximate or missing.
class WorldRenderer {
public:
    WorldRenderer(gfx::Renderer3D& renderer, ShaderCache& shaders) : r_(renderer), shaders_(shaders) {}
    ~WorldRenderer() { clear(); }
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // The video settings for the next load: the level lit by its vertices
    // instead of its lightmaps; how far curves may stray from round, in
    // units; the sky left plain (the frame's clear colour shows).
    void setOptions(bool vertexLight, float subdivisions, bool fastSky) {
        vertexLight_ = vertexLight;
        subdivisions_ = subdivisions;
        fastSky_ = fastSky;
    }
    // Builds the GPU data for `bsp`.
    Error load(const Bsp& bsp, const ShaderLibrary& library);
    void clear();
    // The sky, then the map's surfaces whose shaders sort in sortMin..sortMax,
    // `time` seconds into the map; call between beginFrame() and endFrame().
    // Drawing opaque surfaces, then models, then translucent surfaces keeps
    // what's behind glass right.
    void drawSky(float time) const;
    // `worldOnly`: the level's own surfaces, not its brush entities' (doors,
    // lifts), which the game places (drawModel).
    void drawSurfaces(float time, float sortMin = 0, float sortMax = 1e9f, bool worldOnly = false) const;
    // Brush entity `model` (*1, *2...) placed by `transform`, its surfaces
    // that sort in sortMin..sortMax.
    void drawModel(int model, const gfx::Mat4& transform, float time, float sortMin = 0,
                   float sortMax = 1e9f) const;
    // Adds `lights` to the opaque surfaces within their reach, as the
    // original's dynamic lights: each pixel brightened in proportion to
    // its colour. After the opaque surfaces are drawn.
    void drawDynamicLights(const std::vector<gfx::DynamicLight>& lights) const;
    // Where a view is and which areas closed doors hide (refdef_t's area
    // mask: a bit set for each hidden one): from then on the draws are of
    // only the level's surfaces that can be seen from there, as the
    // original's renderer does, until another setView. `viewProj` (the
    // view's projection times its placement) leaves out, each frame, the
    // surfaces wholly outside the view. Each view `slot` (0 the player's,
    // 1 a mirror's or portal's) keeps its own selection. Without it, every
    // surface is drawn.
    void setView(int slot, const std::array<float, 3>& eye, const std::array<uint8_t, 32>& areaMask,
                 const gfx::Mat4& viewProj);
    // The mirror and portal surfaces the current view draws, nearest
    // first: the plane they lie in (its normal facing `eye`), bounds, and
    // how far off a portal's view is drawn (0: a mirror's always is).
    struct Portal {
        int surface = -1;
        std::array<float, 3> normal{};
        float dist = 0;
        std::array<float, 3> mins{}, maxs{};
        float range = 0;
    };
    std::vector<Portal> visiblePortals(const std::array<float, 3>& eye) const;
    // A mirror or portal surface's triangles in the current view, drawn
    // with `state` (untextured: for marking where it is, or its depth).
    void drawSurfaceShape(int surface, const gfx::DrawState& state) const;

    size_t batchCount() const { return batches_.size(); }
    // What the last setView found, for the info line: the eye's cluster
    // (-1 outside the map), whether only what's visible is drawn, how many
    // of the level's surfaces are, of how many, and the areas hidden.
    struct Visibility {
        int cluster = -1;
        bool culling = false;
        size_t visible = 0, potentiallyVisible = 0, total = 0; // drawn, past the vis data and doors, all
        int hiddenAreas = 0;
    };
    Visibility visibility() const;
    size_t triangleCount() const { return triangles_; }

private:
    bool vertexLight_ = false, fastSky_ = false;
    float subdivisions_ = 4;
    struct Batch {
        float sort = 3;
        int material = 0;
        int lightmap = -1; // index into lightmaps_, or -1
        int fog = -1;      // index into fogs_, or -1
        uint32_t firstIndex = 0, indexCount = 0;
        int model = 0; // the map's model the surfaces belong to (0: the world)
    };
    struct Fog {
        std::array<float, 3> color{};
        float distance = 0;
    };

    void buildSky(const ShaderDef& sky);

    gfx::Renderer3D& r_;
    ShaderCache& shaders_;
    gfx::MeshId mesh_ = 0;
    std::vector<gfx::TextureId> lightmaps_;
    std::vector<Fog> fogs_;
    std::vector<Batch> batches_; // in drawing order
    // Each opaque surface's triangles (in the index buffer) and bounds,
    // for dynamic lights.
    struct LitSurface {
        uint32_t firstIndex = 0, indexCount = 0;
        std::array<float, 3> mins{}, maxs{};
        int surface = -1; // the map's
    };
    std::vector<LitSurface> litSurfaces_;
    // Visibility (setView): each world surface's triangles, the map's
    // leaves and vis data, and what the last view could see.
    struct SurfaceRange {
        int batch = -1; // -1: drawn by no batch
        uint32_t firstIndex = 0, count = 0;
        std::array<float, 3> mins{}, maxs{}; // its bounds
    };
    int leafAt(const std::array<float, 3>& p) const;
    std::vector<SurfaceRange> surfaceRanges_;
    std::vector<uint32_t> allIndices_;
    std::vector<BspPlane> visPlanes_;
    std::vector<BspNode> visNodes_;
    std::vector<BspLeaf> visLeafs_;
    std::vector<int32_t> visLeafSurfaces_;
    int32_t visClusters_ = 0, visClusterBytes_ = 0;
    std::vector<uint8_t> visData_;
    // A view's selection (setView): its cluster and area mask, the
    // surfaces past them (visibleMark == visibleStamp), and this frame's
    // inside the view (frameMark == frameStamp), where each starts in its
    // index list, the list (in its own mesh, over the level's vertices)
    // and where each batch's part of it is.
    struct ViewSlot {
        gfx::MeshId mesh = 0;
        bool culling = false;
        int cluster = -2;
        std::array<uint8_t, 32> mask{};
        std::vector<uint32_t> visibleMark;
        uint32_t visibleStamp = 0;
        std::vector<int32_t> pvsSurfaces;
        gfx::Mat4 viewProj{};
        std::vector<uint32_t> frameMark, frameFirst;
        uint32_t frameStamp = 0;
        std::vector<uint32_t> frameIndices;
        size_t frameSurfaces = 0;
        std::vector<std::pair<uint32_t, uint32_t>> batchRanges; // first, count
    };
    std::array<ViewSlot, 2> slots_;
    int activeSlot_ = 0;
    const ViewSlot* culled() const { return slots_[size_t(activeSlot_)].culling ? &slots_[size_t(activeSlot_)] : nullptr; }
    gfx::MeshId drawMesh() const { return culled() ? culled()->mesh : mesh_; }
    // The level's mirror and portal surfaces.
    std::vector<Portal> portals_;
    size_t triangles_ = 0;

    // The sky: a box around the camera, the far box's six sides then the
    // sky shader's stages (cloud layers) over it.
    gfx::MeshId skyMesh_ = 0;
    std::array<gfx::TextureId, 6> skySides_{};
    std::array<uint32_t, 6> skySideFirst_{}; // index ranges in skyMesh_
    uint32_t skySideCount_ = 0;
    int skyMaterial_ = -1;
    uint32_t cloudCount_ = 0; // the box without its bottom, from index 0
};

// Doubles a light colour the way the original does on loading (it's
// stored at half brightness), keeping its hue when a component would pass
// 255. Used for lightmaps, vertex colours and the light grid.
void brightenLight(uint8_t* rgb);

} // namespace reluva::idtech3
