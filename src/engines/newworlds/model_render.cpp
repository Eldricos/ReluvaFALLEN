// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/model_render.h"

#include "engines/newworlds/view3d.h"

#include <algorithm>
#include <cmath>

namespace reluva::newworlds {

bool loadTexturedModel(const Archives& archives, const std::vector<TextureFile>& textureFiles, const std::string& name,
                       TexturedModel& out) {
    out = TexturedModel{};
    bool ok = false;
    for (const auto& a : archives)
        if (const CatEntry* e = a->find(name)) {
            std::vector<uint8_t> data;
            ok = !a->read(*e, data) && !parseGeo(data, out.geo);
            break;
        }
    if (!ok)
        return false;
    // Its textures as the model viewer finds them (model_textures.h).
    size_t needed = 0;
    for (const auto& face : out.geo.faces)
        needed = std::max<size_t>(needed, face.corners.back().extra[4] + 1);
    std::vector<const TextureFile*> slots = findModelTextures(name, textureFiles, needed);
    addDoorTextures(out.geo, name, textureFiles, slots);
    addPadTextures(out.geo, name, textureFiles, slots);
    for (const TextureFile* t : slots) {
        Texture tex;
        std::vector<uint8_t> data;
        if (t) {
            CatArchive& cat = *archives[t->ref.archive];
            if (cat.read(cat.entries()[t->ref.entry], data) || decodeTexture(data, tex))
                tex = Texture{};
        }
        out.textures.push_back(std::move(tex));
    }
    return true;
}

ModelViewStyle briefingModelStyle(char side) {
    if (side == 'F')
        return {{170, 160, 255}, 0.6f};
    if (side == 'K')
        return {{90, 100, 190}, 1};
    if (side == 'R')
        return {{150, 140, 255}, 0.6f};
    return {};
}

bool drawModelView(Surface& target, Rasterizer& raster, const TexturedModel& m, float yaw, float pitch,
                   const ModelViewStyle& style) {
    Vec lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto& part : m.geo.parts)
        for (const auto& v : part.vertices) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
    if (lo.x > hi.x)
        return false;
    const Vec centre = (lo + hi) * 0.5f;
    const float radius = std::max(1.0f, std::sqrt(dot(hi - lo, hi - lo)) * 0.5f);
    const OrbitCamera cam(centre, yaw, pitch, radius * 3.0f, target.w, target.h);
    const Vec light = normalize({0.4f, -0.3f, 1.0f});
    raster.begin(target);
    for (const GeoModel::Face& face : m.geo.faces) {
        if (face.corners.size() < 3 || face.part >= m.geo.parts.size())
            continue;
        const auto& verts = m.geo.parts[face.part].vertices;
        std::vector<Vec> world;
        for (const GeoModel::Corner& c : face.corners)
            if (c.vertex < verts.size())
                world.push_back({verts[c.vertex].x, verts[c.vertex].y, verts[c.vertex].z});
        if (world.size() != face.corners.size())
            continue;
        const Vec n = normalize(cross(world[1] - world[0], world[2] - world[0]));
        const float lit = 0.45f + 0.55f * std::fabs(dot(n, light));
        std::vector<ViewVertex> poly;
        for (size_t i = 0; i < world.size(); ++i)
            poly.push_back(cam.toView(world[i], face.corners[i].u, face.corners[i].v));
        poly = clipNear(poly, cam.nearZ);
        if (poly.size() < 3)
            continue;
        std::vector<ScreenVertex> s;
        for (const ViewVertex& v : poly)
            s.push_back(cam.project(v));
        const uint32_t ti = face.corners.back().extra[4];
        const Texture* tex = ti < m.textures.size() && !m.textures[ti].rgba.empty() ? &m.textures[ti] : nullptr;
        auto shade = [&](float u, float v, uint8_t* out) {
            uint8_t c[3] = {150, 150, 150};
            if (tex) {
                const int tx = std::clamp(int(u * float(tex->width)), 0, tex->width - 1);
                const int ty = std::clamp(int(v * float(tex->height)), 0, tex->height - 1);
                const uint8_t* p = &tex->rgba[(size_t(ty) * size_t(tex->width) + size_t(tx)) * 4];
                if (p[3] == 0)
                    return false; // colour-keyed: see-through
                c[0] = p[0], c[1] = p[1], c[2] = p[2];
            }
            const uint8_t light[3] = {style.light.r, style.light.g, style.light.b};
            for (int k = 0; k < 3; ++k) {
                const float v = float(c[k]) * lit * float(light[k]) / 255.0f;
                out[k] = uint8_t(style.opacity >= 1 ? v : float(out[k]) * (1 - style.opacity) + v * style.opacity);
            }
            return style.opacity >= 1; // see-through faces don't hide those behind
        };
        for (size_t i = 1; i + 1 < s.size(); ++i)
            raster.drawTriangle(s[0], s[i], s[i + 1], shade);
    }
    return true;
}

} // namespace reluva::newworlds
