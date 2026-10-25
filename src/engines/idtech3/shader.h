// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// One directive inside a shader or stage: its keyword (lower case) and the
// rest of its line's tokens as written.
struct Directive {
    std::string keyword;
    std::vector<std::string> args;
};

// A rendering pass ("stage") of a shader, as described in
// docs/formats/idtech3/shader.md. Every directive is also kept, as
// written, in `directives`.
struct ShaderStage {
    std::vector<std::string> maps; // one image, or animMap's frames
    float animFps = 0;             // animMap's rate; 0 = not animated
    bool clamp = false;            // clampMap
    bool lightmap = false;         // map $lightmap
    bool whiteImage = false;       // map $whiteimage
    std::string blendSrc, blendDst; // GL_* names, lower case; empty = opaque

    enum class RgbGen : uint8_t {
        Default, Identity, IdentityLighting, Vertex, ExactVertex, OneMinusVertex, LightingDiffuse,
        Wave, Const, Entity, OneMinusEntity,
    } rgbGen = RgbGen::Default;
    gfx::Wave rgbWave;
    std::array<float, 3> rgbConst{1, 1, 1};
    enum class AlphaGen : uint8_t {
        Default, Identity, Vertex, OneMinusVertex, Wave, Const, Entity, OneMinusEntity, LightingSpecular, Portal,
    } alphaGen = AlphaGen::Default;
    gfx::Wave alphaWave;
    float alphaConst = 1;
    float portalRange = 256; // alphaGen portal's distance

    enum class TcGen : uint8_t { Default, Base, Lightmap, Environment, Vector } tcGen = TcGen::Default;
    std::array<std::array<float, 3>, 2> tcVectors{};
    struct TcMod {
        enum class Type : uint8_t { Rotate, Scale, Scroll, Stretch, Transform, Turb, EntityTranslate } type;
        std::array<float, 6> v{}; // rotate: degrees/s; scale, scroll: s t; transform: m00 m01 m10 m11 t0 t1
        gfx::Wave wave;           // stretch, turb (turb ignores func)
    };
    std::vector<TcMod> tcMods; // in order

    gfx::AlphaTest alphaFunc = gfx::AlphaTest::None;
    bool depthEqual = false; // depthFunc equal
    bool depthWrite = false; // depthWrite (opaque stages write depth anyway)
    bool detail = false;

    std::vector<Directive> directives;
};

struct ShaderDeform {
    enum class Type : uint8_t {
        Wave, Normal, Bulge, Move, Autosprite, Autosprite2, Text, ProjectionShadow, Other,
    } type = Type::Other;
    float spread = 0;                // wave: 1 / div
    gfx::Wave wave;                  // wave, normal (amplitude, frequency), move
    std::array<float, 3> vector{};   // move
    float width = 0, height = 0, speed = 0; // bulge
};

struct ShaderDef {
    std::string name;   // as written, e.g. "textures/borg/borgfloor1"
    std::string file;   // the .shader file it came from
    std::vector<std::string> surfaceParms; // lower case
    std::vector<ShaderStage> stages;
    std::vector<Directive> directives; // the shader-level ones

    enum class Cull : uint8_t { Front, Back, None } cull = Cull::Front; // the side that's hidden
    std::vector<ShaderDeform> deforms;
    float sort = 0;             // 0 = not given; see sortKey() in world_renderer.cpp
    bool polygonOffset = false;
    bool noMipmaps = false, noPicmip = false;
    bool portal = false;

    bool sky = false;           // skyParms
    std::string skyFarBox;      // e.g. "env/space1" for env/space1_rt.tga...; empty for none
    float cloudHeight = 512;
    bool fog = false;           // fogParms
    std::array<float, 3> fogColor{};
    float fogDistance = 0;      // where the fog becomes opaque

    // Directives this parser doesn't know, lower case, shader and stage
    // ones alike, for finding out what a game adds.
    std::vector<std::string> unknown;

    bool hasSurfaceParm(std::string_view parm) const;
};

// Parse every shader in one .shader file's text, appending to `out`.
// `file` is recorded in each definition and used in messages.
Error parseShaderFile(std::string_view text, const std::string& file, std::vector<ShaderDef>& out);

// The file an image name refers to: shaders and maps name images with or
// without an extension, and the original finds the .tga or the .jpg
// whichever is written (.png too, for mods). Empty if there's none.
std::string findImage(const SearchSet& files, std::string_view name);

// Every shader of a game: all scripts/*.shader files in a search set. When
// two files define a name, the first parsed wins; files are parsed in name
// order. Problems with single files are collected, not fatal.
class ShaderLibrary {
public:
    void load(const SearchSet& files, std::vector<std::string>* warnings = nullptr);
    void add(ShaderDef def);

    // By name, ignoring case, slash direction and any extension (a model's
    // "models/x/y.tga" finds the script "models/x/y", as in the original);
    // nullptr if undefined (then the engine uses the image of that name,
    // see shader.md).
    const ShaderDef* find(std::string_view name) const;
    size_t size() const { return shaders_.size(); }
    const std::vector<ShaderDef>& all() const { return shaders_; }

private:
    std::vector<ShaderDef> shaders_;
    std::unordered_map<std::string, size_t> byName_;
};

} // namespace reluva::idtech3
