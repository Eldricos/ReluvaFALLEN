// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a wireframe of a .bsp's world surfaces
// (polygons, triangle soups and patch control grids) with an orbit camera,
// and a summary of its lumps and entities. A stand-in until the engine has
// a real renderer.

#include "engines/idtech3/entities.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace reluva::idtech3 {

namespace {

using Vec3 = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(const Vec3& v) {
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? Vec3{v[0] / len, v[1] / len, v[2] / len} : v;
}

constexpr float kNear = 4.0f;

} // namespace

MapViewer::MapViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            names_.push_back(n);
    }
}

void MapViewer::activate() {
    if (!names_.empty() && edges_.empty() && error_.empty())
        load(index_);
    dirty_ = true;
}

void MapViewer::load(int index) {
    index_ = index;
    error_.clear();
    info_.clear();
    edges_.clear();
    const std::string& name = names_[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");

    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(name, data)) {
        error_ = err.message;
        return;
    }
    if (Error err = parseBsp(data, bsp_)) {
        error_ = err.message;
        return;
    }
    std::vector<Entity> entities;
    Error entErr = parseEntities(bsp_.entities, entities);

    info_.push_back(name + "  (IBSP " + std::to_string(bsp_.version) + ")");
    info_.push_back(std::to_string(bsp_.shaders.size()) + " shaders, " + std::to_string(bsp_.surfaces.size()) +
                    " surfaces, " + std::to_string(bsp_.vertices.size()) + " vertices, " +
                    std::to_string(bsp_.models.size()) + " models");
    info_.push_back(std::to_string(bsp_.brushes.size()) + " brushes, " + std::to_string(bsp_.leafs.size()) +
                    " leafs, " + std::to_string(bsp_.visClusters) + " clusters, " +
                    std::to_string(bsp_.lightmapCount()) + " lightmaps, " + std::to_string(bsp_.fogs.size()) +
                    " fogs");
    if (const auto& f = bsp_.fixes; f.total())
        info_.push_back("Corrected, as the original does: " + std::to_string(f.lightmaps) + " lightmap, " +
                        std::to_string(f.fogs) + " fog, " + std::to_string(f.flares) + " flare/bad, " +
                        std::to_string(f.types) + " type");
    int missing = 0;
    for (const BspShader& s : bsp_.shaders)
        if (!ctx_.shaders.find(s.name) && !ctx_.files.has(s.name + ".tga") && !ctx_.files.has(s.name + ".jpg"))
            ++missing;
    info_.push_back(std::to_string(missing) + " shaders with neither a script nor an image");
    if (entErr) {
        info_.push_back("Entities: " + entErr.message);
    } else {
        if (!entities.empty() && entities[0].has("message"))
            info_.push_back("\"" + entities[0].get("message") + "\"");
        std::map<std::string, int> classes;
        for (const Entity& e : entities)
            ++classes[e.classname()];
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto& [c, n] : classes)
            sorted.emplace_back(-n, c);
        std::sort(sorted.begin(), sorted.end());
        std::string line = std::to_string(entities.size()) + " entities:";
        for (size_t i = 0; i < sorted.size() && i < 6; ++i)
            line += " " + sorted[i].second + " x" + std::to_string(-sorted[i].first);
        info_.push_back(line);
    }

    if (bsp_.models.empty()) {
        error_ = "The map has no world model";
        return;
    }
    const BspModel& world = bsp_.models[0];
    mins_ = world.mins;
    maxs_ = world.maxs;
    for (int si = world.firstSurface; si < world.firstSurface + world.numSurfaces; ++si) {
        const BspSurface& s = bsp_.surfaces[size_t(si)];
        const ShaderDef* def = ctx_.shaders.find(bsp_.shaders[size_t(s.shader)].name);
        if (def && (def->sky || def->hasSurfaceParm("nodraw")))
            continue;
        const BspVertex* v = &bsp_.vertices[size_t(s.firstVertex)];
        if (s.type == SurfaceType::Planar || s.type == SurfaceType::TriangleSoup) {
            Color c{90, 200, 110};
            if (s.type == SurfaceType::Planar) {
                // Floors and ceilings brighter than walls.
                const float shade = 0.45f + 0.55f * std::fabs(s.lightmapVecs[2][2]);
                c = {uint8_t(110 * shade), uint8_t(160 * shade), uint8_t(255 * shade)};
            }
            std::set<std::pair<int, int>> seen;
            const int32_t* idx = &bsp_.indices[size_t(s.firstIndex)];
            for (int t = 0; t + 2 < s.numIndices; t += 3)
                for (int k = 0; k < 3; ++k) {
                    int a = idx[t + k], b = idx[t + (k + 1) % 3];
                    if (a > b)
                        std::swap(a, b);
                    if (seen.insert({a, b}).second)
                        edges_.push_back({v[a].pos, v[b].pos, c, false});
                }
        } else if (s.type == SurfaceType::Patch) {
            const Color c{255, 170, 60};
            const int w = s.patchWidth, h = s.patchHeight;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    if (x + 1 < w)
                        edges_.push_back({v[y * w + x].pos, v[y * w + x + 1].pos, c, true});
                    if (y + 1 < h)
                        edges_.push_back({v[y * w + x].pos, v[(y + 1) * w + x].pos, c, true});
                }
        }
    }
    resetView();
}

void MapViewer::resetView() {
    for (int i = 0; i < 3; ++i)
        target_[size_t(i)] = (mins_[size_t(i)] + maxs_[size_t(i)]) / 2;
    const Vec3 size = sub(maxs_, mins_);
    distance_ = std::max(64.0f, std::sqrt(dot(size, size)) * 0.8f);
    yaw_ = kPi / 4;
    pitch_ = kPi / 5;
    dirty_ = true;
}

void MapViewer::handleEvent(const Event& ev) {
    if (names_.empty())
        return;
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
        dirty_ = true;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::max(16.0f, distance_ * std::pow(0.9f, float(ev.wheelY)));
        dirty_ = true;
    } else if (ev.type == EventType::KeyDown) {
        const bool shift = ev.mods & keymod::Shift;
        // Moves along the ground, relative to where the camera looks.
        const float step = distance_ * 0.05f;
        const Vec3 fwd{-std::cos(yaw_), -std::sin(yaw_), 0}, side{std::sin(yaw_), -std::cos(yaw_), 0};
        auto move = [&](const Vec3& dir, float amount) {
            for (int i = 0; i < 3; ++i)
                target_[size_t(i)] += dir[size_t(i)] * amount;
        };
        if (shift && ev.key == key::Left)
            yaw_ += 0.1f;
        else if (shift && ev.key == key::Right)
            yaw_ -= 0.1f;
        else if (ev.key == key::Left || ev.key == key::Right) {
            const int n = static_cast<int>(names_.size());
            load((index_ + (ev.key == key::Left ? n - 1 : 1)) % n);
        } else if (shift && ev.key == key::Up)
            pitch_ = std::min(1.5f, pitch_ + 0.1f);
        else if (shift && ev.key == key::Down)
            pitch_ = std::max(-1.5f, pitch_ - 0.1f);
        else if (ev.key == '+' || ev.key == '=')
            distance_ = std::max(16.0f, distance_ * 0.9f);
        else if (ev.key == '-')
            distance_ /= 0.9f;
        else if (ev.key == 'w')
            move(fwd, step);
        else if (ev.key == 's')
            move(fwd, -step);
        else if (ev.key == 'a')
            move(side, -step);
        else if (ev.key == 'd')
            move(side, step);
        else if (ev.key == 'q')
            move({0, 0, 1}, -step);
        else if (ev.key == 'e')
            move({0, 0, 1}, step);
        else if (ev.key == 'r')
            resetView();
        else if (ev.key == 'p')
            showPatches_ = !showPatches_;
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
        else
            return;
        dirty_ = true;
    }
}

void MapViewer::update(uint64_t) {
    if (!dirty_)
        return;
    dirty_ = false;
    render();
}

void MapViewer::render() {
    clear(8, 10, 16);
    const int lh = font::kLineHeight;
    if (!error_.empty()) {
        for (size_t i = 0; i < info_.size(); ++i)
            text(4, 4 + int(i) * lh, info_[i]);
        text(4, 4 + int(info_.size()) * lh, error_, {255, 120, 120});
        return;
    }

    Surface& s = ctx_.screen;
    // Z is up in id Tech 3 maps.
    const Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    for (int i = 0; i < 3; ++i)
        cam_.pos[size_t(i)] = target_[size_t(i)] + offset[size_t(i)] * distance_;
    cam_.forward = normalize(sub(target_, cam_.pos));
    cam_.right = normalize(cross(cam_.forward, {0, 0, 1}));
    cam_.up = cross(cam_.right, cam_.forward);
    cam_.cx = float(s.w) / 2;
    cam_.cy = float(s.h) / 2;
    cam_.focal = float(s.w) / 2; // 90 degree horizontal field of view

    for (const Edge& e : edges_)
        if (showPatches_ || !e.patch)
            drawLine3d(e.a, e.b, e.color);

    if (showInfo_) {
        for (size_t i = 0; i < info_.size(); ++i)
            text(4, 4 + int(i) * lh, info_[i]);
        text(4, s.h - lh - 2, "Blue: polygons  Green: triangle soups  Orange: patch control grids", {150, 150, 150});
    }
}

void MapViewer::drawLine3d(const Vec3& a, const Vec3& b, Color c) {
    const Vec3 ra = sub(a, cam_.pos), rb = sub(b, cam_.pos);
    Vec3 pa{dot(ra, cam_.right), dot(ra, cam_.up), dot(ra, cam_.forward)};
    Vec3 pb{dot(rb, cam_.right), dot(rb, cam_.up), dot(rb, cam_.forward)};
    // Clip against the near plane.
    if (pa[2] < kNear && pb[2] < kNear)
        return;
    if (pa[2] < kNear || pb[2] < kNear) {
        const float t = (kNear - pa[2]) / (pb[2] - pa[2]);
        const Vec3 m{pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t, kNear};
        (pa[2] < kNear ? pa : pb) = m;
    }
    drawLine2d(cam_.cx + pa[0] * cam_.focal / pa[2], cam_.cy - pa[1] * cam_.focal / pa[2],
               cam_.cx + pb[0] * cam_.focal / pb[2], cam_.cy - pb[1] * cam_.focal / pb[2], c);
}

void MapViewer::drawLine2d(float x0, float y0, float x1, float y1, Color c) {
    Surface& s = ctx_.screen;
    // Liang-Barsky clip to the screen, then step one pixel at a time.
    float t0 = 0, t1 = 1;
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0, float(s.w - 1) - x0, y0, float(s.h - 1) - y0};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return;
        } else {
            const float t = q[i] / p[i];
            if (p[i] < 0)
                t0 = std::max(t0, t);
            else
                t1 = std::min(t1, t);
        }
    }
    if (t0 > t1)
        return;
    const float sx = x0 + dx * t0, sy = y0 + dy * t0;
    const float ex = x0 + dx * t1, ey = y0 + dy * t1;
    const int steps = std::max(1, int(std::max(std::fabs(ex - sx), std::fabs(ey - sy))));
    for (int i = 0; i <= steps; ++i) {
        const int x = int(sx + (ex - sx) * float(i) / float(steps) + 0.5f);
        const int y = int(sy + (ey - sy) * float(i) / float(steps) + 0.5f);
        if (x < 0 || y < 0 || x >= s.w || y >= s.h)
            continue;
        uint8_t* px = s.row(y) + x * 4;
        px[0] = c.r;
        px[1] = c.g;
        px[2] = c.b;
        px[3] = 255;
    }
}

} // namespace reluva::idtech3
