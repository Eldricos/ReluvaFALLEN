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
#include "engines/unreal/actors.h"
#include "engines/unreal/model.h"
#include "engines/unreal/package_set.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace reluva::unreal {

// The colour Unreal's hue and saturation (0-255 each; saturation 255 is
// white, 0 the pure hue) give, times brightness / kBrightnessUnit.
constexpr float kBrightnessUnit = 128;
Vec3f lightColor(uint8_t hue, uint8_t saturation, uint8_t brightness);

// Engine.Actor's ELightType: how a light's brightness changes over time.
// The rest (the two texture palette ones) are drawn steady.
constexpr uint8_t kLightNone = 0, kLightSteady = 1, kLightPulse = 2, kLightBlink = 3, kLightFlicker = 4,
                  kLightStrobe = 5, kLightBackdrop = 6, kLightSubtlePulse = 7;

struct LightSource {
    Vec3f position{};
    Vec3f color{};    // 1 = the texture as drawn
    float radius = 0; // world units; 0: this light does nothing
    bool spot = false; // lights only within a cone round `direction`
    Vec3f direction{1, 0, 0};
    float cosCone = 0;
    bool incidence = true; // fades as the surface turns away from the light
    // How its brightness changes (its LightType: kLightPulse...), with a
    // cycle of `period` seconds started `phase` (0-1) of the way through.
    uint8_t type = kLightSteady;
    float period = 1, phase = 0;
    int animated = -1; // its index in LevelLights::animated, if its brightness changes
};

// The share of its brightness (0-1) a light gives `time` seconds in.
float lightLevel(const LightSource& light, float time);

// World units per step of an actor's LightRadius.
constexpr float kRadiusUnit = 25;

// The most light a sample gets: 1, the texture's own colours. The
// original doesn't brighten surfaces past their texture (a lamp against a
// wall shows its texture, not white); see lighting.md. Kept as a
// constant (the lightmaps still hold up to 2) in case another game wants
// UE1's brighter blending.
constexpr float kMaxLight = 1;

struct LevelLights {
    std::vector<LightSource> sources; // one per entry of Model::lights
    std::vector<Vec3f> zoneAmbient;   // one per zone
    std::vector<LightSource> unique;  // each light once (sources repeats them)
    std::vector<LightSource> animated; // those of them whose brightness changes
    int unreadable = 0;               // light actors whose properties didn't read
};
// `scripted`: what the level's scripts changed when it started (lights
// they turned off or destroyed light nothing; actors.h).
LevelLights gatherLights(const PackageSet& packages, const Package& package, const Model& model,
                         const ScriptedActors* scripted = nullptr);

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

// A lightmap that lights whose brightness changes reach, redrawn as they
// do: its rectangle in its page (the border included), its light without
// them, and each one's at full brightness.
struct AnimatedLightmap {
    int page = 0, x = 0, y = 0, w = 0, h = 0;
    std::vector<Vec3f> base; // w * h, rows top to bottom
    std::vector<std::pair<int, std::vector<Vec3f>>> lights; // LevelLights::animated index, its light
    // Its texels with each light at levels[its index], as the pages hold them.
    void draw(const std::vector<float>& levels, std::vector<uint8_t>& rgba) const;
};

struct LightmapAtlas {
    int pageSize = 0;
    std::vector<image::Image> pages; // RGBA, light / 2 (drawn doubled), at most kMaxLight
    std::vector<LightmapPlacement> surfs; // one per Model::surfs
    std::vector<AnimatedLightmap> animated; // drawn into the pages at time 0

    // How the surfaces' sample grids lined up with them (see lighting.md).
    struct Stats {
        int lightmaps = 0, lit = 0; // surfaces with a lightmap; their samples some light reached
        int fromBounds = 0;         // grids the pan didn't place, started at the polygon's corner instead
        int badFit = 0;             // grids that don't cover their polygon even so
        int tooLarge = 0;           // lightmaps larger than a page (drawn with ambient light)
    } stats;

    // The texture coordinates (0-1 in the surface's page) of a point.
    std::array<float, 2> uv(int surf, const Vec3f& point) const;
};

LightmapAtlas buildLightmaps(const Model& model, const LevelLights& lights, int pageSize = 1024);

} // namespace reluva::unreal
