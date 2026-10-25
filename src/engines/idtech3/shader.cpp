// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/shader.h"
#include "engines/idtech3/lexer.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace reluva::idtech3 {

namespace {

// The keyword and the rest of its line.
Directive readDirective(Lexer& lex, std::string_view keyword) {
    Directive d{fs::toLower(keyword), {}};
    while (auto arg = lex.next(false))
        d.args.emplace_back(*arg);
    return d;
}

std::string lower(const std::string& s) { return fs::toLower(s); }

// A directive's arguments with any "(" and ")" tokens dropped, as vectors
// are written "( 1 0 0 )".
std::vector<std::string> plainArgs(const Directive& d) {
    std::vector<std::string> out;
    for (const std::string& a : d.args) {
        std::string t;
        for (char c : a)
            if (c != '(' && c != ')')
                t += c;
        if (!t.empty())
            out.push_back(t);
    }
    return out;
}

float num(const std::vector<std::string>& args, size_t i, float fallback = 0) {
    return i < args.size() ? std::strtof(args[i].c_str(), nullptr) : fallback;
}

bool waveFunc(const std::string& name, gfx::WaveFunc& out) {
    static const std::pair<const char*, gfx::WaveFunc> funcs[] = {
        {"sin", gfx::WaveFunc::Sin},
        {"triangle", gfx::WaveFunc::Triangle},
        {"square", gfx::WaveFunc::Square},
        {"sawtooth", gfx::WaveFunc::Sawtooth},
        {"inversesawtooth", gfx::WaveFunc::InverseSawtooth},
        {"noise", gfx::WaveFunc::Noise},
    };
    for (const auto& [n, f] : funcs)
        if (lower(name) == n) {
            out = f;
            return true;
        }
    return false;
}

// "<func> <base> <amplitude> <phase> <frequency>" starting at args[i].
gfx::Wave readWave(const std::vector<std::string>& args, size_t i) {
    gfx::Wave w;
    if (i < args.size())
        waveFunc(args[i], w.func);
    w.base = num(args, i + 1);
    w.amplitude = num(args, i + 2);
    w.phase = num(args, i + 3);
    w.frequency = num(args, i + 4);
    return w;
}

// Returns false for a directive it doesn't know.
bool applyStageDirective(ShaderStage& stage, const Directive& d) {
    const std::vector<std::string> args = plainArgs(d);
    const std::string a0 = args.empty() ? std::string() : lower(args[0]);
    if (d.keyword == "map" || d.keyword == "clampmap") {
        stage.clamp = d.keyword == "clampmap";
        if (a0 == "$lightmap")
            stage.lightmap = true;
        else if (a0 == "$whiteimage" || a0 == "*white") // the renderer's built-in white image
            stage.whiteImage = true;
        else if (!args.empty())
            stage.maps = {args[0]};
    } else if (d.keyword == "animmap") {
        stage.animFps = num(args, 0);
        if (args.size() >= 2)
            stage.maps.assign(args.begin() + 1, args.end());
    } else if (d.keyword == "blendfunc") {
        // Shorthands, and the explicit two-factor form.
        if (a0 == "add") {
            stage.blendSrc = "gl_one";
            stage.blendDst = "gl_one";
        } else if (a0 == "filter") {
            stage.blendSrc = "gl_dst_color";
            stage.blendDst = "gl_zero";
        } else if (a0 == "blend") {
            stage.blendSrc = "gl_src_alpha";
            stage.blendDst = "gl_one_minus_src_alpha";
        } else if (args.size() >= 2) {
            stage.blendSrc = a0;
            stage.blendDst = lower(args[1]);
        }
    } else if (d.keyword == "rgbgen") {
        using R = ShaderStage::RgbGen;
        static const std::pair<const char*, R> gens[] = {
            {"identity", R::Identity}, {"identitylighting", R::IdentityLighting}, {"vertex", R::Vertex},
            {"exactvertex", R::ExactVertex}, {"oneminusvertex", R::OneMinusVertex},
            {"lightingdiffuse", R::LightingDiffuse}, {"wave", R::Wave}, {"const", R::Const},
            {"entity", R::Entity}, {"oneminusentity", R::OneMinusEntity},
        };
        for (const auto& [n, g] : gens)
            if (a0 == n)
                stage.rgbGen = g;
        if (stage.rgbGen == R::Wave)
            stage.rgbWave = readWave(args, 1);
        else if (stage.rgbGen == R::Const)
            stage.rgbConst = {num(args, 1), num(args, 2), num(args, 3)};
    } else if (d.keyword == "alphagen") {
        using A = ShaderStage::AlphaGen;
        static const std::pair<const char*, A> gens[] = {
            {"identity", A::Identity}, {"vertex", A::Vertex}, {"oneminusvertex", A::OneMinusVertex},
            {"wave", A::Wave}, {"const", A::Const}, {"entity", A::Entity}, {"oneminusentity", A::OneMinusEntity},
            {"lightingspecular", A::LightingSpecular}, {"portal", A::Portal},
        };
        for (const auto& [n, g] : gens)
            if (a0 == n)
                stage.alphaGen = g;
        if (stage.alphaGen == A::Wave)
            stage.alphaWave = readWave(args, 1);
        else if (stage.alphaGen == A::Const)
            stage.alphaConst = num(args, 1, 1);
        else if (stage.alphaGen == A::Portal)
            stage.portalRange = num(args, 1, 256);
    } else if (d.keyword == "tcgen" || d.keyword == "texgen") {
        using T = ShaderStage::TcGen;
        if (a0 == "base" || a0 == "texture")
            stage.tcGen = T::Base;
        else if (a0 == "lightmap")
            stage.tcGen = T::Lightmap;
        else if (a0 == "environment")
            stage.tcGen = T::Environment;
        else if (a0 == "vector") {
            stage.tcGen = T::Vector;
            stage.tcVectors = {{{num(args, 1), num(args, 2), num(args, 3)}, {num(args, 4), num(args, 5), num(args, 6)}}};
        }
    } else if (d.keyword == "tcmod") {
        using M = ShaderStage::TcMod::Type;
        ShaderStage::TcMod m{M::EntityTranslate, {}, {}};
        if (a0 == "rotate")
            m = {M::Rotate, {num(args, 1)}, {}};
        else if (a0 == "scale")
            m = {M::Scale, {num(args, 1), num(args, 2)}, {}};
        else if (a0 == "scroll")
            m = {M::Scroll, {num(args, 1), num(args, 2)}, {}};
        else if (a0 == "stretch")
            m = {M::Stretch, {}, readWave(args, 1)};
        else if (a0 == "transform")
            m = {M::Transform, {num(args, 1), num(args, 2), num(args, 3), num(args, 4), num(args, 5), num(args, 6)}, {}};
        else if (a0 == "turb") {
            // "turb [sin] <base> <amplitude> <phase> <frequency>": the
            // function name is optional.
            gfx::WaveFunc f;
            const size_t at = args.size() > 1 && waveFunc(args[1], f) ? 2 : 1;
            m = {M::Turb, {}, {}};
            m.wave.base = num(args, at);
            m.wave.amplitude = num(args, at + 1);
            m.wave.phase = num(args, at + 2);
            m.wave.frequency = num(args, at + 3);
        } else if (a0 != "entitytranslate")
            return false;
        stage.tcMods.push_back(m);
    } else if (d.keyword == "alphafunc") {
        stage.alphaFunc = a0 == "gt0" ? gfx::AlphaTest::Gt0 : a0 == "lt128" ? gfx::AlphaTest::Lt128
                                                      : a0 == "ge128" ? gfx::AlphaTest::Ge128
                                                                      : gfx::AlphaTest::None;
    } else if (d.keyword == "depthfunc") {
        stage.depthEqual = a0 == "equal";
    } else if (d.keyword == "depthwrite") {
        stage.depthWrite = true;
    } else if (d.keyword == "detail") {
        stage.detail = true;
    } else {
        return false;
    }
    return true;
}

// Returns false for a directive it doesn't know.
bool applyShaderDirective(ShaderDef& def, const Directive& d) {
    const std::vector<std::string> args = plainArgs(d);
    const std::string a0 = args.empty() ? std::string() : lower(args[0]);
    if (d.keyword == "surfaceparm") {
        if (!args.empty())
            def.surfaceParms.push_back(a0);
    } else if (d.keyword == "skyparms") {
        def.sky = true;
        def.skyFarBox = a0 == "-" ? std::string() : args.empty() ? std::string() : args[0];
        if (const float h = num(args, 1); h > 0)
            def.cloudHeight = h;
    } else if (d.keyword == "fogparms") {
        def.fog = true;
        def.fogColor = {num(args, 0), num(args, 1), num(args, 2)};
        def.fogDistance = num(args, 3);
    } else if (d.keyword == "cull") {
        if (a0 == "none" || a0 == "disable" || a0 == "twosided")
            def.cull = ShaderDef::Cull::None;
        else if (a0 == "back" || a0 == "backside" || a0 == "backsided")
            def.cull = ShaderDef::Cull::Back;
        else
            def.cull = ShaderDef::Cull::Front;
    } else if (d.keyword == "deformvertexes") {
        using T = ShaderDeform::Type;
        ShaderDeform df;
        if (a0 == "wave") {
            df.type = T::Wave;
            const float div = num(args, 1);
            df.spread = div == 0 ? 100.0f : 1.0f / div; // as the original treats a 0
            df.wave = readWave(args, 2);
        } else if (a0 == "normal") {
            df.type = T::Normal;
            df.wave.amplitude = num(args, 1);
            df.wave.frequency = num(args, 2);
        } else if (a0 == "bulge") {
            df.type = T::Bulge;
            df.width = num(args, 1);
            df.height = num(args, 2);
            df.speed = num(args, 3);
        } else if (a0 == "move") {
            df.type = T::Move;
            df.vector = {num(args, 1), num(args, 2), num(args, 3)};
            df.wave = readWave(args, 4);
        } else if (a0 == "autosprite") {
            df.type = T::Autosprite;
        } else if (a0 == "autosprite2") {
            df.type = T::Autosprite2;
        } else if (a0.rfind("text", 0) == 0) {
            df.type = T::Text;
        } else if (a0 == "projectionshadow") {
            df.type = T::ProjectionShadow;
        }
        def.deforms.push_back(df);
    } else if (d.keyword == "sort") {
        static const std::pair<const char*, float> keys[] = {
            {"portal", 1}, {"sky", 2}, {"opaque", 3}, {"decal", 4}, {"seethrough", 5},
            {"banner", 6}, {"additive", 10}, {"nearest", 16}, {"underwater", 8},
        };
        def.sort = num(args, 0);
        for (const auto& [n, k] : keys)
            if (a0 == n)
                def.sort = k;
    } else if (d.keyword == "polygonoffset") {
        def.polygonOffset = true;
    } else if (d.keyword == "nomipmaps") {
        def.noMipmaps = def.noPicmip = true;
    } else if (d.keyword == "nopicmip") {
        def.noPicmip = true;
    } else if (d.keyword == "portal") {
        def.portal = true;
    } else if (d.keyword.rfind("qer_", 0) == 0 || d.keyword.rfind("q3map_", 0) == 0 || d.keyword == "light" ||
               d.keyword == "tesssize" || d.keyword == "entitymergable" || d.keyword == "fogonly" ||
               d.keyword == "lightning" || d.keyword == "cloudparms") {
        // For the editor and the map compiler, or nothing the renderer uses.
    } else {
        return false;
    }
    return true;
}

} // namespace

bool ShaderDef::hasSurfaceParm(std::string_view parm) const {
    const std::string p = fs::toLower(parm);
    return std::find(surfaceParms.begin(), surfaceParms.end(), p) != surfaceParms.end();
}

Error parseShaderFile(std::string_view text, const std::string& file, std::vector<ShaderDef>& out) {
    Lexer lex(text);
    auto where = [&] { return file + " line " + std::to_string(lex.line()); };
    while (auto name = lex.next()) {
        ShaderDef def;
        def.name = std::string(*name);
        def.file = file;
        auto open = lex.next();
        if (!open || *open != "{")
            return Error::fail(where() + ": expected '{' after shader " + def.name);
        bool closed = false;
        while (auto tok = lex.next()) {
            if (*tok == "}") {
                closed = true;
                break;
            }
            if (*tok == "{") {
                ShaderStage stage;
                bool stageClosed = false;
                while (auto st = lex.next()) {
                    if (*st == "}") {
                        stageClosed = true;
                        break;
                    }
                    if (*st == "{") { // no nesting inside stages; skip it whole
                        lex.skipBlock();
                        continue;
                    }
                    stage.directives.push_back(readDirective(lex, *st));
                    if (!applyStageDirective(stage, stage.directives.back()))
                        def.unknown.push_back(stage.directives.back().keyword);
                }
                if (!stageClosed)
                    return Error::fail(where() + ": shader " + def.name + " ends inside a stage");
                def.stages.push_back(std::move(stage));
                continue;
            }
            Directive d = readDirective(lex, *tok);
            if (!applyShaderDirective(def, d))
                def.unknown.push_back(d.keyword);
            def.directives.push_back(std::move(d));
        }
        if (!closed)
            return Error::fail(where() + ": shader " + def.name + " is not closed");
        out.push_back(std::move(def));
    }
    return Error::success();
}

std::string findImage(const SearchSet& files, std::string_view name) {
    std::string base(name);
    const size_t dot = base.find_last_of('.');
    const size_t slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        base.resize(dot);
    for (const char* ext : {".tga", ".jpg", ".png"})
        if (files.has(base + ext))
            return base + ext;
    return {};
}

void ShaderLibrary::add(ShaderDef def) {
    if (byName_.try_emplace(normalizeArchiveName(def.name), shaders_.size()).second)
        shaders_.push_back(std::move(def));
}

const ShaderDef* ShaderLibrary::find(std::string_view name) const {
    // Like the original, without any extension: models and skins often
    // name "models/x/y.tga" where a script defines "models/x/y".
    std::string key = normalizeArchiveName(name);
    const size_t dot = key.find_last_of('.'), slash = key.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        key.resize(dot);
    auto it = byName_.find(key);
    return it == byName_.end() ? nullptr : &shaders_[it->second];
}

void ShaderLibrary::load(const SearchSet& files, std::vector<std::string>* warnings) {
    shaders_.clear();
    byName_.clear();
    std::vector<uint8_t> data;
    for (const std::string& name : files.list()) {
        const std::string key = normalizeArchiveName(name);
        if (key.rfind("scripts/", 0) != 0 || key.size() < 7 || key.substr(key.size() - 7) != ".shader")
            continue;
        if (Error err = files.read(name, data)) {
            if (warnings)
                warnings->push_back(err.message);
            continue;
        }
        std::vector<ShaderDef> defs;
        Error err = parseShaderFile(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), name,
                                    defs);
        if (err && warnings)
            warnings->push_back(err.message);
        // Keep what parsed before a problem.
        for (ShaderDef& d : defs)
            add(std::move(d));
    }
}

} // namespace reluva::idtech3
