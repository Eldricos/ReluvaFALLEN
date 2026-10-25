// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/shader.h"

#include "common/archive.h"
#include "common/gfx3d.h"

#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// How a surface is lit, which decides the original's implicit shader for
// names without a script and what $lightmap and lightingDiffuse mean:
// lightmapped map surfaces, map surfaces lit by their vertex colours, and
// models lit by a light (the light grid, for models in a map); and
// pictures drawn flat on the screen (menus, the HUD), tinted by their
// vertex colours, with no depth test or culling.
// Effect: the game's sprites and beams, whose colour is in their
// vertices; both sides drawn.
enum class Lighting { Lightmap, Vertex, Diffuse, TwoD, Effect };

// Shaders turned into what the GPU draws, shared by everything a page
// shows: each stage's textures loaded and states decided once, the parts
// that change with time evaluated per draw. The rules follow the
// original renderer, see docs/formats/idtech3/shader.md.
class ShaderCache {
public:
    // One stage, resolved.
    struct Stage {
        std::vector<gfx::TextureId> frames; // empty for a $lightmap stage
        float fps = 0;
        bool lightmap = false;
        gfx::DrawState state;
        const ShaderStage* def = nullptr;
        gfx::TexGen texGen = gfx::TexGen::Uv;
    };
    struct Material {
        bool skip = false; // sky, fog volume, nodraw or no stages
        float sort = 3;
        Lighting lighting = Lighting::Lightmap;
        std::vector<Stage> stages;
        std::array<gfx::Deform, 2> deforms{};
        bool portal = false;    // a mirror or portal surface
        float portalRange = 0;  // beyond which a portal's view isn't drawn (mirrors: always is)
    };

    ShaderCache(gfx::Renderer3D& renderer, const SearchSet& files, const ShaderLibrary& shaders);
    ~ShaderCache() { clear(); }
    ShaderCache(const ShaderCache&) = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;

    // Frees every texture; materials handed out before are invalid.
    void clear();

    // The material for a shader name lit one way, made on first use.
    int material(const std::string& name, Lighting lighting);
    // A material from a shader definition, which must outlive the cache.
    // `sky`: a sky shader's stages, its cloud layers (other materials skip
    // sky shaders).
    int material(const ShaderDef& def, Lighting lighting, bool sky = false);
    const Material& get(int material) const { return materials_[size_t(material)]; }

    // `picmip`: the texture detail setting may make it smaller.
    gfx::TextureId image(const std::string& name, bool clamp, bool mipmaps, bool picmip = false);
    // The video settings for textures loaded from now on: each halved
    // `picmip` times (unless its shader says nopicmip), and how mipmapped
    // ones are filtered (r_texturemode).
    struct TextureOptions {
        int picmip = 0;
        bool nearest = false, mipmaps = true, mipNearest = false;
        bool anisotropic = false; // r_ext_texture_filter_anisotropic
    };
    void setTextureOptions(const TextureOptions& options) { textureOptions_ = options; }
    gfx::TextureId white();

    // Draws every stage of `material` over the geometry `base` describes
    // (mesh, index range, vertex offset, model matrix, light). `lightmap`
    // is what $lightmap stages show (0: white). `sky` draws cloud layers
    // around the camera, behind everything.
    // `entity`: the colour rgbGen and alphaGen entity ask for (white if
    // not given).
    void draw(int material, const gfx::DrawCall& base, gfx::TextureId lightmap, float time, bool sky = false,
              const std::array<float, 4>* entity = nullptr) const;
    // A fog pass over geometry drawn with `material`.
    void drawFog(int material, const gfx::DrawCall& base, const std::array<float, 3>& color, float distance,
                 float time) const;

    size_t imageCount() const { return images_.size(); }
    int missingImages() const { return missing_; }
    // Features met that aren't drawn, with how many shaders use each.
    const std::map<std::string, int>& unsupported() const { return unsupported_; }

private:
    Material makeMaterial(const ShaderDef& def, Lighting lighting, bool sky);
    Stage makeStage(const ShaderStage& def, const ShaderDef& shader);
    void note(const std::string& feature) { ++unsupported_[feature]; }

    gfx::Renderer3D& r_;
    const SearchSet& files_;
    const ShaderLibrary& shaders_;
    std::vector<Material> materials_;
    std::map<std::pair<std::string, Lighting>, int> byName_;
    std::deque<ShaderDef> implicit_; // the original's shaders for names without a script, and pictures' copies (stable addresses)
    std::unordered_map<std::string, gfx::TextureId> images_; // by normalized name and flags
    gfx::TextureId missingTexture_ = 0;
    gfx::TextureId whiteTexture_ = 0;
    int missing_ = 0;
    std::map<std::string, int> unsupported_;
    TextureOptions textureOptions_;
};

} // namespace reluva::idtech3
