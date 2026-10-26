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
#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
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

enum Held { Forward, Back, Left, Right, Up, Down, Fast };

// Unreal's axes are left-handed (X forward, Y right, Z up); the renderer's
// are right-handed, so Y is negated on the way in.
gfx::Vec3 toWorld(const Vec3f& v) { return {v[0], -v[1], v[2]}; }

// The surface flags that change how a polygon is drawn, for grouping.
constexpr uint32_t kDrawFlags = polyflag::Masked | polyflag::Translucent | polyflag::Modulated |
                                polyflag::FakeBackdrop | polyflag::Unlit;

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
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist >= l.radius || dist <= 0)
            continue;
        float amount = 1 - dist / l.radius;
        if (l.incidence)
            amount *= std::max(0.0f, (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / dist);
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
    for (const GpuMesh& g : gpuMeshes_)
        if (g.mesh)
            renderer_->destroyMesh(g.mesh);
    gpuMeshes_.clear();
    meshes_.clear();
    meshActors_.clear();
    jump_ = -1;
    sky_.reset();
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
    // Each texture uploaded once; its size divides texture coordinates.
    std::map<std::pair<const Package*, int>, std::pair<gfx::TextureId, std::array<int, 2>>> loaded;
    int missing = 0;
    std::string firstMissing;
    auto textureOf = [&](const ObjectRef& tex) -> std::pair<gfx::TextureId, std::array<int, 2>> {
        if (!tex)
            return {0, {64, 64}};
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
        return it->second;
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
        const auto& [isBlended, textureRef, flags, page] = key;
        Batch batch;
        batch.polyFlags = flags;
        batch.lightmap = page >= 0 ? pages[size_t(page)] : 0;
        batch.firstIndex = uint32_t(indices.size());
        const auto [textureId, size] = textureFor(*pkg, textureRef);
        batch.texture = textureId;
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
    const std::vector<MeshActor> meshActors = findMeshActors(ctx_.packages, *pkg, defaults, &meshStats);
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
        // The lights reaching where it stands: some all round, the rest
        // from their average direction.
        Vec3f total{}, toward{};
        for (const LightSource& l : lights.unique) {
            const Vec3f dv{l.position[0] - a.location[0], l.position[1] - a.location[1], l.position[2] - a.location[2]};
            const float dist = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
            if (dist >= l.radius || dist <= 0)
                continue;
            const float amount = 1 - dist / l.radius;
            for (size_t k = 0; k < 3; ++k) {
                total[k] += l.color[k] * amount;
                toward[k] += dv[k] / dist * amount;
            }
        }
        for (size_t k = 0; k < 3; ++k) {
            d.light.ambient[k] = std::min(kMaxLight, 0.15f + 0.5f * total[k]);
            d.light.directed[k] = std::min(kMaxLight, 0.7f * total[k]);
        }
        const float len = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
        d.light.direction = len > 0 ? toWorld(Vec3f{toward[0] / len, toward[1] / len, toward[2] / len})
                                    : gfx::Vec3{0, 0, 1};
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
    info_.push_back("Not drawn yet: sprites, lights' effects (flicker, pulse...), movers' own lighting");
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
        else if (ev.key == '-')
            displayGamma_ = std::max(0.5f, displayGamma_ - 0.05f);
        else if (ev.key == '=')
            displayGamma_ = std::min(2.5f, displayGamma_ + 0.05f);
        else if (ev.key == 'k')
            showSky_ = !showSky_;
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
        else if (ev.key == 'o')
            for (MoverDraw& m : movers_)
                if (m.key == m.target && m.mover.numKeys > 1) {
                    m.target = (m.key + 1) % m.mover.numKeys;
                    m.t = 0;
                }
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
    // Movers between keyframes, at their own pace.
    for (MoverDraw& m : movers_)
        if (m.key != m.target) {
            m.t += dt / std::max(0.05f, m.mover.moveTime);
            if (m.t >= 1) {
                m.key = m.target;
                m.t = 0;
            }
        }

    // The renderer raises colours to 1 / its gamma.
    renderer_->setGamma(1 / displayGamma_);
    const gfx::Mat4 proj = gfx::perspective(fovY, aspect, 4, 65536);
    const bool sky = sky_ && showSky_;
    if (sky) {
        // The sky first: the level seen from the sky zone, looking the
        // same way; then the view, from scratch, with the backdrop
        // surfaces leaving it showing.
        const gfx::Vec3 eye = toWorld(*sky_);
        renderer_->beginFrame(gfx::multiply(proj, gfx::lookAlong(eye, forward, {0, 0, 1})), eye, {20, 24, 40});
        drawBatches(batches_, mesh_, gfx::identity(), true);
        renderer_->clearDepth();
        renderer_->setView(viewProj, pos_);
    } else {
        renderer_->beginFrame(viewProj, pos_, {20, 24, 40});
    }
    // Backdrop surfaces: with the sky, only their depth, hiding what's
    // behind them; without it, a plain colour.
    for (const Batch& b : batches_) {
        if (!(b.polyFlags & polyflag::FakeBackdrop))
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
    drawBatches(batches_, mesh_, gfx::identity(), true);
    for (const MoverDraw& m : movers_)
        drawBatches(m.batches, moverMesh_, moverTransform(m), false);
    if (animate_)
        animTime_ += dt;
    for (const MeshDraw& d : meshActors_) {
        const Mesh& mesh = meshes_[d.mesh];
        const MeshAnimSeq* anim = d.anim >= 0 ? &mesh.anims[size_t(d.anim)] : nullptr;
        drawMesh(*renderer_, gpuMeshes_[d.mesh], d.placement, poseAt(mesh, anim, animTime_, d.startFraction),
                 textured_ ? d.textures : std::vector<gfx::TextureId>{}, d.light, kExposure);
    }
    drawOverlay(nowMs);
    renderer_->endFrame(&overlay_);
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
    // location + postScale * (rotation * (mainScale * (v - prePivot))),
    // then Y negated into the renderer's axes.
    const gfx::Mat4 flip = scaling({1, -1, 1});
    gfx::Mat4 m4 = gfx::multiply(flip, translation(location));
    m4 = gfx::multiply(m4, scaling(m.postScale));
    m4 = gfx::multiply(m4, rotation(rot));
    m4 = gfx::multiply(m4, scaling(m.mainScale));
    return gfx::multiply(m4, translation({-m.prePivot[0], -m.prePivot[1], -m.prePivot[2]}));
}

void MapViewer::drawBatches(const std::vector<Batch>& batches, gfx::MeshId mesh, const gfx::Mat4& model,
                            bool levelLit) {
    auto lit = [&](const Batch& b) {
        return levelLit && lightmaps_ && b.lightmap && !blended(b.polyFlags) &&
               !(b.polyFlags & (polyflag::FakeBackdrop | polyflag::Unlit));
    };
    for (const Batch& b : batches) {
        if (b.polyFlags & polyflag::FakeBackdrop)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.model = model;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.state.cull = gfx::Cull::None;
        call.texture = textured_ ? b.texture : 0;
        // With a lightmap the second pass lights it; else the vertex colours
        // (shading by facing, or a mover's light).
        call.rgbSource = lit(b) ? gfx::ColorSource::Constant : gfx::ColorSource::Vertex;
        if (!(b.polyFlags & polyflag::Modulated)) // those multiply what's there, so stay as they are
            call.color = {kExposure, kExposure, kExposure, 1};
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
    }
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
    char where[200];
    // Shown in Unreal's own coordinates.
    std::snprintf(where, sizeof where, "at %.0f %.0f %.0f, %.0f fps; lightmaps %s, brightness x%.2f, display gamma %.2f",
                  pos_[0], -pos_[1], pos_[2], fps_, !hasLightmaps_ ? "none" : lightmaps_ ? "on" : "off", lightScale_,
                  displayGamma_);
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
            p[x * 4 + 3] = 150;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                       i == errorLine ? Color{255, 120, 120} : Color{230, 230, 230});
}

} // namespace reluva::unreal
