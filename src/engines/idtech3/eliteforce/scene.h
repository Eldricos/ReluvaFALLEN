// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/md3.h"
#include "engines/idtech3/qvm.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// What the client game and the menus draw, as they hand it over through
// the renderer's calls they share (Quake III's: DrawStretchPic, then
// ClearScene, AddRefEntityToScene, AddPolyToScene, AddLightToScene and
// RenderScene).

// 2D: a part of a shader stretched over a rectangle of the screen (in
// 640x480), tinted.
struct Picture {
    float x, y, w, h;
    float s1, t1, s2, t2;
    int32_t shader; // in the program's shaders
    std::array<float, 4> color;
};

// An entity added to the scene: its refEntity_t, kept as the 256 bytes
// from its address, with the size of the memset that cleared it, if one
// did (how its size was learnt).
struct SceneEntity {
    std::vector<uint8_t> bytes;
    int32_t clearedSize = 0;
};

// Each RenderScene draws the entities added since the last ClearScene or
// RenderScene from its own view (refdef_t, the 112 bytes the client game
// clears at start-up: Quake III's without its text): the player's, and
// others such as the HUD's heads and the menus' models, which don't draw
// the world.
struct View {
    std::vector<uint8_t> refdef;
    size_t firstEntity = 0, entityCount = 0; // in entities
    size_t firstPoly = 0, polyCount = 0;     // in polys
    size_t firstLight = 0, lightCount = 0;   // in lights
    // The pictures drawn before it: it goes over them and under the rest,
    // as the original draws in the order asked.
    size_t picturesBefore = 0;
};

struct Scene {
    std::vector<SceneEntity> entities; // every one added this frame
    // Polygons (AddPolyToScene: shader, vertex count, the vertices'
    // bytes, 24 each as Quake III's polyVert_t) and lights
    // (AddLightToScene: origin, intensity, which is the radius it
    // reaches, and colour).
    struct Poly {
        int32_t shader = 0, vertexCount = 0;
        std::vector<uint8_t> vertices;
    };
    std::vector<Poly> polys;
    struct Light {
        std::array<float, 3> origin{};
        float radius = 0;
        std::array<float, 3> color{};
    };
    std::vector<Light> lights;
    std::vector<uint8_t> view; // set by RenderScene: the last one
    int renders = 0;
    std::vector<View> views; // every RenderScene, in order
    size_t pending = 0;       // entities from here wait for the next RenderScene
    size_t pendingPolys = 0;  // the same for polygons
    size_t pendingLights = 0; // and lights

    // The calls, with the program's memory and arguments.
    void clear();
    void addEntity(Qvm& vm, int32_t address, int32_t clearedSize);
    void addPoly(Qvm& vm, int32_t shader, int32_t count, int32_t address);
    void addLight(Qvm& vm, int32_t origin, float intensity, float r, float g, float b);
    void render(Qvm& vm, int32_t refdef, size_t picturesSoFar);
};

// LerpTag (orientation_t*, model, startFrame, endFrame, frac, tagName):
// `model`'s tag between two frames, the origin and axes blended (the axes
// renormalised) as the original renderer does, written at `address`;
// whether it was found (if not, at the origin, unrotated).
int32_t writeLerpTag(Qvm& vm, int32_t address, const Md3Model* model, int32_t startFrame, int32_t endFrame,
                     float frac, const std::string& tagName);

constexpr int32_t kRefdefSize = 112;    // the client game clears this much of cg.refdef
constexpr int32_t kRefEntityKept = 256; // bytes kept of each refEntity_t

} // namespace reluva::idtech3::eliteforce
