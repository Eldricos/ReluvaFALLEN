// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/soft_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::tools {

namespace {

// A vertex after the model and view: clip position, then what's
// interpolated across the triangle.
struct ClipVertex {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
    float wx, wy, wz; // where it is in the world (for the dynamic lights)
};

ClipVertex mix(const ClipVertex& p, const ClipVertex& q, float t) {
    auto l = [t](float a, float b) { return a + (b - a) * t; };
    return {l(p.x, q.x), l(p.y, q.y), l(p.z, q.z), l(p.w, q.w), l(p.u, q.u), l(p.v, q.v),
            l(p.r, q.r), l(p.g, q.g), l(p.b, q.b), l(p.a, q.a), l(p.wx, q.wx), l(p.wy, q.wy), l(p.wz, q.wz)};
}

float factor(gfx::Blend f, float src, float dst, float srcA, float dstA) {
    switch (f) {
    case gfx::Blend::Zero: return 0;
    case gfx::Blend::One: return 1;
    case gfx::Blend::SrcColor: return src;
    case gfx::Blend::OneMinusSrcColor: return 1 - src;
    case gfx::Blend::DstColor: return dst;
    case gfx::Blend::OneMinusDstColor: return 1 - dst;
    case gfx::Blend::SrcAlpha: return srcA;
    case gfx::Blend::OneMinusSrcAlpha: return 1 - srcA;
    case gfx::Blend::DstAlpha: return dstA;
    case gfx::Blend::OneMinusDstAlpha: return 1 - dstA;
    }
    return 1;
}

} // namespace

SoftRenderer3D::SoftRenderer3D(int width, int height) : w_(width), h_(height) {
    color_.create(w_, h_, PixelFormat::RGBA32);
    frame_.create(w_, h_, PixelFormat::RGBA32);
    depth_.assign(size_t(w_) * size_t(h_), 1.0f);
    vw_ = w_;
    vh_ = h_;
}

Error SoftRenderer3D::createTexture(int width, int height, const uint8_t* rgba, const gfx::TextureParams& params,
                                    gfx::TextureId& out) {
    if (width <= 0 || height <= 0 || !rgba)
        return Error::fail("bad texture");
    Texture t;
    t.w = width;
    t.h = height;
    t.clamp = params.clamp;
    t.nearest = params.nearest;
    t.rgba.assign(rgba, rgba + size_t(width) * size_t(height) * 4);
    out = nextId_++;
    textures_[out] = std::move(t);
    return Error::success();
}

void SoftRenderer3D::destroyTexture(gfx::TextureId id) { textures_.erase(id); }

Error SoftRenderer3D::updateTextures(const std::vector<gfx::TextureRegion>& regions) {
    for (const gfx::TextureRegion& r : regions) {
        auto it = textures_.find(r.texture);
        if (it == textures_.end() || !r.rgba)
            continue;
        Texture& t = it->second;
        for (int y = 0; y < r.h && r.y + y < t.h; ++y)
            std::copy_n(r.rgba + size_t(y) * size_t(r.w) * 4, size_t(std::min(r.w, t.w - r.x)) * 4,
                        t.rgba.begin() + (ptrdiff_t(r.y + y) * t.w + r.x) * 4);
    }
    return Error::success();
}

Error SoftRenderer3D::createMesh(const std::vector<gfx::Vertex>& vertices, const std::vector<uint32_t>& indices,
                                 gfx::MeshId& out) {
    out = nextId_++;
    Mesh& m = meshes_[out];
    m.vertexMesh = out;
    m.vertices = vertices;
    m.indices = indices;
    return Error::success();
}

Error SoftRenderer3D::createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t>& indices, gfx::MeshId& out) {
    if (!meshes_.count(vertices))
        return Error::fail("no such mesh");
    out = nextId_++;
    Mesh& m = meshes_[out];
    m.vertexMesh = vertices;
    m.indices = indices;
    return Error::success();
}

Error SoftRenderer3D::setMeshIndices(gfx::MeshId id, const std::vector<uint32_t>& indices) {
    auto it = meshes_.find(id);
    if (it == meshes_.end())
        return Error::fail("no such mesh");
    it->second.indices = indices;
    return Error::success();
}

void SoftRenderer3D::destroyMesh(gfx::MeshId id) { meshes_.erase(id); }

void SoftRenderer3D::beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3& eye, Color clear) {
    viewProj_ = viewProj;
    eye_ = eye;
    vx_ = vy_ = 0;
    vw_ = w_;
    vh_ = h_;
    depthRange_ = 1;
    draws_ = triangles_ = 0;
    if (!drawing) // nothing will be drawn or looked at: no need to clear
        return;
    for (int y = 0; y < h_; ++y) {
        uint8_t* p = color_.row(y);
        for (int x = 0; x < w_; ++x) {
            p[x * 4] = clear.r;
            p[x * 4 + 1] = clear.g;
            p[x * 4 + 2] = clear.b;
            p[x * 4 + 3] = 255;
        }
    }
    std::fill(depth_.begin(), depth_.end(), 1.0f);
}

void SoftRenderer3D::setView(const gfx::Mat4& viewProj, const gfx::Vec3& eye) {
    viewProj_ = viewProj;
    eye_ = eye;
}

void SoftRenderer3D::setViewport(int x, int y, int w, int h) {
    vx_ = x;
    vy_ = y;
    vw_ = w > 0 ? w : w_;
    vh_ = h > 0 ? h : h_;
}

void SoftRenderer3D::clearDepth() { std::fill(depth_.begin(), depth_.end(), 1.0f); }

void SoftRenderer3D::draw(const gfx::DrawCall& call) {
    if (!drawing)
        return;
    auto mit = meshes_.find(call.mesh);
    if (mit == meshes_.end())
        return;
    const Mesh& mesh = mit->second;
    auto vit = meshes_.find(mesh.vertexMesh);
    if (vit == meshes_.end())
        return;
    const std::vector<gfx::Vertex>& verts = vit->second.vertices;
    const Texture* tex = nullptr;
    if (auto t = textures_.find(call.texture); call.texture && t != textures_.end())
        tex = &t->second;
    ++draws_;
    const gfx::Mat4 mvp = gfx::multiply(viewProj_, call.model);
    const gfx::Mat4& m = call.model;
    auto vertexAt = [&](uint32_t index, ClipVertex& out) {
        const size_t i = size_t(int64_t(index) + call.vertexOffset);
        if (i >= verts.size())
            return false;
        const gfx::Vertex& v = verts[i];
        float p[3] = {v.pos[0], v.pos[1], v.pos[2]}, n[3] = {v.normal[0], v.normal[1], v.normal[2]};
        if (call.backLerp > 0) {
            const size_t j = size_t(int64_t(index) + call.oldVertexOffset);
            if (j < verts.size())
                for (int k = 0; k < 3; ++k) {
                    p[k] += (verts[j].pos[k] - p[k]) * call.backLerp;
                    n[k] += (verts[j].normal[k] - n[k]) * call.backLerp;
                }
        }
        out.x = mvp[0] * p[0] + mvp[4] * p[1] + mvp[8] * p[2] + mvp[12];
        out.y = mvp[1] * p[0] + mvp[5] * p[1] + mvp[9] * p[2] + mvp[13];
        out.z = mvp[2] * p[0] + mvp[6] * p[1] + mvp[10] * p[2] + mvp[14];
        out.w = mvp[3] * p[0] + mvp[7] * p[1] + mvp[11] * p[2] + mvp[15];
        out.wx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        out.wy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        out.wz = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
        const float* uv = call.texGen == gfx::TexGen::Uv2 ? v.uv2 : v.uv;
        // Through the pre and post rows (the turbulence between them left out).
        auto rows = [](const std::array<float, 3>& r0, const std::array<float, 3>& r1, float& u, float& w) {
            const float nu = r0[0] * u + r0[1] * w + r0[2], nw = r1[0] * u + r1[1] * w + r1[2];
            u = nu;
            w = nw;
        };
        out.u = uv[0];
        out.v = uv[1];
        rows(call.preRow0, call.preRow1, out.u, out.v);
        rows(call.postRow0, call.postRow1, out.u, out.v);
        std::array<float, 4> c = call.color;
        auto source = [&](gfx::ColorSource s, int k) {
            const float vc = float(v.color[k]) / 255.0f;
            switch (s) {
            case gfx::ColorSource::Vertex: return vc;
            case gfx::ColorSource::OneMinusVertex: return 1 - vc;
            default: return 1.0f;
            }
        };
        if (call.rgbSource == gfx::ColorSource::Lighting) {
            float wn[3];
            for (int k = 0; k < 3; ++k)
                wn[k] = m[k] * n[0] + m[4 + k] * n[1] + m[8 + k] * n[2];
            const float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
            const float facing = len > 0 ? std::max(0.0f, (wn[0] * call.lightDirection[0] + wn[1] * call.lightDirection[1] +
                                                         wn[2] * call.lightDirection[2]) / len)
                                         : 0;
            for (int k = 0; k < 3; ++k)
                c[size_t(k)] *= call.ambientLight[size_t(k)] + call.directedLight[size_t(k)] * facing;
        } else {
            for (int k = 0; k < 3; ++k)
                c[size_t(k)] *= source(call.rgbSource, k);
        }
        c[3] *= source(call.alphaSource, 3);
        out.r = c[0];
        out.g = c[1];
        out.b = c[2];
        out.a = c[3];
        return true;
    };

    const uint32_t end = std::min<uint32_t>(uint32_t(mesh.indices.size()), call.firstIndex + call.indexCount);
    for (uint32_t t = call.firstIndex; t + 2 < end; t += 3) {
        ClipVertex tri[3];
        if (!vertexAt(mesh.indices[t], tri[0]) || !vertexAt(mesh.indices[t + 1], tri[1]) ||
            !vertexAt(mesh.indices[t + 2], tri[2]))
            continue;
        // Clipped at the near plane (z >= 0 in clip space).
        std::vector<ClipVertex> poly;
        for (int k = 0; k < 3; ++k) {
            const ClipVertex& a = tri[k];
            const ClipVertex& b = tri[(k + 1) % 3];
            const bool ina = a.z >= 0 && a.w > 0, inb = b.z >= 0 && b.w > 0;
            if (ina)
                poly.push_back(a);
            if (ina != inb && (a.z - b.z) != 0)
                poly.push_back(mix(a, b, a.z / (a.z - b.z)));
        }
        if (poly.size() < 3)
            continue;
        // To the screen.
        struct Screen {
            float x, y, z, invW;
            ClipVertex c;
        };
        std::vector<Screen> s;
        for (const ClipVertex& c : poly) {
            if (c.w <= 1e-6f)
                continue;
            const float iw = 1 / c.w;
            s.push_back({float(vx_) + (c.x * iw * 0.5f + 0.5f) * float(vw_),
                         float(vy_) + (0.5f - c.y * iw * 0.5f) * float(vh_), c.z * iw * depthRange_, iw, c});
        }
        for (size_t k = 2; k < s.size(); ++k) {
            const Screen* v0 = &s[0];
            const Screen* v1 = &s[k - 1];
            const Screen* v2 = &s[k];
            // Counter-clockwise with Y up is clockwise on the screen (Y down).
            float area = (v1->x - v0->x) * (v2->y - v0->y) - (v1->y - v0->y) * (v2->x - v0->x);
            if (area == 0 || !std::isfinite(area))
                continue;
            const bool front = area < 0;
            if ((call.state.cull == gfx::Cull::Back && !front) || (call.state.cull == gfx::Cull::Front && front))
                continue;
            if (area < 0) {
                std::swap(v1, v2);
                area = -area;
            }
            ++triangles_;
            // Within the viewport (a HUD portrait's), as a GPU keeps to it.
            const int x0 = std::max(std::max(0, vx_), int(std::floor(std::min({v0->x, v1->x, v2->x}))));
            const int x1 = std::min(std::min(w_, vx_ + vw_) - 1, int(std::ceil(std::max({v0->x, v1->x, v2->x}))));
            const int y0 = std::max(std::max(0, vy_), int(std::floor(std::min({v0->y, v1->y, v2->y}))));
            const int y1 = std::min(std::min(h_, vy_ + vh_) - 1, int(std::ceil(std::max({v0->y, v1->y, v2->y}))));
            auto edge = [](const Screen& p, const Screen& q, float x, float y) {
                return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
            };
            const float inv = 1 / area;
            for (int y = y0; y <= y1; ++y) {
                uint8_t* row = color_.row(y);
                for (int x = x0; x <= x1; ++x) {
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float w0 = edge(*v1, *v2, px, py), w1 = edge(*v2, *v0, px, py), w2 = edge(*v0, *v1, px, py);
                    if (w0 < 0 || w1 < 0 || w2 < 0)
                        continue;
                    const float b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
                    const float z = b0 * v0->z + b1 * v1->z + b2 * v2->z;
                    float& d = depth_[size_t(y) * size_t(w_) + size_t(x)];
                    if (call.state.depthTest && (call.state.depthEqual ? std::fabs(z - d) > 1e-5f : z > d))
                        continue;
                    const float iw = b0 * v0->invW + b1 * v1->invW + b2 * v2->invW;
                    auto attr = [&](float ClipVertex::*f) {
                        return (b0 * (v0->c.*f) * v0->invW + b1 * (v1->c.*f) * v1->invW + b2 * (v2->c.*f) * v2->invW) / iw;
                    };
                    float src[4] = {attr(&ClipVertex::r), attr(&ClipVertex::g), attr(&ClipVertex::b), attr(&ClipVertex::a)};
                    // The dynamic lights' light here: alone, or times the
                    // texture (below).
                    const bool lit = call.dynamicLights || call.dynamicLightsTextured;
                    if (lit) {
                        const float px = attr(&ClipVertex::wx), py = attr(&ClipVertex::wy), pz = attr(&ClipVertex::wz);
                        float sum[3] = {0, 0, 0};
                        for (const gfx::DynamicLight& l : lights_) {
                            const float dx = px - l.origin[0], dy = py - l.origin[1], dz = pz - l.origin[2];
                            const float f = std::clamp(1 - std::sqrt(dx * dx + dy * dy + dz * dz) / std::max(l.radius, 1e-3f),
                                                       0.0f, 1.0f);
                            for (int k = 0; k < 3; ++k)
                                sum[k] += l.color[size_t(k)] * f;
                        }
                        for (int k = 0; k < 3; ++k)
                            src[k] = sum[k];
                        src[3] = 1;
                    }
                    if (tex && !call.dynamicLights) {
                        float u = attr(&ClipVertex::u), v = attr(&ClipVertex::v);
                        auto wrap = [&](int c, int n) { return tex->clamp ? std::clamp(c, 0, n - 1) : ((c % n) + n) % n; };
                        auto at = [&](int x, int y) {
                            return &tex->rgba[(size_t(wrap(y, tex->h)) * size_t(tex->w) + size_t(wrap(x, tex->w))) * 4];
                        };
                        float texel[4];
                        if (tex->nearest) {
                            const uint8_t* t = at(int(std::floor(u * float(tex->w))), int(std::floor(v * float(tex->h))));
                            for (int k = 0; k < 4; ++k)
                                texel[k] = float(t[k]);
                        } else { // between the four nearest texels' centres
                            const float fx = u * float(tex->w) - 0.5f, fy = v * float(tex->h) - 0.5f;
                            const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
                            const float ax = fx - float(x0), ay = fy - float(y0);
                            const uint8_t *a = at(x0, y0), *b = at(x0 + 1, y0), *c = at(x0, y0 + 1), *d = at(x0 + 1, y0 + 1);
                            for (int k = 0; k < 4; ++k)
                                texel[k] = (float(a[k]) * (1 - ax) + float(b[k]) * ax) * (1 - ay) +
                                           (float(c[k]) * (1 - ax) + float(d[k]) * ax) * ay;
                        }
                        for (int k = 0; k < 4; ++k)
                            src[k] *= texel[k] / 255.0f;
                    }
                    float alpha = src[3];
                    if (call.dynamicLightsTextured)
                        src[3] = 1; // its alpha only tested
                    if ((call.state.alphaTest == gfx::AlphaTest::Gt0 && alpha <= 0) ||
                        (call.state.alphaTest == gfx::AlphaTest::Lt128 && alpha >= 0.5f) ||
                        (call.state.alphaTest == gfx::AlphaTest::Ge128 && alpha < 0.5f))
                        continue;
                    uint8_t* out = row + x * 4;
                    float dst[4] = {float(out[0]) / 255.0f, float(out[1]) / 255.0f, float(out[2]) / 255.0f,
                                    float(out[3]) / 255.0f};
                    for (int k = 0; k < 4; ++k) {
                        const float s0 = std::clamp(src[k], 0.0f, 1.0f);
                        const float r = s0 * factor(call.state.src, s0, dst[k], alpha, dst[3]) +
                                        dst[k] * factor(call.state.dst, s0, dst[k], alpha, dst[3]);
                        out[k] = uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                    if (call.state.depthWrite)
                        d = z;
                }
            }
        }
    }
}

void SoftRenderer3D::endFrame(const Surface* overlay) {
    if (!drawing)
        return;
    std::array<uint8_t, 256> curve{};
    for (int i = 0; i < 256; ++i)
        curve[size_t(i)] = uint8_t(std::clamp(std::pow(float(i) / 255.0f, 1 / gamma_), 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = 0; y < h_; ++y) {
        const uint8_t* in = color_.row(y);
        uint8_t* out = frame_.row(y);
        const uint8_t* over = overlay && overlay->w == w_ && overlay->h == h_ ? overlay->row(y) : nullptr;
        for (int x = 0; x < w_; ++x)
            for (int k = 0; k < 3; ++k) {
                float c = float(curve[in[x * 4 + k]]);
                if (over) {
                    const float a = float(over[x * 4 + 3]) / 255.0f;
                    c = c * (1 - a) + float(over[x * 4 + k]) * a;
                }
                out[x * 4 + k] = uint8_t(c + 0.5f);
                out[x * 4 + 3] = 255;
            }
    }
    lastDraws_ = draws_;
    lastTriangles_ = triangles_;
    ++frames_;
}

} // namespace reluva::tools
