// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level's static lighting. UE1 levels don't store lit lightmaps: for
// each surface they keep, per light that reaches it, a bitmap of which of
// its sample points that light can see. The light itself (colour,
// brightness, radius, cone) comes from the light actors, and the engine
// works out the brightness at each point. This does that once, when a
// level loads, into lightmap textures packed into pages. See
// docs/formats/unreal/lighting.md.

#include "common/image.h"
#include "engines/unreal/model.h"
#include "engines/unreal/package_set.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::unreal {

// The colour Unreal's hue and saturation (0-255 each; saturation 255 is
// white, 0 the pure hue) give, times brightness / kBrightnessUnit.
constexpr float kBrightnessUnit = 128;
Vec3f lightColor(uint8_t hue, uint8_t saturation, uint8_t brightness);

struct LightSource {
    Vec3f position{};
    Vec3f color{};    // 1 = the texture as drawn
    float radius = 0; // world units; 0: this light does nothing
    bool spot = false; // lights only within a cone round `direction`
    Vec3f direction{1, 0, 0};
    float cosCone = 0;
    bool incidence = true; // fades as the surface turns away from the light
};

// World units per step of an actor's LightRadius.
constexpr float kRadiusUnit = 25;

struct LevelLights {
    std::vector<LightSource> sources; // one per entry of Model::lights
    std::vector<Vec3f> zoneAmbient;   // one per zone
    int unreadable = 0;               // light actors whose properties didn't read
};
LevelLights gatherLights(const PackageSet& packages, const Package& package, const Model& model);

// Where each surface's lightmap is in the pages, and how a point on the
// surface maps into it.
struct LightmapPlacement {
    int page = -1; // -1: none (an unlit surface)
    // The sample grid's coordinates of a point P: (dot(P, uAxis) + uOffset,
    // dot(P, vAxis) + vOffset); sample (i, j) is at (i, j). In the page,
    // sample (0, 0) is the texel at (x, y). For a surface without a
    // lightmap the axes are zero: every point is that one texel.
    int x = 0, y = 0;
    Vec3f uAxis{}, vAxis{};
    float uOffset = 0, vOffset = 0;
};

struct LightmapAtlas {
    int pageSize = 0;
    std::vector<image::Image> pages; // RGBA, light / 2 (drawn doubled, so up to 2x bright)
    std::vector<LightmapPlacement> surfs; // one per Model::surfs

    // How the surfaces' sample grids were lined up with them (see
    // lighting.md), for checking the guesses against a game's data.
    struct Stats {
        int lightmaps = 0, lit = 0;      // surfaces with a lightmap; their samples some light reached
        // Grid along the texture axes as stored, or made unit length (only
        // counted where they differ; sameAxes where they're unit already).
        int rawAxes = 0, unitAxes = 0, sameAxes = 0;
        int panOffset = 0, panPoint = 0, fromBounds = 0; // where the grid starts
        int badFit = 0;                  // surfaces no guess fitted (drawn with the closest)
        int tooLarge = 0;                // lightmaps larger than a page (drawn with ambient light)
    } stats;

    // The texture coordinates (0-1 in the surface's page) of a point.
    std::array<float, 2> uv(int surf, const Vec3f& point) const;
};

LightmapAtlas buildLightmaps(const Model& model, const LevelLights& lights, int pageSize = 1024);

} // namespace reluva::unreal
