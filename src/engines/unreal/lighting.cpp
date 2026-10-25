// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/lighting.h"

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

Vec3f add(const Vec3f& a, const Vec3f& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f scale(const Vec3f& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3f& a) { return std::sqrt(dot(a, a)); }

bool readActor(const PackageSet& packages, const Package& package, int32_t ref, PropertyList& props) {
    const ObjectRef obj = packages.resolve(package, ref);
    if (!obj)
        return false;
    std::vector<uint8_t> data;
    if (obj.package->readExport(obj.exportIndex, data))
        return false;
    Reader r(data, size_t(obj.package->exports()[size_t(obj.exportIndex)].serialOffset));
    return !beginObject(*obj.package, obj.exportIndex, r, props);
}

// A light actor's light; false if its properties don't read (it then
// lights nothing).
bool readLight(const PackageSet& packages, const Package& package, int32_t ref, LightSource& l) {
    PropertyList props;
    if (!readActor(packages, package, ref, props) || !props.vector("Location", l.position))
        return false;
    // Defaults as Engine.Light's: a steady white light of brightness 64
    // and radius 64. Lights the level lists are lights, so a missing
    // LightType counts as steady.
    if (props.find("LightType") && props.byte("LightType") == 0)
        return true;
    l.color = lightColor(props.byte("LightHue"), props.byte("LightSaturation", 255), props.byte("LightBrightness", 64));
    l.radius = kRadiusUnit * (float(props.byte("LightRadius", 64)) + 1);
    const uint8_t effect = props.byte("LightEffect");
    l.incidence = effect != kEffectNonIncidence;
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
    return c;
}

LevelLights gatherLights(const PackageSet& packages, const Package& package, const Model& model) {
    LevelLights out;
    for (const Zone& z : model.zones) {
        PropertyList props;
        Vec3f ambient{};
        if (z.actor && readActor(packages, package, z.actor, props))
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
            if (!readLight(packages, package, ref, l))
                ++out.unreadable;
            it = byRef.emplace(ref, l).first;
        }
        out.sources.push_back(it->second);
    }
    return out;
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

        // Which axes: the texture's as stored, or the same made unit length
        // (world units); whichever spans the polygon in about the samples
        // the lightmap has.
        struct Fit {
            Vec3f ua, va;
            float minU, maxU, minV, maxV, score;
        };
        auto fit = [&](const Vec3f& ua, const Vec3f& va) {
            Fit f{ua, va, 1e30f, -1e30f, 1e30f, -1e30f, 0};
            for (const Vec3f& p : points[s]) {
                const Vec3f d = sub(p, base);
                f.minU = std::min(f.minU, dot(d, ua));
                f.maxU = std::max(f.maxU, dot(d, ua));
                f.minV = std::min(f.minV, dot(d, va));
                f.maxV = std::max(f.maxV, dot(d, va));
            }
            const float us = m.uScale > 0 ? m.uScale : 1, vs = m.vScale > 0 ? m.vScale : 1;
            f.score = std::fabs((f.maxU - f.minU) / us - float(m.uClamp - 1)) +
                      std::fabs((f.maxV - f.minV) / vs - float(m.vClamp - 1));
            return f;
        };
        const float lu = length(tu), lv = length(tv);
        const Fit raw = fit(tu, tv);
        const Fit unit = lu > 0 && lv > 0 ? fit(scale(tu, 1 / lu), scale(tv, 1 / lv)) : raw;
        const bool useUnit = unit.score < raw.score;
        const Fit& f = useUnit ? unit : raw;
        if (std::fabs(lu - 1) < 1e-3f && std::fabs(lv - 1) < 1e-3f)
            ++atlas.stats.sameAxes;
        else
            ++(useUnit ? atlas.stats.unitAxes : atlas.stats.rawAxes);
        if (f.score > 2 || m.uScale <= 0 || m.vScale <= 0)
            ++atlas.stats.badFit;
        const float us = m.uScale > 0 ? m.uScale : std::max(1.0f, (f.maxU - f.minU) / float(std::max(1, m.uClamp - 1)));
        const float vs = m.vScale > 0 ? m.vScale : std::max(1.0f, (f.maxV - f.minV) / float(std::max(1, m.vClamp - 1)));

        // Where the grid starts, along those axes from the base point: the
        // pan as offsets, the pan as a point, or the polygon's own corner.
        auto fits = [&](float ou, float ov) {
            const float tol = 1.0f;
            return (f.minU - ou) / us >= -tol && (f.maxU - ou) / us <= float(m.uClamp - 1) + tol &&
                   (f.minV - ov) / vs >= -tol && (f.maxV - ov) / vs <= float(m.vClamp - 1) + tol;
        };
        float ou = f.minU, ov = f.minV;
        const Vec3f panFromBase = sub(m.pan, base);
        if (fits(m.pan[0], m.pan[1])) {
            ou = m.pan[0];
            ov = m.pan[1];
            ++atlas.stats.panOffset;
        } else if (fits(dot(panFromBase, f.ua), dot(panFromBase, f.va))) {
            ou = dot(panFromBase, f.ua);
            ov = dot(panFromBase, f.va);
            ++atlas.stats.panPoint;
        } else {
            ++atlas.stats.fromBounds;
        }
        Grid g;
        g.surf = int(s);
        g.w = m.uClamp;
        g.h = m.vClamp;
        g.uAxis = scale(f.ua, 1 / us);
        g.vAxis = scale(f.va, 1 / vs);
        g.uOffset = -(dot(base, f.ua) + ou) / us;
        g.vOffset = -(dot(base, f.va) + ov) / vs;
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

    // Light each grid's samples, then copy its edges into the border.
    for (size_t gi = 0; gi < grids.size(); ++gi) {
        const Grid& g = grids[gi];
        image::Image& img = atlas.pages[size_t(placed[gi][0])];
        const int x0 = placed[gi][1], y0 = placed[gi][2];
        auto put = [&](int x, int y, const Vec3f& light) {
            uint8_t* p = img.pixel(x, y);
            for (int k = 0; k < 3; ++k)
                p[k] = uint8_t(std::clamp(light[size_t(k)] / 2, 0.0f, 1.0f) * 255 + 0.5f);
            p[3] = 255;
        };
        if (g.surf < 0) {
            const Vec3f ambient = size_t(g.zone) < lights.zoneAmbient.size() ? lights.zoneAmbient[size_t(g.zone)] : Vec3f{};
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                    put(x0 + x, y0 + y, ambient);
            continue;
        }
        const BspSurf& surf = model.surfs[size_t(g.surf)];
        const LightMapIndex& m = model.lightMaps[size_t(surf.lightMap)];
        const Vec3f& n = model.vectors[size_t(surf.normal)];
        const Vec3f ambient =
            size_t(zones[size_t(g.surf)]) < lights.zoneAmbient.size() ? lights.zoneAmbient[size_t(zones[size_t(g.surf)])] : Vec3f{};
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
                    for (size_t li = size_t(std::max(m.lightActors, 0)); m.lightActors >= 0 && li < model.lights.size() &&
                                                                       model.lights[li];
                         ++li, ++k) {
                        const size_t at = size_t(m.dataOffset) + size_t(k) * size_t(rowBytes) * size_t(m.vClamp) +
                                          size_t(j) * size_t(rowBytes) + size_t(i / 8);
                        if (!((model.lightBits[at] >> (i & 7)) & 1))
                            continue;
                        const LightSource& src = lights.sources[li];
                        if (src.radius <= 0)
                            continue;
                        const Vec3f d = sub(src.position, p);
                        const float dist = length(d);
                        if (dist >= src.radius || dist <= 0)
                            continue;
                        float amount = 1 - dist / src.radius;
                        if (src.incidence) {
                            const float facing = dot(n, d) / dist;
                            if (facing <= 0)
                                continue;
                            amount *= facing;
                        }
                        if (src.spot) {
                            const float c = -dot(d, src.direction) / dist;
                            if (c < src.cosCone)
                                continue;
                            amount *= (c - src.cosCone) / std::max(1e-3f, 1 - src.cosCone);
                        }
                        light = add(light, scale(src.color, amount));
                        anyLit = true;
                    }
                }
                put(x0 + i, y0 + j, light);
            }
        if (anyLit)
            ++atlas.stats.lit;
        // The border repeats the edges, so filtering at them doesn't
        // reach a neighbour's texels.
        for (int j = -1; j <= g.h; ++j)
            for (int i = -1; i <= g.w; ++i) {
                if (i >= 0 && i < g.w && j >= 0 && j < g.h)
                    continue;
                const int si = std::clamp(i, 0, g.w - 1), sj = std::clamp(j, 0, g.h - 1);
                std::copy_n(img.pixel(x0 + si, y0 + sj), 4, img.pixel(x0 + i, y0 + j));
            }
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
