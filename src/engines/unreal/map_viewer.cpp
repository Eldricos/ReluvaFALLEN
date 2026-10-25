// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a level's BSP polygons drawn on the GPU,
// textured but not yet lit by the level's lightmaps (each face is shaded
// by which way it faces instead), with a fly camera starting at the
// level's PlayerStart.

#include "engines/unreal/viewers.h"

#include "engines/unreal/lighting.h"
#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

namespace reluva::unreal {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kSpeed = 400;    // units per second
constexpr float kEyeHeight = 27; // above a PlayerStart's location, roughly a player's eyes

enum Held { Forward, Back, Left, Right, Up, Down, Fast };

// Unreal's axes are left-handed (X forward, Y right, Z up); the renderer's
// are right-handed, so Y is negated on the way in.
gfx::Vec3 toWorld(const Vec3f& v) { return {v[0], -v[1], v[2]}; }

// The surface flags that change how a polygon is drawn, for grouping.
constexpr uint32_t kDrawFlags = polyflag::Masked | polyflag::Translucent | polyflag::Modulated |
                                polyflag::FakeBackdrop | polyflag::Unlit;

bool blended(uint32_t flags) { return flags & (polyflag::Translucent | polyflag::Modulated); }

} // namespace

MapViewer::MapViewer(ViewerContext& ctx) : Page(ctx), names_(ctx.packages.mapNames()) {}

MapViewer::~MapViewer() { deactivate(); }

void MapViewer::deactivate() {
    unload();
    renderer_ = nullptr;
}

void MapViewer::unload() {
    if (!renderer_)
        return;
    if (mesh_)
        renderer_->destroyMesh(mesh_);
    mesh_ = 0;
    for (gfx::TextureId t : textures_)
        renderer_->destroyTexture(t);
    textures_.clear();
    batches_.clear();
}

void MapViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Maps page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        text(4, 4 + 3 * font::kLineHeight, "Tab: the other pages still work.");
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    load(index_);
}

void MapViewer::load(int index) {
    unload();
    index_ = index;
    error_.clear();
    info_.clear();
    const std::string& name = names_[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");
    info_.push_back(name + ctx_.packages.extension(name) + "  " + std::to_string(index + 1) + "/" + std::to_string(names_.size()));

    start_ = {0, 0, 0};
    startYaw_ = 0;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const Package* pkg = ctx_.packages.get(name);
    Model model;
    std::string modelError;
    if (!pkg) {
        error_ = ctx_.packages.error(name);
    } else if (findLevelModel(*pkg, &model, &modelError) < 0) {
        error_ = modelError.empty() ? "No level geometry (a Model with BSP nodes) found" : modelError;
    }
    if (!error_.empty()) {
        toStart();
        return;
    }

    // The lightmaps, lit from the level's lights, as textures.
    const auto tModel = Clock::now();
    const LevelLights lights = gatherLights(ctx_.packages, *pkg, model);
    const auto tLights = Clock::now();
    const LightmapAtlas atlas = buildLightmaps(model, lights);
    const auto tLightmaps = Clock::now();
    std::vector<gfx::TextureId> pages;
    for (const image::Image& page : atlas.pages) {
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        gfx::TextureId id = 0;
        if (renderer_->createTexture(page.w, page.h, page.rgba.data(), params, id))
            id = 0;
        else
            textures_.push_back(id);
        pages.push_back(id);
    }
    hasLightmaps_ = !atlas.pages.empty();

    // Group the polygons by texture, drawing flags and lightmap page.
    const std::vector<Polygon> polys = levelPolygons(model);
    std::map<std::tuple<bool, int32_t, uint32_t, int>, std::vector<const Polygon*>> groups;
    for (const Polygon& p : polys) {
        const BspSurf& s = model.surfs[size_t(p.surf)];
        const uint32_t flags = s.polyFlags & kDrawFlags;
        const int page = blended(flags) || (flags & polyflag::FakeBackdrop) ? -1 : atlas.surfs[size_t(p.surf)].page;
        groups[{blended(flags), s.texture, flags, page}].push_back(&p);
    }

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::map<std::pair<const Package*, int>, std::pair<gfx::TextureId, std::array<int, 2>>> loaded;
    int missing = 0;
    std::string firstMissing;
    const gfx::Vec3 light = [] {
        const float l = std::sqrt(0.3f * 0.3f + 0.5f * 0.5f + 0.8f * 0.8f);
        return gfx::Vec3{0.3f / l, 0.5f / l, 0.8f / l};
    }();
    for (const auto& [key, group] : groups) {
        const auto& [isBlended, textureRef, flags, page] = key;
        Batch batch;
        batch.polyFlags = flags;
        batch.lightmap = page >= 0 ? pages[size_t(page)] : 0;
        batch.firstIndex = uint32_t(indices.size());
        std::array<int, 2> size{64, 64};
        const ObjectRef tex = ctx_.packages.resolve(*pkg, textureRef);
        if (tex) {
            auto it = loaded.find({tex.package, tex.exportIndex});
            if (it == loaded.end()) {
                image::Image img;
                gfx::TextureId id = 0;
                TextureInfo ti;
                if (decodeTexture(ctx_.packages, *tex.package, tex.exportIndex, img, &ti)) {
                    ++missing;
                    if (firstMissing.empty())
                        firstMissing = tex.package->path(tex.exportIndex + 1);
                } else {
                    gfx::TextureParams params;
                    params.nearest = (ti.polyFlags & polyflag::NoSmooth) != 0;
                    if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), params, id))
                        textures_.push_back(id);
                    else
                        id = 0;
                }
                it = loaded.emplace(std::make_pair(tex.package, tex.exportIndex),
                                    std::make_pair(id, std::array<int, 2>{std::max(1, img.w), std::max(1, img.h)}))
                         .first;
            }
            batch.texture = it->second.first;
            size = it->second.second;
        } else if (textureRef != 0) {
            ++missing;
            if (firstMissing.empty())
                firstMissing = pkg->path(textureRef);
        }
        for (const Polygon* p : group) {
            const auto base = uint32_t(vertices.size());
            const gfx::Vec3 n = toWorld(p->normal);
            const float facing = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
            const auto shade = uint8_t((flags & polyflag::Unlit) ? 255 : 150 + int(90 * facing));
            for (size_t k = 0; k < p->points.size(); ++k) {
                gfx::Vertex v{};
                const gfx::Vec3 w = toWorld(p->points[k]);
                std::copy(w.begin(), w.end(), v.pos);
                v.uv[0] = p->texels[k][0] / float(size[0]);
                v.uv[1] = p->texels[k][1] / float(size[1]);
                if (page >= 0) {
                    const auto lm = atlas.uv(p->surf, p->points[k]);
                    v.uv2[0] = lm[0];
                    v.uv2[1] = lm[1];
                }
                std::copy(n.begin(), n.end(), v.normal);
                v.color[0] = v.color[1] = v.color[2] = shade;
                v.color[3] = 255;
                vertices.push_back(v);
            }
            for (size_t k = 2; k < p->points.size(); ++k) {
                indices.push_back(base);
                indices.push_back(base + uint32_t(k) - 1);
                indices.push_back(base + uint32_t(k));
            }
        }
        batch.indexCount = uint32_t(indices.size()) - batch.firstIndex;
        if (batch.indexCount)
            batches_.push_back(batch);
    }
    if (!indices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;
    const auto tMesh = Clock::now();

    // Start at the first PlayerStart, or above the level's middle.
    for (int i = 0; i < 3; ++i)
        start_[size_t(i)] = (model.boundsMin[size_t(i)] + model.boundsMax[size_t(i)]) / 2;
    start_ = toWorld(start_);
    for (int i : pkg->exportsOfClass("PlayerStart")) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (pkg->readExport(i, data))
            continue;
        Reader r(data, size_t(pkg->exports()[size_t(i)].serialOffset));
        std::array<float, 3> loc;
        if (beginObject(*pkg, i, r, props) || !props.vector("Location", loc))
            continue;
        loc[2] += kEyeHeight;
        start_ = toWorld(loc);
        Rotator rot;
        if (props.rotator("Rotation", rot))
            startYaw_ = -float(rot.yaw) * 2 * kPi / 65536; // negated with Y
        break;
    }
    toStart();

    char line[200];
    std::snprintf(line, sizeof line, "version %d/%d: %zu nodes, %zu surfaces, %zu polygons, %zu triangles in %zu batches",
                  pkg->version(), pkg->licensee(), model.nodes.size(), model.surfs.size(), polys.size(),
                  indices.size() / 3, batches_.size());
    info_.push_back(line);
    std::snprintf(line, sizeof line, "%zu textures%s%s", textures_.size(),
                  missing ? ", missing or undecodable: " : "", missing ? std::to_string(missing).c_str() : "");
    info_.push_back(missing ? std::string(line) + " (first: " + firstMissing + ")" : std::string(line));
    if (!model.hasLighting()) {
        info_.push_back("No lightmaps: " + model.lightingError);
    } else {
        const LightmapAtlas::Stats& st = atlas.stats;
        std::snprintf(line, sizeof line,
                      "lightmaps: %d surfaces (%d lit) on %zu pages, %zu lights (%d unreadable), %d grids off fit",
                      st.lightmaps, st.lit, atlas.pages.size(), model.lights.size(), lights.unreadable, st.badFit);
        info_.push_back(line);
    }
    std::snprintf(line, sizeof line,
                  "loaded in %.0f ms: model %.0f, light actors %.0f, lightmaps %.0f, textures and mesh %.0f",
                  ms(t0, tMesh), ms(t0, tModel), ms(tModel, tLights), ms(tLights, tLightmaps), ms(tLightmaps, tMesh));
    info_.push_back(line);
    info_.push_back("Not drawn yet: movers, actors' meshes, the skybox, lights' effects (flicker, pulse...)");
}

void MapViewer::toStart() {
    pos_ = start_;
    yaw_ = startYaw_;
    pitch_ = 0;
}

void MapViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    auto hold = [&](uint32_t k, bool down) {
        switch (k) {
        case 'w': held_[Forward] = down; return true;
        case 's': held_[Back] = down; return true;
        case 'a': held_[Left] = down; return true;
        case 'd': held_[Right] = down; return true;
        case key::Space: case 'e': held_[Up] = down; return true;
        case 'c': case 'q': held_[Down] = down; return true;
        case key::LShift: case key::RShift: held_[Fast] = down; return true;
        default: return false;
        }
    };
    if (ev.type == EventType::KeyUp) {
        hold(ev.key, false);
    } else if (ev.type == EventType::KeyDown) {
        if (hold(ev.key, true) || ev.repeat)
            return;
        const int n = int(names_.size());
        if (ev.key == key::Left || ev.key == key::Right)
            load((index_ + (ev.key == key::Left ? n - 1 : 1)) % n);
        else if (ev.key == 'r')
            toStart();
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
        else if (ev.key == 't')
            textured_ = !textured_;
        else if (ev.key == 'l')
            lightmaps_ = !lightmaps_;
        else if (ev.key == '[')
            lightScale_ = std::max(0.1f, lightScale_ / 1.25f);
        else if (ev.key == ']')
            lightScale_ = std::min(8.0f, lightScale_ * 1.25f);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.005f;
        pitch_ = std::clamp(pitch_ - float(ev.mouseY - lastY_) * 0.005f, -1.55f, 1.55f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    }
}

void MapViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    auto lit = [&](const Batch& b) {
        return lightmaps_ && b.lightmap && !blended(b.polyFlags) &&
               !(b.polyFlags & (polyflag::FakeBackdrop | polyflag::Unlit));
    };
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;

    const gfx::Vec3 forward{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 right{std::sin(yaw_), -std::cos(yaw_), 0};
    const float step = kSpeed * (held_[Fast] ? 3.0f : 1.0f) * dt;
    for (size_t k = 0; k < 3; ++k) {
        pos_[k] += forward[k] * step * float(held_[Forward] - held_[Back]);
        pos_[k] += right[k] * step * float(held_[Right] - held_[Left]);
    }
    pos_[2] += step * float(held_[Up] - held_[Down]);

    // 90 degrees across, Unreal's default field of view.
    const float aspect = float(kFrameW) / float(kFrameH);
    const float fovY = 2 * std::atan(std::tan(kPi / 4) / aspect);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 65536), gfx::lookAlong(pos_, forward, {0, 0, 1}));
    renderer_->beginFrame(viewProj, pos_, {20, 24, 40});
    for (const Batch& b : batches_) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.state.cull = gfx::Cull::None;
        if (b.polyFlags & polyflag::FakeBackdrop) {
            // Where the sky shows through; the skybox isn't drawn yet.
            call.color = {0.15f, 0.2f, 0.35f, 1};
        } else {
            call.texture = textured_ ? b.texture : 0;
            // With a lightmap the second pass lights it; else shading by facing.
            call.rgbSource = lit(b) ? gfx::ColorSource::Constant : gfx::ColorSource::Vertex;
        }
        if (b.polyFlags & polyflag::Masked)
            call.state.alphaTest = gfx::AlphaTest::Ge128;
        if (b.polyFlags & polyflag::Translucent) {
            call.state.src = gfx::Blend::One;
            call.state.dst = gfx::Blend::OneMinusSrcColor;
            call.state.depthWrite = false;
        } else if (b.polyFlags & polyflag::Modulated) {
            call.state.src = gfx::Blend::DstColor;
            call.state.dst = gfx::Blend::SrcColor;
            call.state.depthWrite = false;
            call.rgbSource = gfx::ColorSource::Constant;
        }
        renderer_->draw(call);
        if (lit(b)) {
            // The lightmap over it, doubled (dst * src * 2): light up to
            // twice the texture's colours.
            gfx::DrawCall light = call;
            light.texture = b.lightmap;
            light.texGen = gfx::TexGen::Uv2;
            light.rgbSource = gfx::ColorSource::Constant;
            light.color = {lightScale_, lightScale_, lightScale_, 1};
            light.state.src = gfx::Blend::DstColor;
            light.state.dst = gfx::Blend::SrcColor;
            light.state.depthEqual = true;
            light.state.depthWrite = false;
            light.state.alphaTest = gfx::AlphaTest::None;
            renderer_->draw(light);
        }
    }
    drawOverlay(nowMs);
    renderer_->endFrame(&overlay_);
}

void MapViewer::drawOverlay(uint64_t nowMs) {
    ++frames_;
    if (nowMs - fpsStart_ >= 1000) {
        fps_ = float(frames_) * 1000.0f / float(nowMs - fpsStart_);
        fpsStart_ = nowMs;
        frames_ = 0;
    }
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (!showInfo_ && error_.empty())
        return;
    std::vector<std::string> lines = info_;
    const size_t errorLine = error_.empty() ? size_t(-1) : lines.size();
    if (!error_.empty())
        lines.push_back(error_);
    char where[160];
    // Shown in Unreal's own coordinates.
    std::snprintf(where, sizeof where, "at %.0f %.0f %.0f, %.0f fps; lightmaps %s, brightness x%.2f", pos_[0],
                  -pos_[1], pos_[2], fps_, !hasLightmaps_ ? "none" : lightmaps_ ? "on" : "off", lightScale_);
    lines.push_back(where);
    const int lh = font::kLineHeight;
    size_t widest = 0;
    for (const std::string& l : lines)
        widest = std::max(widest, font::length(l));
    const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
    for (int y = 0; y < boxH; ++y) {
        uint8_t* p = overlay_.row(y);
        for (int x = 0; x < boxW; ++x)
            p[x * 4 + 3] = 150;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
}

} // namespace reluva::unreal
