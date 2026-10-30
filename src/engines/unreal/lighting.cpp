// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/lighting.h"

#include "engines/unreal/class_defaults.h"
#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::unreal {

namespace {

constexpr float kPi = 3.14159265358979f;

// Light effects (an actor's LightEffect) that change the static lighting.
constexpr uint8_t kEffectStaticSpot = 8;
constexpr uint8_t kEffectSpotlight = 12;
constexpr uint8_t kEffectNonIncidence = 13;
constexpr uint8_t kEffectTorchWaver = 1, kEffectFireWaver = 2, kEffectWateryShimmer = 3, kEffectSlowWave = 5,
                  kEffectFastWave = 6, kEffectShock = 9, kEffectDisco = 10, kEffectCylinder = 17, kEffectRotor = 18;

Vec3f add(const Vec3f& a, const Vec3f& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f scale(const Vec3f& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3f& a) { return std::sqrt(dot(a, a)); }

// A light actor's light, its classes' defaults under its own properties;
// false if they don't read (it then lights nothing).
bool readLight(const PackageSet& packages, const Package& package, int32_t ref, ClassDefaults& defaults,
               const ScriptedActors* scripted, LightSource& l) {
    const ObjectRef obj = packages.resolve(package, ref);
    ActorProperties props;
    if (!obj || defaults.actorProperties(*obj.package, obj.exportIndex, props) ||
        !props.vector("Location", l.position))
        return false;
    l.ref = ref;
    // What the level's scripts did to it at the start.
    const ScriptedActor* changed = nullptr;
    if (scripted && obj.package == &package)
        if (auto it = scripted->find(obj.exportIndex); it != scripted->end())
            changed = &it->second;
    if (changed && changed->removed)
        return true;
    // Fallbacks as Engine.Light's defaults: a steady white light of
    // brightness 64 and radius 64. Lights the level lists are lights, so
    // a missing LightType counts as steady.
    l.type = changed && changed->lightType >= 0 ? uint8_t(changed->lightType) : props.byte("LightType", kLightSteady);
    const uint8_t placed = props.byte("LightBrightness", 64);
    uint8_t brightness = changed && changed->lightBrightness >= 0 ? uint8_t(changed->lightBrightness) : placed;
    // Scripts may turn it on later (a TriggerLight waiting for its
    // trigger, one a trigger names by its Tag): kept at the brightness it
    // was placed with, starting at nothing. One its scripts put out at the
    // start (a CTV_TriggerLight not initially on) was left out, and never
    // lit anything when they turned it on (M03_SiskoL1A's BaseLightON
    // lights, once the alarm's red ones go out).
    const std::string cls = obj.package->className(obj.exportIndex + 1);
    const std::string tag = props.nameValue("Tag");
    l.scripted = (!cls.empty() && cls != "Light") || (!tag.empty() && tag != "None" && tag != cls);
    if (l.scripted && brightness == 0 && placed > 0) {
        brightness = placed;
        l.startsOff = true;
    }
    if (brightness == 0 || (l.type == kLightNone && !l.scripted))
        return true;
    l.color = lightColor(props.byte("LightHue"), props.byte("LightSaturation", 255), brightness);
    l.radius = kRadiusUnit * (float(props.byte("LightRadius", 64)) + 1);
    // LightPeriod in 35ths of a second a cycle, LightPhase in 256ths of one.
    l.period = float(std::max<uint8_t>(props.byte("LightPeriod", 32), 1)) / 35;
    l.phase = float(props.byte("LightPhase")) / 256;
    const uint8_t effect = props.byte("LightEffect");
    l.effect = effect;
    l.cylinder = effect == kEffectCylinder;
    l.specialLit = props.boolean("bSpecialLit");
    l.incidence = effect != kEffectNonIncidence;
    l.brightness = brightness;
    if (effect == kEffectSpotlight || effect == kEffectStaticSpot) {
        l.spot = true;
        Rotator rot;
        props.rotator("Rotation", rot);
        const float pitch = float(rot.pitch) * 2 * kPi / 65536, yaw = float(rot.yaw) * 2 * kPi / 65536;
        l.direction = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        // LightCone 0-255 as 0-90 degrees off the axis.
        l.cosCone = std::cos(float(props.byte("LightCone", 128)) / 256.0f * kPi / 2);
    }
    return true;
}

bool animatedEffect(uint8_t effect) {
    return effect == kEffectTorchWaver || effect == kEffectFireWaver || effect == kEffectWateryShimmer ||
           effect == kEffectSlowWave || effect == kEffectFastWave || effect == kEffectShock || effect == kEffectDisco ||
           effect == kEffectRotor;
}

bool changes(const LightSource& l) {
    return l.type == kLightPulse || l.type == kLightBlink || l.type == kLightFlicker || l.type == kLightStrobe ||
           l.type == kLightSubtlePulse || animatedEffect(l.effect) || l.scripted;
}

// A repeatable random number 0-1 for a light at a step of time.
float noise(uint32_t seed, uint32_t step) {
    uint32_t h = seed * 0x9e3779b1u ^ step * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h & 0xffffff) / float(0xffffff);
}

// The zone a surface is in: the zone on the front of its first node.
std::vector<int> surfaceZones(const Model& model) {
    std::vector<int> zones(model.surfs.size(), 0);
    std::vector<bool> seen(model.surfs.size(), false);
    for (const BspNode& n : model.nodes)
        if (n.vertexCount >= 3 && !seen[size_t(n.surf)]) {
            seen[size_t(n.surf)] = true;
            zones[size_t(n.surf)] = n.zone[1] ? n.zone[1] : n.zone[0];
        }
    return zones;
}

} // namespace

Vec3f lightColor(uint8_t hue, uint8_t saturation, uint8_t brightness) {
    // The hue round six sectors (red, yellow, green, cyan, blue, magenta),
    // then mixed with white by the saturation.
    const float h = float(hue) / 256.0f * 6.0f;
    const int sector = int(h) % 6;
    const float f = h - std::floor(h);
    Vec3f c;
    switch (sector) {
    case 0: c = {1, f, 0}; break;
    case 1: c = {1 - f, 1, 0}; break;
    case 2: c = {0, 1, f}; break;
    case 3: c = {0, 1 - f, 1}; break;
    case 4: c = {f, 0, 1}; break;
    default: c = {1, 0, 1 - f}; break;
    }
    const float white = float(saturation) / 255.0f;
    const float b = float(brightness) / kBrightnessUnit;
    for (float& v : c)
        v = (v * (1 - white) + white) * b;
    // As strong whatever its colour: the channels scaled to sum as white's
    // would, so a pure red light puts all its brightness into red (three
    // times what mixing it with white left it). Checked against a
    // recording of the original: the shortfall grew with how saturated a
    // light was (a pale blue lab about right, a yellow-lit strip 1.5 times
    // too dark, the red emergency lights of M03_SiskoL1A 2.7 times).
    const float sum = c[0] + c[1] + c[2];
    if (sum > 0)
        for (float& v : c)
            v *= 3 * b / sum;
    return c;
}

float lightLevel(const LightSource& l, float time) { return lightLevel(l, l.type, time); }

// Linear. Checked against a recording of the original (the opening's lab,
// Kira's monastery courtyard): bright areas came within 10-20%, and how
// far the dark ones fell short followed only how dark they were (the
// recording's brightness setting, not the falloff); 1 - f^2 brightened
// some dark areas towards it and others past it.
float lightFalloff(float f) { return 1 - std::clamp(f, 0.0f, 1.0f); }

// A large cylinder light (the outdoor maps' sky light, radius 1000 and
// more) lights without its shadows. Hass'terral's snowfield (M10_SiskoL1A)
// is lit by 24 of them, radius 6400, high above; the editor baked their
// shadow bits as traced from where they are (94% agreeing with a clear
// line to them), leaving most of the ground in the cliffs' and buildings'
// shadow, but in a recording of the original the whole field is evenly
// lit, no shadow of a cliff anywhere. Ignoring the bits for them matches
// its brightness. The indoor cylinder lights (radius 50 to 625) keep their
// shadows. Reluva's reading: the rule is fitted, the original's code
// isn't known.
bool unshadowed(const LightSource& l) { return l.cylinder && l.radius >= 1000; }

float lightDistance(const LightSource& l, const Vec3f& p) {
    const float dx = l.position[0] - p[0], dy = l.position[1] - p[1], dz = l.cylinder ? 0 : l.position[2] - p[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

namespace {

// Smooth noise 0-1 for a light: random values `rate` times a second,
// blended between.
float smoothNoise(uint32_t seed, float time, float rate) {
    const float t = std::max(0.0f, time) * rate;
    const uint32_t step = uint32_t(t);
    const float f = t - float(step), s = f * f * (3 - 2 * f);
    return noise(seed, step) * (1 - s) + noise(seed, step + 1) * s;
}

uint32_t lightSeed(const LightSource& l) {
    return uint32_t(int32_t(l.position[0] * 3 + l.position[1] * 5 + l.position[2] * 11)) ^ 0x5bd1e995u;
}

// The share a light's LightEffect gives as a whole, 0-1, `time` seconds
// in: what moving actors get, and (shock) the lightmaps too. Reluva's
// reading of the names, at the rates they suggest.
float effectLevel(const LightSource& l, float time) {
    const uint32_t seed = lightSeed(l);
    switch (l.effect) {
    case kEffectTorchWaver: return 0.85f + 0.15f * smoothNoise(seed, time, 6);
    case kEffectFireWaver: return 0.7f + 0.3f * smoothNoise(seed, time, 10);
    case kEffectWateryShimmer:
        return 0.8f + 0.1f * std::sin(time * 5.1f + float(seed & 255)) + 0.1f * std::sin(time * 7.3f + float(seed >> 8 & 255));
    case kEffectSlowWave:
    case kEffectFastWave: {
        const float rate = l.effect == kEffectSlowWave ? 0.5f : 2.0f;
        return 0.7f + 0.3f * (0.5f + 0.5f * std::sin(2 * kPi * (time * rate + l.phase)));
    }
    case kEffectShock: return smoothNoise(seed, time, 15) < 0.3f ? 0.2f : 1.0f;
    case kEffectDisco:
    case kEffectRotor: return 0.6f + 0.4f * (0.5f + 0.5f * std::sin(2 * kPi * (time / std::max(l.period, 0.05f) + l.phase)));
    default: return 1;
    }
}

} // namespace

bool hasLightPattern(const LightSource& l) {
    return l.effect == kEffectTorchWaver || l.effect == kEffectFireWaver || l.effect == kEffectWateryShimmer ||
           l.effect == kEffectSlowWave || l.effect == kEffectFastWave || l.effect == kEffectDisco ||
           l.effect == kEffectRotor;
}

// The patterns, by what the names describe (UE1's own aren't known; these
// are Reluva's reading), each in the same range as the effect as a whole:
// - the wavers: brighter and darker by direction from the flame, the
//   directions' shares changing at random (6 and 10 times a second), so
//   the light flickers across a wall rather than all at once;
// - the shimmer: two ripples crossing over the surfaces, drifting;
// - the waves: rings rolling out from the light (half and two a second),
//   half its radius apart (lightmap texels are tens of units: closer
//   rings would fall between them);
// - the rotor: four beams turning round it once a period; disco: spots
//   on a grid of directions, turning the same way.
PatternPoint patternPoint(const Vec3f& o) {
    PatternPoint p;
    p.offset = o;
    const float flat = std::sqrt(o[0] * o[0] + o[1] * o[1]);
    p.yaw = std::atan2(o[1], o[0]);
    p.pitch = std::atan2(o[2], flat);
    p.distance = std::sqrt(flat * flat + o[2] * o[2]);
    return p;
}

PatternNow patternNow(const LightSource& l, float time) {
    PatternNow n;
    n.time = time;
    const uint32_t seed = lightSeed(l);
    if (l.effect == kEffectTorchWaver || l.effect == kEffectFireWaver) {
        const float rate = l.effect == kEffectTorchWaver ? 6.0f : 10.0f;
        n.a = smoothNoise(seed, time, rate);
        n.b = smoothNoise(seed ^ 0x9e3779b9u, time, rate);
    }
    n.turn = 2 * kPi * (time / std::max(l.period, 0.05f) + l.phase);
    return n;
}

float lightPattern(const LightSource& l, const PatternNow& now, const PatternPoint& p) {
    const Vec3f& o = p.offset;
    switch (l.effect) {
    case kEffectTorchWaver:
    case kEffectFireWaver: {
        const float s = 0.5f + 0.25f * std::sin(5 * p.yaw + 3 * p.pitch + 2 * kPi * now.a) +
                        0.25f * std::sin(9 * p.yaw - 4 * p.pitch + 2 * kPi * now.b);
        return l.effect == kEffectTorchWaver ? 0.85f + 0.15f * s : 0.7f + 0.3f * s;
    }
    case kEffectWateryShimmer: {
        const float s = 0.5f + 0.25f * std::sin((o[0] + o[1]) / 40 + now.time * 5.1f) +
                        0.25f * std::sin((o[0] - o[1]) / 27 + o[2] / 33 - now.time * 7.3f);
        return 0.6f + 0.4f * s;
    }
    case kEffectSlowWave:
    case kEffectFastWave: {
        const float rate = l.effect == kEffectSlowWave ? 0.5f : 2.0f;
        const float wavelength = std::max(l.radius / 2, 64.0f);
        return 0.7f + 0.3f * (0.5f + 0.5f * std::sin(2 * kPi * (p.distance / wavelength - now.time * rate - l.phase)));
    }
    case kEffectRotor: {
        const float s = 0.5f + 0.5f * std::cos(4 * (p.yaw - now.turn));
        return 0.3f + 0.7f * s * s;
    }
    case kEffectDisco: {
        const float s = std::sin(6 * (p.yaw - now.turn)) * std::sin(6 * p.pitch);
        return s > 0.5f ? 1.0f : 0.35f;
    }
    default: return 1;
    }
}

float lightLevel(const LightSource& l, uint8_t type, float time, bool whole) {
    return (whole || !hasLightPattern(l) ? effectLevel(l, time) : 1.0f) * [&] {
    // How far through its cycle it is, 0-1.
    const float cycles = time / std::max(l.period, 1e-3f) + l.phase;
    const float at = cycles - std::floor(cycles);
    switch (type) {
    case kLightPulse: return 0.6f + 0.39f * std::sin(2 * kPi * at);
    case kLightSubtlePulse: return 0.9f + 0.09f * std::sin(2 * kPi * at);
    case kLightBlink: return at < 0.5f ? 1.0f : 0.0f;
    case kLightStrobe: return at < 0.1f ? 1.0f : 0.0f; // a flash at the start of each cycle
    case kLightFlicker: {
        // A new random brightness 20 times a second, now and then out.
        const float r = noise(uint32_t(l.phase * 256) + uint32_t(int32_t(l.position[0] * 7 + l.position[1] * 13 + l.position[2])),
                              uint32_t(std::max(0.0f, time) * 20));
        return r < 0.15f ? 0.0f : 0.4f + 0.6f * r;
    }
    case kLightNone: return 0.0f;
    default: return 1.0f;
    }
    }();
}

LevelLights gatherLights(const PackageSet& packages, const Package& package, const Model& model,
                         const ScriptedActors* scripted) {
    LevelLights out;
    ClassDefaults defaults(packages);
    for (const Zone& z : model.zones) {
        ActorProperties props;
        Vec3f ambient{};
        const ObjectRef zone = z.actor ? packages.resolve(package, z.actor) : ObjectRef{};
        if (zone && !defaults.actorProperties(*zone.package, zone.exportIndex, props))
            ambient = lightColor(props.byte("AmbientHue"), props.byte("AmbientSaturation", 255),
                                 props.byte("AmbientBrightness"));
        out.zoneAmbient.push_back(ambient);
    }
    // The lights list repeats each light for every lightmap it reaches
    // (hundreds of thousands of entries in a large level), so each actor
    // is read once.
    std::map<int32_t, LightSource> byRef;
    for (int32_t ref : model.lights) {
        if (ref == 0) {
            out.sources.push_back({});
            continue;
        }
        auto it = byRef.find(ref);
        if (it == byRef.end()) {
            LightSource l;
            if (!readLight(packages, package, ref, defaults, scripted, l))
                ++out.unreadable;
            if (l.radius > 0 && changes(l)) {
                l.animated = int(out.animated.size());
                out.animated.push_back(l);
            }
            it = byRef.emplace(ref, l).first;
            if (l.radius > 0)
                out.unique.push_back(l);
        }
        out.sources.push_back(it->second);
    }
    return out;
}

void AnimatedLightmap::draw(const std::vector<float>& levels, std::vector<uint8_t>& rgba,
                            const std::vector<LightSource>* sources, const std::vector<PatternNow>* now) const {
    rgba.resize(base.size() * 4);
    for (size_t t = 0; t < base.size(); ++t) {
        Vec3f light = base[t];
        for (const Light& l : lights) {
            const Vec3f& share = l.share[t];
            if (share[0] == 0 && share[1] == 0 && share[2] == 0)
                continue;
            float level = size_t(l.index) < levels.size() ? levels[size_t(l.index)] : 1;
            if (!l.points.empty() && sources && now && size_t(l.index) < sources->size() && size_t(l.index) < now->size())
                level *= lightPattern((*sources)[size_t(l.index)], (*now)[size_t(l.index)], l.points[t]);
            light = add(light, scale(share, level));
        }
        for (size_t k = 0; k < 3; ++k)
            rgba[t * 4 + k] = uint8_t(std::clamp(light[k], 0.0f, kMaxLight) / 2 * 255 + 0.5f);
        rgba[t * 4 + 3] = 255;
    }
}

std::array<float, 2> LightmapAtlas::uv(int surf, const Vec3f& p) const {
    const LightmapPlacement& m = surfs[size_t(surf)];
    const float lu = dot(p, m.uAxis) + m.uOffset, lv = dot(p, m.vAxis) + m.vOffset;
    return {(float(m.x) + lu + 0.5f) / float(pageSize), (float(m.y) + lv + 0.5f) / float(pageSize)};
}

LightmapAtlas buildLightmaps(const Model& model, const LevelLights& lights, int pageSize) {
    LightmapAtlas atlas;
    atlas.pageSize = pageSize;
    atlas.surfs.resize(model.surfs.size());
    if (!model.hasLighting())
        return atlas;

    // Each surface's polygon points, to fit its sample grid to.
    std::vector<std::vector<Vec3f>> points(model.surfs.size());
    for (const BspNode& n : model.nodes)
        for (int k = 0; k < n.vertexCount; ++k)
            points[size_t(n.surf)].push_back(model.points[size_t(model.verts[size_t(n.vertPool + k)].point)]);
    const std::vector<int> zones = surfaceZones(model);

    struct Grid {
        int surf = -1;
        int w = 0, h = 0;  // samples
        int zone = 0;      // for ambient-only blocks (surf -1)
        Vec3f uAxis{}, vAxis{};
        float uOffset = 0, vOffset = 0;
    };
    std::vector<Grid> grids;
    std::map<int, size_t> ambientBlock; // zone -> its grid
    std::vector<int> gridOfSurf(model.surfs.size(), -1);

    auto useAmbient = [&](size_t s) {
        const int z = zones[s];
        auto it = ambientBlock.find(z);
        if (it == ambientBlock.end()) {
            Grid g;
            g.w = g.h = 1;
            g.zone = z;
            it = ambientBlock.emplace(z, grids.size()).first;
            grids.push_back(g);
        }
        gridOfSurf[s] = int(it->second);
    };

    for (size_t s = 0; s < model.surfs.size(); ++s) {
        const BspSurf& surf = model.surfs[s];
        if (points[s].empty() || (surf.polyFlags & (polyflag::Unlit | polyflag::Invisible)))
            continue;
        if (surf.lightMap < 0) {
            useAmbient(s);
            continue;
        }
        const LightMapIndex& m = model.lightMaps[size_t(surf.lightMap)];
        ++atlas.stats.lightmaps;
        if (m.uClamp + 2 > pageSize || m.vClamp + 2 > pageSize || m.uClamp <= 0 || m.vClamp <= 0) {
            ++atlas.stats.tooLarge;
            useAmbient(s);
            continue;
        }
        const Vec3f& base = model.points[size_t(surf.base)];
        const Vec3f& tu = model.vectors[size_t(surf.textureU)];
        const Vec3f& tv = model.vectors[size_t(surf.textureV)];

        // The grid runs along the surface's texture axes, uScale and vScale
        // texture units between samples, from the pan (X along U, Y along V)
        // measured from the base point. Where that doesn't cover the polygon
        // (a few degenerate surfaces), from the polygon's own lowest corner.
        float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
        for (const Vec3f& p : points[s]) {
            const Vec3f d = sub(p, base);
            minU = std::min(minU, dot(d, tu));
            maxU = std::max(maxU, dot(d, tu));
            minV = std::min(minV, dot(d, tv));
            maxV = std::max(maxV, dot(d, tv));
        }
        const bool scaled = m.uScale > 0 && m.vScale > 0;
        const float us = scaled ? m.uScale : std::max(1.0f, (maxU - minU) / float(std::max(1, m.uClamp - 1)));
        const float vs = scaled ? m.vScale : std::max(1.0f, (maxV - minV) / float(std::max(1, m.vClamp - 1)));
        // Covers the polygon, give or take a sample.
        auto fits = [&](float ou, float ov) {
            const float tol = 1.0f;
            return (minU - ou) / us >= -tol && (maxU - ou) / us <= float(m.uClamp - 1) + tol &&
                   (minV - ov) / vs >= -tol && (maxV - ov) / vs <= float(m.vClamp - 1) + tol;
        };
        float ou = m.pan[0], ov = m.pan[1];
        if (!scaled || !fits(ou, ov)) {
            ou = minU;
            ov = minV;
            ++atlas.stats.fromBounds;
            if (!scaled || !fits(ou, ov))
                ++atlas.stats.badFit;
        }
        Grid g;
        g.surf = int(s);
        g.w = m.uClamp;
        g.h = m.vClamp;
        g.uAxis = scale(tu, 1 / us);
        g.vAxis = scale(tv, 1 / vs);
        g.uOffset = -(dot(base, tu) + ou) / us;
        g.vOffset = -(dot(base, tv) + ov) / vs;
        gridOfSurf[s] = int(grids.size());
        grids.push_back(g);
    }

    // Pack the grids, each with a one-texel border, in rows (tallest first).
    std::vector<size_t> order(grids.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return grids[a].h > grids[b].h; });
    std::vector<std::array<int, 3>> placed(grids.size()); // page, x, y of sample (0, 0)
    int page = -1, cx = 0, cy = 0, rowH = 0;
    for (size_t i : order) {
        const int w = grids[i].w + 2, h = grids[i].h + 2;
        if (page < 0 || cx + w > pageSize) {
            cx = 0;
            cy += rowH;
            rowH = 0;
        }
        if (page < 0 || cy + h > pageSize) {
            ++page;
            atlas.pages.push_back({});
            atlas.pages.back().w = atlas.pages.back().h = pageSize;
            atlas.pages.back().rgba.assign(size_t(pageSize) * size_t(pageSize) * 4, 0);
            cx = cy = rowH = 0;
        }
        placed[i] = {page, cx + 1, cy + 1};
        cx += w;
        rowH = std::max(rowH, h);
    }

    // Light each grid's samples, then copy its edges into the border. The
    // lights whose brightness changes are kept apart, each lightmap they
    // reach redrawn with them as they do (drawn at time 0 here).
    std::vector<float> startLevels;
    for (const LightSource& l : lights.animated)
        startLevels.push_back(l.startsOff ? 0.0f : lightLevel(l, l.type, 0, false));
    std::vector<PatternNow> startNow;
    for (const LightSource& l : lights.animated)
        startNow.push_back(patternNow(l, 0));
    std::vector<uint8_t> rgba;
    for (size_t gi = 0; gi < grids.size(); ++gi) {
        const Grid& g = grids[gi];
        image::Image& img = atlas.pages[size_t(placed[gi][0])];
        const int x0 = placed[gi][1], y0 = placed[gi][2];
        // The samples with their border, (i, j) at (i + 1, j + 1).
        const int bw = g.w + 2, bh = g.h + 2;
        auto at = [&](int i, int j) { return size_t(j + 1) * size_t(bw) + size_t(i + 1); };
        AnimatedLightmap anim;
        anim.base.assign(size_t(bw) * size_t(bh), Vec3f{});
        std::map<int, size_t> animIndex; // animated light -> its entry in anim.lights
        if (g.surf < 0) {
            const Vec3f ambient = size_t(g.zone) < lights.zoneAmbient.size() ? lights.zoneAmbient[size_t(g.zone)] : Vec3f{};
            anim.base.assign(anim.base.size(), ambient);
        } else {
            const BspSurf& surf = model.surfs[size_t(g.surf)];
            const LightMapIndex& m = model.lightMaps[size_t(surf.lightMap)];
            const Vec3f& n = model.vectors[size_t(surf.normal)];
            const Vec3f ambient = size_t(zones[size_t(g.surf)]) < lights.zoneAmbient.size()
                                      ? lights.zoneAmbient[size_t(zones[size_t(g.surf)])]
                                      : Vec3f{};
            // A sample's position: where the plane meets the grid lines,
            // solving uAxis.P = i - uOffset, vAxis.P = j - vOffset, n.P = n.base.
            const Vec3f vn = cross(g.vAxis, n), nu = cross(n, g.uAxis), uv = cross(g.uAxis, g.vAxis);
            const float det = dot(g.uAxis, vn);
            const float planeD = dot(n, model.points[size_t(surf.base)]);
            const int rowBytes = (m.uClamp + 7) / 8;
            bool anyLit = false;
            for (int j = 0; j < g.h; ++j)
                for (int i = 0; i < g.w; ++i) {
                    Vec3f light = ambient;
                    if (std::fabs(det) > 1e-12f) {
                        const Vec3f p = scale(add(add(scale(vn, float(i) - g.uOffset), scale(nu, float(j) - g.vOffset)),
                                                  scale(uv, planeD)),
                                              1 / det);
                        int k = 0;
                        for (size_t li = size_t(std::max(m.lightActors, 0));
                             m.lightActors >= 0 && li < model.lights.size() && model.lights[li]; ++li, ++k) {
                            const size_t bit = size_t(m.dataOffset) + size_t(k) * size_t(rowBytes) * size_t(m.vClamp) +
                                               size_t(j) * size_t(rowBytes) + size_t(i / 8);
                            const LightSource& src = lights.sources[li];
                            if (!((model.lightBits[bit] >> (i & 7)) & 1) && !unshadowed(src))
                                continue;
                            if (src.radius <= 0)
                                continue;
                            const Vec3f d = sub(src.position, p);
                            const float dist = lightDistance(src, p), straight = length(d);
                            if (dist >= src.radius || straight <= 0)
                                continue;
                            float amount = lightFalloff(dist / src.radius);
                            if (src.incidence) {
                                const float facing = dot(n, d) / straight;
                                if (facing <= 0)
                                    continue;
                                amount *= facing;
                            }
                            if (src.spot) {
                                const float c = -dot(d, src.direction) / straight;
                                if (c < src.cosCone)
                                    continue;
                                amount *= (c - src.cosCone) / std::max(1e-3f, 1 - src.cosCone);
                            }
                            anyLit = true;
                            if (src.animated < 0) {
                                light = add(light, scale(src.color, amount));
                                continue;
                            }
                            auto it = animIndex.find(src.animated);
                            if (it == animIndex.end()) {
                                it = animIndex.emplace(src.animated, anim.lights.size()).first;
                                AnimatedLightmap::Light l;
                                l.index = src.animated;
                                l.share.assign(anim.base.size(), Vec3f{});
                                if (hasLightPattern(src))
                                    l.points.assign(anim.base.size(), PatternPoint{});
                                anim.lights.push_back(std::move(l));
                            }
                            AnimatedLightmap::Light& al = anim.lights[it->second];
                            al.share[at(i, j)] = add(al.share[at(i, j)], scale(src.color, amount));
                            if (!al.points.empty())
                                al.points[at(i, j)] = patternPoint(sub(p, src.position));
                        }
                    }
                    anim.base[at(i, j)] = light;
                }
            if (anyLit)
                ++atlas.stats.lit;
            // The border repeats the edges, so filtering at them doesn't
            // reach a neighbour's texels.
            auto border = [&](std::vector<Vec3f>& v) {
                for (int j = -1; j <= g.h; ++j)
                    for (int i = -1; i <= g.w; ++i)
                        if (i < 0 || i >= g.w || j < 0 || j >= g.h)
                            v[at(i, j)] = v[at(std::clamp(i, 0, g.w - 1), std::clamp(j, 0, g.h - 1))];
            };
            border(anim.base);
            for (auto& l : anim.lights) {
                border(l.share);
                if (!l.points.empty()) {
                    for (int j = -1; j <= g.h; ++j)
                        for (int i = -1; i <= g.w; ++i)
                            if (i < 0 || i >= g.w || j < 0 || j >= g.h)
                                l.points[at(i, j)] = l.points[at(std::clamp(i, 0, g.w - 1), std::clamp(j, 0, g.h - 1))];
                }
            }
        }
        anim.page = placed[gi][0];
        anim.x = x0 - 1;
        anim.y = y0 - 1;
        anim.w = bw;
        anim.h = bh;
        anim.zone = g.surf < 0 ? g.zone : zones[size_t(g.surf)];
        anim.draw(startLevels, rgba, &lights.animated, &startNow);
        for (int j = 0; j < bh; ++j)
            std::copy_n(rgba.data() + size_t(j) * size_t(bw) * 4, size_t(bw) * 4, img.pixel(anim.x, anim.y + j));
        if (!anim.lights.empty())
            atlas.animated.push_back(std::move(anim));
    }

    for (size_t s = 0; s < model.surfs.size(); ++s) {
        if (gridOfSurf[s] < 0)
            continue;
        const Grid& g = grids[size_t(gridOfSurf[s])];
        LightmapPlacement& p = atlas.surfs[s];
        p.page = placed[size_t(gridOfSurf[s])][0];
        p.x = placed[size_t(gridOfSurf[s])][1];
        p.y = placed[size_t(gridOfSurf[s])][2];
        p.uAxis = g.uAxis;
        p.vAxis = g.vAxis;
        p.uOffset = g.uOffset;
        p.vOffset = g.vOffset;
    }
    return atlas;
}

} // namespace reluva::unreal
