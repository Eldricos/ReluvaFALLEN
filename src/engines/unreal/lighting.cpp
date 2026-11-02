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
constexpr uint8_t kEffectTorchWaver = 1, kEffectFireWaver = 2, kEffectWateryShimmer = 3, kEffectSearchlight = 4,
                  kEffectSlowWave = 5, kEffectFastWave = 6, kEffectShock = 9, kEffectDisco = 10, kEffectShell = 14,
                  kEffectInterference = 16, kEffectCylinder = 17, kEffectRotor = 18;
// The original's random numbers are drawn afresh every frame; Reluva's
// step 35 times a second (the engine's tick).
constexpr float kFrameRate = 35;

Vec3f add(const Vec3f& a, const Vec3f& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f scale(const Vec3f& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3f& a) { return std::sqrt(dot(a, a)); }
// One light's share of a texel, as its palette holds it: at most 1 (127 of
// the lightmap's 7 bits) in each channel.
Vec3f capped(const Vec3f& c) { return {std::min(c[0], 1.0f), std::min(c[1], 1.0f), std::min(c[2], 1.0f)}; }

// A light actor's light, its classes' defaults under its own properties;
// false if they don't read (it then lights nothing).
bool readLight(const PackageSet& packages, const Package& package, int32_t ref, ClassDefaults& defaults,
               const ScriptedActors* scripted, float levelBrightness, LightSource& l) {
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
    l.color = lightColor(props.byte("LightHue"), props.byte("LightSaturation", 255), brightness, levelBrightness);
    l.radius = kRadiusUnit * (float(props.byte("LightRadius", 64)) + 1);
    // LightPeriod in 35ths of a second a cycle, LightPhase in 256ths of one.
    l.period = float(std::max<uint8_t>(props.byte("LightPeriod", 32), 1)) / 35;
    l.phase = float(props.byte("LightPhase")) / 256;
    const uint8_t effect = props.byte("LightEffect");
    l.effect = effect;
    l.cylinder = effect == kEffectCylinder;
    l.specialLit = props.boolean("bSpecialLit");
    l.actorShadows = props.boolean("bActorShadows");
    l.isStatic = props.boolean("bStatic");
    // Its volume (VolumeRadius, as LightRadius): VolumeBrightness its
    // glow's (in the light's colour), VolumeFog how much it hides.
    if (const uint8_t v = props.byte("VolumeRadius")) {
        l.volumeRadius = kRadiusUnit * (float(v) + 1);
        l.volumeColor = mixedLightColor(props.byte("LightHue"), props.byte("LightSaturation", 255),
                                        props.byte("VolumeBrightness", 64));
        l.volumeFog = float(props.byte("VolumeFog")) / 255;
    }
    l.incidence = effect != kEffectNonIncidence;
    l.brightness = brightness;
    if (effect == kEffectSpotlight || effect == kEffectStaticSpot) {
        l.spot = true;
        Rotator rot;
        props.rotator("Rotation", rot);
        const float pitch = float(rot.pitch) * 2 * kPi / 65536, yaw = float(rot.yaw) * 2 * kPi / 65536;
        l.direction = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        // LightCone 0-255 as 0-90 degrees off the axis.
        // LightCone: the cosine of its edge is 1 - LightCone / 256 (as
        // Render.dll's spot lighting has it).
        l.cosCone = 1 - float(props.byte("LightCone", 128)) / 256.0f;
    }
    return true;
}

bool animatedEffect(uint8_t effect) {
    return effect == kEffectTorchWaver || effect == kEffectFireWaver || effect == kEffectWateryShimmer ||
           effect == kEffectSearchlight || effect == kEffectSlowWave || effect == kEffectFastWave ||
           effect == kEffectShock || effect == kEffectDisco || effect == kEffectInterference || effect == kEffectRotor;
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

Vec3f engineHsv(uint8_t hue, uint8_t saturation, uint8_t value) {
    const float v = float(value) * 1.4f / 255;
    const float level = std::clamp(0.7f * v / (std::sqrt(v) + 0.01f), 0.0f, 1.0f);
    const float h = float(hue);
    Vec3f c;
    if (hue < 86)
        c = {(85 - h) / 85, h / 85, 0};
    else if (hue < 171)
        c = {0, (170 - h) / 85, (h - 85) / 85};
    else
        c = {(h - 170) / 85, 0, (255 - h) / 84};
    const float white = float(saturation) / 255;
    for (float& x : c)
        x = (x + (1 - x) * white) * level;
    return c;
}

Vec3f mixedLightColor(uint8_t hue, uint8_t saturation, uint8_t brightness) {
    Vec3f c = engineHsv(hue, saturation, 255);
    for (float& x : c)
        x *= float(brightness) / 255;
    return c;
}

Vec3f lightColor(uint8_t hue, uint8_t saturation, uint8_t brightness, float levelBrightness) {
    // A light's lightmap is a palette of 256 levels (its falloff and
    // shadow at a texel) times its colour, each at most 127, drawn
    // doubled (OpenGLDrv's GL_DST_COLOR x GL_SRC_COLOR blend): 127 is the
    // texture as it is, so a level of 255 times the colour is twice it.
    Vec3f c = mixedLightColor(hue, saturation, brightness);
    for (float& x : c)
        x *= 2 * levelBrightness;
    return c;
}

Vec3f zoneAmbientColor(uint8_t hue, uint8_t saturation, uint8_t brightness) {
    // Filled in as 64 of the 127 before the lights add theirs.
    Vec3f c = engineHsv(hue, saturation, brightness);
    for (float& x : c)
        x *= 64.0f / 127.5f;
    return c;
}

float lightLevel(const LightSource& l, float time) { return lightLevel(l, l.type, time); }

// 1 - 3 f^2 + 2 f^3 of the way out to its radius, as Render.dll's table
// of 4096 steps of (distance / radius)^2 (it divides that by f, and
// multiplies by the light's distance from the surface's plane over its
// radius: together this falloff times the cosine of the light's angle).
float lightFalloff(float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    return 1 - 3 * f * f + 2 * f * f * f;
}

float surfaceFalloff(const LightSource& l, float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    if (l.effect == kEffectShell) // a thin shell from 0.8 to 1 of its radius, whatever the facing
        return f >= 0.8f && f < 1 ? 1 - 10 * std::fabs(f - 0.9f) : 0;
    if (l.cylinder) // across the floor only, no facing: 1 - (d / r)^2
        return 1 - f * f;
    if (!l.incidence) // LE_NonIncidence: straight, no facing
        return std::min(1.0f, 1.02f - f);
    return lightFalloff(f);
}

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
// The share a light's LightEffect gives as a whole: none. The original's
// effects all work texel by texel on the lightmaps (lightPattern); the
// light actors get is its LightType's alone (Render.dll's GlobalLighting).
float effectLevel(const LightSource&, float) { return 1; }

} // namespace

bool hasLightPattern(const LightSource& l) { return animatedEffect(l.effect); }

// The patterns, as Render.dll's lighting functions make them (read for what
// they do), each a share of the light's usual falloff and facing there.
// Angles in the light's own axes: yaw round its vertical, `turn` the time's.
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
    n.frame = uint32_t(std::max(0.0f, time) * kFrameRate);
    // The searchlight's: 35 / LightPeriod radians a second, from LightPhase
    // (in 64ths of a turn).
    const float period = l.period * 35; // LightPeriod
    n.turn = (period > 0.5f ? time * 35 / period : 0.0f) + float(l.phase * 256) * 2 * kPi / 64 + 2 * kPi;
    return n;
}

namespace {

// sin of an angle in 65536ths of a turn, as the engine's table.
float sinTurns(float units) { return std::sin(units * 2 * kPi / 65536); }

uint32_t texelSeed(const PatternPoint& p) {
    return uint32_t(int32_t(p.offset[0] * 3.1f) * 73856093 ^ int32_t(p.offset[1] * 3.1f) * 19349663 ^
                    int32_t(p.offset[2] * 3.1f) * 83492791);
}

} // namespace

float lightPattern(const LightSource& l, const PatternNow& now, const PatternPoint& p) {
    const Vec3f& o = p.offset;
    const float flat2 = o[0] * o[0] + o[1] * o[1];
    switch (l.effect) {
    // A random number each frame for each texel: 0.95-1 (torch), 0.8-1 (fire).
    case kEffectTorchWaver: return 0.95f + 0.05f * noise(texelSeed(p), now.frame);
    case kEffectFireWaver: return 0.8f + 0.2f * noise(texelSeed(p), now.frame);
    // 0.6-1, each texel's number easing towards a new random one (a
    // sixteenth of the way a tick).
    case kEffectWateryShimmer: return 0.6f + 0.4f * smoothNoise(texelSeed(p), now.time, kFrameRate / 16);
    // Rings rolling out 35 units a second, 64 (slow) or 32 (fast) apart.
    case kEffectSlowWave: return 0.7f + 0.3f * sinTurns((p.distance - now.time * 35) * 1024);
    case kEffectFastWave: return 0.7f + 0.3f * sinTurns((p.distance - now.time * 35) * 2048);
    // One beam a turn, a quarter of it wide, nothing near its axis.
    case kEffectSearchlight: {
        const float a = std::fmod(4 * p.yaw + now.turn, 8 * kPi);
        if (a < kPi || a > 3 * kPi)
            return 0;
        return 0.5f * (1 + std::sin(-a)) * std::min(1.0f, flat2 * 6e-5f);
    }
    // Six beams turning, even near its axis.
    case kEffectRotor: {
        const float s = 0.5f * (1 + std::sin(-(6 * p.yaw + 3.5f * now.time)));
        const float k = std::min(1.0f, flat2 * 1e-4f);
        return s * k + (1 - k);
    }
    // Spots by yaw and by height, darkening where they are.
    case kEffectDisco: {
        const float a = 0.5f * (1 + std::sin(-(11 * p.yaw + 5 * now.time)));
        const float b = 0.5f * (1 + std::cos(-(11 * std::atan2(o[2], flat2) + 5 * now.time)));
        return 1 - (a + b - a * b) * std::min(1.0f, flat2 * 5e-5f);
    }
    // Bands by height, rolling.
    case kEffectInterference: return 0.5f * (1 + std::sin(-(11 * std::atan2(o[2], flat2) + 5 * now.time)));
    // Three fine ripples.
    case kEffectShock: {
        const float d = std::round(p.distance * 8), t = now.time * 4000;
        return (0.9f + 0.1f * sinTurns(-16 * (d + t))) * (0.9f + 0.1f * sinTurns(16 * (std::floor(d / 2) - t))) *
               (0.9f + 0.1f * sinTurns(16 * (std::floor(d / 4) - t)));
    }
    default: return 1;
    }
}

float lightLevel(const LightSource& l, uint8_t type, float time, bool whole) {
    return (whole || !hasLightPattern(l) ? effectLevel(l, time) : 1.0f) * [&] {
    // How far through its cycle it is, 0-1.
    const float cycles = time / std::max(l.period, 1e-3f) + l.phase;
    const float at = cycles - std::floor(cycles);
    // As Render.dll's GlobalLighting: a frame's step for the random ones.
    const uint32_t frame = uint32_t(std::max(0.0f, time) * kFrameRate);
    const uint32_t seed = lightSeed(l);
    switch (type) {
    case kLightPulse: return 0.6f + 0.39f * std::sin(2 * kPi * at);
    case kLightSubtlePulse: return 0.9f + 0.09f * std::sin(2 * kPi * at);
    // On or out at random each frame (the parity of its phase in 65536ths).
    case kLightBlink: return noise(seed, frame) < 0.5f ? 1.0f : 0.0f;
    // All of them together, on one frame and out the next.
    case kLightStrobe: return frame % 2 == 0 ? 1.0f : 0.0f;
    // A random number each frame: out under a half, else that bright.
    case kLightFlicker: {
        const float r = noise(seed ^ 0x51ed270bu, frame);
        return r < 0.5f ? 0.0f : r;
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
        Vec3f ambient{}, actorAmbient{};
        uint8_t ambientBrightness = 0;
        bool fog = false;
        const ObjectRef zone = z.actor ? packages.resolve(package, z.actor) : ObjectRef{};
        if (zone && !defaults.actorProperties(*zone.package, zone.exportIndex, props)) {
            ambientBrightness = props.byte("AmbientBrightness");
            ambient = zoneAmbientColor(props.byte("AmbientHue"), props.byte("AmbientSaturation", 255), ambientBrightness);
            fog = props.boolean("bFogZone");
            actorAmbient = engineHsv(props.byte("AmbientHue"), props.byte("AmbientSaturation", 255), ambientBrightness);
        }
        out.zoneAmbient.push_back(ambient);
        out.zoneAmbientBrightness.push_back(ambientBrightness);
        out.zoneActorAmbient.push_back(actorAmbient);
        out.zoneFog.push_back(fog);
    }
    // The lights list repeats each light for every lightmap it reaches
    // (hundreds of thousands of entries in a large level), so each actor
    // is read once.
    // The level's Brightness (LevelInfo) scales every light.
    float levelBrightness = 1;
    for (size_t i = 0; i < package.exports().size(); ++i)
        if (package.className(int32_t(i) + 1) == "LevelInfo") {
            ActorProperties props;
            if (!defaults.actorProperties(package, int(i), props))
                levelBrightness = props.number("Brightness", 1);
            break;
        }
    out.levelBrightness = levelBrightness;
    std::map<int32_t, LightSource> byRef;
    for (int32_t ref : model.lights) {
        if (ref == 0) {
            out.sources.push_back({});
            continue;
        }
        auto it = byRef.find(ref);
        if (it == byRef.end()) {
            LightSource l;
            if (!readLight(packages, package, ref, defaults, scripted, levelBrightness, l))
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
            light = add(light, capped(scale(share, level)));
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
                            float amount = surfaceFalloff(src, dist / src.radius);
                            if (src.incidence && !src.cylinder && src.effect != kEffectShell) {
                                const float facing = dot(n, d) / straight;
                                if (facing <= 0)
                                    continue;
                                amount *= facing;
                            }
                            if (src.spot) {
                                const float c = -dot(d, src.direction) / straight;
                                if (c < src.cosCone)
                                    continue;
                                const float f = (c - src.cosCone) / std::max(1e-3f, 1 - src.cosCone);
                                amount *= f * f;
                            }
                            anyLit = true;
                            if (src.animated < 0) {
                                light = add(light, capped(scale(src.color, amount)));
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
