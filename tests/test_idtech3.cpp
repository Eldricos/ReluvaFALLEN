// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the id Tech 3 engine's readers, on data built here.

#include "engines/idtech3/bindings.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/viewers.h"
#include "common/audio/wav.h"
#include "engines/registry.h"
#include "data/qvm/fake_cgame_qvm.h"
#include "data/qvm/fake_game_qvm.h"
#include "data/qvm/fake_ui_qvm.h"
#include "data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void testLexer() {
    Lexer lex("first \"two words\" // comment\n/* block\ncomment */ third\n  fourth fifth\n{");
    CHECK(lex.next() == "first");
    CHECK(lex.next() == "two words");
    CHECK(!lex.next(false)); // the rest of the line is a comment
    CHECK(lex.peek() == "third" && lex.next() == "third" && lex.line() == 3);
    CHECK(lex.next() == "fourth");
    lex.skipLine();
    CHECK(lex.next() == "{" && lex.line() == 5);
    CHECK(!lex.next() && lex.atEnd());

    Lexer block("a { b { c } d } e");
    CHECK(block.next() == "a" && block.next() == "{" && block.skipBlock() && block.next() == "e");
}

static void testEntities() {
    const std::string lump = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube\"\n}\n"
                             "{\n\"classname\" \"info_player_start\"\n\"origin\" \"64 -32 24\"\n\"ORIGIN\" \"0 0 0\"\n}\n";
    std::vector<Entity> ents;
    CHECK(!parseEntities(lump + std::string(1, '\0') + "junk", ents));
    CHECK(ents.size() == 2);
    CHECK(ents[0].classname() == "worldspawn" && ents[0].get("Message") == "Borg Cube");
    float origin[3];
    CHECK(ents[1].vector("origin", origin) && origin[0] == 64 && origin[1] == -32 && origin[2] == 24);
    CHECK(!ents[1].vector("angles", origin) && ents[1].get("angles", "0") == "0");
    CHECK(parseEntities("{ \"classname\" }", ents)); // a key without a value
    CHECK(parseEntities("\"classname\" \"x\"", ents));
    CHECK(parseEntities("{ \"a\" \"b\"", ents));
}

static void testShaderStages() {
    const std::string text = R"(
textures/effects/panel
{
    cull none
    deformVertexes wave 64 sin 0 3 0 0.5
    deformVertexes move 0 0 1 sawtooth 0 8 0 1
    deformVertexes autosprite
    sort additive
    polygonOffset
    nomipmaps
    efMadeUpDirective 1 2
    {
        map textures/effects/a.tga
        rgbGen wave triangle 0.5 0.5 0.25 2
        alphaGen const 0.4
        tcGen vector ( 0.01 0 0 ) ( 0 0.01 0 )
        tcMod scroll 0.5 -1
        tcMod turb 0 0.1 0 0.3
        tcMod rotate 90
        tcMod stretch sin 1 0.2 0 1
        tcMod transform 1 0 0 1 0.5 0.25
        depthFunc equal
        depthWrite
        alphaFunc GE128
    }
    {
        map $whiteimage
        rgbGen const ( 0.25 0.5 1 )
        alphaGen oneMinusVertex
        tcGen environment
        tcMod turb sin 0 0.2 0.5 1
        efStageThing
    }
}
env/sky
{
    skyParms env/space - -
    fogParms ( 0.5 0.25 0 ) 300
    sort 7
}
)";
    std::vector<ShaderDef> defs;
    CHECK(!parseShaderFile(text, "x.shader", defs));
    CHECK(defs.size() == 2);
    if (defs.size() != 2 || defs[0].stages.size() != 2)
        return;
    const ShaderDef& d = defs[0];
    CHECK(d.cull == ShaderDef::Cull::None && d.sort == 10 && d.polygonOffset && d.noMipmaps);
    CHECK(d.deforms.size() == 3);
    CHECK(d.deforms[0].type == ShaderDeform::Type::Wave && d.deforms[0].spread == 1.0f / 64);
    CHECK(d.deforms[0].wave.amplitude == 3 && d.deforms[0].wave.frequency == 0.5f);
    CHECK(d.deforms[1].type == ShaderDeform::Type::Move && d.deforms[1].vector[2] == 1 &&
          d.deforms[1].wave.func == gfx::WaveFunc::Sawtooth && d.deforms[1].wave.amplitude == 8);
    CHECK(d.deforms[2].type == ShaderDeform::Type::Autosprite);
    CHECK((d.unknown == std::vector<std::string>{"efmadeupdirective", "efstagething"}));

    const ShaderStage& a = d.stages[0];
    CHECK(a.rgbGen == ShaderStage::RgbGen::Wave && a.rgbWave.func == gfx::WaveFunc::Triangle &&
          a.rgbWave.phase == 0.25f && a.rgbWave.frequency == 2);
    CHECK(a.alphaGen == ShaderStage::AlphaGen::Const && a.alphaConst == 0.4f);
    CHECK(a.tcGen == ShaderStage::TcGen::Vector && a.tcVectors[0][0] == 0.01f && a.tcVectors[1][1] == 0.01f);
    CHECK(a.tcMods.size() == 5);
    if (a.tcMods.size() == 5) {
        using M = ShaderStage::TcMod::Type;
        CHECK(a.tcMods[0].type == M::Scroll && a.tcMods[0].v[0] == 0.5f && a.tcMods[0].v[1] == -1);
        CHECK(a.tcMods[1].type == M::Turb && a.tcMods[1].wave.amplitude == 0.1f && a.tcMods[1].wave.frequency == 0.3f);
        CHECK(a.tcMods[2].type == M::Rotate && a.tcMods[2].v[0] == 90);
        CHECK(a.tcMods[3].type == M::Stretch && a.tcMods[3].wave.base == 1 && a.tcMods[3].wave.amplitude == 0.2f);
        CHECK(a.tcMods[4].type == M::Transform && a.tcMods[4].v[4] == 0.5f && a.tcMods[4].v[5] == 0.25f);
    }
    CHECK(a.depthEqual && a.depthWrite && a.alphaFunc == gfx::AlphaTest::Ge128);

    const ShaderStage& b = d.stages[1];
    CHECK(b.whiteImage && b.rgbGen == ShaderStage::RgbGen::Const && b.rgbConst[1] == 0.5f && b.rgbConst[2] == 1);
    CHECK(b.alphaGen == ShaderStage::AlphaGen::OneMinusVertex && b.tcGen == ShaderStage::TcGen::Environment);
    // turb with the (ignored) function name written
    CHECK(b.tcMods.size() == 1 && b.tcMods[0].wave.amplitude == 0.2f && b.tcMods[0].wave.phase == 0.5f);

    const ShaderDef& sky = defs[1];
    CHECK(sky.sky && sky.skyFarBox == "env/space" && sky.cloudHeight == 512 && sky.sort == 7);
    CHECK(sky.fog && sky.fogColor[0] == 0.5f && sky.fogColor[1] == 0.25f && sky.fogDistance == 300);
}

static void testWaves() {
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    gfx::Wave w{gfx::WaveFunc::Sin, 1, 2, 0, 1};
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::Triangle;
    CHECK(approx(gfx::evaluate(w, 0.25f), 3) && approx(gfx::evaluate(w, 0.5f), 1) && approx(gfx::evaluate(w, 0.75f), -1));
    w.func = gfx::WaveFunc::Square;
    CHECK(approx(gfx::evaluate(w, 0.1f), 3) && approx(gfx::evaluate(w, 0.6f), -1));
    w.func = gfx::WaveFunc::Sawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 1.5f) && approx(gfx::evaluate(w, 1.25f), 1.5f));
    w.func = gfx::WaveFunc::InverseSawtooth;
    CHECK(approx(gfx::evaluate(w, 0.25f), 2.5f));
    w = {gfx::WaveFunc::Sin, 0, 1, 0.25f, 0}; // no frequency: constant, set by the phase
    CHECK(approx(gfx::evaluate(w, 0), 1) && approx(gfx::evaluate(w, 100), 1));
    w.func = gfx::WaveFunc::Noise;
    w.frequency = 1;
    for (float t = 0; t < 5; t += 0.37f)
        CHECK(gfx::evaluate(w, t) >= -1 && gfx::evaluate(w, t) <= 1);
}

static void testShaders() {
    const std::string text = R"(
// A comment before the first shader.
textures/borg/floor
{
    surfaceparm metalsteps
    qer_editorimage textures/borg/floor.tga
    {
        map $lightmap
    }
    {
        map textures/borg/floor.tga
        blendFunc filter
    }
    {
        animMap 4 textures/borg/a.tga textures/borg/b.tga
        blendfunc GL_ONE GL_ONE
    }
}
textures/sky/stars
{
    skyParms env/stars - -
    surfaceparm NoDraw
}
)";
    std::vector<ShaderDef> defs;
    CHECK(!parseShaderFile(text, "scripts/borg.shader", defs));
    CHECK(defs.size() == 2);
    if (defs.size() == 2) {
        const ShaderDef& f = defs[0];
        CHECK(f.name == "textures/borg/floor" && f.file == "scripts/borg.shader");
        CHECK(f.hasSurfaceParm("MetalSteps") && f.directives.size() == 2 && !f.sky);
        CHECK(f.stages.size() == 3);
        CHECK(f.stages[0].lightmap && f.stages[0].maps.empty() && f.stages[0].blendSrc.empty());
        CHECK(f.stages[1].maps.size() == 1 && f.stages[1].maps[0] == "textures/borg/floor.tga");
        CHECK(f.stages[1].blendSrc == "gl_dst_color" && f.stages[1].blendDst == "gl_zero");
        CHECK(f.stages[2].animFps == 4 && f.stages[2].maps.size() == 2 && f.stages[2].blendDst == "gl_one");
        CHECK(defs[1].sky && defs[1].hasSurfaceParm("nodraw"));
    }
    defs.clear();
    CHECK(parseShaderFile("broken\n{\n  { map x.tga\n", "x.shader", defs));
    CHECK(parseShaderFile("nobrace map", "x.shader", defs));

    ShaderLibrary lib;
    ShaderDef first, second;
    first.name = "Textures/A";
    first.file = "first.shader";
    second.name = "textures/a";
    second.file = "second.shader";
    lib.add(first);
    lib.add(second);
    CHECK(lib.size() == 1 && lib.find("TEXTURES\\a")->file == "first.shader" && !lib.find("textures/b"));
    // An extension is ignored, as models name their shaders "....tga".
    CHECK(lib.find("textures/a.tga") && lib.find("Textures/A.JPG") && !lib.find("textures/a.b/c"));
}

// A tiny map: one triangle in one leaf of a two-leaf tree, one brush,
// one lightmap and vis data for two clusters.
static std::vector<uint8_t> buildBsp(int version = 46, int badIndex = -1, int32_t lightmap = 0, int32_t fog = -1,
                                     int32_t type = 1) {
    std::vector<uint8_t> lumps[17];
    auto i32 = [](std::vector<uint8_t>& v, int32_t x) {
        for (int i = 0; i < 4; ++i)
            v.push_back(uint8_t(uint32_t(x) >> (8 * i)));
    };
    auto f32 = [&](std::vector<uint8_t>& v, float f) {
        int32_t x;
        std::memcpy(&x, &f, 4);
        i32(v, x);
    };
    auto name = [](std::vector<uint8_t>& v, const char* s) {
        std::vector<uint8_t> n(64, 0);
        std::memcpy(n.data(), s, std::strlen(s));
        v.insert(v.end(), n.begin(), n.end());
    };
    const std::string ents = "{ \"classname\" \"worldspawn\" }";
    lumps[0].assign(ents.begin(), ents.end());
    lumps[0].push_back(0);
    name(lumps[1], "textures/borg/floor");
    i32(lumps[1], 0); i32(lumps[1], 1);
    f32(lumps[2], 0); f32(lumps[2], 0); f32(lumps[2], 1); f32(lumps[2], 16);
    i32(lumps[3], 0); i32(lumps[3], -1); i32(lumps[3], -2);
    for (int k = 0; k < 6; ++k)
        i32(lumps[3], k < 3 ? -64 : 64);
    for (int leaf = 0; leaf < 2; ++leaf) {
        i32(lumps[4], leaf); i32(lumps[4], 0);
        for (int k = 0; k < 6; ++k)
            i32(lumps[4], k < 3 ? -64 : 64);
        i32(lumps[4], 0); i32(lumps[4], leaf); i32(lumps[4], 0); i32(lumps[4], leaf);
    }
    i32(lumps[5], 0);
    i32(lumps[6], 0);
    for (float f : {-64.f, -64.f, -64.f, 64.f, 64.f, 64.f})
        f32(lumps[7], f);
    i32(lumps[7], 0); i32(lumps[7], 1); i32(lumps[7], 0); i32(lumps[7], 1);
    i32(lumps[8], 0); i32(lumps[8], 1); i32(lumps[8], 0);
    i32(lumps[9], 0); i32(lumps[9], 0);
    for (int v = 0; v < 3; ++v) {
        f32(lumps[10], v == 1 ? 32.f : 0.f); f32(lumps[10], v == 2 ? 32.f : 0.f); f32(lumps[10], 16);
        for (int k = 0; k < 4; ++k)
            f32(lumps[10], 0.5f);
        f32(lumps[10], 0); f32(lumps[10], 0); f32(lumps[10], 1);
        for (uint8_t c : {255, 128, 64, 255})
            lumps[10].push_back(c);
    }
    i32(lumps[11], 0); i32(lumps[11], 1); i32(lumps[11], badIndex >= 0 ? badIndex : 2);
    // Surface: shader 0, no fog, planar, vertices 0-2, indices 0-2, lightmap 0.
    for (int32_t x : {0, fog, type, 0, 3, 0, 3, lightmap, 0, 0, 16, 16})
        i32(lumps[13], x);
    for (int k = 0; k < 12; ++k)
        f32(lumps[13], k == 11 ? 1.f : 0.f);
    i32(lumps[13], 0); i32(lumps[13], 0);
    lumps[14].assign(kLightmapSize * kLightmapSize * 3, 7);
    i32(lumps[16], 2); i32(lumps[16], 1);
    lumps[16].push_back(0x01); // cluster 0 sees itself
    lumps[16].push_back(0x03); // cluster 1 sees both

    std::vector<uint8_t> out = {'I', 'B', 'S', 'P'};
    i32(out, version);
    int32_t offset = 8 + 17 * 8;
    for (const auto& l : lumps) {
        i32(out, offset);
        i32(out, int32_t(l.size()));
        offset += int32_t(l.size());
    }
    for (const auto& l : lumps)
        out.insert(out.end(), l.begin(), l.end());
    return out;
}

static void testBsp() {
    Bsp bsp;
    Error err = parseBsp(buildBsp(), bsp);
    CHECK(!err);
    if (err)
        std::fprintf(stderr, "%s\n", err.message.c_str());
    CHECK(bsp.entities == "{ \"classname\" \"worldspawn\" }");
    CHECK(bsp.shaders.size() == 1 && bsp.shaders[0].name == "textures/borg/floor" && bsp.shaders[0].contentFlags == 1);
    CHECK(bsp.planes.size() == 1 && bsp.planes[0].normal[2] == 1 && bsp.planes[0].dist == 16);
    CHECK(bsp.nodes.size() == 1 && bsp.nodes[0].children[1] == -2 && bsp.leafs.size() == 2);
    CHECK(bsp.models.size() == 1 && bsp.models[0].maxs[0] == 64 && bsp.models[0].numSurfaces == 1);
    CHECK(bsp.vertices.size() == 3 && bsp.vertices[1].pos[0] == 32 && bsp.vertices[0].color[1] == 128);
    CHECK(bsp.indices.size() == 3 && bsp.surfaces.size() == 1);
    CHECK(bsp.surfaces[0].type == SurfaceType::Planar && bsp.surfaces[0].fog == -1);
    CHECK(bsp.surfaces[0].lightmapSize[0] == 16 && bsp.surfaces[0].lightmapVecs[2][2] == 1);
    CHECK(bsp.lightmapCount() == 1 && bsp.visClusters == 2);
    CHECK(bsp.clusterVisible(0, 0) && !bsp.clusterVisible(0, 1) && bsp.clusterVisible(1, 0));
    CHECK(bsp.clusterVisible(-1, 1));

    CHECK(bsp.fixes.total() == 0);

    CHECK(parseBsp(buildBsp(47), bsp)); // wrong version
    // An index past the surface's vertices; the message says which.
    err = parseBsp(buildBsp(46, 3), bsp);
    CHECK(err && err.message == "BSP surface 0: index 2 is vertex 3 (of 3)");

    // What the original engine tolerates: a lightmap past the end (vertex
    // lighting), a missing fog, a flare's vertices, an unknown type.
    CHECK(!parseBsp(buildBsp(46, -1, 5, 2), bsp));
    CHECK(bsp.surfaces[0].lightmap == -1 && bsp.surfaces[0].fog == -1);
    CHECK(bsp.fixes.lightmaps == 1 && bsp.fixes.fogs == 1 && bsp.fixes.total() == 2);
    CHECK(!parseBsp(buildBsp(46, 7, 0, -1, 4), bsp)); // a flare's bad index doesn't matter
    CHECK(bsp.surfaces[0].type == SurfaceType::Flare && bsp.surfaces[0].numVertices == 0 && bsp.fixes.flares == 1);
    CHECK(!parseBsp(buildBsp(46, -1, 0, -1, 9), bsp));
    CHECK(bsp.surfaces[0].type == SurfaceType::Bad && bsp.fixes.types == 1);
    auto truncated = buildBsp();
    truncated.resize(truncated.size() - 1);
    CHECK(parseBsp(truncated, bsp));
    CHECK(parseBsp({'I', 'B', 'S', 'P'}, bsp));
}

static void testPatch() {
    // A 3x3 patch bulging up in the middle: every control point at height
    // 0 but the centre one at 4, normals pointing up.
    BspVertex control[9];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            BspVertex& v = control[y * 3 + x];
            v.pos = {float(x) * 8, float(y) * 8, x == 1 && y == 1 ? 4.0f : 0.0f};
            v.st = {float(x) / 2, float(y) / 2};
            v.normal = {0, 0, 1};
            v.color = {200, 100, 0, 255};
        }
    std::vector<BspVertex> verts(1); // something already there: indices must be offset
    std::vector<uint32_t> idx;
    tessellatePatch(control, 3, 3, 4, verts, idx);
    CHECK(verts.size() == 1 + 25 && idx.size() == 16 * 6);
    // Corners stay put; the middle is the Bezier point: 4 * (2 * 0.5 * 0.5)^2 = 1.
    CHECK(verts[1].pos[0] == 0 && verts[1].pos[2] == 0 && verts[25].pos[0] == 16 && verts[25].pos[1] == 16);
    const BspVertex& mid = verts[1 + 12];
    CHECK(std::fabs(mid.pos[0] - 8) < 1e-5f && std::fabs(mid.pos[2] - 1) < 1e-5f);
    CHECK(std::fabs(mid.st[0] - 0.5f) < 1e-5f && mid.color[0] == 200 && std::fabs(mid.normal[2] - 1) < 1e-5f);
    bool inRange = true, clockwise = true;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        for (int k = 0; k < 3; ++k)
            inRange = inRange && idx[i + k] >= 1 && idx[i + k] < verts.size();
        // Clockwise seen from above (where the normals point): the
        // geometric normal points down.
        const auto& a = verts[idx[i]].pos;
        const auto& b = verts[idx[i + 1]].pos;
        const auto& c = verts[idx[i + 2]].pos;
        const float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        clockwise = clockwise && nz < 0;
    }
    CHECK(inRange && clockwise);
    // Normals pointing down flip the winding.
    for (BspVertex& v : control)
        v.normal = {0, 0, -1};
    std::vector<BspVertex> down;
    std::vector<uint32_t> downIdx;
    tessellatePatch(control, 3, 3, 4, down, downIdx);
    const auto& a = down[downIdx[0]].pos;
    const auto& b = down[downIdx[1]].pos;
    const auto& c = down[downIdx[2]].pos;
    CHECK((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0);
    // Two blocks side by side share their edge: 5x3 control points.
    std::vector<BspVertex> wide(15, control[0]), out;
    std::vector<uint32_t> outIdx;
    tessellatePatch(wide.data(), 5, 3, 2, out, outIdx);
    CHECK(out.size() == 5 * 3 && outIdx.size() == 4 * 2 * 6);
}

// A two-frame MD3 with one tag and one triangle; frame 1 is frame 0
// moved up 64 units.
static std::vector<uint8_t> buildMd3(int badIndex = -1) {
    std::vector<uint8_t> out;
    auto i32 = [&](int32_t x) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(uint32_t(x) >> (8 * i)));
    };
    auto i16 = [&](int16_t x) {
        out.push_back(uint8_t(uint16_t(x)));
        out.push_back(uint8_t(uint16_t(x) >> 8));
    };
    auto f32 = [&](float f) {
        int32_t x;
        std::memcpy(&x, &f, 4);
        i32(x);
    };
    auto name = [&](const char* s, size_t n) {
        std::vector<uint8_t> b(n, 0);
        std::memcpy(b.data(), s, std::strlen(s));
        out.insert(out.end(), b.begin(), b.end());
    };
    const int frames = 2, tags = 1;
    const int ofsFrames = 108, ofsTags = ofsFrames + 56 * frames, ofsSurf = ofsTags + 112 * frames * tags;
    out.insert(out.end(), {'I', 'D', 'P', '3'});
    i32(15);
    name("models/test/box.md3", 64);
    i32(0); i32(frames); i32(tags); i32(1); i32(0);
    i32(ofsFrames); i32(ofsTags); i32(ofsSurf); i32(0);
    for (int f = 0; f < frames; ++f) {
        for (float v : {-8.f, -8.f, 0.f + 64 * f, 8.f, 8.f, 16.f + 64 * f, 0.f, 0.f, 0.f, 12.f})
            f32(v);
        name(f ? "up" : "rest", 16);
    }
    for (int f = 0; f < frames; ++f) {
        name("tag_top", 64);
        for (float v : {0.f, 0.f, 16.f + 64 * f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f})
            f32(v);
    }
    // Surface: header (108), shader (68), triangle (12), st (3 * 8), xyz (2 * 3 * 8).
    const int ofsShaders = 108, ofsTris = ofsShaders + 68, ofsSt = ofsTris + 12, ofsXyz = ofsSt + 24,
              ofsEnd = ofsXyz + 48;
    out.insert(out.end(), {'I', 'D', 'P', '3'});
    name("box", 64);
    i32(0); i32(frames); i32(1); i32(3); i32(1);
    i32(ofsTris); i32(ofsShaders); i32(ofsSt); i32(ofsXyz); i32(ofsEnd);
    name("models/test/box", 64);
    i32(0);
    i32(0); i32(1); i32(badIndex >= 0 ? badIndex : 2);
    for (float v : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f})
        f32(v);
    for (int f = 0; f < frames; ++f)
        for (int v = 0; v < 3; ++v) {
            i16(int16_t(v == 1 ? 64 * 8 : 0)); // x 8 for vertex 1 (1/64 units)
            i16(int16_t(v == 2 ? 64 * 8 : 0));
            i16(int16_t(64 * 64 * f));
            i16(int16_t(v == 0 ? 0 : (64 << 8) | 64)); // straight up; lat 64 lng 64 points along +y
        }
    return out;
}

static void testMd3() {
    Md3Model m;
    Error err = parseMd3(buildMd3(), m);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK(m.name == "models/test/box.md3" && m.frames.size() == 2 && m.tagCount == 1 && m.surfaces.size() == 1);
    CHECK(m.frames[1].name == "up" && m.frames[1].maxs[2] == 80 && m.frames[0].radius == 12);
    const Md3Tag* top = m.tag(1, "tag_top");
    CHECK(top && top->origin[2] == 80 && top->axis[2][2] == 1 && !m.tag(0, "tag_head") && !m.tag(5, "tag_top"));
    const Md3Surface& s = m.surfaces[0];
    CHECK(s.name == "box" && s.shaders.size() == 1 && s.shaders[0] == "models/test/box");
    CHECK((s.indices == std::vector<uint32_t>{0, 1, 2}) && s.vertexCount == 3 && s.vertices.size() == 6);
    CHECK(s.st[1][0] == 1 && s.st[2][1] == 1);
    CHECK(s.vertices[1].pos[0] == 8 && s.vertices[2].pos[1] == 8 && s.vertices[3 + 0].pos[2] == 64);
    // Normal 0 (both angles 0) points up; lat 64 lng 64 (a quarter turn each) along +y.
    CHECK(std::fabs(s.vertices[0].normal[2] - 1) < 1e-5f);
    CHECK(std::fabs(s.vertices[1].normal[1] - 1) < 1e-5f && std::fabs(s.vertices[1].normal[2]) < 1e-5f);

    CHECK(parseMd3(buildMd3(3), m)); // a triangle using a vertex past the end
    auto cut = buildMd3();
    cut.resize(cut.size() - 4);
    CHECK(parseMd3(cut, m));
    CHECK(parseMd3({'I', 'D', 'P', '2'}, m));
}

static void testSkinAndPlacement() {
    const Skin skin = parseSkin("h_Head,models/players2/munro/head.tga\r\ntag_head,\n\nbody , models/x/body \nempty,\n");
    CHECK(skin.size() == 2 && skin.at("h_head") == "models/players2/munro/head.tga" && skin.at("body") == "models/x/body");

    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    // Yaw 90: forward is +y, left is -x.
    auto axes = anglesToAxes({0, 90, 0});
    CHECK(approx(axes[0][1], 1) && approx(axes[1][0], -1) && approx(axes[2][2], 1));
    // Pitch 90 (down positive): forward points down.
    axes = anglesToAxes({90, 0, 0});
    CHECK(approx(axes[0][2], -1));
    // A tag 10 up in a model placed at (100, 0, 0) turned 90: what's
    // attached 5 along its x lands at (100, 5, 10).
    Md3Tag tag;
    tag.origin = {0, 0, 10};
    tag.axis = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const gfx::Mat4 m = gfx::multiply(placement({100, 0, 0}, anglesToAxes({0, 90, 0})), tagPlacement(tag));
    const gfx::Vec3 p = gfx::transformPoint(m, {5, 0, 0});
    CHECK(approx(p[0], 100) && approx(p[1], 5) && approx(p[2], 10));
}

static void testLightGrid() {
    // A world 0..128 x 0..64 x 0..128 holds 3 x 2 x 2 grid points.
    Bsp bsp;
    BspModel world;
    world.mins = {0, 0, 0};
    world.maxs = {128, 64, 128};
    bsp.models.push_back(world);
    bsp.lightGrid.assign(3 * 2 * 2 * 8, 0);
    auto point = [&](int x, int y, int z) { return &bsp.lightGrid[size_t(((z * 2 + y) * 3 + x) * 8)]; };
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) {
                uint8_t* p = point(x, y, z);
                p[0] = p[1] = p[2] = uint8_t(x == 0 ? 40 : 80); // ambient, doubled on loading
                p[3] = p[4] = p[5] = 50;                        // directed
                p[6] = 0;                                      // from straight above
            }
    point(2, 0, 0)[0] = point(2, 0, 0)[1] = point(2, 0, 0)[2] = 0; // inside a wall
    LightGrid grid;
    grid.build(bsp);
    CHECK(!grid.empty());
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // On grid point (0,0,0): 40 doubled, plus the minimum ambient of 32.
    PointLight l = grid.sample({0, 0, 0});
    CHECK(approx(l.ambient[0], 112) && approx(l.directed[0], 100) && approx(l.direction[2], 1));
    // Halfway between x = 0 and 1: 80 and 160 blend to 120, + 32.
    l = grid.sample({32, 0, 0});
    CHECK(approx(l.ambient[0], 152));
    // Next to the point in a wall, it's skipped, not blended as black.
    l = grid.sample({96, 0, 0});
    CHECK(approx(l.ambient[0], 160 + 32));
    // A grid of the wrong size is ignored, and sampling still gives light.
    bsp.lightGrid.resize(8);
    grid.build(bsp);
    CHECK(grid.empty() && grid.sample({0, 0, 0}).ambient[0] > 0);
}

// Builds a QVM from instructions, initialised data and literals.
struct QvmAsm {
    std::vector<std::pair<uint8_t, int32_t>> code;
    std::vector<uint8_t> data, lit;
    int32_t bss = 0;
    int add(uint8_t op, int32_t operand = 0) {
        code.emplace_back(op, operand);
        return int(code.size()) - 1;
    }
    std::vector<uint8_t> build(int32_t magic = 0x12721444) const {
        std::vector<uint8_t> bytes;
        for (const auto& [op, v] : code) {
            bytes.push_back(op);
            const int n = qvmop::operandSize(op);
            for (int i = 0; i < n; ++i)
                bytes.push_back(uint8_t(uint32_t(v) >> (8 * i)));
        }
        while (bytes.size() % 4)
            bytes.push_back(0);
        std::vector<uint8_t> out;
        auto i32 = [&](int32_t x) {
            for (int i = 0; i < 4; ++i)
                out.push_back(uint8_t(uint32_t(x) >> (8 * i)));
        };
        const int32_t header = 32;
        i32(magic);
        i32(int32_t(code.size()));
        i32(header);
        i32(int32_t(bytes.size()));
        i32(header + int32_t(bytes.size()));
        i32(int32_t(data.size()));
        i32(int32_t(lit.size()));
        i32(bss);
        out.insert(out.end(), bytes.begin(), bytes.end());
        out.insert(out.end(), data.begin(), data.end());
        out.insert(out.end(), lit.begin(), lit.end());
        return out;
    }
};

static void testQvm() {
    using namespace qvmop;
    int32_t r = 0;
    // vmMain(command, a, b) = command + a * b; arguments sit at
    // sp + frame + 8 after ENTER frame.
    {
        QvmAsm a;
        a.add(Enter, 8);
        a.add(Local, 16), a.add(Load4);
        a.add(Local, 20), a.add(Load4), a.add(Local, 24), a.add(Load4), a.add(Muli), a.add(Add);
        a.add(Leave, 8);
        Qvm vm;
        CHECK(!vm.load(a.build()));
        CHECK(!vm.call(3, {4, 5}, r) && r == 23);
        CHECK(!vm.call(-7, {-2, 3}, r) && r == -13);
    }
    // Recursion: factorial through calls, frames and ARG.
    QvmAsm f;
    f.add(Enter, 16);
    f.add(Local, 24), f.add(Load4), f.add(Arg, 8), f.add(Const, 0), f.add(Call), f.add(Leave, 16);
    const int fact = f.add(Enter, 16);
    f.code[4].second = fact;
    f.add(Local, 24), f.add(Load4), f.add(Const, 1);
    const int gt = f.add(Gti, 0);
    f.add(Const, 1), f.add(Leave, 16);
    f.code[size_t(gt)].second = int32_t(f.code.size());
    f.add(Local, 24), f.add(Load4);
    f.add(Local, 24), f.add(Load4), f.add(Const, 1), f.add(Sub), f.add(Arg, 8), f.add(Const, fact), f.add(Call);
    f.add(Muli), f.add(Leave, 16);
    Qvm vm;
    CHECK(!vm.load(f.build()));
    CHECK(!vm.call(5, {}, r) && r == 120);
    CHECK(!vm.call(10, {}, r) && r == 3628800);

    // A system call with a string and a number, and one that calls back
    // into the VM while it's running.
    {
        QvmAsm a;
        a.lit = {'h', 'i', 0, 0};
        a.data = {0, 0, 0, 0};
        a.add(Enter, 16);
        a.add(Const, 4), a.add(Arg, 8), a.add(Const, 42), a.add(Arg, 12), a.add(Const, -2), a.add(Call);
        a.add(Const, 1), a.add(Add);
        a.add(Local, 24), a.add(Load4), a.add(Arg, 8), a.add(Const, -6), a.add(Call), a.add(Add);
        a.add(Leave, 16);
        Qvm s;
        CHECK(!s.load(a.build()));
        Qvm inner;
        CHECK(!inner.load(f.build()));
        std::string seen;
        int calls = 0;
        s.setSystemCall([&](Qvm& v, int number, const int32_t* args) -> int32_t {
            ++calls;
            if (number == 1) {
                seen = v.string(args[0]) + "/" + std::to_string(args[1]);
                return 100;
            }
            if (number == 5) {
                // Back into a VM from a system call: here another one, and
                // the running one keeps its state.
                int32_t out = 0;
                return inner.call(args[0], {}, out) ? -1 : out;
            }
            return 0;
        });
        CHECK(!s.call(4, {}, r) && r == 101 + 24 && seen == "hi/42" && calls == 2);
        CHECK(s.executed() > 10);
    }

    // Memory: bytes and halfwords sign-extended, block copy, initialised data.
    {
        QvmAsm a;
        a.data = {0x80, 0xff, 0x34, 0x12, 1, 2, 3, 4};
        a.bss = 64;
        a.add(Enter, 8);
        a.add(Const, 0), a.add(Load1), a.add(Sex8);       // -128
        a.add(Const, 0), a.add(Load2), a.add(Sex16), a.add(Add); // -128 + -128
        a.add(Const, 16), a.add(Const, 4), a.add(BlockCopy, 4);
        a.add(Const, 16), a.add(Load4), a.add(Add);       // + 0x04030201
        a.add(Const, 20), a.add(Const, 0x55), a.add(Store1);
        a.add(Const, 20), a.add(Load1), a.add(Add);       // + 0x55
        a.add(Leave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -256 + 0x04030201 + 0x55);
        CHECK(m.readInt(16) == 0x04030201 && m.memory()[20] == 0x55 && m.readInt(0) == int32_t(0x1234ff80));
    }

    // Floats, unsigned comparison, division edge cases.
    {
        QvmAsm a;
        a.add(Enter, 8);
        a.add(Const, Qvm::fromFloat(1.5f)), a.add(Const, Qvm::fromFloat(2.25f)), a.add(Mulf);
        a.add(Const, 3), a.add(Cvif), a.add(Addf), a.add(Negf), a.add(Cvfi); // -(3.375 + 3) -> -6
        a.add(Const, -1), a.add(Const, 1);
        const int b = a.add(Ltu, 0); // 0xffffffff < 1 unsigned: false
        a.add(Const, 1000), a.add(Add);
        a.code[size_t(b)].second = int32_t(a.code.size());
        a.add(Const, INT32_MIN), a.add(Const, -1), a.add(Divi), a.add(Const, INT32_MIN), a.add(Sub), a.add(Add);
        a.add(Const, -7), a.add(Const, 2), a.add(Modi), a.add(Add);    // -1
        a.add(Const, -8), a.add(Const, 1), a.add(Rshi), a.add(Add);    // -4
        a.add(Const, -8), a.add(Const, 28), a.add(Rshu), a.add(Add);   // 15
        a.add(Leave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -6 + 1000 + 0 - 1 - 4 + 15);
    }

    // Faults are errors that name the instruction, not crashes.
    auto runs = [&](std::vector<std::pair<uint8_t, int32_t>> code, uint64_t limit = 0) {
        QvmAsm a;
        a.code = std::move(code);
        Qvm m;
        if (m.load(a.build()))
            return std::string("load");
        m.setInstructionLimit(limit);
        Error err = m.call(0, {}, r);
        return err ? err.message : std::string();
    };
    CHECK(runs({{Enter, 8}, {Const, -100}, {Load4, 0}, {Leave, 8}}).find("reads outside memory") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1 << 29}, {Const, 1}, {Store4, 0}, {Leave, 8}}).find("writes outside") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1}, {Const, 0}, {Divi, 0}, {Leave, 8}}).find("division by zero") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 99}, {Jump, 0}}).find("jumps to 99") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 1}, {Jump, 0}}, 1000).find("instruction limit") != std::string::npos);
    CHECK(runs({{Enter, 0x20000}, {Leave, 0x20000}}).find("stack overflow") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Add, 0}, {Leave, 8}}).find("operand stack") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Const, 5}, {Call, 0}, {Leave, 8}}).find("calls 5") != std::string::npos);
    CHECK(runs({{Enter, 8}, {Eq, 50}, {Leave, 8}}) == "load"); // branch target checked on load
    // Damaged files.
    Qvm bad;
    CHECK(bad.load({1, 2, 3}));
    auto file = f.build();
    file[0] = 0;
    CHECK(bad.load(file));
    file = f.build();
    file[32] = 200; // an unknown opcode
    CHECK(bad.load(file));
    file = f.build();
    file[4] = 100; // more instructions than the code holds
    CHECK(bad.load(file));
    CHECK(!bad.load(f.build(0x12721445))); // the second version's magic
}

// tests/data/qvm/test_vm.c compiled by the Quake III compiler: every case
// must give what the same program gives compiled natively.
static void testCompiledQvm() {
    Qvm vm;
    Error err = vm.load(std::vector<uint8_t>(std::begin(kTestVmQvm), std::end(kTestVmQvm)));
    CHECK(!err);
    if (err)
        return;
    std::string printed;
    vm.setSystemCall([&](Qvm& v, int number, const int32_t* args) -> int32_t {
        switch (number) {
        case 0: printed = v.string(args[0]); return int32_t(printed.size()) * 3;
        case 1: return args[0] * 2 + args[1];
        case 2: {
            int32_t out = 0;
            return v.call(args[0], {args[1]}, out) ? -999 : out;
        }
        case 3: return Qvm::fromFloat(Qvm::asFloat(args[0]) / 2.0f);
        default: return 0;
        }
    });
    for (const QvmCase& c : kQvmCases) {
        int32_t r = 0;
        err = vm.call(c.command, {c.arg0, c.arg1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 100}, r);
        if (err || r != c.expected)
            std::fprintf(stderr, "case %d(%d, %d): got %d, expected %d %s\n", c.command, c.arg0, c.arg1, r,
                         c.expected, err ? err.message.c_str() : "");
        CHECK(!err && r == c.expected);
    }
    // Case 7 builds a string and prints it.
    int32_t r = 0;
    CHECK(!vm.call(7, {42, -17}, r) && printed == "value 42 and -17");
}

static void testFilesystemAndDetection(const std::filesystem::path& tmp) {
    using test::toBytes;
    std::filesystem::remove_all(tmp);
    const auto base = tmp / "BaseEF";
    test::writeFile(base / "pak0.pk3", test::buildZip({{"a.txt", toBytes("pak0"), {}},
                                                       {"b.txt", toBytes("pak0"), {}},
                                                       {"scripts/x.shader", toBytes("textures/x\n{\n}\n"), {}}}));
    test::writeFile(base / "PAK1.pk3", test::buildZip({{"a.txt", toBytes("pak1"), {}},
                                                       {"b.txt", toBytes("pak1"), {}}}));
    test::writeFile(base / "zz_broken.pk3", toBytes("not a zip"));
    // In upper case '_' sorts after letters, so pak_c.pk3 beats pakb.pk3.
    test::writeFile(base / "pak_c.pk3", test::buildZip({{"c.txt", toBytes("underscore"), {}}}));
    test::writeFile(base / "pakb.pk3", test::buildZip({{"c.txt", toBytes("letter"), {}}}));
    test::writeFile(base / "b.txt", toBytes("loose"));

    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "baseef", 0, warnings));
    CHECK(warnings.size() == 1);
    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && data == toBytes("pak1"));
    CHECK(!set.read("b.txt", data) && data == toBytes("loose"));
    CHECK(!set.read("c.txt", data) && data == toBytes("underscore"));
    CHECK(mountGameDir(set, tmp, "missing", 1, warnings));

    ShaderLibrary lib;
    lib.load(set);
    CHECK(lib.size() == 1 && lib.find("textures/x"));

    // Images are found whichever extension is written, .tga first.
    test::writeFile(base / "textures" / "a.jpg", toBytes("jpg"));
    test::writeFile(base / "textures" / "b.tga", toBytes("tga"));
    test::writeFile(base / "textures" / "b.jpg", toBytes("jpg"));
    test::writeFile(base / "v1.5" / "c.tga", toBytes("tga"));
    SearchSet images;
    CHECK(!mountGameDir(images, tmp, "baseef", 0, warnings));
    CHECK(findImage(images, "textures/a.tga") == "textures/a.jpg");
    CHECK(findImage(images, "textures/a") == "textures/a.jpg");
    CHECK(findImage(images, "textures/b.jpg") == "textures/b.tga");
    CHECK(findImage(images, "v1.5/c") == "v1.5/c.tga"); // a dot in a folder isn't an extension
    CHECK(findImage(images, "textures/none.tga").empty());

    // Not Elite Force until the executable is there too.
    const MetaEngine* meta = findEngine("idtech3");
    CHECK(meta && meta->detect(tmp).empty());
    test::writeFile(tmp / "STVOY.EXE", toBytes("MZ"));
    // The test's pak0.pk3 isn't a known release: found by name, and only once.
    auto found = meta ? meta->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1 && found[0].gameId == "stvef" && findGame(found[0].gameId));
    CHECK(found.size() == 1 && found[0].description.find("unknown release") != std::string::npos);
    // Every known release names pak0.pk3's size and hash.
    for (const GameVariant& v : detectionTable())
        CHECK(!v.files.empty() && !v.files[0].md5.empty() && v.files[0].size > 0);
    CHECK(!findGame("nope"));
    std::filesystem::remove_all(tmp);
}

static void testCvars() {
    CvarStore cvars;
    const CvarStore::Cvar& a = cvars.create("Sensitivity", "5.5", CvarStore::Archive);
    CHECK(a.handle == 1 && a.modificationCount == 1);
    CHECK(cvars.value("sensitivity") == 5.5f && cvars.integer("SENSITIVITY") == 5);
    cvars.create("sensitivity", "1", CvarStore::Cheat); // exists: keeps its value, gains the flag
    CHECK(cvars.string("sensitivity") == "5.5" && cvars.find("sensitivity")->flags == (CvarStore::Archive | CvarStore::Cheat));
    cvars.set("sensitivity", "3");
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.set("sensitivity", "3"); // unchanged: not counted
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.create("version", "1.0", CvarStore::ReadOnly);
    cvars.set("version", "2.0");
    CHECK(cvars.string("version") == "1.0");
    cvars.set("version", "2.0", true);
    CHECK(cvars.string("version") == "2.0");
    cvars.set("made_up", "x"); // created by the user, then claimed by the code with its default
    CHECK(cvars.find("made_up")->flags == CvarStore::UserCreated);
    cvars.create("made_up", "y", 0);
    CHECK(cvars.find("made_up")->flags == 0 && cvars.find("made_up")->defaultValue == "y" &&
          cvars.string("made_up") == "x");
    CHECK(cvars.byHandle(2)->name == "version" && !cvars.byHandle(0) && !cvars.byHandle(4));
}

static void testVmFileList() {
    using test::toBytes;
    auto dir = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_list.pk3";
    test::writeFile(path, test::buildZip({{"scripts/a.arena", toBytes("a"), {}},
                                          {"Scripts/B.ARENA", toBytes("b"), {}},
                                          {"scripts/c.txt", toBytes("c"), {}},
                                          {"scripts/deep/d.arena", toBytes("d"), {}},
                                          {"scripts/deep/e.arena", toBytes("e"), {}},
                                          {"scriptsx/f.arena", toBytes("f"), {}}}));
    CHECK(!dir->open(path));
    SearchSet set;
    set.add(std::move(dir), 0);
    const auto arenas = VmFiles::listNames(set, "scripts/", ".arena");
    CHECK(arenas.size() == 2 && arenas[0] == "a.arena" && arenas[1] == "B.ARENA");
    const auto folders = VmFiles::listNames(set, "scripts", "/");
    CHECK(folders.size() == 1 && folders[0] == "deep");
    CHECK(VmFiles::listNames(set, "scripts", "").size() == 3);
    std::filesystem::remove(path);

    HandleTable table;
    CHECK(table.add("gfx/a") == 1 && table.add("GFX\\A") == 1 && table.add("gfx/b") == 2 && table.add("") == 0);
    CHECK(table.name(2) == "gfx/b" && table.name(0).empty() && table.name(3).empty());
}

// Sounds by name: decoded once, missing and broken ones remembered.
static void testSoundLibrary(const std::filesystem::path& tmp) {
    using test::toBytes;
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.format = 1;
    for (int i = 0; i < 2205; ++i)
        tone.samples.push_back(int16_t(i % 50 < 25 ? 8000 : -8000));
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"sound/interface/Button1.wav", audio::encodePcmWav(tone), {}},
                                    {"sound/broken.wav", toBytes("not a sound"), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    SoundLibrary sounds(set);
    const auto a = sounds.get("sound/interface/button1.wav");
    CHECK(a && a->rate == 22050 && a->channels == 1 && a->frames() == 2205 && a->samples[30] == -8000);
    CHECK(sounds.get("SOUND\\interface\\BUTTON1.WAV") == a); // the same, decoded once
    CHECK(!sounds.get("sound/missing.wav") && !sounds.get("sound/broken.wav") && !sounds.get(""));
    CHECK(sounds.problems().size() == 2);
    CHECK(!sounds.get("sound/missing.wav") && sounds.problems().size() == 2); // not tried again
    std::filesystem::remove_all(tmp);
}

static void testSoundSystem(const std::filesystem::path& tmp) {
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.format = 1;
    tone.samples.assign(2205, 4000); // a tenth of a second
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"sound/a.wav", audio::encodePcmWav(tone), {}},
                                    {"sound/b.wav", audio::encodePcmWav(tone), {}},
                                    {"music/intro.wav", audio::encodePcmWav(tone), {}},
                                    {"music/loop.wav", audio::encodePcmWav(tone), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    SoundLibrary library(set);
    audio::Mixer mixer(22050);
    SoundSystem sounds(mixer, library);
    // Listening from the origin, facing +x (left +y, up +z).
    sounds.respatialize(0, {0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
    auto [near, nearPan] = sounds.spatialize({50, 0, 0});
    CHECK(near == 1 && std::fabs(nearPan) < 1e-4f); // within 80 units, ahead
    auto [mid, right] = sounds.spatialize({0, -580, 0});
    CHECK(std::fabs(mid - 0.6f) < 1e-3f && right > 0.99f); // 500 past 80: 0.4 lost; to the right
    auto [far, left] = sounds.spatialize({0, 2000, 0});
    CHECK(far == 0 && left < -0.99f);

    // No sound cuts another, on any channel; the same sound on the same
    // entity within 50 ms is a repeat, skipped.
    std::vector<int16_t> out(2 * 64);
    sounds.setTime(1000);
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/a.wav");
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/b.wav");
    sounds.startSound(std::nullopt, 3, 1, "sound/a.wav"); // following it, not at that spot: not a repeat
    sounds.startSound(std::nullopt, 3, 2, "sound/a.wav"); // but this is
    sounds.startSound(std::nullopt, 4, 1, "sound/a.wav");
    sounds.startSound(std::nullopt, 3, 1, "sound/missing.wav"); // nothing, and nothing stopped
    sounds.startSound(Vec3f{0, 0, 0}, 3, 0, "sound/a.wav");   // a repeat at the same spot
    sounds.startSound(Vec3f{64, 0, 0}, 3, 0, "sound/a.wav");  // elsewhere: not one
    sounds.setTime(1100);
    sounds.startSound(std::nullopt, 3, 1, "sound/a.wav"); // no longer a repeat
    mixer.mix(out.data(), 64);
    int playing = 0;
    for (audio::SoundHandle h = 1; h < 10; ++h)
        playing += mixer.isPlaying(h);
    CHECK(playing == 6);
    sounds.stopAll();

    // A looping sound plays while it's added each frame.
    sounds.clearLoopingSounds();
    sounds.addLoopingSound(5, {100, 0, 0}, "sound/a.wav");
    sounds.endFrame();
    auto effects = [&] {
        int n = 0;
        for (audio::SoundHandle h = 1; h < 20; ++h)
            n += mixer.isPlaying(h);
        return n;
    };
    CHECK(effects() == 1);
    sounds.clearLoopingSounds();
    sounds.addLoopingSound(5, {100, 0, 0}, "sound/a.wav");
    sounds.endFrame();
    CHECK(effects() == 1); // the same one
    sounds.clearLoopingSounds();
    sounds.endFrame();
    CHECK(effects() == 0);

    // Music: the intro, then the loop (names without an extension found).
    sounds.startBackgroundTrack("music/intro", "music/loop");
    CHECK(mixer.isTypePlaying(audio::SoundType::Music));
    std::vector<int16_t> lots(2 * 4000);
    mixer.mix(lots.data(), 4000); // past the intro's end
    sounds.endFrame();
    CHECK(mixer.isTypePlaying(audio::SoundType::Music));
    mixer.mix(lots.data(), 4000);
    CHECK(mixer.isTypePlaying(audio::SoundType::Music)); // looping
    sounds.stopAll();
    CHECK(!mixer.isTypePlaying(audio::SoundType::Music));
    std::filesystem::remove_all(tmp);
}

// A small .mdr: two bones, two frames, one triangle whose first vertex
// hangs half off each bone, one tag on bone 1. Frame 0: bone 0 at the
// origin, bone 1 moved 10 along x; frame 1: bone 1 turned a quarter turn
// about z as well. `packed`: the bones as 16-bit numbers.
static std::vector<uint8_t> buildMdr(bool packed) {
    std::vector<uint8_t> d;
    auto i32 = [&](int32_t v) {
        for (int k = 0; k < 4; ++k)
            d.push_back(uint8_t(uint32_t(v) >> (8 * k)));
    };
    auto f32 = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        i32(int32_t(bits));
    };
    auto u16 = [&](int v) {
        d.push_back(uint8_t(v));
        d.push_back(uint8_t(v >> 8));
    };
    auto text = [&](const char* t, size_t size) {
        for (size_t k = 0; k < size; ++k)
            d.push_back(k < std::strlen(t) ? uint8_t(t[k]) : 0);
    };
    using Bone = std::array<float, 12>; // rows of 3 x 4
    const Bone still{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone moved{1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0};
    const Bone turned{0, -1, 0, 10, 1, 0, 0, 0, 0, 0, 1, 0};
    const std::array<std::array<Bone, 2>, 2> frames{{{still, moved}, {still, turned}}};
    const size_t frameSize = packed ? 40 + 2 * 24 : 56 + 2 * 48;
    const int32_t framesAt = 104, lodAt = framesAt + 2 * int32_t(frameSize);
    // One vertex: normal, st, one or two weights.
    const size_t surfaceSize = 168 + (24 + 2 * 20) + 2 * (24 + 20) + 12;
    const int32_t tagsAt = lodAt + 12 + int32_t(surfaceSize);
    text("RDM5", 4);
    i32(2);
    text("test", 64);
    i32(2);                              // frames
    i32(2);                              // bones
    i32(packed ? -framesAt : framesAt); // negative: packed
    i32(1);                              // levels of detail
    i32(lodAt);
    i32(1); // tags
    i32(tagsAt);
    i32(tagsAt + 36);
    for (const auto& frame : frames) {
        for (int k = 0; k < 10; ++k)
            f32(0); // bounds, origin, radius
        if (!packed)
            text("frame", 16);
        for (const Bone& b : frame) {
            if (packed) {
                for (int r = 0; r < 3; ++r)
                    u16(int(std::lround(b[size_t(r * 4 + 3)] * 64)) + 32768);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        u16(int(std::lround(b[size_t(r * 4 + c)] * 32766)) + 32768);
            } else {
                for (float v : b)
                    f32(v);
            }
        }
    }
    i32(1);  // surfaces
    i32(12); // the first right after the level of detail
    i32(12 + int32_t(surfaceSize));
    i32(0);
    text("Body", 64);
    text("models/test/body", 64);
    i32(0);
    i32(0);
    i32(3);   // vertices
    i32(168); // right after the surface's header
    i32(1);   // triangles
    i32(int32_t(surfaceSize) - 12);
    i32(0);
    i32(0);
    i32(int32_t(surfaceSize));
    // Vertex 0: half on each bone, 1 along y from each; vertices 1 and 2 on bone 1.
    f32(0), f32(0), f32(1), f32(0.25f), f32(0.5f), i32(2);
    i32(0), f32(0.5f), f32(0), f32(1), f32(0);
    i32(1), f32(0.5f), f32(0), f32(1), f32(0);
    for (int v = 1; v < 3; ++v) {
        f32(1), f32(0), f32(0), f32(0), f32(0), i32(1);
        i32(1), f32(1), f32(float(v)), f32(0), f32(0);
    }
    i32(0), i32(1), i32(2);
    i32(1);
    text("tag_weapon", 32);
    return d;
}

// A small .aas file: three areas in a row along x (below 100, 100 to
// 200, above 200), split by two planes, walkable both ways between
// neighbours; version 5's header scrambled when `scramble`.
static std::vector<uint8_t> buildAas(bool scramble) {
    std::vector<std::vector<uint8_t>> lumps(14);
    auto i32 = [](std::vector<uint8_t>& d, int32_t v) {
        for (int k = 0; k < 4; ++k)
            d.push_back(uint8_t(uint32_t(v) >> (8 * k)));
    };
    auto f32 = [&](std::vector<uint8_t>& d, float f) {
        int32_t v;
        std::memcpy(&v, &f, 4);
        i32(d, v);
    };
    // Planes 0 (dummy), x = 100, x = 200.
    for (float dist : {0.0f, 100.0f, 200.0f}) {
        f32(lumps[2], 1), f32(lumps[2], 0), f32(lumps[2], 0), f32(lumps[2], dist), i32(lumps[2], 0);
    }
    // Areas 0 (dummy) to 3: number, faces, mins, maxs, center.
    for (int a = 0; a < 4; ++a) {
        i32(lumps[7], a), i32(lumps[7], 0), i32(lumps[7], 0);
        const float x0 = float(a - 1) * 100, x1 = float(a) * 100;
        for (float v : {x0, -50.0f, 0.0f, x1, 50.0f, 50.0f, (x0 + x1) / 2, 0.0f, 25.0f})
            f32(lumps[7], v);
    }
    // Settings: grounded, standing; reachabilities: area 1 -> 2 (index 1),
    // 2 -> 1 and 2 -> 3 (2, 3), 3 -> 2 (4); index 0 a dummy.
    const int first[4] = {0, 1, 2, 4}, count[4] = {0, 1, 2, 1};
    for (int a = 0; a < 4; ++a)
        for (int v : {0, 1, 2, a == 0 ? 0 : 1, 0, count[a], first[a]})
            i32(lumps[8], v);
    struct R {
        int to;
        float x0, x1;
    };
    const R reach[] = {{0, 0, 0}, {2, 95, 105}, {1, 105, 95}, {3, 195, 205}, {2, 205, 195}};
    for (const R& r : reach) {
        i32(lumps[9], r.to), i32(lumps[9], 0), i32(lumps[9], 0);
        for (float v : {r.x0, 0.0f, 0.0f, r.x1, 0.0f, 0.0f})
            f32(lumps[9], v);
        i32(lumps[9], r.to ? 2 : 0); // walking
        lumps[9].push_back(10), lumps[9].push_back(0), lumps[9].push_back(0), lumps[9].push_back(0);
    }
    // Nodes: 0 dummy; 1 splits at x = 100 (front: node 2, back: area 1);
    // 2 at x = 200 (front: area 3, back: area 2).
    for (int v : {0, 0, 0, 1, 2, -1, 2, -3, -2})
        i32(lumps[10], v);
    std::vector<uint8_t> out = {'E', 'A', 'A', 'S'};
    i32(out, scramble ? 5 : 4);
    std::vector<uint8_t> header;
    i32(header, 1234); // the BSP's checksum
    int32_t at = 12 + 14 * 8;
    for (const auto& l : lumps) {
        i32(header, at), i32(header, int32_t(l.size()));
        at += int32_t(l.size());
    }
    if (scramble)
        for (size_t i = 0; i < header.size(); ++i)
            header[i] ^= uint8_t(i * 119);
    out.insert(out.end(), header.begin(), header.end());
    for (const auto& l : lumps)
        out.insert(out.end(), l.begin(), l.end());
    return out;
}

static void testAas() {
    using namespace reluva::idtech3::botlib;
    for (bool scramble : {false, true}) {
        AasWorld aas;
        CHECK(!aas.load(buildAas(scramble)));
        CHECK(aas.loaded() && aas.areaCount() == 4 && aas.bspChecksum() == 1234);
        CHECK(aas.pointArea({50, 0, 10}) == 1 && aas.pointArea({150, 0, 10}) == 2 && aas.pointArea({250, 0, 10}) == 3);
        // Area 1 to 3: two reachabilities (10 each), crossing area 2 from
        // 105 to 195 (90 units walked, 0.33 a unit: 29), plus 1 for the goal.
        int time = 0, reach = 0;
        CHECK(aas.route(1, nullptr, 3, tfl::Default, time, reach));
        CHECK(reach == 1 && time == 1 + 10 + 10 + 29);
        // From a point: plus walking to the reachability's start.
        const Vec3f at{35, 0, 0};
        CHECK(aas.travelTime(1, at, 3, tfl::Default) == time + 19);
        CHECK(aas.route(3, nullptr, 1, tfl::Default, time, reach) && reach == 4);
        CHECK(aas.route(2, nullptr, 2, tfl::Default, time, reach) && time == 1);
        // Without walking, no way.
        CHECK(!aas.route(1, nullptr, 3, tfl::Jump | tfl::Air, time, reach));
        // Area 2 turned off: no way through.
        aas.enableArea(2, false);
        CHECK(!aas.route(1, nullptr, 3, tfl::Default, time, reach));
        aas.enableArea(2, true);
        CHECK(aas.route(1, nullptr, 3, tfl::Default, time, reach));
        std::vector<int> areas;
        std::vector<Vec3f> points;
        CHECK(aas.traceAreas({50, 0, 0}, {250, 0, 0}, areas, &points, 10) == 3);
        CHECK(areas == std::vector<int>({1, 2, 3}) && std::fabs(points[1][0] - 100) < 0.01f);
        CHECK(aas.nextReachability(2, 0) == 2 && aas.nextReachability(2, 2) == 3 && aas.nextReachability(2, 3) == 0);
    }
}

static void testBotScript() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"defs.h", "#define TWO 2\n#define SQUARE(x) ((x) * (x))\n#define NAME \"bot\" \"name\"\n"},
        {"main.c", "#include \"defs.h\"\n"
                   "#ifdef TWO\nfirst TWO\n#else\nnot this\n#endif\n"
                   "#if TWO > 3 || !defined(TWO)\nnor this\n#elif SQUARE(TWO) == 4\nsquare SQUARE(3)\n#endif\n"
                   "#ifndef THREE\n#define THREE 3\n#endif\n"
                   "value $evalint(TWO * THREE + 1) $evalfloat(1 / 2.0) NAME -1.5\n"
                   "/* a comment */ end // another\n"}};
    BotScript script([&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    });
    CHECK(script.open("main.c"));
    std::vector<std::string> words;
    BotToken t;
    while (script.next(t))
        words.push_back(t.text);
    if (!script.error().empty())
        std::fprintf(stderr, "%s\n", script.error().c_str());
    CHECK(script.error().empty());
    const std::vector<std::string> expected = {"first", "2", "square", "(", "(", "3", ")", "*", "(", "3", ")", ")",
                                               "value", "7.000000", "0.500000", "botname", "-", "1.5", "end"};
    CHECK(words == expected);
    BotScript bad([](const std::string&, std::string&) { return false; });
    CHECK(!bad.open("missing.c") && !bad.error().empty());
}

static void testBotCharacter() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"bots/default_c.c", "skill 1 { 0 \"Default\" 1 0.5 2 7 }\nskill 4 { 0 \"Default\" 1 0.8 2 9 }\n"
                             "skill 5 { 0 \"Default\" 1 1.0 2 9 }\n"},
        {"bots/test_c.c", "#define NAME 0\nskill 1 { NAME \"Test\" 1 0.25 }\nskill 4 { NAME \"Test\" 1 0.75 }\n"}};
    const BotScript::Loader load = [&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    };
    BotCharacters chars;
    const int a = chars.load(load, "bots/test_c.c", 1);
    CHECK(a && chars.string(a, 0) == "Test" && chars.floatValue(a, 1) == 0.25f);
    CHECK(chars.integer(a, 2) == 7); // from the default character
    // Between 1 and 4: floats blended.
    const int b = chars.load(load, "bots/test_c.c", 2.5f);
    CHECK(b && std::fabs(chars.floatValue(b, 1) - 0.5f) < 1e-5f && chars.integer(b, 2) == 7);
    // Skill 5 isn't in the file: the default character's.
    const int c = chars.load(load, "bots/test_c.c", 5);
    CHECK(c && chars.string(c, 0) == "Default" && chars.floatValue(c, 1) == 1.0f);
    CHECK(chars.boundedFloat(a, 1, 0.5f, 1) == 0.5f && chars.boundedInteger(a, 2, 0, 5) == 5);
    CHECK(!chars.load(load, "bots/none_c.c", 1) == false); // falls back to the default character
}

static void testBotAi() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"botfiles/w.c", "weight \"a\" { switch(1) { case 2: return 10; case 5: { switch(2) { default: return 30; } }"
                         " default: return balance(50, 40, 60); } }\nweight \"b\" return 7;\n"
                         "weight \"item_armor\" return 100;\nweight \"Phaser\" { switch(3) { case 1: return 0; default: return 20; } }\n"},
        {"botfiles/items.c", "iteminfo \"item_armor\" { name \"Armor\" model \"m.md3\" modelindex 3 type 1 index 2"
                             " respawntime 25 mins {-15,-15,-15} maxs {15,15,15} }\n"},
        {"botfiles/weapons.c", "projectileinfo { name \"bolt\" damage 20 radius 1.5 }\n"
                               "weaponinfo { number 2 name \"Phaser\" projectile \"bolt\" speed 0 offset {0, 5, -10} }\n"}};
    auto read = [&](const std::string& path, std::vector<uint8_t>& data) {
        if (path == "maps/test.aas") {
            data = buildAas(true);
            return true;
        }
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        data.assign(it->second.begin(), it->second.end());
        return true;
    };
    BotLibrary lib;
    lib.readFile = read;
    // Fuzzy weights: a case while the count is below it.
    auto w = readWeightConfig(lib.scriptLoader(), "w.c");
    CHECK(w && w->weights.size() == 4);
    std::vector<int> inv(256, 0);
    CHECK(w->evaluate(w->find("a"), inv) == 10);
    inv[1] = 3;
    CHECK(w->evaluate(w->find("a"), inv) == 30);
    inv[1] = 9;
    CHECK(w->evaluate(w->find("a"), inv) == 50 && w->evaluate(w->find("b"), inv) == 7);
    std::vector<WeaponInfo> weapons;
    CHECK(readWeaponConfig(lib.scriptLoader(), "weapons.c", weapons) && weapons[2].valid);
    CHECK(weapons[2].proj.damage == 20 && weapons[2].offset[2] == -10 && weapons[2].proj.radius == 1.5f);

    // A level with an armour in area 2, its entity seen twice in place.
    lib.entityCount = 1;
    lib.entityKey = [](int e, const std::string& key, std::string& value) {
        if (e != 1)
            return false;
        if (key == "classname")
            value = "item_armor";
        else if (key == "origin")
            value = "150 0 10";
        else
            return false;
        return true;
    };
    CHECK(lib.setup() == blerr::None && lib.loadMap("test") == blerr::None);
    BotEntityState item;
    item.type = 2;
    item.modelIndex = 3;
    item.origin = {150, 0, 10};
    for (float t : {0.1f, 0.2f}) {
        lib.startFrame(t);
        lib.updateEntity(5, &item);
    }
    lib.updateEntityItems();
    const int gs = lib.allocGoalState(0);
    CHECK(lib.loadItemWeights(gs, "w.c") == blerr::None);
    CHECK(lib.chooseLtgItem(gs, {50, 0, 0}, inv, tfl::Default));
    BotGoal goal;
    CHECK(lib.topGoal(gs, goal) && goal.area == 2 && goal.entity == 5 && (goal.flags & goalflag::Item));
    CHECK(lib.goalName(goal.number) == "Armor");
    // Avoided for a while once chosen.
    lib.popGoal(gs);
    CHECK(!lib.chooseLtgItem(gs, {50, 0, 0}, inv, tfl::Default));
    const int ws = lib.allocWeaponState();
    CHECK(lib.loadWeaponWeights(ws, "w.c") == blerr::None);
    inv[3] = 1;
    CHECK(lib.chooseBestFightWeapon(ws, inv) == 2);

    // Moving: from area 1 towards area 3, walking along +x to the first
    // reachability's start.
    const int ms = lib.allocMoveState();
    BotInitMove init;
    init.origin = {30, 0, 0};
    init.client = 1;
    init.presence = presence::Normal;
    init.orMoveFlags = moveflag::OnGround;
    lib.initMoveState(ms, init);
    BotGoal far;
    far.area = 3;
    far.origin = {250, 0, 0};
    const BotMoveResult r = lib.moveToGoal(ms, far, tfl::Default);
    CHECK(!r.failure && (r.travelType & travel::TypeMask) == travel::Walk);
    const BotInput in = lib.getInput(1, 0.1f);
    CHECK(in.dir[0] > 0.99f && in.speed == 400);
    // In the goal's area: straight to it, slower when close.
    lib.resetInput(1);
    init.origin = {240, 0, 0};
    lib.initMoveState(ms, init);
    lib.moveToGoal(ms, far, tfl::Default);
    CHECK(lib.getInput(1, 0.1f).speed == 40);

    // Predicting a move: a floor where a standing player's origin is 24
    // above z = 0.
    lib.trace = [](const Vec3f& start, const Vec3f& mins, const Vec3f&, const Vec3f& end, int, int) {
        BotLibrary::Trace t;
        const float floor = -mins[2];
        t.end = end;
        if (end[2] < floor && start[2] >= floor) {
            t.fraction = (start[2] - floor) / (start[2] - end[2]);
            for (int i = 0; i < 3; ++i)
                t.end[size_t(i)] = start[size_t(i)] + (end[size_t(i)] - start[size_t(i)]) * t.fraction;
            t.entity = 1022;
            t.normal = {0, 0, 1};
        }
        return t;
    };
    BotClientMove cm;
    CHECK(lib.predictClientMovement(cm, 1, {50, 0, 200}, presence::Normal, false, {}, {}, 0, 50, 0.1f,
                                    stopevent::HitGround, 0));
    // (On the ground counts from 10 units above it, as the original's.)
    CHECK(cm.stopEvent == stopevent::HitGround && cm.endPos[2] >= 24 && cm.endPos[2] <= 34 && cm.frames > 3);
    // Running along +x for half a second on the floor.
    CHECK(lib.predictClientMovement(cm, 1, {50, 0, 24}, presence::Normal, true, {}, {400, 0, 0}, 5, 5, 0.1f, 0, 0));
    CHECK(cm.stopEvent == stopevent::None && cm.endPos[0] > 100 && cm.velocity[0] > 250 &&
          std::fabs(cm.endPos[2] - 24) < 1);
    // Entering area 3 on the way.
    CHECK(lib.predictClientMovement(cm, 1, {150, 0, 24}, presence::Normal, true, {320, 0, 0}, {400, 0, 0}, 20, 20,
                                    0.1f, stopevent::EnterArea, 3));
    CHECK(cm.stopEvent == stopevent::EnterArea && cm.endArea == 3 && std::fabs(cm.endPos[0] - 200) < 1);
}

static void testBotChat() {
    using namespace reluva::idtech3::botlib;
    const std::map<std::string, std::string> files = {
        {"syn.c", "1 { [(\"hello\", 1), (\"hi\", 1)] }\n"},
        {"rnd.c", "greeting = { \"hey\"; \"yo\"; }\n"},
        {"match.c", "2 { \"hello \", 0 = (5, 1);\n 0, \" says: \", 1 = (6, 0); }\n"},
        {"rchat.c", "[\"hello\", !\"bye\"] = 5 { \"hello yourself, \", 0, \"!\"; }\n"},
        {"bots/test_t.c", "chat \"other\" { type \"x\" { \"no\"; } }\n"
                          "chat \"test\" { type \"game_enter\" { \"hi all, I'm \", 0, \".\"; greeting, \" there\"; } }\n"}};
    const BotScript::Loader load = [&](const std::string& path, std::string& text) {
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        text = it->second;
        return true;
    };
    BotChat chat;
    std::string log;
    chat.setup(load, nullptr, &log);
    CHECK(log.find("1 synonym lists, 1 random lists, 2 match templates, 1 replies") != std::string::npos);
    BotMatch m;
    CHECK(chat.findMatch("Bob says: good game\n", m, 2) && m.type == 6);
    CHECK(BotChat::matchVariable(m, 0) == "Bob" && BotChat::matchVariable(m, 1) == "good game");
    CHECK(!chat.findMatch("Bob says: hi", m, 4)); // not in that context
    std::string text = "  a  ^7b !c ";
    BotChat::unifyWhiteSpaces(text);
    CHECK(text == "a 7b c");
    text = "hi there, hit it";
    chat.replaceSynonyms(text, 1);
    CHECK(text == "hello there, hit it");

    std::vector<std::string> said;
    chat.command = [&](int client, const std::string& c) { said.push_back(std::to_string(client) + " " + c); };
    const int cs = chat.allocState();
    CHECK(chat.loadChatFile(cs, load, "bots/test_t.c", "TEST") && chat.numInitialChats(cs, "game_enter") == 2);
    chat.setName(cs, "Munro");
    std::vector<const char*> vars(8, nullptr);
    vars[0] = "Munro";
    chat.initialChat(cs, "game_enter", 0, vars);
    std::string line = chat.takeChatMessage(cs);
    CHECK(line == "hi all, I'm Munro." || line == "hey there" || line == "yo there");
    vars[0] = "Bob";
    CHECK(chat.replyChat(cs, "hello Munro", 0, 0, vars));
    chat.enterChat(cs, 3, 0);
    CHECK(said.size() == 1 && said[0] == "3 say hello yourself, Bob!");
    CHECK(!chat.replyChat(cs, "hello and bye", 0, 0, vars)); // a ! key present
    chat.queueConsoleMessage(cs, 1, "first");
    chat.queueConsoleMessage(cs, 1, "second");
    CHECK(chat.numConsoleMessages(cs) == 2 && chat.nextConsoleMessage(cs)->text == "first");
    chat.removeConsoleMessage(cs, chat.nextConsoleMessage(cs)->handle);
    CHECK(chat.nextConsoleMessage(cs)->text == "second");
}

static void testPatchCollision() {
    using namespace reluva::idtech3;
    // One patch, 3x3 control points over x and y from -64 to 64: flat at
    // z = 0, or (bent) its middle row raised to 64, an arch along y.
    for (bool bent : {false, true}) {
        Bsp bsp;
        bsp.shaders.push_back({"curve", 0, contents::Solid});
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                BspVertex v;
                v.pos = {float(x - 1) * 64, float(y - 1) * 64, bent && y == 1 ? 64.0f : 0.0f};
                bsp.vertices.push_back(v);
            }
        BspSurface surf;
        surf.type = SurfaceType::Patch;
        surf.numVertices = 9;
        surf.patchWidth = surf.patchHeight = 3;
        bsp.surfaces.push_back(surf);
        BspModel model;
        model.numSurfaces = 1;
        bsp.models.push_back(model);
        CollisionWorld world(bsp);
        const Vec3f mins{-15, -15, -24}, maxs{15, 15, 32};
        // Dropped onto its middle: stopped with the box's bottom on it.
        TraceResult t = world.trace({0, 0, 200}, {0, 0, -200}, mins, maxs, 0, contents::Solid);
        const float top = bent ? 32.0f : 0.0f; // the arch's height there: half the raised row's
        CHECK(t.fraction < 1 && !t.startSolid && std::fabs(t.endPos[2] - (top + 24.5f)) < 1);
        CHECK(t.normal[2] > 0.9f);
        // From below, too: both sides stop.
        t = world.trace({0, 0, -200}, {0, 0, 200}, {}, {}, 0, contents::Solid);
        CHECK(t.fraction < 1 && std::fabs(t.endPos[2] - (top - 0.5f)) < 1);
        // A point passing beside it isn't stopped, nor other contents.
        CHECK(world.trace({100, 0, 200}, {100, 0, -200}, {}, {}, 0, contents::Solid).fraction == 1);
        CHECK(world.trace({0, 0, 200}, {0, 0, -200}, {}, {}, 0, contents::Water).fraction == 1);
        if (bent) {
            // Walking into the arch's slope from the side: stopped, the
            // normal leaning back towards the walker.
            t = world.trace({0, -100, 8}, {0, 100, 8}, {}, {}, 0, contents::Solid);
            CHECK(t.fraction < 1 && t.normal[1] < -0.3f);
        }
    }
}

static void testPlayerAnimation() {
    const auto anims = parseAnimationConfig("sex f\nsoundpath alexa\n\n"
                                            "0\t47\t0\t10\t\t// BOTH_DEATH1\n"
                                            "95 18 0 15 // TORSO_GESTURE\n"
                                            "134 2 0 1 // TORSO_STAND\n"
                                            "138 14 14 10 // LEGS_WALKCR\n"
                                            "208 10 10 15 // LEGS_IDLE\n"
                                            "186 6 0 10 // LEGS_JUMP\n");
    CHECK(anims.size() == 6);
    CHECK(anims[0].name == "BOTH_DEATH1" && anims[0].part == PlayerAnimation::Part::Both && anims[0].first == 0);
    CHECK(anims[2].part == PlayerAnimation::Part::Torso && anims[2].first == 134);
    // Legs moved back by 138 - 95 into the legs model.
    CHECK(anims[3].part == PlayerAnimation::Part::Legs && anims[3].first == 95);
    CHECK(anims[4].name == "LEGS_IDLE" && anims[4].first == 165 && anims[4].loop == 10 && anims[4].fps == 15);
    int frame = 0, next = 0;
    float f = 0;
    anims[4].frameAt(0.5f / 15, frame, next, f); // halfway through the first frame
    CHECK(frame == 165 && next == 166 && std::fabs(f - 0.5f) < 1e-4f);
    anims[4].frameAt(9.0f / 15, frame, next, f); // the last frame, on its way round to the first
    CHECK(frame == 174 && next == 165);
    anims[4].frameAt(23.0f / 15, frame, next, f); // looping
    CHECK(frame == 168);
    anims[5].frameAt(100, frame, next, f); // not looping: held on the last
    CHECK(frame == 186 - 43 + 5 && next == frame && f == 0);
}

static void testMdr() {
    for (bool packed : {false, true}) {
        Md3Model m;
        CHECK(!parseModel(buildMdr(packed), m));
        CHECK(m.frames.size() == 2 && m.surfaces.size() == 1 && m.tagCount == 1);
        const Md3Surface& s = m.surfaces[0];
        CHECK(s.name == "body" && s.shaders[0] == "models/test/body" && s.vertexCount == 3);
        CHECK(s.indices == std::vector<uint32_t>({0, 1, 2}) && s.st[0][0] == 0.25f && s.st[0][1] == 0.5f);
        auto near = [](const Vec3& a, Vec3 b) {
            return std::fabs(a[0] - b[0]) < 0.02f && std::fabs(a[1] - b[1]) < 0.02f && std::fabs(a[2] - b[2]) < 0.02f;
        };
        // Frame 0: vertex 0 between (0,1,0) and (10,1,0); vertex 1 at 10 + 1.
        CHECK(near(s.vertices[0].pos, {5, 1, 0}) && near(s.vertices[1].pos, {11, 0, 0}));
        CHECK(near(s.vertices[0].normal, {0, 0, 1}) && near(s.vertices[1].normal, {1, 0, 0}));
        // Frame 1: bone 1 turned, so its offsets are too: (1,0,0) -> (0,1,0).
        CHECK(near(s.vertices[3 + 1].pos, {10, 1, 0}) && near(s.vertices[3 + 2].pos, {10, 2, 0}));
        CHECK(near(s.vertices[3 + 0].pos, {4.5f, 0.5f, 0}) && near(s.vertices[3 + 1].normal, {0, 1, 0}));
        // The tag follows bone 1: its x axis is the matrix's first column.
        const Md3Tag* tag = m.tag(1, "tag_weapon");
        CHECK(tag && near(tag->origin, {10, 0, 0}) && near(tag->axis[0], {0, 1, 0}) && near(tag->axis[1], {-1, 0, 0}));
        // A level of detail past its only one: that one.
        Md3Model low;
        CHECK(!parseModel(buildMdr(packed), low, 2) && low.surfaces.size() == 1 &&
              low.surfaces[0].vertices.size() == s.vertices.size());
    }
    // A broken one: the triangles past the end.
    std::vector<uint8_t> broken = buildMdr(false);
    broken.resize(broken.size() - 60);
    Md3Model m;
    CHECK(parseModel(broken, m));
}

// A small map for collision: a floor in the world (x and y -512..512, z
// -64..0) and one brush model, *1, a door 16 x 128 x 128 standing on its
// own origin.
static std::shared_ptr<Bsp> collisionMap() {
    auto bsp = std::make_shared<Bsp>();
    bsp->shaders.push_back({"textures/test/solid", 0, contents::Solid});
    auto addBox = [&](Vec3f mins, Vec3f maxs) {
        const int first = int(bsp->brushSides.size());
        for (int axis = 0; axis < 3; ++axis) {
            BspPlane hi, lo;
            hi.normal[size_t(axis)] = 1;
            hi.dist = maxs[size_t(axis)];
            lo.normal[size_t(axis)] = -1;
            lo.dist = -mins[size_t(axis)];
            bsp->brushSides.push_back({int(bsp->planes.size()), 0});
            bsp->planes.push_back(hi);
            bsp->brushSides.push_back({int(bsp->planes.size()), 0});
            bsp->planes.push_back(lo);
        }
        bsp->brushes.push_back({first, 6, 0});
    };
    addBox({-512, -512, -64}, {512, 512, 0});
    addBox({-8, -64, 0}, {8, 64, 128});
    // One node, far below everything: the floor's leaf in front, an empty one behind.
    BspPlane split;
    split.normal = {0, 0, 1};
    split.dist = -10000;
    bsp->planes.push_back(split);
    BspNode node;
    node.plane = int(bsp->planes.size()) - 1;
    node.children = {-1, -2};
    bsp->nodes.push_back(node);
    BspLeaf floorLeaf;
    floorLeaf.numLeafBrushes = 1;
    bsp->leafs.push_back(floorLeaf);
    BspLeaf empty;
    empty.cluster = -1;
    bsp->leafs.push_back(empty);
    bsp->leafBrushes.push_back(0);
    BspModel world;
    world.mins = {-512, -512, -64};
    world.maxs = {512, 512, 0};
    world.numBrushes = 1;
    BspModel door;
    door.mins = {-8, -64, 0};
    door.maxs = {8, 64, 128};
    door.firstBrush = 1;
    door.numBrushes = 1;
    bsp->models = {world, door};
    return bsp;
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void testCollision() {
    const auto bsp = collisionMap();
    const CollisionWorld world(*bsp);
    const Vec3f zero{};
    // A point falling onto the floor stops an eighth above it.
    TraceResult t = world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Solid);
    CHECK(!t.startSolid && near(t.endPos[2], 0.125f) && near(t.fraction, 99.875f / 200));
    CHECK(t.normal[2] == 1 && t.contents == contents::Solid && near(t.dist, 0));
    // A player's box (feet 24 below its origin) stops with its feet there.
    t = world.trace({0, 0, 100}, {0, 0, -100}, {-15, -15, -24}, {15, 15, 32}, 0, contents::Solid);
    CHECK(near(t.endPos[2], 24.125f));
    // Nothing beyond the floor's edge; nothing for a mask without solid.
    CHECK(world.trace({1000, 0, 100}, {1000, 0, -100}, zero, zero, 0, contents::Solid).fraction == 1);
    CHECK(world.trace({0, 0, 100}, {0, 0, -100}, zero, zero, 0, contents::Water).fraction == 1);
    // Starting inside: startSolid; staying inside: allSolid.
    t = world.trace({0, 0, -10}, {0, 0, -1000}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && !t.allSolid);
    t = world.trace({0, 0, -10}, {0, 0, -20}, zero, zero, 0, contents::Solid);
    CHECK(t.startSolid && t.allSolid && t.fraction == 0);
    // Sideways into the door (model 1, in its own coordinates).
    t = world.trace({-100, 0, 64}, {100, 0, 64}, zero, zero, 1, contents::Solid);
    CHECK(near(t.endPos[0], -8.125f) && t.normal[0] == -1);
    CHECK(world.pointContents({0, 0, -32}, 0) == contents::Solid && world.pointContents({0, 0, 32}, 0) == 0);
    CHECK(world.pointContents({0, 0, 64}, 1) == contents::Solid);
    Vec3f mins, maxs;
    CHECK(world.modelBounds(1, mins, maxs) && maxs[2] == 128 && !world.modelBounds(2, mins, maxs));
    // A box entity.
    t = CollisionWorld::traceBox({0, 0, 100}, {0, 0, -100}, zero, zero, {-16, -16, 0}, {16, 16, 32}, contents::Body);
    CHECK(near(t.endPos[2], 32.125f) && t.contents == contents::Body);
}

// The game logic starting a level: its entities handed over token by
// token, as the original server does, by a QVM the Quake III compiler
// made from tests/data/qvm/fake_game.c.
static void testGameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/qagame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeGameQvm), std::end(kFakeGameQvm)), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::GameHost game(set, cvars);
    CHECK(!game.load());
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Borg Cube, deck 4\"\n}\n"
                                 "// a comment\n{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n"
                                 "\"origin\" \"200 0 0\"\n}\n/* another */ {\n\"classname\" \"item_health\"\n"
                                 "\"origin\" \"-200 0 16\"\n}\n{\n\"classname\" \"info_player_deathmatch\"\n"
                                 "\"origin\" \"0 0 300\"\n}\n";
    auto map = collisionMap();
    map->entities = entities;
    const Error err = game.startLevel("borg2", map);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    CHECK(game.log() == "------- Game Initialization -------\n");
    CHECK(game.gameData().count == 4 && game.gameData().entitySize == 352 && game.gameData().clientSize == 740);
    CHECK((game.linked() == std::set<int32_t>{1, 2, 3})); // the world unlinked
    CHECK(game.configstrings().count(12) && game.configstrings().at(12) == "borg2");
    // The server info, as the client game finds the map's name.
    CHECK(game.configstrings().count(0) && game.configstrings().at(0).find("\\mapname\\borg2") != std::string::npos);
    CHECK(game.levelTime() == 3 * ef::GameHost::kFrameMs);
    int32_t r = 0;
    CHECK(!game.call(23, {}, r) && r == 3); // the three settling frames
    CHECK(!game.call(21, {2}, r) && r == 'i' * 1000 + 't');
    CHECK(!game.call(21, {0}, r) && r == 'w' * 1000 + 'o');
    CHECK(!game.call(22, {}, r) && r == 803); // sv_maxclients 8, ceil(2.25)

    // SetBrushModel: the door's model, box and contents, linked.
    CHECK(!game.call(30, {1}, r) && r == 111);
    CHECK(game.entityVec(1, 232)[2] == 128 && game.entityInt(1, 244) == -1);
    // Linking fills in the box in the world, a unit larger: the item's z -16..16 around 16.
    CHECK(!game.call(27, {2}, r) && r == -1 * 1000 + 33);
    // Traces down onto the floor, the door (entity 1) and the item (2), in eighths.
    CHECK(!game.call(24, {0, 0, 100, 0, -1}, r) && r == 1022);
    CHECK(!game.call(25, {}, r) && r == 1);
    CHECK(!game.call(26, {}, r) && r == 1000); // the floor's normal points up
    CHECK(!game.call(24, {200, 0, 500, 0, -1}, r) && r == 1);
    CHECK(!game.call(25, {}, r) && r == 128 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, -1}, r) && r == 2);
    CHECK(!game.call(25, {}, r) && r == 32 * 8 + 1);
    CHECK(!game.call(24, {-200, 0, 500, 0, 2}, r) && r == 1022); // ignoring the item
    CHECK(!game.call(24, {0, 0, 100, 16, -1}, r) && !game.call(25, {}, r) && r == 16 * 8 + 1); // a box
    CHECK(!game.call(24, {0, 0, -10, 0, -1}, r) && !game.call(26, {}, r) && r == 101); // starting in the floor
    CHECK(!game.call(24, {1000, 0, 100, 0, -1}, r) && r == 1023); // nothing there
    CHECK(!game.call(28, {0, 0, -32}, r) && r == contents::Solid);
    CHECK(!game.call(28, {200, 0, 64}, r) && r == -1); // inside the door
    CHECK(!game.call(28, {0, 0, 64}, r) && r == 0);
    CHECK(!game.call(29, {-200, 0, 16}, r) && r == 102);
    CHECK(!game.call(29, {0, 0, 300}, r) && r == 103); // the spawn point

    // A player joins: refused without a name, then let in and placed.
    std::string refused;
    CHECK(!game.connectClient(0, "", refused) && refused == "No name given");
    CHECK(!game.connectClient(0, "\\name\\Reluva\\model\\munro", refused) && refused.empty());
    CHECK(!game.beginClient(0));
    CHECK(!game.call(31, {}, r) && r == 4 && game.linked().count(4));
    CHECK(game.entityVec(4, 272)[0] == 0 && game.entityVec(4, 272)[2] == 309); // the spawn point, 9 up
    // Its commands, field by field in Elite Force's usercmd_t.
    ef::GameHost::UserCommand cmd;
    cmd.serverTime = 400;
    cmd.angles = {1000, 16384, 0};
    cmd.buttons = 5;
    cmd.weapon = 3;
    cmd.forward = 127;
    cmd.right = -127;
    cmd.up = 10;
    CHECK(!game.clientThink(0, cmd));
    CHECK(!game.call(32, {}, r) && r == 4005);
    CHECK(!game.call(33, {}, r) && r == 1000 + 16384 * 100000);
    CHECK(!game.call(34, {}, r) && r == 3000000 + 255 * 10000 + 1 * 100 + 60);
    CHECK(game.entityVec(4, 272)[0] == 127); // moved by it
    // Its snapshot: every linked entity (the door, the item, the spawn
    // point, the player's box at 4: the stand-in's player isn't entity 0),
    // each the first 204 bytes of the game's entity.
    const auto snap = game.snapshot(0);
    CHECK(snap.serverTime == 3 * ef::GameHost::kFrameMs && snap.entities.size() == 4 && snap.entities[0].size() == 204);
    CHECK(snap.entities[0][0] == 1 && snap.entities[3][0] == 4); // s.number
    CHECK(snap.serverCommandSequence == 1);
    // The player state, copied but for the word the original doesn't send.
    {
        const int32_t client = game.gameData().clients;
        CHECK(game.gameData().clientSize >= 460);
        game.vm().writeInt(client + 44, 66);
        game.vm().writeInt(client + 48, 166);
        const auto withTimer = game.snapshot(0);
        int32_t w44 = 0, w48 = -1;
        std::memcpy(&w44, &withTimer.playerState[44], 4);
        std::memcpy(&w48, &withTimer.playerState[48], 4);
        CHECK(w44 == 66 && w48 == 0 && game.clientInt(0, 48) == 166);
    }
    CHECK(game.serverCommands().size() == 1 && game.serverCommands()[0].first == -1 &&
          game.serverCommands()[0].second == "print \"Reluva entered the game\n\"");
    CHECK(game.unhandled().empty());

    // Broken entity text: the game's own error stops it.
    ef::GameHost broken(set, cvars);
    CHECK(!broken.load());
    auto brokenMap = std::make_shared<Bsp>();
    brokenMap->entities = "{ \"classname\" \"worldspawn\" ";
    const Error e2 = broken.startLevel("x", brokenMap);
    CHECK(e2 && e2.message.find("broken entity text") != std::string::npos);
    std::filesystem::remove_all(tmp);
}

// The client game starting: the server's configstrings handed over as
// its game state (Quake III's gamestate_t), by a QVM the Quake III
// compiler made from tests/data/qvm/fake_cgame.c.
static void testCgameHost(const std::filesystem::path& tmp) {
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/cgame.qvm",
                                     std::vector<uint8_t>(std::begin(kFakeCgameQvm), std::end(kFakeCgameQvm)), {}},
                                    {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::CgameHost cgame(set, cvars);
    CHECK(!cgame.load());
    cgame.setConfigstrings({{12, "baseef-1"}, {289, "sound/player/fry.wav"}, {544, "n\\Reluva\\t\\0"}, {5, ""}});
    int32_t r = 0;
    CHECK(!cgame.call(ef::CgameHost::Init, {0, 0, 3}, r) && r == 3);
    CHECK(cgame.log() == "fake cgame\n");
    CHECK(!cgame.call(20, {12}, r) && r == 'b' * 1000 + 'a');
    CHECK(!cgame.call(21, {289}, r) && r == 20);
    CHECK(!cgame.call(20, {544}, r) && r == 'n' * 1000 + '\\');
    CHECK(!cgame.call(21, {5}, r) && r == 0);   // empty
    CHECK(!cgame.call(21, {100}, r) && r == 0); // never set
    CHECK(!cgame.call(22, {}, r) && r == 1 + 9 + 21 + 13);
    CHECK(!cgame.call(23, {}, r) && r == 640480);
    CHECK(!cgame.call(24, {}, r) && r == 101);
    CHECK((cgame.commands() == std::vector<std::string>{"+zoom", "weapnext"}));
    CHECK(cgame.music().first == "music/intro" && cgame.music().second == "music/loop");

    // A snapshot, in Elite Force's snapshot_t (52740 bytes).
    CHECK(!cgame.call(25, {}, r) && r == -1); // none yet
    ef::GameHost::Snapshot snap;
    snap.serverTime = 1300;
    snap.playerState.assign(460, 0);
    snap.playerState[0] = 7;   // the first word: 7
    snap.playerState[456] = 2; // the last: 2
    for (int e = 0; e < 3; ++e) {
        std::vector<uint8_t> state(204, 0);
        state[0] = uint8_t(10 + e); // s.number
        state[200] = uint8_t(e);    // the last word
        snap.entities.push_back(state);
    }
    snap.serverCommandSequence = 2;
    cgame.newSnapshot(snap);
    CHECK(!cgame.call(25, {}, r) && r == 1300);
    CHECK(!cgame.call(26, {}, r) && r == 3002);
    CHECK(!cgame.call(27, {}, r) && r == 9);
    CHECK(!cgame.call(28, {2}, r) && r == 12 * 1000 + 2);
    CHECK(cgame.snapshotRequests().size() == 2 && cgame.snapshotRequests()[1].first == 1);
    // Server commands, split into words.
    cgame.setServerCommands({"print \"Reluva entered the game\n\"", "cs 544 \"n\\Reluva\""});
    CHECK(!cgame.call(29, {1, 1}, r) && r == 2000 + 'R');
    CHECK(!cgame.call(29, {2, 0}, r) && r == 3000 + 'c');
    CHECK(!cgame.call(29, {3, 0}, r) && r == -1);

    // The player's commands, for prediction.
    CHECK(!cgame.call(30, {}, r) && r == -1); // none yet
    ef::GameHost::UserCommand cmd;
    cmd.forward = 100;
    cgame.addUserCommand(cmd);
    cgame.addUserCommand(cmd);
    CHECK(!cgame.call(30, {}, r) && r == 2100);
    // A scene: its entities kept with the size that cleared them, the view.
    CHECK(!cgame.call(31, {}, r));
    CHECK(cgame.scene().entities.size() == 3 && cgame.scene().entities[0].clearedSize == 160);
    CHECK(cgame.scene().renders == 2 && cgame.scene().view.size() == 112 && cgame.scene().view[8] == 0x80);
    // Each render with the entities added since the last.
    CHECK(cgame.scene().views.size() == 2 && cgame.scene().views[0].entityCount == 2 &&
          cgame.scene().views[1].firstEntity == 2 && cgame.scene().views[1].entityCount == 1 &&
          cgame.scene().views[0].refdef[76] == 0 && cgame.scene().views[1].refdef[76] == 1);
    bool handled = false;
    CHECK(!cgame.consoleCommand("weapnext", handled) && handled);
    CHECK(!cgame.consoleCommand("weapon 3", handled) && !handled);
    // A tag: tag_top half way between the box's two frames.
    int32_t box = 0;
    CHECK(!cgame.call(33, {}, box) && box > 0);
    Md3Model boxModel;
    CHECK(!parseMd3(buildMd3(), boxModel));
    const float halfway = (boxModel.tag(0, "tag_top")->origin[2] + boxModel.tag(1, "tag_top")->origin[2]) / 2;
    CHECK(!cgame.call(32, {box}, r) && r == int(halfway) * 10 + 1);
    CHECK(cgame.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

// The tables switch statements jump through are found and kept from
// writes: a system call writing over one stops the run, named.
static void testJumpTableGuard() {
    Qvm vm;
    CHECK(!vm.load(std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm))));
    CHECK(!vm.protectedRanges().empty());
    if (vm.protectedRanges().empty())
        return;
    // vmMain's (commands 0..33) and test command 32's (cases 100..107):
    // exactly their entries, nothing before them.
    // (They may sit side by side, as one range.)
    size_t bytes = 0;
    for (const auto& [first, end] : vm.protectedRanges())
        bytes += end - first;
    CHECK(bytes == 4 * (34 + 8));
    const auto table = vm.protectedRanges().front();
    vm.setSystemCall([&](Qvm& v, int number, const int32_t*) -> int32_t {
        if (number == 1) // Print, during Init
            v.writeString(int32_t(table.first), "-75", 4);
        return 0;
    });
    int32_t r = 0;
    const Error err = vm.call(1, {}, r);
    CHECK(err && err.message.find("system call 1") != std::string::npos &&
          err.message.find("jump table") != std::string::npos);
    CHECK(vm.readInt(int32_t(table.first)) >= 0 && vm.readInt(int32_t(table.first)) < int32_t(vm.code().size()));
    vm.setSystemCall(nullptr);
    CHECK(!vm.call(0, {}, r) && r == 4); // the table intact, it still works
    const int expected[8] = {1, 7, 3, 9, 2, 8, 4, 6};
    for (int i = 0; i < 8; ++i)
        CHECK(!vm.call(32, {100 + i}, r) && r == expected[i]);
    CHECK(!vm.call(32, {99}, r) && r == 0);
}

// The menus' system calls, called by a QVM the Quake III compiler made
// from tests/data/qvm/fake_ui.c.
static void testUiHost(const std::filesystem::path& tmp) {
    using test::toBytes;
    namespace ef = eliteforce;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"vm/ui.qvm", std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm)), {}},
                                    {"scripts/test.txt", toBytes("hello world\n"), {}},
                                    {"scripts/other.txt", toBytes("x"), {}},
                                    {"scripts/sub/deep.txt", toBytes("x"), {}},
                                    {"scripts/a.shader", toBytes("x"), {}},
                                    {"models/test/box.md3", buildMd3(), {}}}));
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    CvarStore cvars;
    ef::UiHost ui(set, cvars);
    Error err = ui.load();
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return;
    }
    int32_t r = 0;
    CHECK(!ui.call(ef::UiHost::Init, {}, r) && r == 1202); // 12 bytes, 2 .txt files directly in scripts/
    CHECK(ui.log() == "fake ui\n");
    CHECK(cvars.string("ui_test") == "7" && cvars.find("ui_test")->flags == CvarStore::Archive);
    CHECK(!ui.call(20, {}, r) && r == 7);
    cvars.set("ui_test", "9");
    CHECK(!ui.call(20, {}, r) && r == 7); // the program's copy changes on Cvar_Update
    CHECK(!ui.call(21, {}, r) && r == 9);
    CHECK(!ui.call(25, {}, r) && r == 2);
    CHECK(!ui.call(22, {}, r) && r == 640480);

    CHECK(!ui.call(ef::UiHost::SetActiveMenu, {1}));
    CHECK(!ui.call(ef::UiHost::Refresh, {1234}, r) && r == 1234);
    CHECK(ui.pictures().size() == 3); // two, then the pointer
    if (ui.pictures().size() == 3) {
        const auto& p = ui.pictures()[0];
        CHECK(p.x == 10 && p.y == 20 && p.w == 320 && p.h == 30 && p.s1 == 0 && p.t2 == 0.5f);
        CHECK(p.color[0] == 1 && p.color[1] == 0.5f && p.color[2] == 0 && p.color[3] == 1);
        CHECK(ui.shaders().name(p.shader) == "menu/test");
        CHECK(ui.pictures()[1].color == (std::array<float, 4>{1, 1, 1, 1}));
    }
    CHECK(!ui.call(23, {}, r) && r == 'h' * 1000000 + 'e' * 1000); // "hel", then NUL padding
    CHECK(cvars.string("ui_text") == "hello world\n");
    cvars.set("ui_number", "16");
    CHECK(!ui.call(24, {}, r) && r == 40);
    err = ui.call(26, {}, r);
    CHECK(err && err.message.find("boom") != std::string::npos);
    CHECK(!ui.call(20, {}, r) && r == 9); // usable again after the program's error
    // Typed characters come as keys with 1024 added; plain keys don't type.
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'q', 1}));
    CHECK(!ui.call(ef::UiHost::KeyEvent, {'Q' | 1024, 1}));
    CHECK(!ui.call(27, {}, r) && r == 'Q' * 1000 + 1);

    // The CD key: taken if it has 18 letters and digits, kept in the key
    // file, read back by a new run, which then counts it as checked.
    ui.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!ui.call(28, {1}, r) && r == 0 && !std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    CHECK(!ui.call(28, {0}, r) && r == 1 && std::filesystem::exists(tmp / "save" / "cdkey.txt"));
    // Call 58, the model bounds the main menu asks for: frame 0's box.
    Md3Model box;
    CHECK(!parseMd3(buildMd3(), box));
    const auto& f0 = box.frames[0];
    const int boxCode = int(f0.mins[0] * 10) + int(f0.mins[1] * 100) + int(f0.mins[2] * 1000) +
                        int(f0.maxs[0] * 10000) + int(f0.maxs[1] * 100000) + int(f0.maxs[2] * 1000000);
    CHECK(boxCode != 0);
    CHECK(!ui.call(29, {}, r) && r == boxCode);
    CHECK(!ui.call(29, {99}, r) && r == 0); // no such model: zeros
    CvarStore fresh;
    ef::UiHost again(set, fresh);
    again.setKeyFile(tmp / "save" / "cdkey.txt");
    CHECK(!again.load() && !again.call(ef::UiHost::Init) && fresh.string("ui_cdkeychecked") == "1");

    // A frame with no menu open opens the main one, as the original
    // engine does while not connected; with one open, it's left alone.
    CHECK(!ui.call(31, {}, r) && r == 1);
    ui.clearPictures();
    CHECK(!ui.refresh(2000) && !ui.call(31, {}, r) && r == 1 && ui.pictures().size() == 4);
    CHECK(!ui.call(30));
    ui.clearPictures();
    CHECK(!ui.refresh(2016) && !ui.call(31, {}, r) && r == 2 && ui.pictures().size() == 4);

    // A menu sound: passed on by name with its channel.
    std::vector<std::pair<std::string, int>> played;
    ui.startLocalSound = [&](const std::string& name, int channel) { played.emplace_back(name, channel); };
    CHECK(!ui.call(33));
    CHECK(played.size() == 1 && played[0].first == "sound/interface/button1.wav" && played[0].second == 6);

    CHECK(ui.unhandled().empty());
    std::filesystem::remove_all(tmp);
}

void testKeyBindings() {
    using reluva::idtech3::KeyBindings;
    namespace q3key = reluva::idtech3::q3key;
    CHECK(KeyBindings::keyNumber("MOUSE2") == q3key::Mouse2 && KeyBindings::keyNumber("space") == q3key::Space);
    CHECK(KeyBindings::keyNumber("W") == 'w' && KeyBindings::keyNumber("F12") == q3key::F1 + 11);
    CHECK(KeyBindings::keyNumber("nonsense") == -1 && KeyBindings::keyNumber("0x22") == '"');
    CHECK(KeyBindings::keyName(q3key::WheelUp) == "MWHEELUP" && KeyBindings::keyName('w') == "w");
    CHECK(KeyBindings::keyName(';') == "SEMICOLON" && KeyBindings::keyName(q3key::F1 + 2) == "F3");

    reluva::idtech3::CvarStore cvars;
    KeyBindings b;
    int ignored = 0;
    auto readFile = [](const std::string& name, std::string& out) {
        if (name != "more.cfg")
            return false;
        out = "bind x \"say hi\"\n";
        return true;
    };
    b.execute("unbindall\nbind w +forward // a comment\nbind MOUSE2 \"+altattack\"\n"
              "bind 3 \"weapon 3; echo three\"\nseta sensitivity 7\nset cl_run 0; exec more\n"
              "unbind w\nbind TAB +scores\nconnect somewhere\n",
              cvars, &ignored, readFile);
    CHECK(b.get('w').empty() && b.get(q3key::Mouse2) == "+altattack" && b.get('3') == "weapon 3; echo three");
    CHECK(b.get('x') == "say hi" && b.get(q3key::Tab) == "+scores" && b.changed);
    CHECK(cvars.integer("sensitivity") == 7 && (cvars.find("sensitivity")->flags & cvars.Archive));
    CHECK(cvars.integer("cl_run") == 0 && ignored == 1);
    // Written and read back, the same.
    KeyBindings again;
    again.execute(b.write(), cvars);
    for (int32_t k = 0; k < q3key::Count; ++k)
        CHECK(again.get(k) == b.get(k));
    const auto parts = reluva::idtech3::splitCommands("a \"b;c\"; d\ne // f; g");
    CHECK(parts.size() == 3 && parts[0] == "a \"b;c\"" && reluva::idtech3::splitCommand(parts[0]).size() == 2);
}

void testMarks() {
    using namespace reluva::idtech3;
    // A floor, 128 units square at height 0, facing up, as two triangles.
    Bsp bsp;
    bsp.shaders.push_back({"floor", 0, 1});
    for (auto [x, y] : std::array<std::pair<float, float>, 4>{{{-64, -64}, {64, -64}, {64, 64}, {-64, 64}}}) {
        BspVertex v;
        v.pos = {x, y, 0};
        v.normal = {0, 0, 1};
        bsp.vertices.push_back(v);
    }
    bsp.indices = {0, 1, 2, 0, 2, 3};
    BspSurface floor;
    floor.type = SurfaceType::Planar;
    floor.numVertices = 4;
    floor.numIndices = 6;
    bsp.surfaces.push_back(floor);
    BspModel world;
    world.numSurfaces = 1;
    bsp.models.push_back(world);

    MarkSurfaces marks(bsp);
    CHECK(marks.triangleCount() == 2);
    // A scorch 16 units across, just above the floor, pushed down onto it.
    const std::vector<Vec3f> square{{-8, -8, 1}, {8, -8, 1}, {8, 8, 1}, {-8, 8, 1}};
    std::vector<Vec3f> points;
    std::vector<MarkSurfaces::Fragment> fragments;
    marks.mark(square, {0, 0, -20}, 384, 128, points, fragments);
    CHECK(!fragments.empty() && fragments.size() <= 2);
    float area = 0;
    for (const auto& f : fragments) {
        CHECK(f.numPoints >= 3);
        for (int i = 1; i + 1 < f.numPoints; ++i) {
            const Vec3f& a = points[size_t(f.firstPoint)];
            const Vec3f& b = points[size_t(f.firstPoint + i)];
            const Vec3f& c = points[size_t(f.firstPoint + i + 1)];
            area += std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2;
        }
    }
    for (const Vec3f& p : points)
        CHECK(std::fabs(p[2]) < 1e-4f && std::fabs(p[0]) <= 8.001f && std::fabs(p[1]) <= 8.001f);
    CHECK(std::fabs(area - 256) < 0.1f); // the whole square, on the floor
    // From below, the floor faces away; too far above, it's out of reach.
    points.clear();
    fragments.clear();
    marks.mark(square, {0, 0, 20}, 384, 128, points, fragments);
    CHECK(fragments.empty());
    const std::vector<Vec3f> high{{-8, -8, 100}, {8, -8, 100}, {8, 8, 100}, {-8, 8, 100}};
    marks.mark(high, {0, 0, -20}, 384, 128, points, fragments);
    CHECK(fragments.empty());
    // Room for only a few points: nothing past the limit.
    marks.mark(square, {0, 0, -20}, 4, 128, points, fragments);
    CHECK(points.size() <= 4);
    // A surface that takes no marks.
    bsp.shaders[0].surfaceFlags = surfaceflags::NoMarks;
    MarkSurfaces none(bsp);
    CHECK(none.triangleCount() == 0);
}

void testAreaPortals() {
    using namespace reluva::idtech3;
    // Three areas in a row, portals between 0-1 and 1-2.
    AreaPortals areas(3);
    CHECK(areas.connected(1, 1) && !areas.connected(0, 1) && !areas.connected(0, -1));
    areas.adjust(0, 1, true);
    CHECK(areas.connected(0, 1) && !areas.connected(0, 2));
    areas.adjust(1, 2, true);
    CHECK(areas.connected(0, 2));
    // Opened twice, closed once: still open.
    areas.adjust(0, 1, true);
    areas.adjust(0, 1, false);
    CHECK(areas.connected(0, 2));
    areas.adjust(0, 1, false);
    CHECK(!areas.connected(0, 2) && areas.connected(1, 2));
    areas.adjust(0, 1, false); // closing a closed one changes nothing
    areas.adjust(0, 1, true);
    CHECK(areas.connected(0, 2));
}

void testVideoSettings() {
    using namespace reluva::idtech3;
    CvarStore cvars;
    VideoSettings v = videoSettings(cvars);
    CHECK(v.width == 960 && v.height == 720 && !v.fullscreen && v.mipmaps && v.subdivisions == 4);
    cvars.set("r_mode", "3");
    cvars.set("r_fullscreen", "1");
    cvars.set("r_picmip", "9");
    cvars.set("r_texturemode", "GL_LINEAR_MIPMAP_LINEAR");
    cvars.set("r_subdivisions", "999");
    cvars.set("r_lodBias", "1");
    cvars.set("r_fastsky", "1");
    cvars.set("r_ext_texture_filter_anisotropic", "1");
    v = videoSettings(cvars);
    CHECK(v.anisotropic);
    CHECK(v.width == 640 && v.height == 480 && v.fullscreen);
    CHECK(v.picmip == 4 && !v.nearest && v.mipmaps && !v.mipNearest);
    CHECK(v.subdivisions == 80 && v.lodBias == 1 && v.fastSky);
    cvars.set("r_mode", "-1");
    cvars.set("r_customwidth", "1920");
    cvars.set("r_customheight", "1080");
    cvars.set("r_texturemode", "GL_NEAREST");
    v = videoSettings(cvars);
    CHECK(v.width == 1920 && v.height == 1080 && v.nearest && !v.mipmaps);

    // A 3x3 patch: flat cuts once, a bend cuts finer for a smaller error.
    BspVertex control[9];
    for (int i = 0; i < 9; ++i)
        control[i].pos = {float(i % 3) * 64, float(i / 3) * 64, 0};
    CHECK(patchLevel(control, 3, 3, 4) == 1);
    control[4].pos[2] = 64;
    const int coarse = patchLevel(control, 3, 3, 4), fine = patchLevel(control, 3, 3, 0.5f);
    CHECK(coarse >= 2 && fine > coarse && fine <= 16);
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_idtech3_tests";
    testLexer();
    testEntities();
    testShaders();
    testShaderStages();
    testWaves();
    testBsp();
    testPatch();
    testMd3();
    testSkinAndPlacement();
    testLightGrid();
    testQvm();
    testCompiledQvm();
    testCvars();
    testKeyBindings();
    testMarks();
    testMdr();
    testPlayerAnimation();
    testAas();
    testBotScript();
    testBotCharacter();
    testBotAi();
    testBotChat();
    testPatchCollision();
    testVideoSettings();
    testAreaPortals();
    testVmFileList();
    testSoundLibrary(tmp / "sounds");
    testSoundSystem(tmp / "soundsystem");
    testCollision();
    testGameHost(tmp / "game");
    testCgameHost(tmp / "cgame");
    testJumpTableGuard();
    testUiHost(tmp / "ui");
    testFilesystemAndDetection(tmp / "fs");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
