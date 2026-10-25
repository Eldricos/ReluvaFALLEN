// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/scene_drawer.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3 {

namespace {
constexpr float kPi = 3.14159265358979f;
}

int32_t readInt(const std::vector<uint8_t>& bytes, size_t at) {
    int32_t v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, &bytes[at], 4);
    return v;
}

float readFloat(const std::vector<uint8_t>& bytes, size_t at) { return Qvm::asFloat(readInt(bytes, at)); }

gfx::Vec3 readVec(const std::vector<uint8_t>& bytes, size_t at) {
    return {readFloat(bytes, at), readFloat(bytes, at + 4), readFloat(bytes, at + 8)};
}

void SceneDrawer::reset() {
    modelIds_.clear();
    pictureMaterials_.clear();
    effectMaterials_.clear();
    placed_.clear();
    effects_.clear();
    lights_.clear();
    unloadable_.clear();
    otherEntities_ = 0;
}

const std::string& SceneDrawer::name(const HandleTable* table, int32_t handle) {
    static const std::string none;
    return table ? table->name(handle) : none;
}

int SceneDrawer::material(std::vector<int>& cache, int32_t handle, Lighting lighting) {
    if (handle <= 0)
        return -1;
    if (size_t(handle) >= cache.size())
        cache.resize(size_t(handle) + 1, -2);
    int& m = cache[size_t(handle)];
    if (m == -2)
        m = shaders_.material(name(handles_.shaders, handle), lighting);
    return m;
}

void SceneDrawer::drawPictures(const std::vector<eliteforce::Picture>& pictures, size_t first, size_t last,
                               int screenW, int screenH, float time) {
    last = std::min(last, pictures.size());
    if (first >= last)
        return;
    // Every picture a quad in one mesh, in the program's screen.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve((last - first) * 4);
    indices.reserve((last - first) * 6);
    for (size_t n = first; n < last; ++n) {
        const auto& p = pictures[n];
        const auto base = uint32_t(vertices.size());
        uint8_t rgba[4];
        for (int i = 0; i < 4; ++i)
            rgba[i] = uint8_t(std::clamp(p.color[size_t(i)], 0.0f, 1.0f) * 255.0f + 0.5f);
        const float xs[4] = {p.x, p.x + p.w, p.x + p.w, p.x};
        const float ys[4] = {p.y, p.y, p.y + p.h, p.y + p.h};
        const float ss[4] = {p.s1, p.s2, p.s2, p.s1};
        const float ts[4] = {p.t1, p.t1, p.t2, p.t2};
        for (int i = 0; i < 4; ++i) {
            gfx::Vertex v{};
            v.pos[0] = xs[i];
            v.pos[1] = ys[i];
            v.uv[0] = ss[i];
            v.uv[1] = ts[i];
            v.normal[2] = 1;
            std::memcpy(v.color, rgba, 4);
            vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(base + i);
    }
    gfx::MeshId mesh = 0;
    if (r_.createMesh(vertices, indices, mesh) || !mesh)
        return;
    // The screen to clip space: x right, y down, as the programs count.
    gfx::Mat4 ortho{};
    ortho[0] = 2.0f / float(screenW);
    ortho[5] = -2.0f / float(screenH);
    ortho[10] = 1;
    ortho[12] = -1;
    ortho[13] = 1;
    ortho[15] = 1;
    r_.setViewport(0, 0, 0, 0);
    r_.setDepthRange(1);
    r_.setView(ortho, {0, 0, 0});
    for (size_t n = first; n < last; ++n) {
        const int m = material(pictureMaterials_, pictures[n].shader, Lighting::TwoD);
        if (m < 0 || shaders_.get(m).skip)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.firstIndex = uint32_t((n - first) * 6);
        call.indexCount = 6;
        shaders_.draw(m, call, 0, time);
    }
    r_.destroyMesh(mesh);
}

void SceneDrawer::drawView(const eliteforce::Scene& scene, const eliteforce::View& view, int screenW, int screenH,
                           float time, bool firstView, WorldRenderer* world, const LightGrid* grid,
                           bool dynamicLights) {
    const std::vector<uint8_t>& refdef = view.refdef;
    mainRefdef_ = refdef;
    const bool withWorld = world && !(readInt(refdef, 76) & kNoWorldModel);
    // refdef_t: fov y at 20, the eye at 24, the axes from 36: forward,
    // left, up.
    eye_ = readVec(refdef, 24);
    const gfx::Vec3 forward = readVec(refdef, 36), up = readVec(refdef, 60);
    const float fovY = std::clamp(readFloat(refdef, 20), 1.0f, 170.0f) * kPi / 180.0f;
    // Where on the screen (x, y, width, height in the program's screen):
    // smaller with a smaller screen size setting, or a box in a menu.
    const int frameW = r_.width(), frameH = r_.height();
    int vx = 0, vy = 0, vw = frameW, vh = frameH;
    if (const int32_t w = readInt(refdef, 8), h = readInt(refdef, 12); w > 0 && h > 0 && w <= screenW && h <= screenH) {
        vx = readInt(refdef, 0) * frameW / screenW;
        vy = readInt(refdef, 4) * frameH / screenH;
        vw = w * frameW / screenW;
        vh = h * frameH / screenH;
    }
    const float aspect = float(vw) / float(std::max(vh, 1));
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(fovY, aspect, 4, 32768), gfx::lookAlong(eye_, forward, up));
    if (!firstView)
        r_.clearDepth();
    r_.setView(viewProj, eye_);
    r_.setViewport(vx, vy, vw, vh);
    // The view's dynamic lights (shots, explosions): on the level's solid
    // surfaces, and in the models' lighting.
    lights_.clear();
    for (size_t i = view.firstLight; dynamicLights && i < view.firstLight + view.lightCount; ++i)
        if (i < scene.lights.size() && scene.lights[i].radius > 0)
            lights_.push_back({scene.lights[i].origin, scene.lights[i].radius, scene.lights[i].color});
    // Without the level, models are lit as the original lights them then:
    // evenly, from above (an empty light grid's answer).
    static const LightGrid kNoLevel;
    placeEntities(scene, view, withWorld && grid ? *grid : kNoLevel, time);
    if (withWorld) {
        // Only the level's surfaces that can be seen from the eye, past the
        // doors the area mask (refdef_t's last 32 bytes, from 80) closes.
        std::array<uint8_t, 32> areaMask{};
        if (refdef.size() >= 112)
            std::copy(refdef.begin() + 80, refdef.begin() + 112, areaMask.begin());
        world->setView(0, eye_, areaMask, viewProj);
        world->drawSky(time);
        // A mirror or portal in view: what's seen through it first, inside
        // its shape; then the rest over it, past its depth.
        if (r_.hasStencil())
            drawPortalView(*world, areaMask, readVec(refdef, 48), fovY, aspect, viewProj, time);
    }
    drawLayers(time, world && withWorld ? world : nullptr, false);
}

void SceneDrawer::drawLayers(float time, WorldRenderer* world, bool throughPortal) {
    // The level's solid surfaces and models, then its translucent ones and
    // the models' translucent parts, then effects. Through a mirror or
    // portal, the player's own body (third person) is seen and their
    // weapon (first person) isn't, as the original draws them.
    constexpr float kLastOpaque = 5;
    auto shown = [&](const Placed& p) { return throughPortal ? !p.firstPerson : !p.thirdPerson; };
    if (world) {
        world->drawSurfaces(time, 0, kLastOpaque, true);
        for (const auto& [model, transform] : brushes_)
            world->drawModel(model, transform, time, 0, kLastOpaque);
        world->drawDynamicLights(lights_);
    }
    for (const Placed& p : placed_) {
        if (!shown(p))
            continue;
        r_.setDepthRange(p.depthHack && !throughPortal ? refent::kDepthHackRange : 1);
        models_.draw(p.model, p.frame, p.transform, p.light, time - p.shaderTime, p.skin, 0, kLastOpaque, p.shader,
                     &p.color, p.oldFrame, p.backLerp);
    }
    r_.setDepthRange(1);
    if (world) {
        world->drawSurfaces(time, kLastOpaque + 0.5f, 1e9f, true);
        for (const auto& [model, transform] : brushes_)
            world->drawModel(model, transform, time, kLastOpaque + 0.5f);
    }
    for (const Placed& p : placed_) {
        if (!shown(p))
            continue;
        r_.setDepthRange(p.depthHack && !throughPortal ? refent::kDepthHackRange : 1);
        models_.draw(p.model, p.frame, p.transform, p.light, time - p.shaderTime, p.skin, kLastOpaque + 0.5f, 1e9f,
                     p.shader, &p.color, p.oldFrame, p.backLerp);
    }
    drawEffects(time);
    r_.setDepthRange(1);
}

namespace {

using V3 = gfx::Vec3;
V3 sub3(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot3(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross3(const V3& a, const V3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V3 scale3(const V3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
V3 add3(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

// `v` turned `degrees` about the unit axis `axis`.
V3 rotateAbout(const V3& v, const V3& axis, float degrees) {
    const float r = degrees * kPi / 180, c = std::cos(r), s = std::sin(r);
    return add3(add3(scale3(v, c), scale3(cross3(axis, v), s)), scale3(axis, dot3(axis, v) * (1 - c)));
}

// A 4x4 matrix's inverse (column-major), by Gauss-Jordan elimination.
gfx::Mat4 inverse(const gfx::Mat4& m) {
    double a[4][8] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            a[r][c] = m[size_t(c * 4 + r)];
        a[r][4 + r] = 1;
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c]))
                pivot = r;
        std::swap(a[c], a[pivot]);
        const double p = a[c][c] != 0 ? a[c][c] : 1e-12;
        for (double& v : a[c])
            v /= p;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                const double f = a[r][c];
                for (int k = 0; k < 8; ++k)
                    a[r][k] -= f * a[c][k];
            }
    }
    gfx::Mat4 out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[size_t(c * 4 + r)] = float(a[r][4 + c]);
    return out;
}

} // namespace

void SceneDrawer::drawPortalView(WorldRenderer& world, const std::array<uint8_t, 32>& areaMask, const gfx::Vec3& left,
                                 float fovY, float aspect, const gfx::Mat4& mainViewProj, float time) {
    // As the original: the nearest mirror or portal surface in view with a
    // portal entity within 64 units of its plane; a portal (not a mirror)
    // only within its shader's range.
    const auto portals = world.visiblePortals(eye_);
    const PortalEntity* entity = nullptr;
    WorldRenderer::Portal surface;
    for (const auto& p : portals) {
        for (const PortalEntity& e : portalEntities_) {
            const float d = dot3(e.origin, p.normal) - p.dist;
            if (d > 64 || d < -64)
                continue;
            const bool mirror = e.origin == e.oldOrigin;
            float d2 = 0;
            for (size_t k = 0; k < 3; ++k) {
                const float c = std::clamp(eye_[k], p.mins[k], p.maxs[k]) - eye_[k];
                d2 += c * c;
            }
            if (!mirror && d2 > p.range * p.range)
                continue;
            entity = &e;
            surface = p;
            break;
        }
        if (entity)
            break;
    }
    if (!entity)
        return;
    // The surface's axes (its normal, facing the eye, first) and where the
    // view comes out: a mirror's own, turned back; a portal's camera.
    std::array<V3, 3> sAxis;
    sAxis[0] = surface.normal;
    {
        // Any direction across the normal.
        const V3 helper = std::fabs(sAxis[0][2]) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0};
        sAxis[1] = cross3(helper, sAxis[0]);
        const float l = std::sqrt(dot3(sAxis[1], sAxis[1]));
        sAxis[1] = scale3(sAxis[1], 1 / l);
        sAxis[2] = cross3(sAxis[0], sAxis[1]);
    }
    V3 sOrigin;
    std::array<V3, 3> cAxis;
    V3 cOrigin;
    if (entity->origin == entity->oldOrigin) {
        sOrigin = scale3(sAxis[0], surface.dist);
        cOrigin = sOrigin;
        cAxis = {scale3(sAxis[0], -1), sAxis[1], sAxis[2]};
    } else {
        const float d = dot3(entity->origin, sAxis[0]) - surface.dist;
        sOrigin = sub3(entity->origin, scale3(sAxis[0], d));
        cOrigin = entity->oldOrigin;
        cAxis = {scale3(entity->axes[0], -1), scale3(entity->axes[1], -1), entity->axes[2]};
        // Turning, as its frames and skin number say.
        float turn = 0;
        if (entity->oldFrame)
            turn = entity->frame ? time * float(entity->frame)
                                 : float(entity->skinNum) + std::sin(time * 1000 * 0.003f) * 4;
        else
            turn = float(entity->skinNum);
        if (turn != 0) {
            cAxis[1] = rotateAbout(cAxis[1], cAxis[0], turn);
            cAxis[2] = cross3(cAxis[0], cAxis[1]);
        }
    }
    auto mirrorVector = [&](const V3& v) {
        V3 out{};
        for (size_t i = 0; i < 3; ++i)
            out = add3(out, scale3(cAxis[i], dot3(v, sAxis[i])));
        return out;
    };
    auto mirrorPoint = [&](const V3& p) { return add3(mirrorVector(sub3(p, sOrigin)), cOrigin); };
    // The main view's placement taken through it.
    const V3 forward = readVec(mainRefdef_, 36), up = readVec(mainRefdef_, 60);
    const V3 eye = mirrorPoint(eye_);
    const V3 f2 = mirrorVector(forward), l2 = mirrorVector(left), u2 = mirrorVector(up);
    const bool mirrored = dot3(cross3(f2, l2), u2) < 0; // turned inside out: drawn flipped
    const gfx::Mat4 view = gfx::lookAlong(eye, f2, u2);
    gfx::Mat4 proj = gfx::perspective(fovY, aspect, 4, 32768);
    if (mirrored)
        for (int c = 0; c < 4; ++c)
            proj[size_t(c * 4)] = -proj[size_t(c * 4)];
    // Nothing between the camera and the surface: the near plane laid on
    // the surface's plane (as seen from the camera, facing away from it),
    // so what's behind it is clipped (Lengyel's oblique near plane, for
    // depth 0 to 1).
    {
        const V3 n = scale3(cAxis[0], -1);
        const float d = dot3(cOrigin, n);
        const gfx::Mat4 viewInverse = inverse(view);
        // The plane in the view's own space: (view^-1)^T times (n, -d).
        std::array<float, 4> plane{};
        const std::array<float, 4> world{n[0], n[1], n[2], -d};
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
                plane[size_t(j)] += viewInverse[size_t(j * 4 + i)] * world[size_t(i)];
        const gfx::Mat4 projInverse = inverse(proj);
        const std::array<float, 4> corner{plane[0] > 0 ? 1.0f : -1.0f, plane[1] > 0 ? 1.0f : -1.0f, 1, 1};
        std::array<float, 4> q{};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                q[size_t(r)] += projInverse[size_t(c * 4 + r)] * corner[size_t(c)];
        const float s = plane[0] * q[0] + plane[1] * q[1] + plane[2] * q[2] + plane[3] * q[3];
        if (std::fabs(s) > 1e-9f)
            for (int c = 0; c < 4; ++c)
                proj[size_t(c * 4 + 2)] = plane[size_t(c)] / s;
    }
    const gfx::Mat4 viewProj = gfx::multiply(proj, view);

    // Where the surface is on the screen, marked; the view through it
    // drawn only there (its own sky first), turned inside out for a mirror.
    gfx::DrawState shape;
    shape.src = gfx::Blend::Zero;
    shape.dst = gfx::Blend::One; // colours kept
    shape.depthTest = shape.depthWrite = false;
    shape.cull = gfx::Cull::None;
    r_.setStencilMode(gfx::Renderer3D::StencilMode::Mark);
    world.drawSurfaceShape(surface.surface, shape);
    r_.setStencilMode(gfx::Renderer3D::StencilMode::Inside);
    r_.setMirrored(mirrored);
    r_.setView(viewProj, eye);
    world.setView(1, entity->oldOrigin, areaMask, viewProj);
    world.drawSky(time);
    drawLayers(time, &world, true);
    // Back to the main view: the surface's depth written, so nothing
    // behind it draws over what's seen through it.
    r_.setMirrored(false);
    r_.setStencilMode(gfx::Renderer3D::StencilMode::None);
    r_.clearDepth();
    r_.setView(mainViewProj, eye_);
    world.setView(0, eye_, areaMask, mainViewProj);
    gfx::DrawState depth = shape;
    depth.depthTest = depth.depthWrite = true;
    world.drawSurfaceShape(surface.surface, depth);
    ++portalViews_;
}

std::array<float, 4> SceneDrawer::entityColor(const std::vector<uint8_t>& e) {
    // refEntity_t's colour: bytes r, g, b, a.
    std::array<float, 4> c{};
    for (size_t i = 0; i < 4; ++i)
        c[i] = (refent::Rgba + i < e.size() ? e[refent::Rgba + i] : 255) / 255.0f;
    return c;
}

float SceneDrawer::shaderTime(const std::vector<uint8_t>& e, float time) {
    // When the entity's shader animations start, in seconds (Quake III's
    // shaderTime): an explosion's from its first frame. Only a time that
    // has come is taken.
    const float t = readFloat(e, refent::ShaderTime);
    return t > 0 && t <= time + 1 ? t : 0;
}

void SceneDrawer::addEffect(const std::vector<uint8_t>& e, const gfx::Vec3& left, const gfx::Vec3& up, float time) {
    const int32_t type = readInt(e, refent::Type);
    const int32_t shader = readInt(e, refent::CustomShader);
    if (shader <= 0)
        return;
    using V = gfx::Vec3;
    auto add = [](const V& a, const V& b, float s) { return V{a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s}; };
    auto sub = [](const V& a, const V& b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto cross = [](const V& a, const V& b) {
        return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto normalized = [](V v) {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len > 1e-6f)
            for (float& c : v)
                c /= len;
        return v;
    };
    const auto first = uint32_t(effectIndices_.size());
    const int32_t rgba = readInt(e, refent::Rgba);
    auto vertex = [&](const V& pos, float s, float t) {
        gfx::Vertex v{};
        for (size_t k = 0; k < 3; ++k)
            v.pos[k] = pos[k];
        v.uv[0] = s;
        v.uv[1] = t;
        v.normal[2] = 1;
        std::memcpy(v.color, &rgba, 4); // bytes r, g, b, a
        effectVertices_.push_back(v);
        return uint32_t(effectVertices_.size() - 1);
    };
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        for (uint32_t i : {a, b, c, a, c, d})
            effectIndices_.push_back(i);
    };
    // A ribbon through `points`, facing the eye, as wide as `widths`
    // says at each; the texture across it in s and along it in t, to
    // `repeat` at the end.
    auto ribbon = [&](const std::vector<V>& points, const std::vector<float>& widths, float repeat) {
        uint32_t prevA = 0, prevB = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            const V along = sub(points[std::min(i + 1, points.size() - 1)], points[i ? i - 1 : 0]);
            const V side = normalized(cross(along, sub(eye_, points[i])));
            const float t = repeat * float(i) / float(points.size() - 1);
            const uint32_t a = vertex(add(points[i], side, widths[i] / 2), 0, t);
            const uint32_t b = vertex(add(points[i], side, -widths[i] / 2), 1, t);
            if (i)
                quad(prevA, a, b, prevB);
            prevA = a;
            prevB = b;
        }
    };
    const V origin = readVec(e, refent::Origin);
    if (type == refent::TypeSprite || type == refent::TypeQuad) {
        const float r = readFloat(e, refent::Radius);
        V l = left, u = up;
        if (type == refent::TypeQuad) {
            l = readVec(e, refent::Axes + 12);
            u = readVec(e, refent::Axes + 24);
        } else if (const float deg = readFloat(e, refent::Rotation); deg != 0) {
            const float c = std::cos(deg * kPi / 180), sn = std::sin(deg * kPi / 180);
            for (size_t i = 0; i < 3; ++i) {
                l[i] = left[i] * c + up[i] * sn;
                u[i] = up[i] * c - left[i] * sn;
            }
        }
        quad(vertex(add(add(origin, l, r), u, r), 0, 0), vertex(add(add(origin, l, -r), u, r), 1, 0),
             vertex(add(add(origin, l, -r), u, -r), 1, 1), vertex(add(add(origin, l, r), u, -r), 0, 1));
    } else if (type == refent::TypeLine || type == refent::TypeTaperedLine) {
        const float w0 = readFloat(e, refent::Width);
        const float w1 = type == refent::TypeTaperedLine ? readFloat(e, refent::EndWidth) : w0;
        const float repeat = readFloat(e, refent::Repeat) > 0 ? readFloat(e, refent::Repeat) : 1.0f;
        ribbon({origin, readVec(e, refent::OldOrigin)}, {w0, w1}, repeat);
    } else if (type == refent::TypeCurve) {
        // A cubic Bezier curve from the origin through two control points
        // to the old origin.
        const V p0 = origin, p1 = readVec(e, refent::Control1), p2 = readVec(e, refent::Control2),
                p3 = readVec(e, refent::OldOrigin);
        std::vector<V> points;
        constexpr int kSteps = 16;
        for (int i = 0; i <= kSteps; ++i) {
            const float t = float(i) / kSteps, m = 1 - t;
            V p{};
            for (size_t k = 0; k < 3; ++k)
                p[k] = m * m * m * p0[k] + 3 * m * m * t * p1[k] + 3 * m * t * t * p2[k] + t * t * t * p3[k];
            points.push_back(p);
        }
        ribbon(points, std::vector<float>(points.size(), readFloat(e, refent::Width)), 1);
    } else if (type == refent::TypeBolt) {
        // Electricity: a line broken into steps, each point pushed aside at
        // random by the jitter times the step's length; new each frame.
        const V end = readVec(e, refent::OldOrigin);
        const V along = sub(end, origin);
        const float len = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        const V dir = normalized(along);
        V side1 = normalized(cross(dir, V{0, 0, 1}));
        if (std::fabs(dir[2]) > 0.99f)
            side1 = V{1, 0, 0};
        const V side2 = cross(dir, side1);
        const int steps = std::clamp(int(len / 16), 2, 32);
        const float jitter = readFloat(e, refent::Jitter) * len / float(steps);
        std::vector<V> points;
        for (int i = 0; i <= steps; ++i) {
            V p = add(origin, along, float(i) / float(steps));
            if (i > 0 && i < steps) {
                const float r1 = float(effectRandom_() % 2001) / 1000.0f - 1;
                const float r2 = float(effectRandom_() % 2001) / 1000.0f - 1;
                p = add(add(p, side1, r1 * jitter), side2, r2 * jitter);
            }
            points.push_back(p);
        }
        const float scale = readFloat(e, refent::Repeat);
        ribbon(points, std::vector<float>(points.size(), readFloat(e, refent::Width)),
               scale > 0 ? len * scale / 16 : 1);
    } else if (type == refent::TypeCylinder) {
        // A ring or cone around the first axis: the radius at 132 at the
        // origin, the one at 136 a height (140) along the axis; the texture
        // round it (repeated as 144 says) and up it.
        const V axis = normalized(readVec(e, refent::Axes));
        const V a1 = normalized(readVec(e, refent::Axes + 12)), a2 = normalized(cross(axis, a1));
        const float r0 = readFloat(e, refent::Width), r1 = readFloat(e, refent::EndWidth);
        const float height = readFloat(e, refent::Repeat);
        const float around = readFloat(e, refent::CylinderRepeat) > 0 ? readFloat(e, refent::CylinderRepeat) : 1.0f;
        const V top = add(origin, axis, height);
        constexpr int kSides = 24;
        uint32_t prevA = 0, prevB = 0;
        for (int i = 0; i <= kSides; ++i) {
            const float ang = 2 * kPi * float(i) / kSides;
            const V radial =
                add(V{a1[0] * std::cos(ang), a1[1] * std::cos(ang), a1[2] * std::cos(ang)}, a2, std::sin(ang));
            const float s = around * float(i) / kSides;
            const uint32_t a = vertex(add(origin, radial, r0), s, 1), b = vertex(add(top, radial, r1), s, 0);
            if (i)
                quad(prevA, a, b, prevB);
            prevA = a;
            prevB = b;
        }
    } else {
        ++otherEntities_;
        return;
    }
    effects_.push_back({shader, first, uint32_t(effectIndices_.size()) - first, entityColor(e), shaderTime(e, time),
                        (readInt(e, refent::Flags) & refent::DepthHack) != 0});
}

void SceneDrawer::drawEffects(float time) {
    // This view's effects, in the order the program added them, over
    // everything else (they blend, as their shaders say).
    if (effects_.empty())
        return;
    gfx::MeshId mesh = 0;
    if (r_.createMesh(effectVertices_, effectIndices_, mesh) || !mesh)
        return;
    for (const Effect& fx : effects_) {
        const int m = material(effectMaterials_, fx.shader, Lighting::Effect);
        if (m < 0 || shaders_.get(m).skip)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.firstIndex = fx.firstIndex;
        call.indexCount = fx.indexCount;
        r_.setDepthRange(fx.depthHack ? refent::kDepthHackRange : 1);
        shaders_.draw(m, call, 0, time - fx.shaderTime, false, &fx.color);
    }
    r_.destroyMesh(mesh);
}

void SceneDrawer::placeEntities(const eliteforce::Scene& scene, const eliteforce::View& view, const LightGrid& grid,
                                float time) {
    // The view's entities: models where the program put them, and effects
    // as quads facing the eye (refdef_t's axes: left at 48, up at 60).
    placed_.clear();
    portalEntities_.clear();
    brushes_.clear();
    effects_.clear();
    effectVertices_.clear();
    effectIndices_.clear();
    otherEntities_ = 0;
    unloadable_.clear();
    const gfx::Vec3 left = readVec(view.refdef, 48), up = readVec(view.refdef, 60);
    forward_ = readVec(view.refdef, 36);
    focal_ = 1 / std::tan(std::clamp(readFloat(view.refdef, 20), 1.0f, 170.0f) * kPi / 360.0f);
    // Polygons (marks: scorches, holes, shadows): each vertex a position,
    // texture coordinates and a colour (Quake III's polyVert_t, 24 bytes),
    // drawn as a fan.
    for (size_t i = view.firstPoly; i < view.firstPoly + view.polyCount && i < scene.polys.size(); ++i) {
        const auto& poly = scene.polys[i];
        const size_t n = std::min(size_t(std::max(poly.vertexCount, 0)), poly.vertices.size() / 24);
        if (n < 3 || poly.shader <= 0)
            continue;
        const auto base = uint32_t(effectVertices_.size()), first = uint32_t(effectIndices_.size());
        for (size_t k = 0; k < n; ++k) {
            gfx::Vertex v{};
            std::memcpy(v.pos, &poly.vertices[k * 24], 12);
            std::memcpy(v.uv, &poly.vertices[k * 24 + 12], 8);
            std::memcpy(v.color, &poly.vertices[k * 24 + 20], 4);
            v.normal[2] = 1;
            effectVertices_.push_back(v);
        }
        for (uint32_t k = 1; k + 1 < n; ++k)
            for (uint32_t idx : {0u, k, k + 1})
                effectIndices_.push_back(base + idx);
        effects_.push_back({poly.shader, first, uint32_t(effectIndices_.size()) - first, {1, 1, 1, 1}, 0});
    }
    for (size_t i = view.firstEntity; i < view.firstEntity + view.entityCount && i < scene.entities.size(); ++i) {
        const std::vector<uint8_t>& e = scene.entities[i].bytes;
        if (readInt(e, refent::Type) == refent::TypePortal) {
            // A mirror's or portal's camera (Quake III's portal surface,
            // its type one on in Elite Force's numbering): where it is,
            // where it looks from (the same for a mirror), its axes, and
            // how it turns (frame, old frame, skin number).
            portalEntities_.push_back({readVec(e, refent::Origin), readVec(e, refent::OldOrigin),
                                       {readVec(e, refent::Axes), readVec(e, refent::Axes + 12),
                                        readVec(e, refent::Axes + 24)},
                                       readInt(e, refent::Frame), readInt(e, refent::OldFrame),
                                       readInt(e, refent::SkinNum)});
            continue;
        }
        if (readInt(e, refent::Type) != refent::TypeModel) {
            addEffect(e, left, up, time);
            continue;
        }
        const int32_t flags = readInt(e, refent::Flags);
        // One of the level's own models (*1, *2...: doors, lifts), where the
        // game has moved it.
        if (const std::string& n = name(handles_.models, readInt(e, refent::Model)); n.size() > 1 && n[0] == '*') {
            std::array<gfx::Vec3, 3> axes{readVec(e, refent::Axes), readVec(e, refent::Axes + 12),
                                          readVec(e, refent::Axes + 24)};
            if (axes[0] == gfx::Vec3{} && axes[1] == gfx::Vec3{} && axes[2] == gfx::Vec3{})
                axes = {gfx::Vec3{1, 0, 0}, gfx::Vec3{0, 1, 0}, gfx::Vec3{0, 0, 1}}; // left cleared: unturned
            brushes_.push_back({std::atoi(n.c_str() + 1), placement(readVec(e, refent::Origin), axes)});
            continue;
        }
        const int model = modelId(readInt(e, refent::Model));
        if (model < 0)
            continue;
        Placed p;
        const gfx::Vec3 origin = readVec(e, refent::Origin);
        p.model = models_.lod(model, lodLevel(model, readInt(e, refent::Frame), origin));
        p.transform = placement(origin, {readVec(e, refent::Axes), readVec(e, refent::Axes + 12),
                                         readVec(e, refent::Axes + 24)});
        // Between two frames, as far back towards the old one as it says.
        p.frame = readInt(e, refent::Frame);
        p.oldFrame = readInt(e, refent::OldFrame);
        p.backLerp = readFloat(e, refent::BackLerp);
        const gfx::Vec3 lightAt = flags & refent::UseLightingOrigin ? readVec(e, refent::LightingOrigin) : origin;
        p.light = grid.sample(lightAt);
        addDynamicLights(p.light, lightAt);
        if (const int32_t skin = readInt(e, refent::CustomSkin); skin > 0 && !name(handles_.skins, skin).empty())
            p.skin = &models_.skin(name(handles_.skins, skin));
        // A shader in place of the model's own (explosions, shells), its
        // colour fading as the program says.
        if (const int32_t shader = readInt(e, refent::CustomShader); shader > 0)
            p.shader = name(handles_.shaders, shader);
        p.color = entityColor(e);
        p.shaderTime = shaderTime(e, time);
        p.depthHack = flags & refent::DepthHack;
        p.thirdPerson = flags & refent::ThirdPerson;
        p.firstPerson = flags & refent::FirstPerson;
        placed_.push_back(p);
    }
}

void SceneDrawer::addDynamicLights(PointLight& light, const gfx::Vec3& at) const {
    // Each dynamic light adds to a model's directed light, from its
    // direction, as the original does: 16 * radius^2 / distance^2 times
    // its colour (distances under 16 counted as 16), on the light grid's
    // 0-255 scale.
    // The directions summed weighted by strength: the grid's first.
    gfx::Vec3 direction{};
    float total = 0;
    for (int c = 0; c < 3; ++c)
        total += light.directed[size_t(c)];
    for (size_t k = 0; k < 3; ++k)
        direction[k] = light.direction[k] * total;
    bool any = false;
    for (const gfx::DynamicLight& l : lights_) {
        gfx::Vec3 d{l.origin[0] - at[0], l.origin[1] - at[1], l.origin[2] - at[2]};
        float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist >= l.radius)
            continue;
        if (dist > 0)
            for (float& v : d)
                v /= dist;
        dist = std::max(dist, 16.0f);
        const float power = 16 * l.radius * l.radius / (dist * dist);
        for (size_t c = 0; c < 3; ++c) {
            light.directed[c] = std::min(light.directed[c] + power * l.color[c], 255.0f);
            direction[c] += d[c] * power;
        }
        any = true;
    }
    if (!any)
        return;
    const float len =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (len > 1e-6f)
        for (size_t k = 0; k < 3; ++k)
            light.direction[k] = direction[k] / len;
}

int SceneDrawer::lodLevel(int model, int frame, const gfx::Vec3& origin) const {
    // As the original: the model's radius as seen, as a share of half the
    // view's height (1 at most; 0 when it's at or behind the eye, as the
    // first-person weapon), picks the level: 1 - 5 times it, of the
    // levels there are; then the detail setting moves it further down.
    const int count = models_.lodCount(model);
    if (count < 2)
        return 0;
    const float dist = (origin[0] - eye_[0]) * forward_[0] + (origin[1] - eye_[1]) * forward_[1] +
                       (origin[2] - eye_[2]) * forward_[2];
    const float seen = dist > 0 ? std::min(models_.radius(model, frame) * focal_ / dist, 1.0f) : 0.0f;
    const float detail = seen > 0 ? 1 - seen * 5 : 0;
    const int level = std::clamp(int(detail * float(count)), 0, count - 1);
    return std::clamp(level + models_.lodBias(), 0, count - 1);
}

int SceneDrawer::modelId(int32_t handle) {
    // The program's model handle to a loaded model, loaded on first use;
    // -1 if it can't be.
    if (handle <= 0)
        return -1;
    if (const auto it = modelIds_.find(handle); it != modelIds_.end()) {
        if (it->second < 0)
            unloadable_.insert(name(handles_.models, handle));
        return it->second;
    }
    const std::string& n = name(handles_.models, handle);
    int id = -1;
    if (n.size() > 4 && (fs::toLower(n.substr(n.size() - 4)) == ".md3" || fs::toLower(n.substr(n.size() - 4)) == ".mdr"))
        id = models_.load(n);
    modelIds_[handle] = id;
    if (id < 0)
        unloadable_.insert(n);
    return id;
}

} // namespace reluva::idtech3
