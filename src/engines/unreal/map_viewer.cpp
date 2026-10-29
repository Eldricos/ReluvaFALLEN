// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Maps page: a level's BSP polygons drawn on the GPU,
// textured and lit by its lightmaps, its movers (doors, lifts) from their
// own brushes and its sky seen from the sky zone, with a fly camera
// starting at the level's PlayerStart.

#include "engines/unreal/viewers.h"

#include "engines/unreal/actors.h"
#include "engines/unreal/class_defaults.h"
#include "engines/unreal/lighting.h"
#include "engines/unreal/procedural.h"
#include "engines/unreal/vm_level.h"
#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace reluva::unreal {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kSpeed = 400;    // units per second
constexpr float kEyeHeight = 27; // above a PlayerStart's location, roughly a player's eyes
// The frame's colours are multiplied by this before the display gamma; with
// a gamma of 1.3 it matches the original's screenshots (lighting.md).
constexpr float kExposure = 0.88f;

enum Held { Forward, Back, Left, Right, Up, Down, Fast, TurnLeft, TurnRight, LookUp, LookDown, Fire, HeldCount };

// Unreal's axes are left-handed (X forward, Y right, Z up); the renderer's
// are right-handed, so Y is negated on the way in.
gfx::Vec3 toWorld(const Vec3f& v) { return {v[0], -v[1], v[2]}; }

// The surface flags that change how a polygon is drawn, for grouping.
constexpr uint32_t kDrawFlags = polyflag::Masked | polyflag::Translucent | polyflag::Modulated |
                                polyflag::FakeBackdrop | polyflag::Unlit | polyflag::TwoSided;

bool blended(uint32_t flags) { return flags & (polyflag::Translucent | polyflag::Modulated); }

// Column-major 4x4 matrices for placing movers (gfx3d.h's conventions).
gfx::Mat4 translation(const Vec3f& t) {
    gfx::Mat4 m = gfx::identity();
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}
gfx::Mat4 scaling(const Vec3f& s) {
    gfx::Mat4 m = gfx::identity();
    m[0] = s[0];
    m[5] = s[1];
    m[10] = s[2];
    return m;
}
gfx::Mat4 rotation(const Rotator& r) {
    const std::array<Vec3f, 3> axes = rotationAxes(r);
    gfx::Mat4 m = gfx::identity();
    for (size_t c = 0; c < 3; ++c)
        for (size_t k = 0; k < 3; ++k)
            m[c * 4 + k] = axes[c][k];
    return m;
}

// The light at a point facing `n` from every light, without shadows (for
// movers, which the level's lightmaps don't cover), at most kMaxLight.
Vec3f lightAt(const LevelLights& lights, const Vec3f& p, const Vec3f& n) {
    Vec3f total{0.1f, 0.1f, 0.1f}; // a little, so unlit sides aren't black
    for (const LightSource& l : lights.unique) {
        const Vec3f d{l.position[0] - p[0], l.position[1] - p[1], l.position[2] - p[2]};
        const float straight = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float dist = lightDistance(l, p);
        if (dist >= l.radius || straight <= 0)
            continue;
        float amount = lightFalloff(dist / l.radius);
        if (l.incidence)
            amount *= std::max(0.0f, (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / straight);
        for (size_t k = 0; k < 3; ++k)
            total[k] += l.color[k] * amount;
    }
    for (float& c : total)
        c = std::min(c, kMaxLight);
    return total;
}

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
    if (moverMesh_)
        renderer_->destroyMesh(moverMesh_);
    mesh_ = moverMesh_ = 0;
    movers_.clear();
    if (spriteQuad_)
        renderer_->destroyMesh(spriteQuad_);
    spriteQuad_ = 0;
    sprites_.clear();
    for (const GpuMesh& g : gpuMeshes_)
        if (g.mesh)
            renderer_->destroyMesh(g.mesh);
    gpuMeshes_.clear();
    meshes_.clear();
    posedTopology_.clear();
    liveMeshes_.clear(); // indexes into meshes_
    meshActors_.clear();
    jump_ = -1;
    sky_.reset();
    for (gfx::TextureId t : textures_)
        renderer_->destroyTexture(t);
    textures_.clear();
    batches_.clear();
    tints_.clear();
    surfRanges_.clear();
    swappedBatches_.clear();
    swappedSerial_ = -1;
    animatedLightmaps_.clear();
    animatedTextures_.clear();
    changingLights_.clear();
    lightmapPages_.clear();
    textureCache_.clear();
    seeThroughTextures_.clear();
    hudFlis_.clear();
    lights_ = {};
    stopPlay();
    posers_.clear();
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
    zones_ = {};
    zonesSeen_ = ~uint64_t(0);
    if (!pkg) {
        error_ = ctx_.packages.error(name);
    } else if (findLevelModel(*pkg, &model, &modelError) < 0) {
        error_ = modelError.empty() ? "No level geometry (a Model with BSP nodes) found" : modelError;
    }
    if (!error_.empty()) {
        toStart();
        return;
    }

    // The level's scripts, started (and a second of play): what they
    // change by the time play begins (lights turned off, editor icons
    // hidden, actors destroyed). P turns this off.
    const auto tModel = Clock::now();
    LevelStart start;
    std::string startError;
    bool started = false;
    if (runScripts_) {
        if (Error err = runLevelStart(ctx_.packages, *pkg, defaultGameType(ctx_.packages), 1.0f, start))
            startError = err.message;
        else
            started = true;
    }
    const ScriptedActors* scripted = started ? &start.changed : nullptr;

    // The lightmaps, lit from the level's lights, as textures.
    lights_ = gatherLights(ctx_.packages, *pkg, model, scripted);
    staticLightRefs_.clear();
    for (const int32_t ref : model.lights)
        if (ref)
            staticLightRefs_.insert(ref);
    const LevelLights& lights = lights_;
    const auto tLights = Clock::now();
    LightmapAtlas atlas = buildLightmaps(model, lights);
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
    lightmapPages_ = pages;
    changingLights_ = lights.animated;
    lightLevels_.clear();
    mapLevels_.clear();
    for (const LightSource& l : changingLights_) {
        lightLevels_.push_back(l.startsOff ? 0.0f : lightLevel(l, 0));
        mapLevels_.push_back(l.startsOff ? 0.0f : lightLevel(l, l.type, 0, false));
    }
    animatedLightmaps_ = std::move(atlas.animated);
    if (std::getenv("LIGHTDBG")) {
        std::map<int, std::array<float, 3>> stat; // index -> texels lit, max, sum
        for (const AnimatedLightmap& m : animatedLightmaps_)
            for (const AnimatedLightmap::Light& l : m.lights)
                for (const Vec3f& c : l.share) {
                    const float v = std::max({c[0], c[1], c[2]});
                    if (v <= 0) continue;
                    auto& st = stat[l.index];
                    st[0] += 1; st[1] = std::max(st[1], v); st[2] += v;
                }
        for (const auto& [i, st] : stat)
            std::fprintf(stderr, "ANIM %d %s texels %.0f max %.2f mean %.3f level %.2f\n", i, pkg->path(changingLights_[size_t(i)].ref).c_str(), st[0], st[1], st[2] / std::max(1.0f, st[0]), lightLevels_[size_t(i)]);
    }
    staleLightmaps_.assign(animatedLightmaps_.size(), false);
    lightTime_ = 0;

    // Group the polygons by texture, drawing flags and lightmap page.
    const std::vector<Polygon> polys = levelPolygons(model);
    // And by the zones they're seen from, to leave out those not seen.
    zones_.build(model);
    std::map<std::tuple<bool, int32_t, uint32_t, int, uint64_t>, std::vector<const Polygon*>> groups;
    for (const Polygon& p : polys) {
        const BspSurf& s = model.surfs[size_t(p.surf)];
        const uint32_t flags = s.polyFlags & kDrawFlags;
        const int page = blended(flags) || (flags & polyflag::FakeBackdrop) ? -1 : atlas.surfs[size_t(p.surf)].page;
        groups[{blended(flags), s.texture, flags, page, zones_.portalCount() ? p.zones : ~uint64_t(0)}].push_back(&p);
    }

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    // Each texture uploaded once; its size divides texture coordinates.
    int missing = 0;
    std::string firstMissing;
    auto textureOf = [&](const ObjectRef& tex) {
        bool unreadable = false;
        const auto t = this->textureOf(tex, &unreadable);
        if (unreadable) {
            ++missing;
            if (firstMissing.empty())
                firstMissing = tex.package->path(tex.exportIndex + 1);
        }
        return t;
    };
    auto textureFor = [&](const Package& from, int32_t ref) -> std::pair<gfx::TextureId, std::array<int, 2>> {
        const ObjectRef tex = ctx_.packages.resolve(from, ref);
        if (!tex && ref != 0) {
            ++missing;
            if (firstMissing.empty())
                firstMissing = from.path(ref);
        }
        return textureOf(tex);
    };
    const gfx::Vec3 light = [] {
        const float l = std::sqrt(0.3f * 0.3f + 0.5f * 0.5f + 0.8f * 0.8f);
        return gfx::Vec3{0.3f / l, 0.5f / l, 0.8f / l};
    }();
    for (const auto& [key, group] : groups) {
        const auto& [isBlended, textureRef, flags, page, zones] = key;
        Batch batch;
        batch.polyFlags = flags;
        batch.zones = zones;
        batch.lightmap = page >= 0 ? pages[size_t(page)] : 0;
        batch.firstIndex = uint32_t(indices.size());
        const auto [textureId, size] = textureFor(*pkg, textureRef);
        batch.texture = textureId;
        batch.textureRef = ctx_.packages.resolve(*pkg, textureRef);
        for (const Polygon* p : group) {
            const auto base = uint32_t(vertices.size());
            if (p->points.size() >= 3)
                surfRanges_[p->surf].push_back(
                    {batches_.size(), uint32_t(indices.size()), uint32_t(p->points.size() - 2) * 3});
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
        // A translucent surface the lights reach: its tint's samples.
        if ((flags & polyflag::Translucent) && !(flags & polyflag::Unlit) && hasLightmaps_) {
            std::vector<TintSample> samples;
            for (const Polygon* p : group) {
                const LightmapPlacement& pl = atlas.surfs[size_t(p->surf)];
                if (pl.page < 0 || size_t(pl.page) >= atlas.pages.size() || p->points.empty())
                    continue;
                std::vector<Vec3f> at = p->points;
                Vec3f mid{};
                for (const Vec3f& q : p->points)
                    for (size_t k = 0; k < 3; ++k)
                        mid[k] += q[k] / float(p->points.size());
                at.push_back(mid);
                const image::Image& img = atlas.pages[size_t(pl.page)];
                for (const Vec3f& q : at) {
                    const auto uv = atlas.uv(p->surf, q);
                    const int x = std::clamp(int(uv[0] * float(atlas.pageSize)), 0, img.w - 1);
                    const int y = std::clamp(int(uv[1] * float(atlas.pageSize)), 0, img.h - 1);
                    TintSample t;
                    t.at = q;
                    for (size_t a = 0; a < animatedLightmaps_.size() && t.anim < 0; ++a) {
                        const AnimatedLightmap& m = animatedLightmaps_[a];
                        if (m.page == pl.page && x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
                            t.anim = int(a);
                            t.texel = size_t(y - m.y) * size_t(m.w) + size_t(x - m.x);
                        }
                    }
                    const uint8_t* px = img.pixel(x, y);
                    t.light = {float(px[0]) / 255 * 2, float(px[1]) / 255 * 2, float(px[2]) / 255 * 2};
                    samples.push_back(t);
                }
            }
            if (!samples.empty()) {
                batch.tint = int(tints_.size());
                tints_.push_back(std::move(samples));
            }
        }
        batch.indexCount = uint32_t(indices.size()) - batch.firstIndex;
        if (batch.indexCount)
            batches_.push_back(batch);
    }
    if (!indices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;

    // The movers: each brush's polygons in its own coordinates, grouped by
    // texture and flags, lit (without shadows) where it starts.
    std::vector<std::string> moverErrors;
    std::vector<Mover> movers = findMovers(ctx_.packages, *pkg, &moverErrors);
    vertices.clear();
    indices.clear();
    for (Mover& m : movers) {
        MoverDraw d;
        d.key = d.target = m.keyNum;
        d.mover = std::move(m);
        const gfx::Mat4 place = moverTransform(d);
        std::map<std::pair<int32_t, uint32_t>, std::vector<const Poly*>> moverGroups;
        for (const Poly& p : d.mover.polys)
            if (p.vertices.size() >= 3 && !(p.polyFlags & (polyflag::Invisible | polyflag::Portal)))
                moverGroups[{p.texture, p.polyFlags & kDrawFlags}].push_back(&p);
        for (const auto& [key, group] : moverGroups) {
            Batch batch;
            batch.polyFlags = key.second;
            batch.firstIndex = uint32_t(indices.size());
            const auto [textureId, size] = textureFor(*pkg, key.first);
            batch.texture = textureId;
            for (const Poly* p : group) {
                const auto base = uint32_t(vertices.size());
                // The normal as the mover is placed, in Unreal's axes: where
                // its tip goes less where the origin goes, made unit length.
                const gfx::Vec3 origin = gfx::transformPoint(place, Vec3f{});
                const gfx::Vec3 tip = gfx::transformPoint(place, p->normal);
                Vec3f normal{tip[0] - origin[0], -(tip[1] - origin[1]), tip[2] - origin[2]};
                const float len = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                for (float& c : normal)
                    c = len > 0 ? c / len : 0;
                for (const Vec3f& v : p->vertices) {
                    gfx::Vertex vx{};
                    std::copy(v.begin(), v.end(), vx.pos);
                    const Vec3f d2{v[0] - p->base[0], v[1] - p->base[1], v[2] - p->base[2]};
                    vx.uv[0] = (d2[0] * p->textureU[0] + d2[1] * p->textureU[1] + d2[2] * p->textureU[2] + float(p->panU)) /
                               float(size[0]);
                    vx.uv[1] = (d2[0] * p->textureV[0] + d2[1] * p->textureV[1] + d2[2] * p->textureV[2] + float(p->panV)) /
                               float(size[1]);
                    std::copy(p->normal.begin(), p->normal.end(), vx.normal);
                    const gfx::Vec3 w = gfx::transformPoint(place, v);
                    const Vec3f light = (key.second & polyflag::Unlit)
                                            ? Vec3f{1, 1, 1}
                                            : lightAt(lights, Vec3f{w[0], -w[1], w[2]}, normal);
                    for (size_t k = 0; k < 3; ++k)
                        vx.color[k] = uint8_t(light[k] * 255 + 0.5f);
                    vx.color[3] = 255;
                    vertices.push_back(vx);
                }
                for (size_t k = 2; k < p->vertices.size(); ++k) {
                    indices.push_back(base);
                    indices.push_back(base + uint32_t(k) - 1);
                    indices.push_back(base + uint32_t(k));
                }
            }
            batch.indexCount = uint32_t(indices.size()) - batch.firstIndex;
            if (batch.indexCount)
                d.batches.push_back(batch);
        }
        movers_.push_back(std::move(d));
    }
    if (!indices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, moverMesh_))
            error_ = err.message;
    sky_ = findSkyZone(ctx_.packages, *pkg);

    // Actors drawn as meshes: each mesh read and uploaded once; its skins
    // by texture index (MultiSkins, else Skin, else the mesh's own); lit
    // by the level's lights where it stands.
    ClassDefaults defaults(ctx_.packages);
    MeshActorStats meshStats;
    std::map<std::pair<const Package*, int>, int> meshIndex; // -1: unreadable
    int badMeshes = 0, borrowed = 0;
    std::string firstBadMesh;
    const std::vector<MeshActor> meshActors = findMeshActors(ctx_.packages, *pkg, defaults, &meshStats, scripted);
    // The animations each mesh's actors play, and the idles that may stand
    // in for a one-frame pose (below).
    std::map<std::pair<const Package*, int>, std::set<std::string>> wanted;
    for (const MeshActor& a : meshActors) {
        auto& w = wanted[{a.mesh.package, a.mesh.exportIndex}];
        if (!a.animSequence.empty())
            w.insert(a.animSequence);
        w.insert(idleFor(a.animSequence));
    }
    for (const MeshActor& a : meshActors) {
        auto it = meshIndex.find({a.mesh.package, a.mesh.exportIndex});
        if (it == meshIndex.end()) {
            Mesh mesh;
            GpuMesh gpu;
            Error err = readMesh(*a.mesh.package, a.mesh.exportIndex, mesh);
            // Characters' animations past their own frames, from the shared
            // animation sets (read the first time one's needed).
            if (!err && mesh.bones)
                for (const std::string& seq : wanted[{a.mesh.package, a.mesh.exportIndex}]) {
                    if (mesh.findAnim(seq))
                        continue;
                    if (!ctx_.animationSets) {
                        const auto t0 = Clock::now();
                        ctx_.sets();
                        animationSetsMs_ = int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
                    }
                    borrowed += borrowNamedAnimation(*a.mesh.package, a.mesh.exportIndex, ctx_.sets(), seq, mesh);
                }
            if (!err)
                err = uploadMesh(*renderer_, mesh, gpu, ctx_.meshTurn, ctx_.meshYawQuarters);
            int at = -1;
            if (err) {
                ++badMeshes;
                if (firstBadMesh.empty())
                    firstBadMesh = err.message;
            } else {
                at = int(meshes_.size());
                meshes_.push_back(std::move(mesh));
                gpuMeshes_.push_back(gpu);
            }
            it = meshIndex.emplace(std::make_pair(a.mesh.package, a.mesh.exportIndex), at).first;
        }
        if (it->second < 0)
            continue;
        const Mesh& mesh = meshes_[size_t(it->second)];
        MeshDraw d;
        d.mesh = size_t(it->second);
        d.label = a.name + " (" + a.mesh.package->path(a.mesh.exportIndex + 1) +
                  (mesh.bones ? ", " + std::to_string(mesh.bones) + " bones" : std::string()) + ")";
        d.at = toWorld(a.location);
        d.drawScale = a.drawScale;
        d.skeletal = mesh.bones > 0;
        d.placement = meshPlacement(a.location, a.rotation, a.drawScale, a.prePivot);
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < a.multiSkins.size() ? a.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = a.skin;
            if (!tex && k < mesh.textures.size())
                tex = ctx_.packages.resolve(*a.mesh.package, mesh.textures[k]);
            d.textures.push_back(textureOf(tex).first);
        }
        d.light = meshLightAt(a.location);
        const MeshAnimSeq* anim = mesh.findAnim(a.animSequence);
        // Characters are placed in a one-frame pose (BaseRelax, BaseReady),
        // which the game's scripts follow with an idle loop: its idle
        // (Idle1, or IdleReady1 for a ready pose), which every humanoid's
        // list has, so they move when animations play.
        if (mesh.bones && (!anim || anim->numFrames <= 1))
            if (const MeshAnimSeq* idle = mesh.findAnim(idleFor(a.animSequence)))
                anim = idle;
        if (anim) {
            d.anim = int(anim - mesh.anims.data());
        } else if (mesh.bones) {
            // A character naming an animation its mesh doesn't have (the
            // Grigari drones' "BaseReady"): its idle, as the game's scripts
            // would play something.
            for (size_t k = 0; k < mesh.anims.size() && d.anim < 0; ++k)
                if (fs::toLower(mesh.anims[k].name).find("idle") != std::string::npos && mesh.anims[k].numFrames > 1)
                    d.anim = int(k);
        }
        d.startFraction = a.animFrame;
        meshActors_.push_back(std::move(d));
    }

    // Actors drawn as sprites, on one square turned to face the camera.
    SpriteActorStats spriteStats;
    for (const SpriteActor& a : findSpriteActors(ctx_.packages, *pkg, defaults, &spriteStats, scripted)) {
        const auto [texture, size] = textureOf(a.texture);
        if (!texture)
            continue;
        SpriteDraw d;
        d.texture = texture;
        d.at = toWorld(Vec3f{a.location[0] - a.prePivot[0], a.location[1] - a.prePivot[1], a.location[2] - a.prePivot[2]});
        d.width = float(size[0]) * a.drawScale;
        d.height = float(size[1]) * a.drawScale;
        d.style = a.style;
        d.vertical = a.vertical;
        // Lit by the level's lights where it stands, as meshes are, unless
        // unlit or blended (glowing effects).
        Vec3f light{1, 1, 1};
        if (!a.unlit && a.style != kStyleTranslucent && a.style != kStyleModulated) {
            light = {0.15f, 0.15f, 0.15f};
            for (const LightSource& l : lights.unique) {
                const float dist = lightDistance(l, a.location);
                if (dist < l.radius)
                    for (size_t k = 0; k < 3; ++k)
                        light[k] += l.color[k] * lightFalloff(dist / l.radius);
            }
            for (float& c : light)
                c = std::min(c, kMaxLight);
        }
        for (size_t k = 0; k < 3; ++k)
            d.color[k] = light[k] * a.scaleGlow * kExposure;
        sprites_.push_back(d);
    }
    {
        // A square of side 1 across X (right) and Y (up), its texture
        // upright: the sprites', and the HUD's tiles when playing.
        std::vector<gfx::Vertex> quad(4);
        const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        for (size_t k = 0; k < 4; ++k) {
            quad[k] = {{corners[k][0], corners[k][1], 0}, {corners[k][0] + 0.5f, 0.5f - corners[k][1]}, {0, 0},
                       {0, 0, 1}, {255, 255, 255, 255}};
        }
        if (Error err = renderer_->createMesh(quad, {0, 1, 2, 0, 2, 3}, spriteQuad_))
            sprites_.clear();
    }
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

    char line[320];
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
                      "lightmaps: %d surfaces (%d lit) on %zu pages, %zu lights (%d unreadable, %zu pulsing or "
                      "flickering over %zu lightmaps), %d grids off fit",
                      st.lightmaps, st.lit, atlas.pages.size(), lights.unique.size(), lights.unreadable,
                      changingLights_.size(), animatedLightmaps_.size(), st.badFit);
        info_.push_back(line);
    }
    std::snprintf(line, sizeof line,
                  "loaded in %.0f ms: model %.0f, light actors %.0f, lightmaps %.0f, textures and mesh %.0f",
                  ms(t0, tMesh), ms(t0, tModel), ms(tModel, tLights), ms(tLights, tLightmaps), ms(tLightmaps, tMesh));
    info_.push_back(line);
    std::snprintf(line, sizeof line, "%zu movers%s; sky zone: %s", movers_.size(),
                  moverErrors.empty() ? "" : (", " + std::to_string(moverErrors.size()) + " unreadable").c_str(),
                  sky_ ? "yes" : "none");
    info_.push_back(moverErrors.empty() ? std::string(line) : std::string(line) + " (first: " + moverErrors[0] + ")");
    std::snprintf(line, sizeof line,
                  "%zu actors drawn as %zu meshes (%d with the mesh from class defaults, %d hidden, %d meshes "
                  "unreadable); class defaults found %d, not %d",
                  meshActors_.size(), meshes_.size(), meshStats.fromDefaults, meshStats.hidden, badMeshes,
                  defaults.found(), defaults.missing());
    info_.push_back(line);
    const auto turned = std::count_if(meshes_.begin(), meshes_.end(), [](const Mesh& m) { return bool(m.importAxes); });
    if (turned)
        info_.push_back(std::to_string(turned) + " imported meshes turned from their packed vertices: drawn " +
                        meshTurnName(ctx_.meshTurn) + " (M changes), then " +
                        std::to_string(ctx_.meshYawQuarters * 90) + " degrees of yaw (Y changes)");
    if (borrowed)
        info_.push_back(std::to_string(borrowed) + " animations given to characters from " +
                        std::to_string(ctx_.animationSets ? ctx_.animationSets->size() : 0) +
                        " shared animation sets (read in " + std::to_string(animationSetsMs_) + " ms)");
    if (!firstBadMesh.empty())
        info_.push_back("First unreadable mesh: " + firstBadMesh);
    std::snprintf(line, sizeof line, "%zu sprites (%d editor icons and %d hidden left out); %zu animated textures",
                  sprites_.size(), spriteStats.editorOnly, spriteStats.hidden, animatedTextures_.size());
    info_.push_back(line);
    if (started) {
        std::snprintf(line, sizeof line,
                      "scripts started in %.0f ms (P turns them off), player %s: %d lights changed, %d actors hidden, %d shown, "
                      "%d drawn differently, %d destroyed; %zu kinds of script error, %zu natives not written, None "
                      "accessed %d times",
                      start.milliseconds, start.player.empty() ? ("none, " + start.loginError).c_str() : start.player.c_str(),
                      start.lights, start.hidden, start.shown, start.drawTypes, start.removed, start.errors,
                      start.missingNatives, start.accessedNone);
        info_.push_back(line);
    } else {
        info_.push_back(runScripts_ ? "scripts didn't start: " + startError : "scripts off (P turns them on)");
    }
    info_.push_back("Not drawn yet: light effects' patterns across surfaces (a torch's waver, water's shimmer: as brightness only)");
}

bool MapViewer::watchActor(const std::string& name) {
    if (!play_)
        return false;
    for (VmObject* a : play_->level().actors()) {
        if (a->deleted || fs::toLower(play_->vm().nameText(a->name)) != fs::toLower(name))
            continue;
        LevelPlay::Look look;
        if (!play_->look(a, look))
            return false;
        // In front of it, a little above, looking back at it.
        const Value* radius = a->find(play_->vm(), "CollisionRadius");
        const float back = std::max(60.0f, (radius ? radius->asFloat() : 20) * 4);
        const Vec3f ahead = rotationAxes(look.rotation)[0];
        const Vec3f eye{look.location[0] + ahead[0] * back, look.location[1] + ahead[1] * back,
                        look.location[2] + back * 0.3f};
        pos_ = toWorld(eye);
        const gfx::Vec3 d{look.location[0] - eye[0], -(look.location[1] - eye[1]), look.location[2] - eye[2]};
        yaw_ = std::atan2(d[1], d[0]);
        pitch_ = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
        freeCam_ = true;
        return true;
    }
    return false;
}

bool MapViewer::teleportPlayer(const std::string& name, const Vec3f& offset, float yaw) {
    if (!play_ || !play_->player())
        return false;
    Vm& vm = play_->vm();
    for (VmObject* a : play_->level().actors()) {
        if (a->deleted || fs::toLower(vm.nameText(a->name)) != fs::toLower(name))
            continue;
        const Value* at = a->find(vm, "Location");
        if (!at)
            return false;
        const auto l = Vm::vectorOf(*at);
        VmObject* p = play_->player();
        play_->level().setLocation(p, {l[0] + offset[0], l[1] + offset[1], l[2] + offset[2]});
        const int32_t units = int32_t(std::lround(yaw * 65536 / 360)) & 0xffff;
        for (const char* rot : {"Rotation", "ViewRotation"})
            if (Value* r = p->find(vm, rot))
                *r = vm.rotatorValue(0, units, 0);
        if (Value* v = p->find(vm, "Velocity"))
            *v = vm.vectorValue(0, 0, 0);
        if (Value* ph = p->find(vm, "Physics"))
            ph->i = 2; // falling, onto whatever's under it
        return true;
    }
    return false;
}

std::string MapViewer::consoleCommand(const std::string& command) {
    if (!play_ || !play_->player())
        return "no player";
    const Value v = play_->vm().callEvent(play_->player(), "ConsoleCommand", {Value::stringValue(command)});
    return v.s;
}

std::string MapViewer::describeActor(const std::string& name) {
    if (!play_)
        return "not playing";
    Vm& vm = play_->vm();
    for (VmObject* a : play_->level().actors()) {
        if (a->deleted || fs::toLower(vm.nameText(a->name)) != fs::toLower(name))
            continue;
        auto num = [&](const char* p) {
            const Value* v = a->find(vm, p);
            return v ? v->asFloat() : 0.0f;
        };
        auto flag = [&](const char* p) {
            const Value* v = a->find(vm, p);
            return v && v->asBool();
        };
        const Value* at = a->find(vm, "Location");
        const auto l = at ? Vm::vectorOf(*at) : std::array<float, 3>{};
        auto object = [&](const char* p) {
            const Value* v = a->find(vm, p);
            return v && v->asObject() ? vm.nameText(v->o->name) : std::string("None");
        };
        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "%s at %.1f %.1f %.1f, state %s, physics %d, radius %.0f height %.0f%s%s%s; draw type %d, "
                      "style %d, scale %.2f, glow %.2f, mesh %s, skin %s, texture %s, %zu copies",
                      a->cls->name.c_str(), double(l[0]), double(l[1]), double(l[2]),
                      a->state ? a->state->name.c_str() : "none", int(num("Physics")), double(num("CollisionRadius")),
                      double(num("CollisionHeight")), flag("bCollideActors") ? ", collides with actors" : "",
                      flag("bHidden") ? ", hidden" : "", a->disabled.empty() ? "" : ", some events disabled",
                      int(num("DrawType")), int(num("Style")), double(num("DrawScale")), double(num("ScaleGlow")),
                      object("Mesh").c_str(), object("Skin").c_str(), object("Texture").c_str(),
                      play_->level().multiMeshes(a).size());
        std::string out = buf;
        if (num("LightType") != 0 || num("LightBrightness") != 0) {
            char light[160];
            std::snprintf(light, sizeof light, "; light type %d, brightness %d, hue %d, saturation %d, radius %d, effect %d",
                          int(num("LightType")), int(num("LightBrightness")), int(num("LightHue")),
                          int(num("LightSaturation")), int(num("LightRadius")), int(num("LightEffect")));
            out += light;
        }
        return out;
    }
    return "no such actor";
}

std::string MapViewer::describeView() {
    if (!play_)
        return "not playing";
    char buf[200];
    auto deg = [](int32_t r) { return double(int16_t(r & 0xffff)) * 360 / 65536; };
    std::snprintf(buf, sizeof buf, "at %.1f %.1f %.1f, pitch %.1f yaw %.1f roll %.1f, fov %.1f", double(view_.location[0]),
                  double(view_.location[1]), double(view_.location[2]), deg(view_.rotation.pitch), deg(view_.rotation.yaw),
                  deg(view_.rotation.roll), double(view_.fov));
    std::string out = buf;
    const std::array<float, 3> at{view_.location[0], view_.location[1], view_.location[2]};
    if (play_->level().inSolid(at))
        out += ", in solid";
    return out;
}

std::string MapViewer::actorProperty(const std::string& name, const std::string& property) {
    if (!play_)
        return "not playing";
    Vm& vm = play_->vm();
    for (VmObject* a : play_->level().actors())
        if (!a->deleted && fs::toLower(vm.nameText(a->name)) == fs::toLower(name)) {
            const Value* v = a->find(vm, property);
            if (!v)
                return "no such property";
            if (v->type != VType::Struct || v->fields.empty())
                return vm.toString(*v);
            std::string out = "(";
            for (const Value& f : v->fields)
                out += (out.size() > 1 ? ", " : "") + vm.toString(f);
            return out + ")";
        }
    return "no such actor";
}

std::vector<std::string> MapViewer::linesSaid() {
    std::vector<std::string> out;
    if (!play_)
        return out;
    ScriptLevel& level = play_->level();
    for (const int i : level.linesSaid()) {
        const DialogLine& l = level.dialogLines()[size_t(i)];
        out.push_back(l.key() + ": " + l.text + (l.event.empty() ? "" : " [" + l.event + "]"));
    }
    return out;
}

std::string MapViewer::describePlayer() {
    if (!play_ || !play_->player())
        return "no player";
    Vm& vm = play_->vm();
    VmObject* p = play_->player();
    auto vec = [&](const char* name) {
        const Value* v = p->find(vm, name);
        if (!v)
            return std::string("?");
        const auto a = Vm::vectorOf(*v);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%.1f %.1f %.1f", double(a[0]), double(a[1]), double(a[2]));
        return std::string(buf);
    };
    const Value* physics = p->find(vm, "Physics");
    const Value* seq = p->find(vm, "AnimSequence");
    const Value* frame = p->find(vm, "AnimFrame");
    char buf[64];
    std::snprintf(buf, sizeof buf, " %.2f", double(frame ? frame->asFloat() : 0));
    const Value* rot = p->find(vm, "Rotation");
    const int yaw = rot ? int(std::lround(double(Vm::rotatorOf(*rot)[1] & 0xffff) * 360 / 65536)) : 0;
    std::string weapon = "none";
    if (const Value* w = p->find(vm, "Weapon"); w && w->asObject()) {
        weapon = vm.nameText(w->o->name);
        if (const Value* ammo = w->o->find(vm, "AmmoType"); ammo && ammo->asObject())
            if (const Value* n = ammo->o->find(vm, "AmmoAmount"))
                weapon += " (" + std::to_string(n->asInt()) + ")";
    }
    return "weapon " + weapon + ", at " + vec("Location") + ", yaw " + std::to_string(yaw) + ", velocity " + vec("Velocity") + ", state " + play_->playerState() +
           ", physics " + std::to_string(physics ? physics->asInt() : -1) + ", anim " +
           (seq ? vm.nameText(seq->i) : std::string("?")) + buf;
}

bool MapViewer::selectMap(const std::string& name) {
    for (size_t i = 0; i < names_.size(); ++i)
        if (fs::toLower(names_[i]) == fs::toLower(name)) {
            if (renderer_)
                load(int(i));
            else
                index_ = int(i);
            return true;
        }
    return false;
}

void MapViewer::toStart() {
    pos_ = start_;
    yaw_ = startYaw_;
    pitch_ = 0;
}

namespace {

// A key as the engine names it to the console (EInputKey): 13 Enter, 27
// Escape, 37 to 40 the arrows, letters and digits as their capitals' ASCII,
// 0x70 F1...; 0 for one it doesn't know.
int inputKey(uint32_t k) {
    if (k >= 'a' && k <= 'z')
        return int(k - 'a' + 'A');
    if ((k >= '0' && k <= '9') || k == key::Space || k == key::Tab || k == key::Backspace || k == key::Escape)
        return int(k);
    if (k >= key::F1 && k < key::F1 + 12)
        return int(0x70 + k - key::F1);
    switch (k) {
    case key::Return: case key::KpEnter: return 13;
    case key::Left: return 37;
    case key::Up: return 38;
    case key::Right: return 39;
    case key::Down: return 40;
    case key::LShift: case key::RShift: return 16;
    case key::LCtrl: case key::RCtrl: return 17;
    case key::LAlt: case key::RAlt: return 18;
    case key::PageUp: return 0x21;
    case key::PageDown: return 0x22;
    case key::End: return 0x23;
    case key::Home: return 0x24;
    case key::Insert: return 0x2d;
    case key::Delete: return 0x2e;
    default: return 0;
    }
}

} // namespace

void MapViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    // Playing with the game's view, each key goes to the game's console
    // first, as the engine hands them on: while its menus show, it takes
    // the arrows and Enter, and the page leaves them be.
    if (play_ && !freeCam_ && (ev.type == EventType::KeyDown || ev.type == EventType::KeyUp) && !ev.repeat &&
        !(ev.type == EventType::KeyDown && ev.key == 'v'))
        if (const int k = inputKey(ev.key)) {
            // The menu's keys (Enter, the arrows) are the game's whatever
            // KeyEvent answers: the HUD's says false having acted on Enter.
            const bool menuKey = play_->menuShowing() && (k == 13 || (k >= 37 && k <= 40));
            if ((play_->consoleKey(k, ev.type == EventType::KeyDown) || menuKey) && ev.type == EventType::KeyDown) {
                for (bool& h : held_)
                    h = false;
                return;
            }
        }
    auto hold = [&](uint32_t k, bool down) {
        if (play_ && !freeCam_) // playing, the arrows turn and look, and Ctrl or F fires
            switch (k) {
            case key::Left: held_[TurnLeft] = down; return true;
            case key::Right: held_[TurnRight] = down; return true;
            case key::Up: held_[LookUp] = down; return true;
            case key::Down: held_[LookDown] = down; return true;
            case key::LCtrl: case key::RCtrl: case 'f': held_[Fire] = down; return true;
            default: break;
            }
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
        if (play_ && !freeCam_ && ev.key >= key::F1 && ev.key < key::F1 + 12)
            play_->pressKey("F" + std::to_string(ev.key - key::F1 + 1), false);
        hold(ev.key, false);
    } else if (ev.type == EventType::KeyDown) {
        if (play_ && !ev.repeat && ev.key == 'v') { // the camera let loose, or back to the game's
            freeCam_ = !freeCam_;
            for (bool& h : held_)
                h = false;
            return;
        }
        if (play_ && !freeCam_ && !ev.repeat) { // the keys bound to exec functions
            if (ev.key == key::Space)
                play_->press("Jump");
            else if (ev.key == key::LCtrl || ev.key == key::RCtrl || ev.key == 'f')
                play_->press("Fire");
            else if (ev.key >= '0' && ev.key <= '9') // the weapons, as the game binds them
                play_->pressKey(std::string(1, char(ev.key)));
            else if (ev.key >= key::F1 && ev.key < key::F1 + 12) // the communicator, the tricorder...
                play_->pressKey("F" + std::to_string(ev.key - key::F1 + 1));
            else if (ev.key == key::Escape) { // the game's menu (ShowMenu)
                play_->pressKey("Escape");
                return;
            }
        }
        if (hold(ev.key, true) || ev.repeat)
            return;
        if (ev.key == key::Return || ev.key == key::KpEnter) {
            for (bool& h : held_)
                h = false;
            if (play_)
                stopPlay();
            else
                startPlay();
            return;
        }
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
        else if (ev.key == '-')
            displayGamma_ = std::max(0.5f, displayGamma_ - 0.05f);
        else if (ev.key == '=')
            displayGamma_ = std::min(2.5f, displayGamma_ + 0.05f);
        else if (ev.key == 'k')
            showSky_ = !showSky_;
        else if (ev.key == 'z')
            zoneCulling_ = !zoneCulling_;
        else if (ev.key == 'n')
            animate_ = !animate_;
        else if (ev.key == 'g' || ev.key == 'h')
            jumpToMeshActor(ev.key == 'h');
        else if (ev.key == 'm' || ev.key == 'y') {
            if (ev.key == 'm')
                ctx_.meshTurn = MeshTurn((int(ctx_.meshTurn) + 1) % 3);
            else
                ctx_.meshYawQuarters = (ctx_.meshYawQuarters + 1) % 4;
            // Kept where it is: only the meshes need uploading again, but
            // the level loads quickly enough to just load it again.
            const auto pos = pos_;
            const float yaw = yaw_, pitch = pitch_;
            load(index_);
            pos_ = pos;
            yaw_ = yaw;
            pitch_ = pitch;
        }
        else if (ev.key == 'p') {
            runScripts_ = !runScripts_;
            const auto pos = pos_;
            const float yaw = yaw_, pitch = pitch_;
            load(index_);
            pos_ = pos;
            yaw_ = yaw;
            pitch_ = pitch;
        }
        else if (ev.key == 'o')
            for (MoverDraw& m : movers_)
                if (m.key == m.target && m.mover.numKeys > 1) {
                    m.target = (m.key + 1) % m.mover.numKeys;
                    m.t = 0;
                }
    } else if (play_ && !freeCam_ && play_->windowsOpen() &&
               (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp)) {
        // The game's windows (the options) open: the mouse is theirs, its
        // pointer where the page's is, its buttons keys 1 and 2.
        play_->consoleMouse(float(ev.mouseX), float(ev.mouseY));
        if (ev.type != EventType::MouseMove && (ev.button == MouseButton::Left || ev.button == MouseButton::Right))
            play_->consoleKey(ev.button == MouseButton::Left ? 1 : 2, ev.type == EventType::MouseDown);
    } else if (play_ && !freeCam_ && play_->menuShowing() &&
               (ev.type == EventType::MouseDown || ev.type == EventType::MouseUp) &&
               (ev.button == MouseButton::Left || ev.button == MouseButton::Right)) {
        // The HUD's menus: the buttons as the game binds them (LeftMouse
        // Fire selects, RightMouse AltFire steps the choice), not a drag.
        const bool left = ev.button == MouseButton::Left, down = ev.type == EventType::MouseDown;
        if (!play_->consoleKey(left ? 1 : 2, down))
            play_->pressKey(left ? "LeftMouse" : "RightMouse", down);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_ && play_ && !freeCam_) {
        // Playing, the mouse's movement goes to the player's input.
        mouseDx_ += float(ev.mouseX - lastX_);
        mouseDy_ -= float(ev.mouseY - lastY_);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
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
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;

    if (play_) {
        updatePlay(dt);
        // Its sounds, heard from the game's camera.
        if (ctx_.mixer && !audio_)
            audio_ = std::make_unique<LevelAudio>(*ctx_.mixer, ctx_.packages);
        if (audio_)
            audio_->update(*play_, view_.location, view_.rotation, dt);
        if (play_->quitAsked())
            stopPlay();
        else if (const std::string url = play_->takeTravel(); !url.empty())
            travel(url);
    }
    if (!play_ || freeCam_) { // flying: not playing, or playing with the camera let loose (V)
        if (!play_)
            fov_ = 90; // Unreal's default field of view, across
        const gfx::Vec3 f{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
        const gfx::Vec3 r{std::sin(yaw_), -std::cos(yaw_), 0};
        const float step = kSpeed * (held_[Fast] ? 3.0f : 1.0f) * dt;
        for (size_t k = 0; k < 3; ++k) {
            pos_[k] += f[k] * step * float(held_[Forward] - held_[Back]);
            pos_[k] += r[k] * step * float(held_[Right] - held_[Left]);
        }
        pos_[2] += step * float(held_[Up] - held_[Down]);
    }
    const gfx::Vec3 forward{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 right{std::sin(yaw_), -std::cos(yaw_), 0};

    const float aspect = float(kFrameW) / float(kFrameH);
    const float fovY = 2 * std::atan(std::tan(std::clamp(fov_, 1.0f, 170.0f) * kPi / 360) / aspect);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 65536), gfx::lookAlong(pos_, forward, {0, 0, 1}));
    // Movers between keyframes, at their own pace.
    for (MoverDraw& m : movers_)
        if (m.key != m.target) {
            m.t += dt / std::max(0.05f, m.mover.moveTime);
            if (m.t >= 1) {
                m.key = m.target;
                m.t = 0;
            }
        }

    updateLightmaps(dt);
    updateAnimatedTextures(dt);
    gatherDynamicLights(pos_);
    // The Fallen's distance fog: the camera's zone's (DistanceFogging, the
    // distance at which all is its DistanceFogColor), growing evenly from
    // the camera. Reluva's reading.
    fog_ = {};
    if (play_)
        fog_.distance = play_->distanceFog(Vec3f{pos_[0], -pos_[1], pos_[2]}, fog_.color);

    // The renderer raises colours to 1 / its gamma.
    renderer_->setGamma(1 / displayGamma_);
    const gfx::Mat4 proj = gfx::perspective(fovY, aspect, 4, 65536);
    const bool sky = sky_ && showSky_;
    if (sky) {
        // The sky first: the level seen from the sky zone, looking the
        // same way; then the view, from scratch, with the backdrop
        // surfaces leaving it showing.
        const gfx::Vec3 eye = toWorld(*sky_);
        const gfx::Mat4 skyView = gfx::multiply(proj, gfx::lookAlong(eye, forward, {0, 0, 1}));
        seeZonesFrom(eye, skyView);
        renderer_->beginFrame(skyView, eye, {20, 24, 40});
        drawBatches(levelBatches(), mesh_, gfx::identity(), true);
        drawSurfaceSwaps();
        renderer_->clearDepth();
        renderer_->setView(viewProj, pos_);
    } else {
        renderer_->beginFrame(viewProj, pos_, {20, 24, 40});
    }
    seeZonesFrom(pos_, viewProj);
    // Backdrop surfaces: with the sky, only their depth, hiding what's
    // behind them; without it, a plain colour.
    for (const Batch& b : batches_) {
        if (!(b.polyFlags & polyflag::FakeBackdrop) || !(b.zones & zonesSeen_))
            continue;
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.state.cull = gfx::Cull::None;
        call.color = {0.15f, 0.2f, 0.35f, 1};
        if (sky) {
            call.state.src = gfx::Blend::Zero;
            call.state.dst = gfx::Blend::One;
        }
        renderer_->draw(call);
    }
    drawBatches(levelBatches(), mesh_, gfx::identity(), true);
    drawSurfaceSwaps();
    for (size_t i = 0; i < movers_.size(); ++i) {
        const MoverDraw& m = movers_[i];
        VmObject* live = play_ && i < moverActors_.size() ? moverActors_[i] : nullptr;
        LevelPlay::Look look;
        // Lit where it is now, by the lights as they are now.
        if (live && play_->look(live, look)) {
            const MeshLight light = meshLightAt(look.location);
            drawBatches(m.batches, moverMesh_, moverPlacement(m.mover, look.location, look.rotation), false, false,
                        &light);
        } else {
            const gfx::Mat4 place = moverTransform(m);
            const MeshLight light = meshLightAt(Vec3f{place[12], -place[13], place[14]});
            drawBatches(m.batches, moverMesh_, place, false, false, &light);
        }
    }
    if (animate_)
        animTime_ += dt;
    if (play_) {
        coronaDt_ = dt;
        drawLiveActors(forward, right);
    } else {
        for (const MeshDraw& d : meshActors_) {
            const Mesh& mesh = meshes_[d.mesh];
            const MeshAnimSeq* anim = d.anim >= 0 ? &mesh.anims[size_t(d.anim)] : nullptr;
            drawMesh(*renderer_, gpuMeshes_[d.mesh], d.placement, poseAt(mesh, anim, animTime_, d.startFraction),
                     textured_ ? d.textures : std::vector<gfx::TextureId>{}, d.light, kExposure);
        }
        drawSprites(sprites_, forward, right);
    }
    if (play_ && !freeCam_)
        drawGameHud();
    drawOverlay(nowMs);
    if (play_)
        drawPlayHud();
    renderer_->endFrame(&overlay_);
}

// The game's own HUD, over the world: the player's PreRender and
// PostRender with the canvas (ScriptLevel::renderHud), then the actors and
// tiles they drew, in screen pixels: cut out where see-through, translucent ones added,
// modulated ones multiplied in.
void MapViewer::drawGameHud() {
    const int w = renderer_->width(), h = renderer_->height();
    ScriptLevel::HudView v;
    v.location = {view_.location[0], view_.location[1], view_.location[2]};
    v.rotation = {view_.rotation.pitch, view_.rotation.yaw, view_.rotation.roll};
    v.fov = view_.fov;
    v.width = w;
    v.height = h;
    play_->level().renderHud(v);
    const ScriptLevel::Hud& hud = play_->level().hud();
    gfx::Mat4 pixels = gfx::identity();
    pixels[0] = 2.0f / float(w);
    pixels[5] = -2.0f / float(h);
    pixels[10] = 0;
    pixels[12] = -1;
    pixels[13] = 1;
    // In the order the HUD drew them: the actors over the tiles before
    // them, each in its view (the camera and rectangle SetRenderFrame set:
    // a talker's head in its portrait), the depth cleared first if asked.
    size_t next = 0;
    bool flat = false; // set for tiles
    auto drawActorsUpTo = [&](size_t tiles) {
        std::vector<SpriteDraw> sprites;
        while (next < hud.actors.size() && hud.actors[next].afterTiles <= tiles) {
            const ScriptLevel::HudActor& a = hud.actors[next++];
            const ScriptLevel::HudView& v = a.view;
            const auto axes = rotationAxes(Rotator{v.rotation[0], v.rotation[1], v.rotation[2]});
            const gfx::Vec3 eye = toWorld(Vec3f{v.location[0], v.location[1], v.location[2]});
            const gfx::Vec3 ahead = toWorld(axes[0]), up = toWorld(axes[2]);
            const gfx::Vec3 side{-toWorld(axes[1])[0], -toWorld(axes[1])[1], -toWorld(axes[1])[2]};
            const float aspect = float(v.width) / float(std::max(1, v.height));
            const float fovY = 2 * std::atan(std::tan(std::clamp(v.fov, 1.0f, 170.0f) * kPi / 360) / aspect);
            renderer_->setViewport(v.x, v.y, v.width, v.height);
            renderer_->setView(gfx::multiply(gfx::perspective(fovY, aspect, 1, 65536), gfx::lookAlong(eye, ahead, up)),
                               eye);
            if (a.clearZ)
                renderer_->clearDepth();
            sprites.clear();
            drawLiveActor(a.actor, sprites, &a);
            drawSprites(sprites, ahead, side);
            flat = false;
        }
        renderer_->setViewport(0, 0, w, h);
    };
    for (size_t i = 0; i < hud.tiles.size(); ++i) {
        if (next < hud.actors.size() && hud.actors[next].afterTiles <= i)
            drawActorsUpTo(i);
        if (!flat) {
            renderer_->setView(pixels, gfx::Vec3{0, 0, 0});
            flat = true;
        }
        const ScriptLevel::HudTile& t = hud.tiles[i];
        const auto [id, size] = t.frame > 0 ? hudFliTexture(t.texture, t.frame, t.seeThrough)
                                            : hudTexture(t.texture, t.seeThrough);
        if (!id)
            continue;
        gfx::DrawCall call;
        call.mesh = spriteQuad_;
        call.indexCount = 6;
        call.model = gfx::identity();
        // The quad's corners are -0.5..0.5 with v down from its top.
        call.model[0] = t.w;
        call.model[5] = -t.h;
        call.model[12] = t.x + t.w / 2;
        call.model[13] = t.y + t.h / 2;
        call.texture = id;
        const float tw = float(size[0]), th = float(size[1]);
        call.preRow0 = {t.ul / tw, 0, t.u / tw};
        call.preRow1 = {0, t.vl / th, t.v / th};
        call.color = {float(t.color[0]) / 255, float(t.color[1]) / 255, float(t.color[2]) / 255, 1};
        call.state.cull = gfx::Cull::None;
        call.state.depthTest = false;
        call.state.depthWrite = false;
        // See-through texels stay so in every style (letters are drawn
        // masked however they blend).
        call.state.alphaTest = gfx::AlphaTest::Ge128;
        if (t.style == kStyleTranslucent) {
            call.state.src = call.state.dst = gfx::Blend::One;
        } else if (t.style == kStyleModulated) {
            call.color = {1, 1, 1, 1};
            call.state.src = gfx::Blend::DstColor;
            call.state.dst = gfx::Blend::SrcColor;
        }
        renderer_->draw(call);
    }
    drawActorsUpTo(hud.tiles.size());
}

// An FliTexture's frame for the HUD: its animation shown at that frame and
// uploaded when it changes (one texture each, and each way of colour 0).
std::pair<gfx::TextureId, std::array<int, 2>> MapViewer::hudFliTexture(const ObjectRef& tex, int frame,
                                                                      bool seeThrough) {
    auto key = std::make_tuple(tex.package, tex.exportIndex, seeThrough);
    auto it = hudFlis_.find(key);
    if (it == hudFlis_.end()) {
        HudFli f;
        f.anim = openAnimatedTexture(ctx_.packages, *tex.package, tex.exportIndex);
        if (f.anim) {
            f.anim->showFrame(frame, seeThrough);
            f.frame = frame;
            const image::Image& img = f.anim->image();
            gfx::TextureParams params;
            params.nearest = true;
            params.mipmaps = false;
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), params, f.id))
                textures_.push_back(f.id);
            else
                f.id = 0;
        }
        it = hudFlis_.emplace(key, std::move(f)).first;
    }
    HudFli& f = it->second;
    if (!f.anim || !f.id)
        return textureOf(tex);
    if (f.frame != frame && f.anim->showFrame(frame, seeThrough)) {
        const image::Image& img = f.anim->image();
        renderer_->updateTextures({{f.id, 0, 0, img.w, img.h, img.rgba.data()}});
    }
    f.frame = frame;
    return {f.id, {std::max(1, f.anim->image().w), std::max(1, f.anim->image().h)}};
}

// A texture for the HUD, with its colour 0 see-through if asked (letters,
// masked tiles), kept by both.
std::pair<gfx::TextureId, std::array<int, 2>> MapViewer::hudTexture(const ObjectRef& tex, bool seeThrough) {
    if (!seeThrough)
        return textureOf(tex);
    auto it = seeThroughTextures_.find({tex.package, tex.exportIndex});
    if (it == seeThroughTextures_.end()) {
        image::Image img;
        gfx::TextureId id = 0;
        gfx::TextureParams params;
        params.nearest = true;
        params.mipmaps = false;
        if (!decodeTexture(ctx_.packages, *tex.package, tex.exportIndex, img, nullptr, true) &&
            !renderer_->createTexture(img.w, img.h, img.rgba.data(), params, id))
            textures_.push_back(id);
        else
            id = 0;
        it = seeThroughTextures_
                 .emplace(std::make_pair(tex.package, tex.exportIndex),
                          std::make_pair(id, std::array<int, 2>{std::max(1, img.w), std::max(1, img.h)}))
                 .first;
    }
    return it->second;
}

// The sprites, facing the camera: those drawn solid first, then the
// blended ones over them (not hiding what's behind).
void MapViewer::drawSprites(const std::vector<SpriteDraw>& sprites, const gfx::Vec3& forward, const gfx::Vec3& right) {
    const gfx::Vec3 up{right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                       right[0] * forward[1] - right[1] * forward[0]};
    for (int blendedPass = 0; blendedPass < 2; ++blendedPass)
        for (const SpriteDraw& d : sprites) {
            const bool blend = d.style == kStyleTranslucent || d.style == kStyleModulated;
            if (blend != (blendedPass == 1))
                continue;
            // Vertical sprites stay upright, turning only about Z.
            const gfx::Vec3 u = d.oriented ? d.up : d.vertical ? gfx::Vec3{0, 0, 1} : up;
            const gfx::Vec3 r = d.oriented ? d.right : right;
            gfx::DrawCall call;
            call.mesh = spriteQuad_;
            call.indexCount = 6;
            call.model = gfx::identity();
            for (size_t k = 0; k < 3; ++k) {
                call.model[k] = r[k] * d.width;
                call.model[4 + k] = u[k] * d.height;
                call.model[8 + k] = -forward[k];
                call.model[12 + k] = d.at[k];
            }
            call.state.depthBias = d.oriented;
            if (d.onTop)
                call.state.depthTest = false;
            call.texture = textured_ ? d.texture : 0;
            call.color = {d.color[0], d.color[1], d.color[2], 1};
            call.state.cull = gfx::Cull::None;
            if (d.style == kStyleTranslucent) {
                call.state.src = call.state.dst = gfx::Blend::One;
                call.state.depthWrite = false;
            } else if (d.style == kStyleModulated) {
                // The background times the texture, doubled: grey leaves it as is.
                call.color = {1, 1, 1, 1};
                call.state.src = gfx::Blend::DstColor;
                call.state.dst = gfx::Blend::SrcColor;
                call.state.depthWrite = false;
            } else {
                call.state.alphaTest = gfx::AlphaTest::Ge128; // masked: index 0 see-through
            }
            renderer_->draw(call);
        }
}

// Moves the fire, water, wet, ice and FLC textures on, uploading those that
// changed.
void MapViewer::updateAnimatedTextures(float dt) {
    std::vector<gfx::TextureRegion> regions;
    for (const auto& [id, anim] : animatedTextures_)
        if (anim->advance(dt))
            regions.push_back({id, 0, 0, anim->image().w, anim->image().h, anim->image().rgba.data()});
    if (!regions.empty())
        renderer_->updateTextures(regions);
}

// A changing light's share of its brightness now: its curve; while
// playing, a light the scripts may change as its live actor is (its
// LightType and LightBrightness: a TriggerLight fading, a light turned
// off, or destroyed).
float MapViewer::liveLightLevel(const LightSource& l, bool whole) {
    if (!play_ && l.startsOff)
        return 0; // as the level starts: its scripts haven't turned it on
    if (!play_ || !l.scripted || !l.ref)
        return lightLevel(l, l.type, lightTime_, whole);
    VmObject* a = play_->actorAt(l.ref);
    if (!a)
        return 0;
    Vm& vm = play_->vm();
    const Value* type = a->find(vm, "LightType");
    const Value* brightness = a->find(vm, "LightBrightness");
    const float share = brightness && l.brightness ? float(brightness->asInt()) / float(l.brightness) : 1;
    return std::clamp(lightLevel(l, type ? uint8_t(type->asInt()) : l.type, lightTime_, whole) * share, 0.0f, 2.0f);
}

// Redraws the lightmaps that pulsing, flickering... lights reach, where
// those lights' brightness changed, and those with a light effect's
// pattern on them every frame (it moves across them). Meshes take each
// light's level as a whole (lightLevels_); the lightmaps theirs less a
// pattern, which lightPattern gives texel by texel (mapLevels_). Only
// lightmaps in a zone seen are drawn; the others are drawn when it is.
void MapViewer::updateLightmaps(float dt) {
    lightTime_ += dt;
    std::vector<bool> changed(changingLights_.size(), false);
    patternNow_.resize(changingLights_.size());
    for (size_t i = 0; i < changingLights_.size(); ++i) {
        lightLevels_[i] = liveLightLevel(changingLights_[i]);
        const float level = liveLightLevel(changingLights_[i], false);
        const bool pattern = hasLightPattern(changingLights_[i]) && level > 0;
        if (pattern)
            patternNow_[i] = patternNow(changingLights_[i], lightTime_);
        if (pattern || std::fabs(level - mapLevels_[i]) * 255 >= 1) {
            mapLevels_[i] = level;
            changed[i] = true;
        }
    }
    staleLightmaps_.resize(animatedLightmaps_.size(), false);
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<gfx::TextureRegion> regions;
    for (size_t k = 0; k < animatedLightmaps_.size(); ++k) {
        const AnimatedLightmap& m = animatedLightmaps_[k];
        const bool redraw = staleLightmaps_[k] || std::any_of(m.lights.begin(), m.lights.end(), [&](const AnimatedLightmap::Light& l) {
                                return changed[size_t(l.index)];
                            });
        if (!redraw || size_t(m.page) >= lightmapPages_.size() || !lightmapPages_[size_t(m.page)])
            continue;
        if (m.zone >= 0 && m.zone < 64 && !((zonesSeen_ >> m.zone) & 1)) {
            staleLightmaps_[k] = true;
            continue;
        }
        staleLightmaps_[k] = false;
        pixels.emplace_back();
        m.draw(mapLevels_, pixels.back(), &changingLights_, &patternNow_);
        regions.push_back({lightmapPages_[size_t(m.page)], m.x, m.y, m.w, m.h, nullptr});
    }
    if (regions.empty())
        return;
    for (size_t i = 0; i < regions.size(); ++i)
        regions[i].rgba = pixels[i].data();
    renderer_->updateTextures(regions);
}

// The idle loop a character placed in pose `anim` plays (see load()).
std::string MapViewer::idleFor(const std::string& anim) {
    return fs::toLower(anim).rfind("baseready", 0) == 0 ? "IdleReady1" : "Idle1";
}

// Puts the camera before the next placed mesh (or the next whose mesh is
// posed from bones: the characters), looking at it along X.
void MapViewer::jumpToMeshActor(bool skeletalOnly) {
    const int n = int(meshActors_.size());
    for (int step = 1; step <= n; ++step) {
        const int i = (jump_ + step) % n;
        const MeshDraw& d = meshActors_[size_t(i)];
        if (skeletalOnly && !d.skeletal)
            continue;
        jump_ = i;
        // Back from it by its size as placed (The Fallen's Arachnids are
        // drawn at 0.03 of their mesh: backing off by the mesh's own size
        // went through the walls).
        const GpuMesh& g = gpuMeshes_[d.mesh];
        const float back = std::clamp(g.radius * std::fabs(d.drawScale) * 3, 30.0f, 400.0f);
        pos_ = {d.at[0] - back, d.at[1], d.at[2] + back * 0.25f};
        yaw_ = 0;
        pitch_ = -0.24f;
        return;
    }
}

gfx::Mat4 MapViewer::moverTransform(const MoverDraw& d) const {
    const Mover& m = d.mover;
    // Where it is between its two keyframes.
    const Vec3f base = m.basePosition();
    const Rotator baseRot = m.baseRotation();
    const Vec3f& a = m.keyPos[size_t(d.key)];
    const Vec3f& b = m.keyPos[size_t(d.target)];
    const Rotator& ra = m.keyRot[size_t(d.key)];
    const Rotator& rb = m.keyRot[size_t(d.target)];
    const float t = d.t;
    const Vec3f location{base[0] + a[0] + (b[0] - a[0]) * t, base[1] + a[1] + (b[1] - a[1]) * t,
                         base[2] + a[2] + (b[2] - a[2]) * t};
    const Rotator rot{baseRot.pitch + ra.pitch + int32_t(float(rb.pitch - ra.pitch) * t),
                      baseRot.yaw + ra.yaw + int32_t(float(rb.yaw - ra.yaw) * t),
                      baseRot.roll + ra.roll + int32_t(float(rb.roll - ra.roll) * t)};
    return moverPlacement(m, location, rot);
}

gfx::Mat4 MapViewer::moverPlacement(const Mover& m, const Vec3f& location, const Rotator& rot) {
    // location + postScale * (rotation * (mainScale * (v - prePivot))),
    // then Y negated into the renderer's axes.
    const gfx::Mat4 flip = scaling({1, -1, 1});
    gfx::Mat4 m4 = gfx::multiply(flip, translation(location));
    m4 = gfx::multiply(m4, scaling(m.postScale));
    m4 = gfx::multiply(m4, rotation(rot));
    m4 = gfx::multiply(m4, scaling(m.mainScale));
    return gfx::multiply(m4, translation({-m.prePivot[0], -m.prePivot[1], -m.prePivot[2]}));
}

// The zones seen from `eye` (gfx's coordinates) through the portals, for
// drawBatches to leave out the rest.
void MapViewer::seeZonesFrom(const gfx::Vec3& eye, const gfx::Mat4& viewProj) {
    if (!zoneCulling_) {
        zonesSeen_ = ~uint64_t(0);
        return;
    }
    // Unreal's coordinates have y the other way.
    gfx::Mat4 flip = gfx::identity();
    flip[5] = -1;
    zonesSeen_ = zones_.visible(Vec3f{eye[0], -eye[1], eye[2]}, gfx::multiply(viewProj, flip));
}

// The level's batches, playing: with the textures the scripts have swapped
// for others everywhere (SetSurfaceTexture's bReplaceAllWorld).
const std::vector<MapViewer::Batch>& MapViewer::levelBatches() {
    if (!play_ || play_->level().surfaceSwaps().world.empty())
        return batches_;
    const ScriptLevel::SurfaceSwaps& swaps = play_->level().surfaceSwaps();
    if (swappedSerial_ != swaps.serial) {
        swappedSerial_ = swaps.serial;
        swappedBatches_ = batches_;
        for (Batch& b : swappedBatches_)
            if (auto it = swaps.world.find({b.textureRef.package, b.textureRef.exportIndex}); it != swaps.world.end())
                b.texture = textureOf(it->second).first;
    }
    return swappedBatches_;
}

// One surface's texture swapped (SetSurfaceTexture): its polygons drawn
// again over themselves with the new one, then its lightmap. The texture
// coordinates are the old texture's, so a new one of another size would
// be stretched; The Fallen's switches swap textures of one size.
void MapViewer::drawSurfaceSwaps() {
    if (!play_)
        return;
    for (const auto& [surf, tex] : play_->level().surfaceSwaps().surfaces) {
        auto it = surfRanges_.find(surf);
        if (it == surfRanges_.end())
            continue;
        const gfx::TextureId texture = textureOf(tex).first;
        for (const SurfRange& r : it->second) {
            if (r.batch >= batches_.size())
                continue;
            Batch b = batches_[r.batch];
            b.firstIndex = r.firstIndex;
            b.indexCount = r.indexCount;
            b.texture = texture;
            drawBatches({b}, mesh_, gfx::identity(), true, true);
        }
    }
}

Vec3f MapViewer::tintOf(int tint) const {
    const std::vector<TintSample>& samples = tints_[size_t(tint)];
    Vec3f sum{};
    for (const TintSample& t : samples) {
        Vec3f light = t.light;
        if (t.anim >= 0) {
            const AnimatedLightmap& m = animatedLightmaps_[size_t(t.anim)];
            light = m.base[t.texel];
            for (const AnimatedLightmap::Light& l : m.lights) {
                const float level = size_t(l.index) < mapLevels_.size() ? mapLevels_[size_t(l.index)] : 1;
                for (size_t k = 0; k < 3; ++k)
                    light[k] += l.share[t.texel][k] * level;
            }
        }
        for (const LightSource& l : dynamic_) {
            const float dist = lightDistance(l, t.at);
            if (dist < l.radius)
                for (size_t k = 0; k < 3; ++k)
                    light[k] += l.color[k] * lightFalloff(dist / l.radius);
        }
        for (size_t k = 0; k < 3; ++k)
            sum[k] += std::min(light[k], kMaxLight);
    }
    // Twice the average, at most the texture's own colour: fitted to a
    // recording of the original's logos (lit by a TriggerLight that only
    // reaches 60% before its dispatcher puts it out, they show at about
    // their texture's colour; the plain average was 2.5 times too dark).
    for (float& v : sum)
        v = std::min(kMaxLight, 2 * v / float(std::max<size_t>(samples.size(), 1)));
    return sum;
}

void MapViewer::drawBatches(const std::vector<Batch>& batches, gfx::MeshId mesh, const gfx::Mat4& model,
                            bool levelLit, bool over, const MeshLight* moverLight) {
    auto lit = [&](const Batch& b) {
        return levelLit && lightmaps_ && b.lightmap && !blended(b.polyFlags) &&
               !(b.polyFlags & (polyflag::FakeBackdrop | polyflag::Unlit));
    };
    for (const Batch& b : batches) {
        if ((b.polyFlags & polyflag::FakeBackdrop) || !(b.zones & zonesSeen_))
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.model = model;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        // One-sided, as UE1's surfaces are (their back, the way our
        // winding goes, is the renderer's front), unless flagged two-sided:
        // a camera in solid sees through the wall it's in. M03_SiskoL1A's
        // opening rides down inside the rock beside the lift shaft, looking
        // in at the far wall's lamp and braces; drawn two-sided, all it saw
        // was the back of the near wall.
        // A mover's polygons (its brush's) wind the other way round: from
        // inside M04_WorfL1a's turbolift, culled as the level's, all that
        // showed was its brush's untextured outer faces from behind.
        call.state.cull = (b.polyFlags & polyflag::TwoSided) ? gfx::Cull::None
                          : moverLight                              ? gfx::Cull::Back
                                                                    : gfx::Cull::Front;
        call.texture = textured_ ? b.texture : 0;
        // With a lightmap the second pass lights it; else the vertex colours
        // (shading by facing, or a mover's light).
        call.rgbSource = lit(b) ? gfx::ColorSource::Constant : gfx::ColorSource::Vertex;
        if (moverLight && !(b.polyFlags & polyflag::Unlit) && !blended(b.polyFlags)) {
            call.rgbSource = gfx::ColorSource::Lighting;
            call.ambientLight = moverLight->ambient;
            call.directedLight = moverLight->directed;
            call.lightDirection = moverLight->direction;
        }
        if (!(b.polyFlags & polyflag::Modulated)) // those multiply what's there, so stay as they are
            call.color = {kExposure, kExposure, kExposure, 1};
        if (b.tint >= 0 && lightmaps_) {
            const Vec3f t = tintOf(b.tint);
            call.color = {kExposure * t[0] * lightScale_, kExposure * t[1] * lightScale_, kExposure * t[2] * lightScale_, 1};
        }
        if (b.polyFlags & polyflag::Masked)
            call.state.alphaTest = gfx::AlphaTest::Ge128;
        if (over) // drawn over what's there already
            call.state.depthEqual = true;
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
            // The lightmap over it, doubled (dst * src * 2).
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
        // The lights that move and change (a spark's flash, an
        // explosion's) added over it: its texture times their light, as
        // UE1 adds dynamic light to the lightmap's.
        if (!dynamic_.empty() && !over && !blended(b.polyFlags) && !(b.polyFlags & polyflag::Unlit) && call.texture) {
            gfx::DrawCall dyn = call;
            dyn.dynamicLightsTextured = true;
            dyn.state.src = gfx::Blend::One;
            dyn.state.dst = gfx::Blend::One;
            dyn.state.depthEqual = true;
            dyn.state.depthWrite = false;
            renderer_->draw(dyn);
        }
        // The zone's distance fog over it, last.
        if (fog_.distance > 0 && !over && !blended(b.polyFlags))
            drawFogOver(*renderer_, call, fog_);
    }
}

// The lit actors that aren't the level's own lights (those are in its
// lightmaps) nor static (as in UE1, those light the world only through
// lightmaps): their light as it is now, the nearest kMaxDynamicLights to
// the eye given to the renderer.
void MapViewer::gatherDynamicLights(const gfx::Vec3& eye) {
    dynamic_.clear();
    if (!play_)
        return;
    play_->level().litActors(litScratch_);
    const Package* map = &play_->map();
    for (const ScriptLevel::LitActor& a : litScratch_) {
        if (a.isStatic || (a.actor->package == map && staticLightRefs_.count(a.actor->exportIndex + 1)))
            continue;
        LightSource l;
        l.position = {a.location[0], a.location[1], a.location[2]};
        l.radius = kRadiusUnit * (float(a.radius) + 1);
        l.type = a.type;
        l.effect = a.effect;
        l.period = float(std::max<uint8_t>(a.period, 1)) / 35;
        l.phase = float(a.phase) / 256;
        const float level = lightLevel(l, lightTime_);
        if (level <= 0)
            continue;
        const Vec3f c = lightColor(a.hue, a.saturation, a.brightness);
        l.color = {c[0] * level, c[1] * level, c[2] * level};
        dynamic_.push_back(l);
    }
    const Vec3f e{eye[0], -eye[1], eye[2]};
    auto dist2 = [&](const LightSource& l) {
        const float dx = l.position[0] - e[0], dy = l.position[1] - e[1], dz = l.position[2] - e[2];
        return dx * dx + dy * dy + dz * dz;
    };
    if (dynamic_.size() > size_t(gfx::kMaxDynamicLights)) {
        std::partial_sort(dynamic_.begin(), dynamic_.begin() + gfx::kMaxDynamicLights, dynamic_.end(),
                          [&](const LightSource& a, const LightSource& b) { return dist2(a) < dist2(b); });
        dynamic_.resize(size_t(gfx::kMaxDynamicLights));
    }
    std::vector<gfx::DynamicLight> lights;
    for (const LightSource& l : dynamic_)
        lights.push_back({toWorld(l.position), l.radius, {l.color[0], l.color[1], l.color[2]}});
    renderer_->setDynamicLights(lights);
}

void MapViewer::drawOverlay(uint64_t nowMs) {
    ++frames_;
    if (nowMs - fpsStart_ >= 1000) {
        fps_ = float(frames_) * 1000.0f / float(nowMs - fpsStart_);
        fpsStart_ = nowMs;
        frames_ = 0;
    }
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (play_ && view_.letterbox) { // a cut scene's black bars, under the text
        const int bar = kFrameH / 8;
        for (int y = 0; y < kFrameH; ++y) {
            if (y >= bar && y < kFrameH - bar)
                continue;
            uint8_t* p = overlay_.row(y);
            for (int x = 0; x < kFrameW; ++x) {
                p[x * 4] = p[x * 4 + 1] = p[x * 4 + 2] = 0;
                p[x * 4 + 3] = 255;
            }
        }
    }
    if (!showInfo_ && error_.empty())
        return;
    std::vector<std::string> lines = info_;
    const size_t errorLine = error_.empty() ? size_t(-1) : lines.size();
    if (!error_.empty())
        lines.push_back(error_);
    if (!playError_.empty())
        lines.push_back("Can't play: " + playError_);
    lines.push_back(!play_     ? "Enter plays the level"
                    : freeCam_ ? "Playing, the camera loose (V back to the game's): W/S/A/D, Space/C and drag fly it"
                               : "Playing (Enter stops): W/S move, A/D step aside, arrows turn and look, drag to look, Space "
                                 "jumps, C crouches, Ctrl or F fires, 0-9 pick weapons, F1-F12 as the game binds them, Esc the game's menu, V lets "
                                 "the camera loose");
    char where[240];
    // Shown in Unreal's own coordinates.
    const std::string zones =
        !zones_.portalCount() ? "no zone portals"
        : !zoneCulling_       ? "all zones drawn (Z)"
        : zonesSeen_ == ~uint64_t(0)
            ? "all zones seen"
        : std::popcount(zonesSeen_) == 1
            ? "1 zone seen (Z draws all)"
            : std::to_string(std::popcount(zonesSeen_)) + " zones seen (Z draws all)";
    std::snprintf(where, sizeof where,
                  "at %.0f %.0f %.0f, %.0f fps; lightmaps %s, brightness x%.2f, display gamma %.2f; %s", pos_[0],
                  -pos_[1], pos_[2], fps_, !hasLightmaps_ ? "none" : lightmaps_ ? "on" : "off", lightScale_,
                  displayGamma_, zones.c_str());
    lines.push_back(where);
    if (jump_ >= 0 && size_t(jump_) < meshActors_.size()) {
        const MeshDraw& d = meshActors_[size_t(jump_)];
        std::snprintf(where, sizeof where, "mesh actor %d of %zu: %s, at %.0f %.0f %.0f (G next, H next posed from bones)",
                      jump_ + 1, meshActors_.size(), d.label.c_str(), d.at[0], -d.at[1], d.at[2]);
        lines.push_back(where);
    }
    const int lh = font::kLineHeight;
    size_t widest = 0;
    for (const std::string& l : lines)
        widest = std::max(widest, font::length(l));
    const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
    for (int y = 0; y < boxH; ++y) {
        uint8_t* p = overlay_.row(y);
        for (int x = 0; x < boxW; ++x)
            p[x * 4 + 3] = std::max<uint8_t>(p[x * 4 + 3], 150);
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
}

std::pair<gfx::TextureId, std::array<int, 2>> MapViewer::textureOf(const ObjectRef& tex, bool* unreadable) {
    if (!tex)
        return {0, {64, 64}};
    auto it = textureCache_.find({tex.package, tex.exportIndex});
    if (it == textureCache_.end()) {
        image::Image img;
        gfx::TextureId id = 0;
        TextureInfo ti;
        if (decodeTexture(ctx_.packages, *tex.package, tex.exportIndex, img, &ti)) {
            if (unreadable)
                *unreadable = true;
        } else {
            // Fire, water, wet, ice and FLC textures play, redrawn as they
            // change (so without mipmaps, which updates don't redo).
            std::unique_ptr<AnimatedTexture> anim;
            if (isAnimatedTextureClass(tex.package->className(tex.exportIndex + 1)))
                anim = openAnimatedTexture(ctx_.packages, *tex.package, tex.exportIndex);
            if (anim && anim->image().w == img.w && anim->image().h == img.h)
                img = anim->image();
            else
                anim.reset();
            gfx::TextureParams params;
            params.nearest = (ti.polyFlags & polyflag::NoSmooth) != 0;
            params.mipmaps = !anim;
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), params, id)) {
                textures_.push_back(id);
                if (anim)
                    animatedTextures_.push_back({id, std::move(anim)});
            } else {
                id = 0;
            }
        }
        it = textureCache_
                 .emplace(std::make_pair(tex.package, tex.exportIndex),
                          std::make_pair(id, std::array<int, 2>{std::max(1, img.w), std::max(1, img.h)}))
                 .first;
    }
    return it->second;
}

// The lights reaching where an actor stands: some all round, the rest
// from their average direction.
MeshLight MapViewer::meshLightAt(const Vec3f& at) const {
    MeshLight light;
    Vec3f total{}, toward{};
    // The level's lights as bright as they are now (pulsing, flickering,
    // turned off by scripts), then the lights that move and change.
    // While playing, only lights that see it: the level's walls (its BSP)
    // between a light and the actor keep its light off it, as they keep it
    // off the walls behind them (Sisko lit white in M03_SiskoL1A's red room
    // by the yellow lamps of the rooms round it).
    auto add = [&](const LightSource& l, float level) {
        if (level <= 0)
            return;
        const Vec3f dv{l.position[0] - at[0], l.position[1] - at[1], l.position[2] - at[2]};
        const float straight = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        const float dist = lightDistance(l, at);
        if (dist >= l.radius || straight <= 0)
            return;
        if (play_ && play_->wallBetween(at, l.position))
            return;
        const float amount = lightFalloff(dist / l.radius) * level;
        for (size_t k = 0; k < 3; ++k) {
            total[k] += l.color[k] * amount;
            toward[k] += dv[k] / straight * amount;
        }
    };
    for (const LightSource& l : lights_.unique)
        add(l, levelOf(l));
    for (const LightSource& l : dynamic_)
        add(l, 1);
    // At most the texture's own colour where the light falls full on it,
    // as the lightmaps are: ambient and directed together under kMaxLight.
    // Each capped alone, they reached twice it, washing characters' faces
    // under bright lights out to pale (the opening's Terrell, against a
    // recording of the original).
    for (size_t k = 0; k < 3; ++k) {
        light.ambient[k] = std::min(kMaxLight, 0.15f + 0.5f * total[k]);
        light.directed[k] = std::min(0.7f * total[k], std::max(0.0f, kMaxLight - light.ambient[k]));
    }
    const float len = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    light.direction = len > 0 ? toWorld(Vec3f{toward[0] / len, toward[1] / len, toward[2] / len}) : gfx::Vec3{0, 0, 1};
    return light;
}

// --- Playing --------------------------------------------------------------------------------

Error MapViewer::saveGame(const std::string& name) {
    if (!play_)
        return Error::fail("not playing");
    return play_->level().saveGame(name);
}

void MapViewer::travel(const std::string& url) {
    // "?RESTART": the map again, from where it began.
    const std::string lower = fs::toLower(url);
    if (lower.rfind("?restart", 0) == 0) {
        const std::string portal = portal_;
        startPlay(portal);
        return;
    }
    // "?LOAD=Save1.dss": a saved game, on its own map.
    if (const size_t load = lower.find("?load="); load != std::string::npos) {
        std::string name = url.substr(load + 6);
        name = name.substr(0, name.find('?'));
        const std::filesystem::path file = ctx_.saveFolder / name;
        ScriptLevel::SaveInfo info;
        if (ctx_.saveFolder.empty() || ScriptLevel::readSaveInfo(file, info)) {
            // None there: the game's word for it (its quick load).
            if (play_)
                play_->hudEvent(1010);
            return;
        }
        if (!selectMap(info.map)) {
            stopPlay();
            playError_ = "load " + name + ": no map " + info.map;
            return;
        }
        startPlay({}, file);
        return;
    }
    std::string where = url.substr(0, url.find('?'));
    const size_t cut = where.find_first_of("/#");
    const std::string portal = cut == std::string::npos ? std::string() : where.substr(cut + 1);
    std::string map = where.substr(0, cut);
    if (const size_t dot = map.rfind('.'); dot != std::string::npos)
        map = map.substr(0, dot);
    if (!selectMap(map)) {
        stopPlay();
        playError_ = "travel to " + url + ": no map " + map;
        return;
    }
    startPlay(portal);
}

void MapViewer::startPlay(const std::string& portal, const std::filesystem::path& saved) {
    stopPlay();
    const Package* pkg = names_.empty() ? nullptr : ctx_.packages.get(names_[size_t(index_)]);
    if (!pkg || !error_.empty())
        return;
    auto play = std::make_unique<LevelPlay>(ctx_.packages, *pkg);
    if (Error err = play->start(portal, saved, ctx_.saveFolder)) {
        playError_ = err.message;
        return;
    }
    if (!play->player()) {
        playError_ = "no player logged in: " + play->loginError();
        return;
    }
    // Each mover's live actor, by its path in the map.
    std::map<std::string, VmObject*> byPath;
    for (VmObject* a : play->level().actors())
        if (a->package == pkg && a->exportIndex >= 0)
            byPath[pkg->path(a->exportIndex + 1)] = a;
    moverActors_.clear();
    for (const MoverDraw& m : movers_) {
        auto it = byPath.find(m.mover.name);
        moverActors_.push_back(it == byPath.end() ? nullptr : it->second);
    }
    playError_.clear();
    portal_ = portal;
    playClock_ = 0;
    held_[Fast] = false;
    play_ = std::move(play);
}

void MapViewer::stopPlay() {
    freeCam_ = false;
    if (renderer_)
        for (const GpuMesh& g : posedMeshes_)
            if (g.mesh)
                renderer_->destroyMesh(g.mesh);
    posedMeshes_.clear();
    if (audio_)
        audio_->reset();
    play_.reset();
    moverActors_.clear();
    // The live actors' meshes stay uploaded (in meshes_), for playing again.
}

void MapViewer::updatePlay(float dt) {
    ScriptLevel::Controls& c = play_->controls();
    if (freeCam_) { // the keys fly the camera: the player stands
        c = {};
        play_->advance(dt);
        playClock_ += dt;
        view_ = play_->view();
        return;
    }
    c.forward = float(held_[Forward] - held_[Back]);
    c.strafe = float(held_[Right] - held_[Left]);
    c.turn = float(held_[TurnRight] - held_[TurnLeft]);
    c.lookUp = float(held_[LookUp] - held_[LookDown]);
    c.mouseX += mouseDx_;
    c.mouseY += mouseDy_;
    c.jump = held_[Up];
    c.duck = held_[Down]; // C, as the game binds it (Duck)
    c.fire = held_[Fire];
    mouseDx_ = mouseDy_ = 0;
    play_->advance(dt);
    playClock_ += dt;
    // The camera where the game puts it.
    view_ = play_->view();
    pos_ = toWorld(view_.location);
    // The way it looks, from its rotation's direction: the game's camera
    // writes the same direction with the yaw turned round and the pitch
    // past straight up when it's ahead of the player (CTVE_Camera), which
    // taken as yaw and pitch alone looks straight down.
    const Vec3f f = rotationAxes(view_.rotation)[0];
    yaw_ = std::atan2(-f[1], f[0]); // negated with Y
    pitch_ = std::clamp(std::asin(std::clamp(f[2], -1.0f, 1.0f)), -1.55f, 1.55f);
    fov_ = view_.fov;
}

int MapViewer::liveMesh(const ObjectRef& ref, const std::string& anim) {
    auto it = liveMeshes_.find({ref.package, ref.exportIndex});
    if (it == liveMeshes_.end()) {
        Mesh mesh;
        GpuMesh gpu;
        int at = -1;
        if (!readMesh(*ref.package, ref.exportIndex, mesh) &&
            !uploadMesh(*renderer_, mesh, gpu, ctx_.meshTurn, ctx_.meshYawQuarters)) {
            at = int(meshes_.size());
            meshes_.push_back(std::move(mesh));
            gpuMeshes_.push_back(gpu);
        }
        it = liveMeshes_.emplace(std::make_pair(ref.package, ref.exportIndex), at).first;
    }
    const int at = it->second;
    if (at < 0 || anim.empty())
        return at;
    // A character's animation from the shared sets, the first time it plays
    // one its mesh doesn't have: borrowed, and the mesh uploaded again.
    Mesh& mesh = meshes_[size_t(at)];
    if (mesh.bones && !mesh.findAnim(anim) && borrowNamedAnimation(*ref.package, ref.exportIndex, ctx_.sets(), anim, mesh)) {
        GpuMesh gpu;
        if (!uploadMesh(*renderer_, mesh, gpu, ctx_.meshTurn, ctx_.meshYawQuarters)) {
            if (gpuMeshes_[size_t(at)].mesh)
                renderer_->destroyMesh(gpuMeshes_[size_t(at)].mesh);
            gpuMeshes_[size_t(at)] = gpu;
        }
    }
    return at;
}

// The level's actors as they are now: meshes posed where their animation
// is (AnimFrame), and sprites.
void MapViewer::drawLiveActors(const gfx::Vec3& forward, const gfx::Vec3& right) {
    for (const GpuMesh& g : posedMeshes_) // the last frame's, drawn by now
        if (g.mesh)
            renderer_->destroyMesh(g.mesh);
    posedMeshes_.clear();
    std::vector<SpriteDraw> sprites;
    // Only those that may be seen: not hidden, in a zone seen (the rest
    // aren't drawn, so their scripts aren't asked to get them ready).
    for (VmObject* a : play_->level().actors())
        if (play_->mayBeSeen(a, zonesSeen_))
            drawLiveActor(a, sprites);
    addDecals(sprites);
    addCoronas(sprites, forward, coronaDt_);
    drawSprites(sprites, forward, right);
}

// A corona is the light's texture glowing where the light is, added over
// everything, the same size on the screen however far it is: its texture
// times its DrawScale, times 8 on a 960-wide frame (the credits station's
// flares, DrawScale 0.025 and 0.05 of a 128 texture, were about 15 and 22
// pixels across in a recording of the original, their glow's visible part
// about 60% of the quad). Shown while the level's walls aren't between it
// and the camera, fading in and out over a fifth of a second (Reluva's
// reading: the original's code isn't known).
void MapViewer::addCoronas(std::vector<SpriteDraw>& sprites, const gfx::Vec3& forward, float dt) {
    play_->coronas(coronaScratch_);
    const Vec3f eye{pos_[0], -pos_[1], pos_[2]};
    const float tanHalf = std::tan(std::clamp(fov_, 1.0f, 170.0f) * kPi / 360);
    std::map<VmObject*, float> fades;
    for (const LevelPlay::Corona& c : coronaScratch_) {
        const gfx::Vec3 at = toWorld(c.location);
        const float depth = (at[0] - pos_[0]) * forward[0] + (at[1] - pos_[1]) * forward[1] + (at[2] - pos_[2]) * forward[2];
        const bool zoneSeen = c.zone < 0 || c.zone >= 64 || ((zonesSeen_ >> c.zone) & 1);
        const bool visible = depth > 8 && zoneSeen && !play_->wallBetween(eye, c.location);
        auto old = coronaFade_.find(c.actor);
        float fade = old == coronaFade_.end() ? (visible ? 1.0f : 0.0f) : old->second;
        fade = std::clamp(fade + (visible ? 5.0f : -5.0f) * dt, 0.0f, 1.0f);
        fades[c.actor] = fade;
        if (fade <= 0 || depth <= 8)
            continue;
        const auto [texture, size] = textureOf(c.texture);
        if (!texture)
            continue;
        const float pixels = float(size[0]) * c.drawScale * 8 * float(kFrameW) / 960;
        const float world = pixels * depth * 2 * tanHalf / float(kFrameW);
        SpriteDraw d;
        d.texture = texture;
        d.at = at;
        d.width = d.height = world;
        d.style = kStyleTranslucent;
        d.color = {fade, fade, fade};
        d.onTop = true;
        sprites.push_back(d);
    }
    coronaFade_ = std::move(fades);
}

// One of the level's actors, as it looks now: a mesh drawn, a sprite kept
// for drawSprites. Hidden ones only if `evenHidden` (the HUD draws some).
void MapViewer::drawLiveActor(VmObject* a, std::vector<SpriteDraw>& sprites, const ScriptLevel::HudActor* hud) {
    constexpr int kDrawSprite = 1, kDrawMesh = 2, kDrawVerticalSprite = 5, kDrawSpriteAnimOnce = 7;
    const bool evenHidden = hud != nullptr;
    LevelPlay::Look look;
    // Drawn for the HUD: as it looked when the HUD asked.
    auto asAsked = [&] {
        if (!hud)
            return;
        look.drawType = hud->drawType;
        look.style = uint8_t(hud->style);
        look.scaleGlow = hud->scaleGlow;
    };
    if (!play_->look(a, look))
        return;
    asAsked();
    if (look.hidden && !evenHidden)
        return;
    // The Fallen's copies of it (MultiMesh), drawn in its place unless it
    // asks to be drawn itself as well.
    if (!hud) {
        std::vector<LevelPlay::Copy> copies;
        if (play_->copies(a, copies)) {
            drawCopies(a, look, copies, sprites);
            if (!look.renderInitial)
                return;
        }
    }
    if (look.drawType == kDrawMesh && look.mesh) {
        // About to be drawn: its scripts get their say first, and it's
        // drawn as they leave it.
        play_->preRender(a);
        look = {};
        if (!play_->look(a, look))
            return;
        asAsked();
        if ((look.hidden && !evenHidden) || look.drawType != kDrawMesh || !look.mesh)
            return;
        if (!look.tweenFrom.empty())
            liveMesh(look.mesh, look.tweenFrom); // borrowed too, if it's from the shared sets
        const int at = liveMesh(look.mesh, look.animSequence);
        if (at < 0)
            return;
        const Mesh& mesh = meshes_[size_t(at)];
        const MeshAnimSeq* anim = mesh.findAnim(look.animSequence);
        MeshPose pose = poseAtFraction(mesh, anim, look.animFrame, look.animLoop);
        // Tweening into it: from the frame it was showing to its first.
        if (const MeshAnimSeq* from = look.tweenFrom.empty() ? nullptr : mesh.findAnim(look.tweenFrom);
            from && anim && look.animFrame < 0) {
            const MeshPose old = poseAtFraction(mesh, from, look.tweenFromFrame, look.tweenFromLoop);
            pose = {old.lerp < 0.5f ? old.frame : old.next, pose.frame, tweenProgress(look, *anim)};
        }
        std::vector<gfx::TextureId> skins;
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < look.multiSkins.size() ? look.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = look.skin;
            if (!tex && k < mesh.textures.size())
                tex = ctx_.packages.resolve(*look.mesh.package, mesh.textures[k]);
            if (!tex) // no skin of its own: the actor's Texture (the transporter's strips)
                tex = look.texture;
            skins.push_back(textured_ ? textureOf(tex).first : 0);
        }
        play_->drawn(a);
        const gfx::Mat4 placement = meshPlacement(look.location, look.rotation, look.drawScale, look.prePivot);
        const MeshLight light = meshLightAt(look.location);
        if ((look.layers.empty() && look.adjust.empty() && look.faces.empty()) || !mesh.bones ||
            !drawLayered(look, at, placement, skins, light))
            drawMesh(*renderer_, gpuMeshes_[size_t(at)], placement, pose, skins, light, kExposure, look.style, &fog_);
    } else if ((look.drawType == kDrawSprite || look.drawType == kDrawVerticalSprite ||
                look.drawType == kDrawSpriteAnimOnce) && look.texture) {
        const auto [texture, size] = textureOf(look.texture);
        if (!texture)
            return;
        play_->drawn(a);
        SpriteDraw d;
        d.texture = texture;
        d.at = toWorld(Vec3f{look.location[0] - look.prePivot[0], look.location[1] - look.prePivot[1],
                             look.location[2] - look.prePivot[2]});
        d.width = float(size[0]) * look.drawScale;
        d.height = float(size[1]) * look.drawScale;
        d.style = look.style;
        d.vertical = look.drawType == kDrawVerticalSprite;
        const float glow = look.scaleGlow * kExposure;
        d.color = {glow, glow, glow};
        sprites.push_back(d);
    }
}

// An actor's copies: its mesh posed as it is (its animation now) in each
// one's place, turned, scaled by DrawScale and along its own axes by
// AxisScale (along its axes before RotOrigin), in its style, as bright as its ScaleGlow (where it's lit, or
// wholly if the actor's unlit); or, for a sprite, its texture (the copy's
// own, else the actor's) facing the camera.
void MapViewer::drawCopies(VmObject* a, const LevelPlay::Look& look, const std::vector<LevelPlay::Copy>& copies,
                           std::vector<SpriteDraw>& sprites) {
    constexpr int kDrawSprite = 1, kDrawMesh = 2, kDrawVerticalSprite = 5, kDrawSpriteAnimOnce = 7;
    play_->drawn(a);
    if (look.drawType == kDrawMesh && look.mesh) {
        const int at = liveMesh(look.mesh, look.animSequence);
        if (at < 0)
            return;
        const Mesh& mesh = meshes_[size_t(at)];
        std::vector<gfx::TextureId> skins;
        for (size_t k = 0; k < std::max<size_t>(mesh.textures.size(), 8); ++k) {
            ObjectRef tex = k < look.multiSkins.size() ? look.multiSkins[k] : ObjectRef{};
            if (!tex)
                tex = look.skin;
            if (!tex && k < mesh.textures.size())
                tex = ctx_.packages.resolve(*look.mesh.package, mesh.textures[k]);
            if (!tex) // no skin of its own: the actor's Texture (the transporter's strips)
                tex = look.texture;
            skins.push_back(textured_ ? textureOf(tex).first : 0);
        }
        const MeshPose pose = poseAtFraction(mesh, mesh.findAnim(look.animSequence), look.animFrame, look.animLoop);
        for (const LevelPlay::Copy& c : copies) {
            const gfx::Mat4 placement =
                gfx::multiply(meshPlacement(c.location, c.rotation, c.drawScale, look.prePivot),
                              meshAxisScaling(mesh, c.axisScale));
            MeshLight light = look.unlit ? MeshLight{{1, 1, 1}, {}, {0, 0, 1}} : meshLightAt(c.location);
            for (size_t k = 0; k < 3; ++k) {
                light.ambient[k] *= c.scaleGlow;
                light.directed[k] *= c.scaleGlow;
            }
            drawMesh(*renderer_, gpuMeshes_[size_t(at)], placement, pose, skins, light, kExposure, c.style, &fog_);
        }
    } else if (look.drawType == kDrawSprite || look.drawType == kDrawVerticalSprite ||
               look.drawType == kDrawSpriteAnimOnce) {
        for (const LevelPlay::Copy& c : copies) {
            const auto [texture, size] = textureOf(c.texture ? c.texture : look.texture);
            if (!texture)
                continue;
            SpriteDraw d;
            d.texture = texture;
            d.at = toWorld(c.location);
            d.width = float(size[0]) * c.drawScale;
            d.height = float(size[1]) * c.drawScale;
            d.style = c.style;
            d.vertical = look.drawType == kDrawVerticalSprite;
            const float glow = c.scaleGlow * kExposure;
            d.color = {glow, glow, glow};
            sprites.push_back(d);
        }
    }
}

// The decals stuck to the world: their texture flat on the surface, its
// size by their scale, multiplying what's under it (grey leaves it as it
// is: marks, scorches, footprints).
void MapViewer::addDecals(std::vector<SpriteDraw>& sprites) {
    for (const ScriptLevel::Decal& d : play_->decals()) {
        const auto [texture, size] = textureOf(d.texture);
        if (!texture)
            continue;
        const std::array<float, 3>& n = d.normal;
        const std::array<float, 3>& u = d.up;
        const Vec3f right{u[1] * n[2] - u[2] * n[1], u[2] * n[0] - u[0] * n[2], u[0] * n[1] - u[1] * n[0]};
        SpriteDraw s;
        s.texture = texture;
        s.at = toWorld(Vec3f{d.at[0] + n[0] * 0.5f, d.at[1] + n[1] * 0.5f, d.at[2] + n[2] * 0.5f});
        s.width = float(size[0]) * d.scale;
        s.height = float(size[1]) * d.scale;
        s.style = kStyleModulated;
        s.oriented = true;
        s.right = toWorld(right);
        s.up = toWorld(Vec3f{u[0], u[1], u[2]});
        sprites.push_back(s);
    }
}

// How far through its tween into its animation an actor is (0 at the
// start, 1 there): AnimFrame rises from -1/frames to 0.
float MapViewer::tweenProgress(const LevelPlay::Look& look, const MeshAnimSeq& anim) {
    return std::clamp(1 + look.animFrame * float(std::max(1, anim.numFrames)), 0.0f, 1.0f);
}

// A mesh with layered animations playing on parts of it, its bones bent
// by its scripts, or its face moved: posed now from its own animation and theirs, uploaded as one frame (kept until the next
// frame is drawn) and drawn. False if it can't be (no skeleton, an
// animation it hasn't): drawn as its own animation alone then.
bool MapViewer::drawLayered(const LevelPlay::Look& look, int at, const gfx::Mat4& placement,
                            const std::vector<gfx::TextureId>& skins, const MeshLight& light) {
    auto it = posers_.find({look.mesh.package, look.mesh.exportIndex});
    if (it == posers_.end()) {
        Poser p;
        p.poser = LayeredPoser::open(*look.mesh.package, look.mesh.exportIndex, ctx_.sets());
        if (p.poser) {
            // Where its skeleton's axes go as it's drawn.
            const Mesh& m = meshes_[size_t(at)];
            const Vec3f zero = placeMeshVertex(m, m.origin, ctx_.meshTurn, ctx_.meshYawQuarters);
            std::array<Vec3f, 3> axes{};
            for (size_t k = 0; k < 3; ++k) {
                Vec3f step = m.origin;
                step[k] += m.scale[k] != 0 ? 1 / m.scale[k] : 0;
                const Vec3f to = placeMeshVertex(m, step, ctx_.meshTurn, ctx_.meshYawQuarters);
                axes[k] = {to[0] - zero[0], to[1] - zero[1], to[2] - zero[2]};
            }
            p.poser->setPlacement(axes);
        }
        it = posers_.emplace(std::make_pair(look.mesh.package, look.mesh.exportIndex), std::move(p)).first;
    }
    Poser& p = it->second;
    if (!p.poser)
        return false;
    std::vector<LayeredPoser::Layer> layers;
    for (const LevelPlay::Look::Layer& l : look.layers) {
        auto part = p.parts.find({l.bone, l.rest});
        if (part == p.parts.end())
            part = p.parts.emplace(std::make_pair(l.bone, l.rest), p.poser->part(l.bone, l.rest)).first;
        if (!part->second.empty())
            layers.push_back({l.anim, l.frame, &part->second, l.weight, l.clear});
    }
    std::vector<LayeredPoser::Face> faces;
    if (p.poser->hasFace())
        for (const LevelPlay::Look::Face& f : look.faces)
            faces.push_back({f.anim, f.seconds, f.weight});
    std::vector<Vec3f> verts;
    if ((layers.empty() && look.adjust.empty() && faces.empty()) ||
        !p.poser->pose(look.animSequence, look.animFrame, layers, verts, &look.adjust, &faces))
        return false;
    // Tweening into its animation: from the pose it was in.
    const Mesh& own = meshes_[size_t(at)];
    if (const MeshAnimSeq* anim = own.findAnim(look.animSequence); anim && look.animFrame < 0 && !look.tweenFrom.empty()) {
        std::vector<Vec3f> old;
        if (p.poser->pose(look.tweenFrom, look.tweenFromFrame, layers, old, &look.adjust, &faces) &&
            old.size() == verts.size()) {
            const float t = tweenProgress(look, *anim);
            for (size_t v = 0; v < verts.size(); ++v)
                for (size_t k = 0; k < 3; ++k)
                    verts[v][k] = old[v][k] + (verts[v][k] - old[v][k]) * t;
        }
    }
    // One frame of the mesh: its vertices swapped for the posed ones while
    // it's uploaded.
    Mesh& mesh = meshes_[size_t(at)];
    if (verts.size() != size_t(mesh.frameVerts))
        return false;
    std::swap(mesh.verts, verts);
    const int32_t frames = mesh.animFrames;
    mesh.animFrames = 1;
    GpuMesh gpu;
    auto topology = posedTopology_.find(at);
    if (topology == posedTopology_.end()) {
        MeshTopology t;
        buildMeshTopology(mesh, t); // none: the upload below fails as uploadMesh's would
        topology = posedTopology_.emplace(at, std::move(t)).first;
    }
    const Error err = topology->second.indices.empty()
                          ? Error::fail("the mesh has no visible triangles")
                          : uploadMesh(*renderer_, mesh, topology->second, gpu, ctx_.meshTurn, ctx_.meshYawQuarters);
    mesh.animFrames = frames;
    std::swap(mesh.verts, verts);
    if (err)
        return false;
    posedMeshes_.push_back(gpu);
    drawMesh(*renderer_, gpu, placement, MeshPose{}, skins, light, kExposure, look.style, &fog_);
    return true;
}

// A line of how the game's going (the cut scene's bars are drawn under
// the info, drawOverlay).
void MapViewer::drawPlayHud() {
    char line[200];
    const std::string title = play_->title();
    std::snprintf(line, sizeof line, "%s%s%s  health %d  %s  %.0f s played%s%s", title.c_str(), title.empty() ? "" : "  ",
                  play_->player()->cls->name.c_str(),
                  play_->health(), play_->playerState().c_str(), double(play_->time()),
                  view_.letterbox ? "  (cut scene)" : "",
                  play_->behind() > 1 ? ("  " + std::to_string(int(play_->behind())) + " s behind real time").c_str() : "");
    const int y = kFrameH - font::kLineHeight - 4;
    const int width = std::min(kFrameW, int(font::length(line)) * font::kAdvance + 8);
    // With the game's own HUD drawn, between its panels at the bottom
    // corners, and its objectives and subtitles are its own.
    const bool gameHud = !play_->level().hud().tiles.empty();
    const int left = gameHud ? std::max(0, (kFrameW - width) / 2) : 0;
    for (int yy = y - 2; yy < kFrameH; ++yy) {
        uint8_t* p = overlay_.row(yy);
        for (int x = left; x < std::min(kFrameW, left + width); ++x)
            p[x * 4 + 3] = std::max<uint8_t>(p[x * 4 + 3], 150);
    }
    font::drawText(overlay_, left + 4, y, line, Color{230, 230, 230});
    if (gameHud)
        return;
    // The objectives active now, above it.
    int above = y - 2;
    const std::vector<std::string> objectives = play_->activeObjectives();
    if (!objectives.empty()) {
        std::string list = "Objectives:";
        for (const std::string& o : objectives)
            list += (list.size() > 11 ? "; " : " ") + o;
        const size_t columns = size_t(kFrameW - 8) / size_t(font::kAdvance);
        if (list.size() > columns)
            list = list.substr(0, columns - 3) + "...";
        above -= font::kLineHeight + 2;
        const int w = std::min(kFrameW, int(font::length(list)) * font::kAdvance + 8);
        for (int yy = above - 1; yy < above + font::kLineHeight; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = std::max<uint8_t>(p[x * 4 + 3], 150);
        }
        font::drawText(overlay_, 4, above, list.c_str(), Color{160, 200, 255});
    }
    drawSubtitles(above - 6);
}

// The lines being said, above the status line: who says it (the main
// characters by name, the rest by their code) and the line, wrapped.
void MapViewer::drawSubtitles(int bottom) {
    static const std::map<std::string, std::string> names = {
        {"KRA", "Kira"}, {"SKO", "Sisko"}, {"WRF", "Worf"},   {"OBN", "O'Brien"}, {"DAX", "Dax"},
        {"BSR", "Bashir"}, {"ODO", "Odo"}, {"OBK", "Obanak"}, {"GRK", "Garak"},   {"CPU", "Computer"},
    };
    const size_t columns = size_t(kFrameW - 16) / size_t(font::kAdvance);
    std::vector<std::string> rows;
    for (const ScriptLevel::Subtitle& s : play_->level().subtitles()) {
        auto it = names.find(s.speaker);
        std::string text = (it != names.end() ? it->second : s.speaker) + ": " + s.text;
        while (!text.empty()) {
            size_t cut = text.size() <= columns ? text.size() : text.rfind(' ', columns);
            if (cut == std::string::npos || cut == 0)
                cut = std::min(columns, text.size());
            rows.push_back(text.substr(0, cut));
            text.erase(0, std::min(text.size(), cut + 1));
        }
    }
    int y = bottom - int(rows.size()) * font::kLineHeight;
    for (const std::string& row : rows) {
        const int width = int(font::length(row.c_str())) * font::kAdvance + 8;
        const int x = std::max(0, (kFrameW - width) / 2);
        for (int yy = std::max(0, y - 1); yy < std::min(kFrameH, y + font::kLineHeight); ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int xx = x; xx < std::min(kFrameW, x + width); ++xx)
                p[xx * 4 + 3] = std::max<uint8_t>(p[xx * 4 + 3], 170);
        }
        font::drawText(overlay_, x + 4, y, row.c_str(), Color{255, 240, 160});
        y += font::kLineHeight;
    }
}

} // namespace reluva::unreal
