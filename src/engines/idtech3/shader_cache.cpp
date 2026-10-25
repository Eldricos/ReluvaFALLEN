// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the original renderer interprets shaders (defaults, sort order,
// implicit shaders) was read from the GPL Quake III Arena renderer
// (tr_shader.c, tr_shade_calc.c); docs/formats/idtech3/shader.md
// describes it. This is new code written from that description.

#include "engines/idtech3/shader_cache.h"

#include "common/image.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reluva::idtech3 {

namespace {

constexpr float kPi = 3.14159265358979f;

// Sort keys, as the original numbers them.
constexpr float kSortPortal = 1, kSortOpaque = 3, kSortDecal = 4, kSortSeeThrough = 5, kSortBlend = 9;

gfx::Blend blendFactor(const std::string& gl) {
    static const std::pair<const char*, gfx::Blend> names[] = {
        {"gl_zero", gfx::Blend::Zero},
        {"gl_one", gfx::Blend::One},
        {"gl_src_color", gfx::Blend::SrcColor},
        {"gl_one_minus_src_color", gfx::Blend::OneMinusSrcColor},
        {"gl_dst_color", gfx::Blend::DstColor},
        {"gl_one_minus_dst_color", gfx::Blend::OneMinusDstColor},
        {"gl_src_alpha", gfx::Blend::SrcAlpha},
        {"gl_one_minus_src_alpha", gfx::Blend::OneMinusSrcAlpha},
        {"gl_dst_alpha", gfx::Blend::DstAlpha},
        {"gl_one_minus_dst_alpha", gfx::Blend::OneMinusDstAlpha},
    };
    for (const auto& [name, b] : names)
        if (gl == name)
            return b;
    return gfx::Blend::One; // gl_src_alpha_saturate isn't supported
}

bool stageBlends(const ShaderStage& s) {
    if (s.blendSrc.empty())
        return false;
    return !(blendFactor(s.blendSrc) == gfx::Blend::One && blendFactor(s.blendDst) == gfx::Blend::Zero);
}

std::string stripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    const size_t slash = name.find_last_of("/\\");
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? name.substr(0, dot) : name;
}

// An affine map of texture coordinates: (u, v) -> (r0 . (u, v, 1), r1 . (u, v, 1)).
struct Affine {
    std::array<float, 3> r0{1, 0, 0}, r1{0, 1, 0};
};

// `op` applied after `m`.
Affine then(const Affine& m, const Affine& op) {
    Affine r;
    r.r0 = {op.r0[0] * m.r0[0] + op.r0[1] * m.r1[0], op.r0[0] * m.r0[1] + op.r0[1] * m.r1[1],
            op.r0[0] * m.r0[2] + op.r0[1] * m.r1[2] + op.r0[2]};
    r.r1 = {op.r1[0] * m.r0[0] + op.r1[1] * m.r1[0], op.r1[0] * m.r0[1] + op.r1[1] * m.r1[1],
            op.r1[0] * m.r0[2] + op.r1[1] * m.r1[2] + op.r1[2]};
    return r;
}

// One tcMod at `time` as an affine map (turb is handled by the caller).
Affine tcModMatrix(const ShaderStage::TcMod& m, float time) {
    using T = ShaderStage::TcMod::Type;
    Affine a;
    switch (m.type) {
    case T::Scroll: {
        // Only the fraction matters, and keeps precision over long times.
        const float s = m.v[0] * time, t = m.v[1] * time;
        a.r0[2] = s - std::floor(s);
        a.r1[2] = t - std::floor(t);
        break;
    }
    case T::Scale:
        a.r0[0] = m.v[0];
        a.r1[1] = m.v[1];
        break;
    case T::Rotate: {
        // Around the texture's centre; positive degrees turn it clockwise.
        const float rad = -m.v[0] * time * kPi / 180;
        const float c = std::cos(rad), s = std::sin(rad);
        a.r0 = {c, -s, 0.5f - 0.5f * c + 0.5f * s};
        a.r1 = {s, c, 0.5f - 0.5f * s - 0.5f * c};
        break;
    }
    case T::Stretch: {
        const float w = gfx::evaluate(m.wave, time);
        const float p = std::fabs(w) > 1e-4f ? 1.0f / w : 1e4f;
        a.r0 = {p, 0, 0.5f - 0.5f * p};
        a.r1 = {0, p, 0.5f - 0.5f * p};
        break;
    }
    case T::Transform:
        a.r0 = {m.v[0], m.v[2], m.v[4]};
        a.r1 = {m.v[1], m.v[3], m.v[5]};
        break;
    case T::Turb:
    case T::EntityTranslate:
        break;
    }
    return a;
}

gfx::Cull cullOf(ShaderDef::Cull c) {
    return c == ShaderDef::Cull::Front ? gfx::Cull::Front : c == ShaderDef::Cull::Back ? gfx::Cull::Back
                                                                                       : gfx::Cull::None;
}

} // namespace

ShaderCache::ShaderCache(gfx::Renderer3D& renderer, const SearchSet& files, const ShaderLibrary& shaders)
    : r_(renderer), files_(files), shaders_(shaders) {}

void ShaderCache::clear() {
    for (auto& [name, t] : images_)
        if (t != missingTexture_)
            r_.destroyTexture(t);
    images_.clear();
    for (gfx::TextureId t : {missingTexture_, whiteTexture_})
        if (t)
            r_.destroyTexture(t);
    missingTexture_ = whiteTexture_ = 0;
    materials_.clear();
    byName_.clear();
    implicit_.clear();
    unsupported_.clear();
    missing_ = 0;
}

gfx::TextureId ShaderCache::white() {
    if (!whiteTexture_) {
        const uint8_t px[4] = {255, 255, 255, 255};
        gfx::TextureParams p;
        p.mipmaps = false;
        r_.createTexture(1, 1, px, p, whiteTexture_);
    }
    return whiteTexture_;
}

gfx::TextureId ShaderCache::image(const std::string& name, bool clamp, bool mipmaps, bool picmip) {
    const std::string key = normalizeArchiveName(stripExtension(name)) + (clamp ? "|clamp" : "") +
                            (mipmaps ? "" : "|nomip") + (picmip ? "|picmip" : "");
    if (auto it = images_.find(key); it != images_.end())
        return it->second;

    if (!missingTexture_) {
        // Grey checks for images that can't be found.
        uint8_t px[8 * 8 * 4];
        for (int i = 0; i < 64; ++i) {
            const uint8_t v = ((i % 8) / 4 + (i / 8) / 4) % 2 ? 150 : 110;
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
            px[i * 4 + 3] = 255;
        }
        gfx::TextureParams p;
        p.nearest = true;
        r_.createTexture(8, 8, px, p, missingTexture_);
    }
    gfx::TextureId id = missingTexture_;
    std::vector<uint8_t> data;
    image::Image img;
    if (const std::string file = findImage(files_, name);
        !file.empty() && !files_.read(file, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        params.clamp = clamp;
        params.mipmaps = mipmaps;
        if (mipmaps) {
            // The settings' filter, for textures in the world.
            params.mipmaps = textureOptions_.mipmaps;
            params.nearest = textureOptions_.nearest;
            params.mipNearest = textureOptions_.mipNearest;
            params.anisotropic = textureOptions_.anisotropic;
        }
        // Texture detail: halved (each 2x2 pixels averaged), down to a
        // pixel at least.
        for (int i = 0; picmip && i < textureOptions_.picmip && (img.w > 1 || img.h > 1); ++i) {
            const int w = std::max(1, img.w / 2), h = std::max(1, img.h / 2);
            std::vector<uint8_t> half(size_t(w) * size_t(h) * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 4; ++c) {
                        int sum = 0;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx) {
                                const int sx = std::min(img.w - 1, x * 2 + dx), sy = std::min(img.h - 1, y * 2 + dy);
                                sum += img.rgba[(size_t(sy) * size_t(img.w) + size_t(sx)) * 4 + size_t(c)];
                            }
                        half[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = uint8_t((sum + 2) / 4);
                    }
            img.rgba = std::move(half);
            img.w = w;
            img.h = h;
        }
        gfx::TextureId t = 0;
        if (!r_.createTexture(img.w, img.h, img.rgba.data(), params, t))
            id = t;
    }
    if (id == missingTexture_)
        ++missing_;
    images_[key] = id;
    return id;
}

ShaderCache::Stage ShaderCache::makeStage(const ShaderStage& def, const ShaderDef& shader) {
    Stage st;
    st.def = &def;
    st.fps = def.animFps;
    if (def.lightmap) {
        st.lightmap = true;
    } else if (def.whiteImage || def.maps.empty()) {
        st.frames = {white()};
    } else {
        for (const std::string& m : def.maps)
            st.frames.push_back(image(m, def.clamp, !shader.noMipmaps, !shader.noPicmip));
    }

    const bool blends = stageBlends(def);
    if (blends) {
        st.state.src = blendFactor(def.blendSrc);
        st.state.dst = blendFactor(def.blendDst);
    }
    // Stages write depth unless they blend, or say depthWrite.
    st.state.depthWrite = !blends || def.depthWrite;
    st.state.depthEqual = def.depthEqual;
    st.state.alphaTest = def.alphaFunc;
    st.state.cull = cullOf(shader.cull);
    st.state.depthBias = shader.polygonOffset;

    using T = ShaderStage::TcGen;
    switch (def.tcGen) {
    case T::Default: st.texGen = def.lightmap ? gfx::TexGen::Uv2 : gfx::TexGen::Uv; break;
    case T::Base: st.texGen = gfx::TexGen::Uv; break;
    case T::Lightmap: st.texGen = gfx::TexGen::Uv2; break;
    case T::Environment: st.texGen = gfx::TexGen::Environment; break;
    case T::Vector: st.texGen = gfx::TexGen::Vector; break;
    }
    return st;
}

ShaderCache::Material ShaderCache::makeMaterial(const ShaderDef& def, Lighting lighting, bool sky) {
    Material m;
    m.lighting = lighting;
    for (const std::string& u : def.unknown)
        note("unknown directive " + u);
    // Sky surfaces show the sky drawn behind everything; fog volumes'
    // surfaces only mark the volume.
    if ((def.sky && !sky) || def.fog || def.hasSurfaceParm("nodraw") || def.stages.empty()) {
        m.skip = true;
        return m;
    }
    // A mirror or portal: the view through it is drawn first (SceneDrawer),
    // this over it; how far off it's drawn, the alphaGen portal range.
    if (def.portal) {
        m.portal = true;
        for (const ShaderStage& s : def.stages)
            if (s.alphaGen == ShaderStage::AlphaGen::Portal)
                m.portalRange = std::max(m.portalRange, s.portalRange);
    }

    for (const ShaderStage& s : def.stages)
        m.stages.push_back(makeStage(s, def));

    size_t used = 0;
    for (const ShaderDeform& d : def.deforms) {
        using T = ShaderDeform::Type;
        gfx::Deform g;
        if (d.type == T::Wave) {
            g.type = gfx::Deform::Type::Wave;
            g.wave = d.wave;
            // Without a frequency the whole surface moves as one.
            g.spread = d.wave.frequency == 0 ? 0 : d.spread;
        } else if (d.type == T::Bulge) {
            g.type = gfx::Deform::Type::Bulge;
            g.width = d.width;
            g.height = d.height;
            g.speed = d.speed;
        } else if (d.type == T::Move) {
            g.type = gfx::Deform::Type::Move;
            g.wave = d.wave;
            g.vector = d.vector;
        } else if (d.type == T::Normal) {
            continue; // only changes lighting by normals, which isn't modelled
        } else {
            static const char* const names[] = {"wave", "normal", "bulge", "move", "autosprite", "autosprite2",
                                                "text", "projectionShadow", "?"};
            note(std::string("deformVertexes ") + names[size_t(d.type)]);
            continue;
        }
        if (used < m.deforms.size())
            m.deforms[used++] = g;
        else
            note("more than two deformVertexes");
    }

    // Sort order: as given, or as the original decides it.
    if (def.sort > 0) {
        m.sort = def.sort;
    } else if (def.portal) {
        m.sort = kSortPortal;
    } else if (def.polygonOffset) {
        m.sort = kSortDecal;
    } else {
        m.sort = kSortOpaque;
        if (stageBlends(def.stages.front()))
            for (const Stage& s : m.stages)
                if (stageBlends(*s.def)) {
                    m.sort = s.state.depthWrite ? kSortSeeThrough : kSortBlend;
                    break;
                }
    }
    return m;
}

int ShaderCache::material(const ShaderDef& def, Lighting lighting, bool sky) {
    if (lighting == Lighting::TwoD && !(def.noMipmaps && def.noPicmip)) {
        // Pictures (menus, the HUD) are registered without mipmaps, so the
        // texture detail and filter settings leave them alone.
        // The copy is kept: the material's stages point into it.
        ShaderDef& flat = implicit_.emplace_back(def);
        flat.noMipmaps = flat.noPicmip = true;
        materials_.push_back(makeMaterial(flat, lighting, sky));
        return static_cast<int>(materials_.size()) - 1;
    }
    materials_.push_back(makeMaterial(def, lighting, sky));
    return static_cast<int>(materials_.size()) - 1;
}

int ShaderCache::material(const std::string& name, Lighting lighting) {
    const auto key = std::make_pair(normalizeArchiveName(name), lighting);
    if (auto it = byName_.find(key); it != byName_.end())
        return it->second;
    const ShaderDef* def = shaders_.find(name);
    if (!def) {
        // The original's implicit shaders: the image times the lightmap,
        // the vertex colours, or the light.
        ShaderDef& d = implicit_.emplace_back();
        d.name = name;
        ShaderStage tex;
        tex.maps = {name};
        if (lighting == Lighting::Lightmap) {
            ShaderStage lm;
            lm.lightmap = true;
            lm.rgbGen = ShaderStage::RgbGen::Identity;
            tex.blendSrc = "gl_dst_color";
            tex.blendDst = "gl_zero";
            tex.rgbGen = ShaderStage::RgbGen::Identity;
            d.stages = {lm, tex};
        } else if (lighting == Lighting::TwoD) {
            // Pictures: blended by their alpha, coloured by the vertices,
            // the image clamped and without mipmaps.
            tex.blendSrc = "gl_src_alpha";
            tex.blendDst = "gl_one_minus_src_alpha";
            tex.rgbGen = ShaderStage::RgbGen::Vertex;
            tex.alphaGen = ShaderStage::AlphaGen::Vertex;
            tex.clamp = true;
            d.noMipmaps = true;
            d.stages = {tex};
        } else if (lighting == Lighting::Effect) {
            // An effect's image: coloured and blended by the entity's colour.
            tex.blendSrc = "gl_src_alpha";
            tex.blendDst = "gl_one_minus_src_alpha";
            tex.rgbGen = ShaderStage::RgbGen::Vertex;
            tex.alphaGen = ShaderStage::AlphaGen::Vertex;
            d.stages = {tex};
        } else {
            tex.rgbGen = lighting == Lighting::Vertex ? ShaderStage::RgbGen::ExactVertex
                                                      : ShaderStage::RgbGen::LightingDiffuse;
            d.stages = {tex};
        }
        def = &d;
    }
    const int index = material(*def, lighting);
    byName_[key] = index;
    return index;
}

void ShaderCache::draw(int material, const gfx::DrawCall& base, gfx::TextureId lightmap, float time, bool sky,
                       const std::array<float, 4>* entity) const {
    const std::array<float, 4> ent = entity ? *entity : std::array<float, 4>{1, 1, 1, 1};
    const Material& m = materials_[size_t(material)];
    for (const Stage& st : m.stages) {
        const ShaderStage& d = *st.def;
        gfx::DrawCall call = base;
        call.time = time;
        call.deforms = m.deforms;
        call.state = st.state;
        if (st.lightmap)
            call.texture = lightmap ? lightmap : whiteTexture_;
        else if (st.fps > 0 && st.frames.size() > 1)
            call.texture = st.frames[size_t(std::fmod(std::floor(time * st.fps), float(st.frames.size())))];
        else
            call.texture = st.frames.front();

        call.texGen = st.texGen;
        call.texVector0 = d.tcVectors[0];
        call.texVector1 = d.tcVectors[1];
        // tcMods in order; a turb splits them into before and after.
        Affine pre, post;
        bool turbSeen = false;
        for (const ShaderStage::TcMod& mod : d.tcMods) {
            if (mod.type == ShaderStage::TcMod::Type::Turb && !turbSeen) {
                turbSeen = true;
                call.turbAmplitude = mod.wave.amplitude;
                call.turbPhase = mod.wave.phase + time * mod.wave.frequency;
                continue;
            }
            Affine& target = turbSeen ? post : pre;
            target = then(target, tcModMatrix(mod, time));
        }
        call.preRow0 = pre.r0;
        call.preRow1 = pre.r1;
        call.postRow0 = post.r0;
        call.postRow1 = post.r1;

        // Models have no vertex colours: whatever asks for light gets the light.
        const gfx::ColorSource lit =
            m.lighting == Lighting::Diffuse ? gfx::ColorSource::Lighting : gfx::ColorSource::Vertex;
        using R = ShaderStage::RgbGen;
        call.rgbSource = gfx::ColorSource::Constant;
        call.alphaSource = gfx::ColorSource::Constant;
        call.color = {1, 1, 1, 1};
        switch (d.rgbGen) {
        case R::Vertex:
        case R::ExactVertex:
        case R::LightingDiffuse: call.rgbSource = lit; break;
        case R::OneMinusVertex: call.rgbSource = gfx::ColorSource::OneMinusVertex; break;
        case R::Wave: {
            const float v = std::clamp(gfx::evaluate(d.rgbWave, time), 0.0f, 1.0f);
            call.color = {v, v, v, 1};
            break;
        }
        case R::Const: call.color = {d.rgbConst[0], d.rgbConst[1], d.rgbConst[2], 1}; break;
        case R::Entity: call.color = {ent[0], ent[1], ent[2], 1}; break;
        case R::OneMinusEntity: call.color = {1 - ent[0], 1 - ent[1], 1 - ent[2], 1}; break;
        default: break; // identity: white
        }
        // Lit by its vertices (no lightmap, or the vertex lighting
        // setting): a lightmap stage is the vertex colours instead.
        if (st.lightmap && !lightmap && m.lighting == Lighting::Vertex)
            call.rgbSource = gfx::ColorSource::Vertex;
        using A = ShaderStage::AlphaGen;
        switch (d.alphaGen) {
        case A::Vertex:
            if (m.lighting != Lighting::Diffuse) // models: opaque
                call.alphaSource = gfx::ColorSource::Vertex;
            break;
        case A::OneMinusVertex: call.alphaSource = gfx::ColorSource::OneMinusVertex; break;
        case A::Wave: call.color[3] = std::clamp(gfx::evaluate(d.alphaWave, time), 0.0f, 1.0f); break;
        case A::Const: call.color[3] = d.alphaConst; break;
        case A::Entity: call.color[3] = ent[3]; break;
        case A::OneMinusEntity: call.color[3] = 1 - ent[3]; break;
        case A::Portal: call.fogDistance = d.portalRange; break; // see-through near, the stage's own far off
        default: break; // identity, lightingSpecular: opaque
        }

        if (m.lighting == Lighting::TwoD) {
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
        }
        if (m.lighting == Lighting::Effect)
            call.state.cull = gfx::Cull::None;
        if (sky) {
            call.followCamera = true;
            call.texGen = gfx::TexGen::Uv2; // the cloud coordinates
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
            call.deforms = {};
        }
        r_.draw(call);
    }
}

void ShaderCache::drawFog(int material, const gfx::DrawCall& base, const std::array<float, 3>& color,
                          float distance, float time) const {
    const Material& m = materials_[size_t(material)];
    if (m.stages.empty() || distance <= 0)
        return;
    gfx::DrawCall call = base;
    call.texture = whiteTexture_;
    call.texGen = gfx::TexGen::Uv;
    call.rgbSource = call.alphaSource = gfx::ColorSource::Constant;
    call.color = {color[0], color[1], color[2], 1};
    call.fogDistance = distance;
    call.time = time;
    call.deforms = m.deforms;
    call.state = {};
    call.state.src = gfx::Blend::SrcAlpha;
    call.state.dst = gfx::Blend::OneMinusSrcAlpha;
    call.state.depthWrite = false;
    call.state.depthEqual = m.sort <= kSortOpaque;
    call.state.cull = m.stages.front().state.cull;
    call.state.depthBias = m.stages.front().state.depthBias;
    r_.draw(call);
}

} // namespace reluva::idtech3
