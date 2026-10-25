// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the New Worlds .cat reader against archives built here, following
// docs/formats/newworlds/cat.md, so no game data is needed.

#include "engines/newworlds/arf.h"
#include "engines/newworlds/briefing.h"
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/colony.h"
#include "engines/newworlds/economy.h"
#include "engines/newworlds/fif.h"
#include "engines/newworlds/string_table.h"
#include "engines/newworlds/texture.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/gmf.h"
#include "engines/newworlds/levels.h"
#include "engines/newworlds/mdl.h"
#include "engines/newworlds/mission_script.h"
#include "engines/newworlds/model_pose.h"
#include "engines/newworlds/model_textures.h"
#include "engines/newworlds/rasterizer.h"
#include "engines/newworlds/star_system.h"
#include "engines/newworlds/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::newworlds;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

struct Item {
    std::string path; // empty for containers
    uint16_t tag;
    std::string payload;
    std::vector<Item> children;
};

Item file(std::string path, uint16_t tag, std::string payload) { return {std::move(path), tag, std::move(payload), {}}; }
Item container(uint16_t tag, std::vector<Item> children) { return {"", tag, "", std::move(children)}; }

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void set32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

uint32_t chunkLength(const Item& it) {
    if (it.children.empty() && !isContainerTag(it.tag))
        return 6 + uint32_t(it.payload.size());
    uint32_t n = 6;
    for (const Item& c : it.children)
        n += chunkLength(c);
    return n;
}

struct Builder {
    std::vector<uint8_t> table, data;
    uint32_t count = 0, index = 0;

    void add(const std::vector<Item>& items) {
        for (const Item& it : items) {
            const bool isContainer = isContainerTag(it.tag);
            // The real offset is data position + 12 (two chunk headers).
            put32(table, uint32_t(data.size()) + 12);
            put16(table, uint16_t(index++));
            table.push_back(0);
            table.push_back(0x15);
            put16(data, it.tag);
            if (isContainer) {
                put32(data, chunkLength(it) - 6);
                table.resize(table.size() + 256, 0xcd); // junk, as in real files
                ++count;
                add(it.children);
                ++index; // a folder uses an extra index after its children
            } else {
                put32(data, uint32_t(it.payload.size()));
                data.insert(data.end(), it.payload.begin(), it.payload.end());
                const size_t base = table.size();
                table.resize(base + 326, 0);
                std::memcpy(&table[base], it.path.data(), it.path.size());
                set32(table, base + 0x116, uint32_t(it.payload.size()));
                set32(table, base + 0x11e, 966294866); // 2000-08-14 23:14:26 UTC
                ++count;
            }
        }
    }

    std::vector<uint8_t> build() const {
        std::vector<uint8_t> body;
        put16(body, 5);
        put32(body, uint32_t(table.size() + 4));
        put32(body, count);
        body.insert(body.end(), table.begin(), table.end());
        body.insert(body.end(), data.begin(), data.end());
        std::vector<uint8_t> out;
        put16(out, 1);
        put32(out, uint32_t(body.size()));
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
};

std::filesystem::path writeArchive(const std::filesystem::path& dir, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(dir);
    auto p = dir / "test.cat";
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return p;
}

const char* kPrefix = "c:\\prjs\\game\\stc\\bin\\rsc\\dat\\pc_win32\\";

std::vector<Item> sampleItems() {
    return {
        file(std::string(kPrefix) + "str\\eng\\miss\\rom01.stb", cattag::Strings, "strings!"),
        container(cattag::TextureSet,
                  {file(std::string(kPrefix) + "lbp\\txr\\rom\\rb_muzzl.mmb", cattag::Mmb, "8 bytes."),
                   file(std::string(kPrefix) + "lbp\\txr\\rom\\rb_muzzl.mp0", cattag::Texture, "pixels")}),
        container(cattag::FontSet,
                  {container(cattag::FontPart, {file(std::string(kPrefix) + "gui\\font.fif", cattag::Fif, "font")})}),
        file("c:\\prjs\\STNWSFX\\alert003.wav", cattag::Wave, "RIFF...."),
    };
}

void testRead(const std::filesystem::path& tmp) {
    Builder b;
    b.add(sampleItems());
    CatArchive cat;
    Error err = cat.open(writeArchive(tmp, b.build()));
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "  %s\n", err.message.c_str());
        return;
    }

    const auto& e = cat.entries();
    CHECK(e.size() == 8);
    CHECK(e[0].name == "str/eng/miss/rom01.stb" && e[0].tag == cattag::Strings && e[0].parent == -1);
    CHECK(e[0].modified == 966294866);
    CHECK(e[1].container && e[1].parent == -1);
    CHECK(e[2].name == "lbp/txr/rom/rb_muzzl.mmb" && e[2].parent == 1);
    CHECK(e[3].parent == 1);
    CHECK(e[4].container && e[5].container && e[5].parent == 4);
    CHECK(e[6].name == "gui/font.fif" && e[6].parent == 5);
    CHECK(e[7].name == "stnwsfx/alert003.wav" && e[7].parent == -1);

    const CatEntry* wav = cat.find("STNWSFX\\Alert003.WAV");
    CHECK(wav == &e[7]);
    std::vector<uint8_t> data;
    CHECK(wav && !cat.read(*wav, data));
    CHECK(std::string(data.begin(), data.end()) == "RIFF....");
    const CatEntry* mp0 = cat.find("lbp/txr/rom/rb_muzzl.mp0");
    CHECK(mp0 && !cat.read(*mp0, data) && std::string(data.begin(), data.end()) == "pixels");
    CHECK(cat.find("missing.stb") == nullptr);
}

void testRejectsCorruption(const std::filesystem::path& tmp) {
    Builder b;
    b.add(sampleItems());
    const std::vector<uint8_t> good = b.build();
    CatArchive cat;

    std::vector<uint8_t> bad = good;
    bad[0] = 2; // wrong root tag
    CHECK(cat.open(writeArchive(tmp, bad)));

    bad = good;
    bad.push_back(0); // root size no longer matches the file
    CHECK(cat.open(writeArchive(tmp, bad)));

    bad = good;
    bad[0x10] += 1; // first entry's offset off by one: breaks the chunk chain
    CHECK(cat.open(writeArchive(tmp, bad)));

    CHECK(!cat.open(writeArchive(tmp, good)));
}

void testNormalizeName() {
    CHECK(CatArchive::normalizeName("c:\\prjs\\game\\stc\\bin\\rsc\\dat\\pc_win32\\geo\\rom\\rb_mid.geo") ==
          "geo/rom/rb_mid.geo");
    CHECK(CatArchive::normalizeName("c:\\prjs\\STNWSFX\\alert003.wav") == "stnwsfx/alert003.wav");
    CHECK(CatArchive::normalizeName("") == "");
}

// Laid out like the start of rom01.stb: count, then (id, UTF-16 text, 0).
std::vector<uint8_t> stringTableBytes(const std::vector<std::pair<uint32_t, std::u16string>>& strings,
                                      uint32_t count) {
    std::vector<uint8_t> b;
    put32(b, count);
    for (const auto& [id, str] : strings) {
        put32(b, id);
        for (char16_t c : str)
            put16(b, uint16_t(c));
        put16(b, 0);
    }
    return b;
}

void testStringTable() {
    const std::vector<std::pair<uint32_t, std::u16string>> strings = {
        {1, u" 'Et Tu?'"},
        {23, u"Stardate 8988.4.  The type 6 world Sakuntula has significant deposits of dilithium."},
        {22, u"~STELLAR CARTOGRAPHY~"},
        {5, u""},
    };
    StringTable t;
    CHECK(!t.parse(stringTableBytes(strings, 4)));
    CHECK(t.strings().size() == 4);
    CHECK(t.find(1) && *t.find(1) == " 'Et Tu?'");
    CHECK(t.find(22) && *t.find(22) == "~STELLAR CARTOGRAPHY~");
    CHECK(t.find(5) && t.find(5)->empty());
    CHECK(t.find(99) == nullptr);

    CHECK(t.parse(stringTableBytes(strings, 5)));    // count too high
    CHECK(t.parse(stringTableBytes(strings, 3)));    // bytes left over
    auto cut = stringTableBytes(strings, 4);
    cut.resize(cut.size() - 1);
    CHECK(t.parse(cut));                             // last string unterminated
}

// Format 6: u32 6, u32 palette count, palette (blue, green, red, 0),
// u32 0, u32 width, u32 height, indices, alpha.
// keyIndex >= 0: colour-keyed layout (u32 1, u32 key, width, height).
std::vector<uint8_t> textureBytes(uint32_t w, uint32_t h, uint32_t format = 6, int keyIndex = -1) {
    std::vector<uint8_t> b;
    put32(b, format);
    put32(b, 2);
    b.insert(b.end(), {0x10, 0x20, 0xf0, 0}); // index 0: red 0xf0, green 0x20, blue 0x10
    b.insert(b.end(), {0xff, 0x00, 0x00, 0}); // index 1: pure blue
    if (keyIndex >= 0) {
        put32(b, 1);
        put32(b, uint32_t(keyIndex));
    } else {
        put32(b, 0);
    }
    put32(b, w);
    put32(b, h);
    for (uint32_t i = 0; i < w * h; ++i)
        b.push_back(uint8_t(i % 2));          // indices alternate 0, 1
    if (format == 6)
        for (uint32_t i = 0; i < w * h; ++i)
            b.push_back(uint8_t(i * 10));     // alpha
    return b;
}

void testTexture() {
    Texture t;
    CHECK(!decodeTexture(textureBytes(4, 2), t));
    CHECK(t.width == 4 && t.height == 2 && t.rgba.size() == 32);
    CHECK(t.rgba[0] == 0xf0 && t.rgba[1] == 0x20 && t.rgba[2] == 0x10 && t.rgba[3] == 255); // pixel 0
    CHECK(t.rgba[4] == 0x00 && t.rgba[5] == 0x00 && t.rgba[6] == 0xff && t.rgba[7] == 255); // pixel 1
    CHECK(t.alpha.size() == 8 && t.alpha[0] == 0 && t.alpha[1] == 10 && t.alpha[7] == 70); // separate layer

    // Format 1: one layer, opaque.
    CHECK(!decodeTexture(textureBytes(4, 2, 1), t));
    CHECK(t.format == 1 && t.width == 4 && t.height == 2 && !t.colourKeyed);
    CHECK(t.rgba[4] == 0x00 && t.rgba[6] == 0xff && t.rgba[7] == 255 && t.alpha.empty());

    // Colour key: pixels using the key index become transparent.
    CHECK(!decodeTexture(textureBytes(5, 3, 1, 1), t));
    CHECK(t.colourKeyed && t.keyIndex == 1 && t.width == 5 && t.height == 3);
    CHECK(t.rgba[3] == 255 && t.rgba[7] == 0);            // index 0 opaque, index 1 keyed out
    CHECK(!decodeTexture(textureBytes(4, 2, 6, 0), t));
    CHECK(t.rgba[3] == 0 && t.rgba[7] == 255 && t.alpha[1] == 10); // key is separate from the alpha layer

    CHECK(decodeTexture(textureBytes(4, 2, 7), t)); // unknown format
    auto bad = textureBytes(4, 2);
    bad.pop_back();
    CHECK(decodeTexture(bad, t));                  // wrong size
    CHECK(decodeTexture(textureBytes(0, 2), t));   // zero width
}

void putF(std::vector<uint8_t>& b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, v);
}

// A two-sided triangle laid out as in docs/formats/newworlds/geo.md: face 0
// in the file header, face 1 its back, one edge group (or none, as in
// terrain tiles).
// Two triangles back to back. With twoParts the back is part 1, with its
// own copy of the vertices.
std::vector<uint8_t> triangleGeo(bool withEdges = true, bool oddAttachment = false, bool twoParts = false) {
    const float pos[3][2] = {{0, 0}, {100, 0}, {0, 200}};
    const float uv[3][2] = {{0, 0}, {65536, 0}, {0, 65536}};
    const uint32_t parts = twoParts ? 2 : 1;
    std::vector<uint8_t> b;
    auto corner = [&](uint32_t v) {
        put32(b, v);
        putF(b, uv[v][0]);
        putF(b, uv[v][1]);
        for (int i = 0; i < 5; ++i)
            put32(b, 0);
    };
    put32(b, 0); put32(b, 2); put32(b, 3); put32(b, 0); // header = face 0: 2 faces, 3 corners
    corner(0); corner(1); corner(2);
    put32(b, 0); put32(b, 0); put32(b, 3); put32(b, parts - 1); // face 1, the back
    corner(2); corner(1); corner(0);
    put32(b, parts - 1); put32(b, 1); put32(b, parts); put32(b, 0); // bounds, no attachments
    putF(b, 0); putF(b, 0); putF(b, 224);
    put32(b, 1); put32(b, 0); put32(b, 0);
    if (oddAttachment) // odd-length data puts the part data off 4-byte alignment
        b.insert(b.end(), {0x1c, 0x00, 0x0c});
    put32(b, 0); put32(b, 1); put32(b, parts);          // part data
    for (uint32_t part = 0; part < parts; ++part) {
        if (part > 0)
            put32(b, 0);                                // end of the previous part
        put32(b, part); put32(b, 3);                    // part 1 starts with 1, as in ptank_l
        for (auto& p : pos) {
            putF(b, p[0]); putF(b, p[1]); putF(b, 0);
        }
        put32(b, 1); put32(b, 2);                       // two normals
        putF(b, 0); putF(b, 0); putF(b, 1);
        putF(b, 0); putF(b, 0); putF(b, -1);
        if (withEdges && !twoParts) {
            put32(b, 1); put32(b, 1); put32(b, 3);      // one group of three edges
            const uint32_t edges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
            for (auto& e : edges) {
                put32(b, 0); put32(b, e[0]); put32(b, e[1]); put32(b, 0); put32(b, 1);
            }
        } else {
            put32(b, 0);
        }
    }
    return b;
}

void testGeo() {
    GeoModel m;
    Error err = parseGeo(triangleGeo(), m);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "  %s\n", err.message.c_str());
        return;
    }
    CHECK(m.parts.size() == 1 && m.parts[0].vertices.size() == 3 && m.parts[0].vertices[2].y == 200.0f);
    CHECK(m.faces.size() == 2 && m.faces[0].corners.size() == 3 && m.faces[1].corners[0].vertex == 2);
    CHECK(m.faces[0].corners[1].vertex == 1 && m.faces[0].corners[1].u == 1.0f);
    const auto& part = m.parts[0];
    CHECK(part.edges.size() == 3 && part.edges[1].v1 == 2 && part.edges[1].faceB == 1);
    CHECK(part.normalLists.size() == 1 && part.normalLists[0].size() == 2 && m.radius == 224.0f);

    CHECK(!parseGeo(triangleGeo(false), m) && m.parts[0].edges.empty()); // terrain-style: no edges

    err = parseGeo(triangleGeo(true, true), m);
    CHECK(!err && m.parts[0].vertices.size() == 3 && m.parts[0].vertices[1].x == 100.0f);

    err = parseGeo(triangleGeo(false, false, true), m);
    CHECK(!err && m.parts.size() == 2 && m.faces[1].part == 1 && m.parts[1].vertices.size() == 3 &&
          m.parts[1].node == 1);

    auto bad = triangleGeo();
    bad[bad.size() - 16] = 7; // last edge now starts at vertex 7
    CHECK(parseGeo(bad, m));
}

// A 2 x 1 map: 2 tiles, 1 block, both cells using it.
std::vector<uint8_t> tinyGmf() {
    std::vector<uint8_t> b;
    auto put16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    put16(2); put16(2); put16(1); put16(2); put16(1);
    for (int i = 0; i < 2; ++i) { // type 21, contents unknown
        put16(21); put16(4); put16(0); put32(b, 0);
    }
    for (uint32_t i = 0; i < 2; ++i) { // type 20: number, 0, model, flags, u16, 8 x 8 grid
        put16(20); put16(18 + 64); put16(0);
        put32(b, i); put32(b, 0); put32(b, 6 + i); put32(b, 0x18000 + 0x4000 * i); put16(0);
        for (int c = 0; c < 64; ++c)
            b.push_back(uint8_t(c == 0 ? 2 : 11));
    }
    put16(19); put16(22); put16(0); // block: 6 bytes, then four tile numbers
    for (int i = 0; i < 6; ++i)
        b.push_back(0);
    for (uint32_t t : {0u, 1u, 1u, 0u})
        put32(b, t);
    put32(b, 3); put32(b, 0x00a1b277); put32(b, 0xffffffffu); put32(b, 0xffffffffu); // lists
    for (uint32_t cell = 0; cell < 2; ++cell) {                                     // grid
        put16(0); put16(50); put16(0); put32(b, cell * 2); // one object, then none
    }
    // The colour table: magenta, then one colour (B, G, R), then the end.
    const uint8_t palette[8] = {0xff, 0x00, 0xff, 0x00, 0xf9, 0xc6, 0x42, 0x00};
    b.insert(b.end(), palette, palette + 8);
    b.insert(b.end(), 1024 - 8, 0);
    return b;
}

void testGmf() {
    GmfMap m;
    Error err = parseGmf(tinyGmf(), m);
    CHECK(!err);
    if (err) {
        std::fprintf(stderr, "  %s\n", err.message.c_str());
        return;
    }
    CHECK(m.width == 2 && m.height == 1 && m.tiles.size() == 2 && m.blocks.size() == 1 && m.cells.size() == 2);
    CHECK(m.tiles[1].model == 7 && m.tiles[0].layers[0] == 2 && m.tiles[0].layers[1] == 11);
    CHECK(m.tiles[0].rotation == 2 && m.tiles[1].rotation == 3);
    CHECK(m.blocks[0][2] == 1 && m.cells[1].block == 0 && m.cells[1].value == 50);
    CHECK(m.scenery.size() == 1 && m.scenery[0].bx == 0 && m.scenery[0].x == 7 && m.scenery[0].y == 7);
    CHECK(m.scenery[0].type == 1 && m.scenery[0].turn == 4 && m.scenery[0].variant == 2 && m.scenery[0].extra == 50);
    CHECK(m.palette.size() == 2 && m.palette[1][0] == 0x42 && m.palette[1][1] == 0xc6 && m.palette[1][2] == 0xf9);
    // Model numbers: the planet's fixed tile list first, then the rest, lod last.
    const std::vector<std::string> geos = {"geo/map/d/brid_01.geo", "geo/map/d/d_01.geo", "geo/map/d/lod/d_01.geo",
                                           "geo/map/d/needle_0.geo", "geo/map/d/d_26.geo", "geo/map/d/ramp.geo"};
    const std::vector<size_t> order = tileModelOrder(geos);
    // brid_01 at 27, ramp 29, d_26 38, needle_0 51, then the lod model after the 55 places.
    CHECK(order.size() == 56 && order[0] == 1 && order[27] == 0 && order[24] == kNoModel && order[29] == 5 &&
          order[38] == 4 && order[51] == 3 && order[55] == 2);
    auto bad = tinyGmf();
    bad[bad.size() - 1024 - 10] = 5; // the last cell uses block 5 of 1
    CHECK(parseGmf(bad, m));
}

// A metamodel: title, four name lists (the first with leftovers after it,
// as when its count is larger than its entries), then u32 0 and the states.
void testMdl() {
    std::vector<uint8_t> b;
    auto name = [&](const char* n, uint32_t v) {
        char buf[32] = {};
        std::snprintf(buf, sizeof buf, "%s", n);
        b.insert(b.end(), buf, buf + 32);
        put32(b, v);
    };
    char title[32] = "testmetamodel";
    b.insert(b.end(), title, title + 32);
    put32(b, 3); name("main", 0); name("turret_1", 1); name("terminate", 0xffffffffu);
    put32(b, 0x150); put32(b, 0x009a0300); // leftovers
    put32(b, 1); name("gun_muzzle_1", 0); name("terminate", 0xffffffffu);
    put32(b, 0); name("terminate", 0xffffffffu);
    put32(b, 0); name("terminate", 0xffffffffu);
    put32(b, 0); put32(b, 2); name("normal", 0); name("low detail", 1); name("terminate", 0xffffffffu);
    MdlModel m;
    Error err = parseMdl(b, m);
    CHECK(!err);
    CHECK(m.title == "testmetamodel" && m.parts.size() == 2 && m.parts[1].name == "turret_1" && m.parts[1].value == 1);
    CHECK(m.points.size() == 1 && m.points[0].name == "gun_muzzle_1" && m.places.empty() && m.animated.empty());
    CHECK(m.states.size() == 2 && m.states[1] == "low detail");
}

// An animation frame: u32 count, then per part 9 f32 rotation, f32 scale and
// a position chunk (u16 0x1c, u32 12, 3 f32).
void testArf() {
    std::vector<uint8_t> b;
    auto putf = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(b, v); };
    put32(b, 2);
    for (int part = 0; part < 2; ++part) {
        for (float f : {0.f, 0.f, -1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f})
            putf(f);
        b.push_back(0x1c); b.push_back(0);
        put32(b, 12);
        putf(float(part)); putf(2); putf(52);
    }
    std::vector<PartTransform> t;
    CHECK(!parseArf(b, t));
    CHECK(t.size() == 2 && t[1].position.x == 1 && t[1].position.z == 52 && t[0].rotation[2] == -1 && t[0].scale == 1);
    b.pop_back();
    CHECK(bool(parseArf(b, t))); // a byte short
}


void testFif() {
    // Three glyphs from 32: space (2 wide), '!' (1 wide), '"' (none).
    std::vector<uint8_t> b(std::begin("FONTPROP"), std::end("FONTPROP") - 1);
    b.push_back(32); b.push_back(0); b.push_back(3); b.push_back(0);
    put32(b, 2); put32(b, 4); put32(b, 0);
    for (uint32_t v : {0u, 0u, 2u, 4u, 2u, 0u, 1u, 4u, 0u, 0u, 0u, 0u})
        put32(b, v);
    GameFont f;
    CHECK(!parseFif(b, f));
    CHECK(f.first == 32 && f.lineHeight == 4 && f.size == 2 && f.glyphs.size() == 3);
    CHECK(f.glyphs[1].x == 2 && f.glyphs[1].width == 1 && f.glyphs[2].height == 0);
    // No glyph for '"' or U+2603: drawn as '?', which this font lacks too.
    CHECK(f.glyph('!') == &f.glyphs[1] && f.glyph('"') == nullptr && f.glyph(0x2603) == nullptr);
    CHECK(f.textWidth(" ! !") == 2 + 1 + 2 + 1);
    // '!' is a full-coverage column: drawn red over black at x = 2 after the space.
    f.imageWidth = 3, f.imageHeight = 4;
    f.coverage = {0, 0, 255, 0, 0, 255, 0, 0, 128, 0, 0, 0};
    Surface s;
    s.create(8, 4, PixelFormat::RGBA32);
    CHECK(f.drawText(s, 0, 0, " !", {200, 0, 0}) == 3);
    CHECK(s.row(0)[2 * 4] == 200 && s.row(2)[2 * 4] == 100 && s.row(3)[2 * 4] == 0 && s.row(0)[1 * 4] == 0);
    b.push_back(0);
    CHECK(bool(parseFif(b, f))); // a byte too many
}

void testLevels() {
    std::vector<Level> l;
    CHECK(!parseLevelList("// format comment\r\nF1,2,Power Corrupts\r\n// J1,6,Defiant\r\n"
                          "J1,3,Into the Arena\r\n\r\n//C9,2,(Bonus) \"Slaver's Run\"  \r\n",
                          l));
    CHECK(l.size() == 4 && l[0].shortName == "F1" && l[0].maxPlayers == 2 && l[0].title == "Power Corrupts");
    CHECK(l[1].commentedOut && l[1].title == "Defiant" && !l[2].commentedOut);
    CHECK(l[3].commentedOut && l[3].title == "(Bonus) \"Slaver's Run\"");
    CHECK(levelForMap(l, "map/f1.gmf") == &l[0] && levelForMap(l, "MAP\\J1.GMF") == &l[2]);
    CHECK(levelForMap(l, "map/kurt64.gmf") == nullptr);
    CHECK(bool(parseLevelList("F1,two,Power Corrupts\n", l)));

    CHECK(missionSides("F1") == std::vector<char>{'F'} && missionSides("K12") == std::vector<char>{'K'});
    CHECK(missionSides("J4").size() == 3 && missionSides("C1") == std::vector<char>{'F'});
    CHECK(missionTableName("F1", 'F') == "str/eng/miss/fed01.stb");
    CHECK(missionTableName("K12", 'K') == "str/eng/miss/klg12.stb");
    CHECK(missionTableName("R2", 'R') == "str/eng/miss/rom02.stb");
    CHECK(missionTableName("J1", 'K') == "str/eng/miss/jnt01klg.stb");
    CHECK(missionTableName("C5", 'F') == "str/eng/miss/cvr03.stb");
    CHECK(missionTableName("Test", 'F') == "str/eng/miss/tst.stb" && missionTableName("E1", 'F').empty());
    CHECK(missionMapNames("F1") == std::vector<std::string>{"map/f1.gmf"});
    CHECK((missionMapNames("R14") == std::vector<std::string>{"map/r14.gmf", "map/r12.gmf"}));
    CHECK(isMissionClip("F1005", "F1", 'F') && isMissionClip("f1015", "F1", 'F'));
    CHECK(!isMissionClip("F12005", "F1", 'F') && isMissionClip("F12005", "F12", 'F'));
    CHECK(isMissionClip("K12009A", "K12", 'K') && !isMissionClip("K12009", "K1", 'K'));
    CHECK(isMissionClip("J1R011", "J1", 'R') && !isMissionClip("J1R011", "J1", 'F'));
    CHECK(planetTheme("ICEWRLD.CAT") == "IceStart" && planetTheme("fedgui.cat").empty());
}

// A string table's bytes: u32 count, then u32 id and UTF-16LE text each.
std::vector<uint8_t> makeTable(const std::vector<std::pair<uint32_t, std::string>>& strings) {
    std::vector<uint8_t> b;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    u32(uint32_t(strings.size()));
    for (const auto& [id, text] : strings) {
        u32(id);
        for (char c : text)
            b.push_back(uint8_t(c)), b.push_back(0);
        b.push_back(0), b.push_back(0);
    }
    return b;
}

void testBriefing() {
    CHECK(clipBefore("K12009", "K12009A") && clipBefore("K12009A", "K12010") && !clipBefore("K12010", "K12009A"));
    StringTable table;
    CHECK(!table.parse(makeTable({{1, "'Test.'"},
                                  {2, "~INCOMING TRANSMISSION~Test"},
                                  {3, "~STELLAR CARTOGRAPHY~Systems"},
                                  {4, "~PLANETARY DATA~A planet"},
                                  {5, "~SPEECH~ "},
                                  {6, "~OBJECTIVE~Do it"},
                                  {7, "~MISSION PASS~Done"}})));
    // No recording of R8: the general rule.
    auto steps = briefingScript("R8", 'R', table, {"R8007", "R8005", "R8006", "R8008"});
    CHECK(steps.size() == 4 && steps[0].clip == "R8005" && steps[0].page == 3 &&
          steps[0].view == BriefingView::Galaxy);
    CHECK(steps[1].page == 4 && steps[1].view == BriefingView::StarSystem);
    CHECK(steps[2].clip == "R8007" && steps[2].page == 6 && steps[2].view == BriefingView::Ground);
    CHECK(steps[3].clip == "R8008" && steps[3].page == 0 && steps[3].view == BriefingView::Emblem);
    CHECK(!hasRecordedBriefing("R8", 'R') && hasRecordedBriefing("R1", 'R') && hasRecordedBriefing("k12", 'K') && hasRecordedBriefing("J1", 'K'));
    // A recorded one: K12's lettered clip as its own step; a joint mission's
    // clips carry the side's letter; J8 opens with a silent step.
    steps = briefingScript("K12", 'K', table, {});
    CHECK(steps.size() == 10 && steps[5].clip == "K12009A" && steps[6].clip == "K12010");
    steps = briefingScript("J8", 'K', table, {});
    CHECK(steps[0].clip.empty() && steps[0].page == 7 && steps[1].clip == "J8K005");
    steps = briefingScript("F1", 'F', table, {});
    CHECK(steps[0].clip.empty() && steps[0].page == 3); // the silent transmission page first
    CHECK(steps[5].clip == "F1009" && steps[5].siteA == 407 && steps[5].siteB == 209); // the colony site
    CHECK(steps[1].siteA == -1 && steps[7].view == BriefingView::Model && steps[7].model == "geo/bad/tt.geo");
    CHECK(missionSides("C10") == std::vector<char>{'K'} && missionSides("C11") == std::vector<char>{'R'});
}

void testMissionScripts() {
    std::vector<MissionScript> s;
    CHECK(!parseMissionScripts("mission T F\nsite base 8 16\nobjective 5\nobjective 6 hidden\nlose 9 destroyed hub\n"
                               "on built-all yard*2 gen|adv\n  say command \"Built at {site:base}.\"\n  complete 5\n"
                               "  reveal 6\n  timer t 10\non timer t\n  complete 6\non all-completed\n  win\n",
                               s));
    CHECK(s.size() == 1 && s[0].sites.at("base") == std::make_pair(8, 16) && s[0].objectives.size() == 2);
    MissionRun run(s[0], nullptr);
    CHECK(run.objectiveState(6) == MissionRun::State::Hidden);
    CHECK(run.fire({"built", {"yard"}}).empty() && run.fire({"built", {"adv"}}).empty());
    auto out = run.fire({"built", {"yard"}}); // the second yard: all built
    CHECK(out.size() == 3 && out[0].text == "Built at <008:016>." && out[0].sender == "Command");
    CHECK(out[1].kind == MissionRun::Output::Completed && out[2].kind == MissionRun::Output::Revealed);
    CHECK(!run.finished() && run.nextTimer() == 10);
    out = run.advance(10);
    CHECK(run.won() && out.back().kind == MissionRun::Output::Won);
    MissionRun lost(s[0], nullptr);
    CHECK(lost.fire({"destroyed", {"hub"}}).back().kind == MissionRun::Output::Lost && lost.finished());
    CHECK(bool(parseMissionScripts("mission T F\non start\n  frobnicate\n", s)));
    CHECK(bool(parseMissionScripts("  say command \"x\"\n", s)));

    // Reluva's scripts all read, and each recorded mission's walkthrough
    // plays through without losing; those whose recordings reached the end
    // win.
    CHECK(missionScripts().size() >= 31);
    const MissionScript* generic = findMissionScript("generic", 'F');
    CHECK(generic != nullptr);
    for (const MissionScript& m : missionScripts()) {
        if (&m == generic)
            continue;
        MissionRun r(m, generic);
        for (const ScriptEvent& e : r.walkthrough())
            r.fire(e);
        while (!r.finished() && r.nextTimer() >= 0)
            r.advance(r.nextTimer());
        const bool endsInWin = std::any_of(m.handlers.begin(), m.handlers.end(), [](const auto& h) {
            return std::any_of(h.actions.begin(), h.actions.end(), [](const auto& a) { return a.verb == "win"; });
        });
        if (r.finished() && !r.won())
            std::fprintf(stderr, "%s (%c) lost in its walkthrough\n", m.mission.c_str(), m.side);
        if (endsInWin && !r.won())
            std::fprintf(stderr, "%s (%c) didn't win its walkthrough\n", m.mission.c_str(), m.side);
        CHECK(!(r.finished() && !r.won()));
        CHECK(!endsInWin || r.won());
    }
    // The generic rules speak for any mission.
    MissionRun f1(*findMissionScript("F1", 'F'), generic);
    out = f1.fire({"mine-exhausted", {}, 404, 220});
    CHECK(out.size() == 1 && out[0].text == "The mine at 404, 220 has run out of minerals.");
    out = f1.fire({"built", {"vehicle-yard"}});
    CHECK(!out.empty() && out[0].text == "Vehicle yard finished.");
    // The game's spoken announcements.
    CHECK(announcementFor({"built", {"construction-yard"}}, {}, 'F') == "B_fcycom");
    CHECK(announcementFor({"built", {"accommodation"}}, {}, 'K') == "A_kacom");
    CHECK(announcementFor({"built", {"sickbay"}}, {}, 'R').empty());
    CHECK(announcementFor({"power-low", {}}, {}, 'K') == "klowpowr");
    CHECK(announcementFor({"short-crew", {}}, {}, 'F') == "FED002c" && announcementFor({"short-crew", {}}, {}, 'K').empty());
    CHECK(announcementFor({"built", {"vehicle-yard"}}, {{MissionRun::Output::Completed, {}, {}, 16}}, 'R') == "robjmet");
    CHECK(announcementFor({"x", {}}, {{MissionRun::Output::Completed, {}, {}, 16}, {MissionRun::Output::Won, {}, {}, 0}},
                          'F') == "fmiswin");
}

void testColony() {
    CHECK(colonySite("F1", 'F').a == 407 && colonySite("F1", 'F').b == 209);
    CHECK(colonySite("J2", 'K').a == 92 && colonySite("J2", 'F').a == 220 && colonySite("J1", 'R').b == 442);
    CHECK(colonySite("F8", 'F').a < 0);
    bool matched = false;
    CHECK(buildingModel("vehicle-yard", 'F', &matched) == "geo/fed/fvpy_a.geo" && matched);
    CHECK(buildingModel("power-generator", 'F', &matched) == "geo/fed/fpgs.geo" && !matched);
    CHECK(buildingModel("colony-hub", 'K') == "geo/kln/kch.geo");
    CHECK(buildingModel("accommodation", 'K', &matched) == "geo/kln/kab.geo" && matched); // the Arsenal
    CHECK(buildingModel("sickbay", 'R') == "geo/rom/ruh_a.geo");
    CHECK(buildingModel("transporter-pad", 'R', &matched) == "geo/rom/rbcs.geo" && matched);
    CHECK(buildingModel("transporter-pad", 'F').empty() && buildingModel("disruptor-tank", 'K').empty());

    // A flat 8 x 8 block map, a deposit in block (6, 4), a cliff along
    // block row 2's west half, nothing beyond column 7; buildings one tile
    // square, the accommodation block two.
    ColonyGround ground{8, 8, [](int bx, int by) { return bx == 6 && by == 4 ? 1 : 0; },
                        [](int a, int b) {
                            if (b >= 64)
                                return std::nanf("");
                            return a >= 16 && a < 24 && b < 32 ? 5000.0f : 0.0f;
                        },
                        [](const std::string& model) {
                            return model.find("accom") != std::string::npos ? std::pair{12000.0f, 12000.0f}
                                                                            : std::pair{6000.0f, 6000.0f};
                        }};
    ColonyLayout colony('F', ground);
    CHECK(colony.start(36, 36));
    CHECK(colony.buildings().size() == 1 && colony.buildings()[0].model == "geo/fed/fch.geo");
    // The pad: round the hub, off the cliff and the deposit, on the map.
    const ColonyBuilding start = colony.buildings()[0];
    CHECK(!colony.pad().empty());
    bool underHub = false;
    for (const auto& [tx, ty] : colony.pad()) {
        underHub = underHub || (tx == start.b / 4 && ty == start.a / 4) || (tx == (start.b - 1) / 4 && ty == (start.a - 1) / 4);
        CHECK(tx < 16 && !(ty >= 4 && ty < 6 && tx < 8) && !(tx / 2 == 6 && ty / 2 == 4));
        CHECK(std::abs(tx * 4 + 2 - start.b) <= 16 && std::abs(ty * 4 + 2 - start.a) <= 16);
    }
    CHECK(underHub);
    for (const char* b : {"construction-yard", "vehicle-yard", "power-generator", "accommodation", "mining-station",
                          "colony-hub-upgrade", "resource-processor", "sickbay"})
        CHECK(colony.add(b));
    CHECK(!colony.add("colony-hub-upgrade")); // once
    CHECK(!colony.add("disruptor-tank"));     // a unit
    CHECK(!colony.add("science-station-upgrade"));
    const auto& all = colony.buildings();
    CHECK(all.size() == 9);
    const ColonyBuilding& hub = all[0];
    CHECK(std::abs(hub.a - 36) <= 24 && std::abs(hub.b - 36) <= 24);
    for (const ColonyBuilding& c : all) {
        CHECK(c.b < 64 && !(c.a >= 14 && c.a < 26 && c.b < 34)); // on the map, off the cliff
        if (c.name == "colony-hub-upgrade")
            CHECK(c.a == hub.a && c.b == hub.b);
        else if (c.name == "mining-station")
            CHECK(c.b / 8 == 6 && c.a / 8 == 4); // on the deposit
        else
            CHECK(!(c.b / 8 == 6 && c.a / 8 == 4));
        CHECK(std::abs(c.a - hub.a) <= 32 && std::abs(c.b - hub.b) <= 32); // close by
    }
    // None overlap (one tile each, two for accommodation).
    auto half = [](const ColonyBuilding& c) { return c.name == "accommodation" ? 4 : 2; };
    for (size_t i = 0; i < all.size(); ++i)
        for (size_t j = i + 1; j < all.size(); ++j)
            if (all[j].name != "colony-hub-upgrade")
                CHECK(std::abs(all[i].a - all[j].a) >= half(all[i]) + half(all[j]) ||
                      std::abs(all[i].b - all[j].b) >= half(all[i]) + half(all[j]));
    // No ground at all: no colony.
    ColonyGround none = ground;
    none.heightAt = [](int, int) { return std::nanf(""); };
    ColonyLayout nowhere('K', none);
    CHECK(!nowhere.start(36, 36) && !nowhere.add("vehicle-yard"));
}

void testStarSystem() {
    // The system a table names most, by letters only.
    StringTable table;
    CHECK(!table.parse(makeTable({{1, "~STELLAR CARTOGRAPHY~Near Alpha and the F'Thorc system"},
                                  {2, "~PLANETARY DATA~F'thorc III, past ALPHA's belt; fthorc"}})));
    CHECK(starSystemName(table, {"alpha", "fthorc", "taal"}) == "fthorc");
    CHECK(starSystemName(table, {"taal"}).empty());
    CHECK(planetPicture("icewrld.cat") == "ice" && planetPicture("ALNWRLD.CAT") == "alien4" && planetPicture("x").empty());

    // A model: a starfield wall (texture 2), a star at the middle (two
    // slices numbered 0), a planet (two slices in 1, their backs in 0) and
    // a ringed planet (a slice in 3, the ring in 4, backs in 0).
    GeoModel m;
    m.parts.resize(1);
    auto quad = [&](float cx, float cy, float z, float r, uint32_t slot, bool back) {
        auto& v = m.parts[0].vertices;
        const uint32_t base = uint32_t(v.size());
        for (const auto& [dx, dy] : {std::pair{-r, -r}, std::pair{r, -r}, std::pair{r, r}, std::pair{-r, r}})
            v.push_back({cx + dx, cy + dy, z});
        auto face = [&](std::initializer_list<uint32_t> corners, uint32_t number) {
            GeoModel::Face f;
            for (const uint32_t c : corners)
                f.corners.push_back({base + c, 0, 0, {0, 0, 0, 0, 0}});
            f.corners.back().extra[4] = number;
            m.faces.push_back(f);
        };
        if (back)
            face({1, 0, 2}, 0), face({2, 0, 3}, 0);
        face({0, 1, 2, 3}, slot);
    };
    quad(0, 0, 0, 4000, 2, false);              // the wall
    quad(0, 0, 30, 70, 0, false), quad(0, 0, -30, 70, 0, false); // the star
    quad(2000, 0, 50, 80, 1, true), quad(2000, 0, -50, 80, 1, true);
    quad(-2000, 0, 0, 140, 4, true), quad(-2000, 0, 40, 90, 3, true), quad(-2000, 0, -40, 90, 3, true);
    const StarSystemLayout layout = readStarSystem(m);
    CHECK(layout.backdropSlots == std::vector<int>{2});
    CHECK(layout.bodies.size() == 3);
    int stars = 0, ringed = 0;
    for (const auto& b : layout.bodies) {
        stars += b.star;
        if (b.ringSlot >= 0) {
            CHECK(b.ringSlot == 4 && b.slot == 3 && std::fabs(b.centre.x + 2000) < 1);
            ++ringed;
        }
    }
    CHECK(stars == 1 && ringed == 1);
    // Backs take their slice's number; the ring's back the ring's.
    CHECK(layout.faceSlot[0] == 2 && layout.faceSlot[3] == 1 && layout.faceSlot[4] == 1);
    CHECK(layout.faceSlot[m.faces.size() - 9] == 4 && layout.faceSlot[m.faces.size() - 8] == 4);
    CHECK(layout.faceBody[0] == -1 && layout.faceBody[1] >= 0);
    CHECK(starSystemTextures(layout, "ice", "bezouk")[0] == "sun3");
    const auto tex = starSystemTextures(layout, "ice");
    CHECK(tex.size() == 5 && tex[0] == "sun" && tex[1] == "ice" && tex[2] == "stars1");
    CHECK(tex[3] == "red" && tex[4] == "red_r");
    const int look = missionBody(layout);
    CHECK(look >= 0 && layout.bodies[size_t(look)].slot == 1);
}

void testModelPose() {
    // A made-up tank: hull (node 0), turret (node 1) and gun (node 2), the
    // turret's axis at (0, 0, 10) and the gun's elevation point at
    // (0, -5, 12); the gun along -y.
    GeoModel g;
    g.parts.resize(3);
    for (uint32_t i = 0; i < 3; ++i)
        g.parts[i].node = i;
    g.attachments.resize(2);
    g.attachments[0].position = {0, 0, 10};
    g.attachments[1].position = {0, -5, 12};
    MdlModel m;
    m.parts = {{"main", 0}, {"turret", 1}, {"gun", 2}, {"muzzle_flash", 2}};
    m.points = {{"turret_axis", 0}, {"gun_elevate", 1}};
    CHECK(hasTurret(m) && !hasTurret(MdlModel{}));
    auto near = [](GeoModel::Vec3 a, float x, float y, float z) {
        return std::fabs(a.x - x) < 1e-3f && std::fabs(a.y - y) < 1e-3f && std::fabs(a.z - z) < 1e-3f;
    };
    const float q = 3.14159265f / 2;
    Pose p = turretPose(g, m, q, 0);
    CHECK(!p[0].moved && near(p[1].apply({1, 0, 10}), 0, 1, 10));
    CHECK(near(p[2].apply({0, -15, 12}), 15, 0, 12)); // the gun turns with the turret
    p = turretPose(g, m, 0, q);
    CHECK(!p[1].moved || near(p[1].apply({1, 0, 10}), 1, 0, 10));
    CHECK(near(p[2].apply({0, -15, 12}), 0, -5, 22)); // raised straight up
    // An animation frame: the arm, stored at the origin, turned a quarter
    // turn and moved to (1, 0, 0).
    m.parts = {{"main", 0}, {"arm", 1}};
    m.animated = {{"arm", 0}};
    m.reference.resize(1);
    std::vector<PartTransform> frame(1);
    const float r[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    std::copy(r, r + 9, frame[0].rotation);
    frame[0].position = {1, 0, 0};
    g.parts.resize(2);
    p = framePose(g, m, frame);
    CHECK(!p[0].moved && p[1].moved && near(p[1].apply({1, 0, 0}), 1, 1, 0));
}

void testEconomy() {
    auto count = [](const std::vector<ScriptEvent>& ev, const char* kind) {
        return std::count_if(ev.begin(), ev.end(), [&](const ScriptEvent& e) { return e.kind == kind; });
    };
    // Dilithium (kind 1) under tile 4,6's block, 50 units of it.
    ColonyMap map;
    map.depositKind = [](int tx, int ty) { return tx / 2 == 2 && ty / 2 == 3 ? 1 : 0; };
    map.depositAmount = [](int, int) { return 50; };
    Economy e('F', {500, 988, 2211, 497, 3500, 5000}, 20, 50, map);
    e.addBuilt("colony-hub", 0, 0);
    const BuildingType* yard = buildingType("construction-yard");
    const BuildingType* mine = buildingType("mining-station");
    CHECK(yard && mine && e.available(*yard) && !e.available(*mine) && !e.available(*buildingType("colony-hub")));
    CHECK(e.order("mining-station", 0, 0, 4, 6) == 0); // no processor yet
    const int id = e.order("construction-yard", 1000, 1000, 0, 0);
    CHECK(id != 0 && e.stock()[Thermoconc] == 2211);
    auto ev = e.advance(1);
    CHECK(e.stock()[Thermoconc] == 2211 - 89 && e.stock()[Nitrium] == 3450 && count(ev, "built") == 0);
    for (int i = 0; i < 40 && count(ev, "built") == 0; ++i)
        ev = e.advance(1);
    CHECK(count(ev, "built") == 1 && ev[0].args.size() == 1 && ev[0].args[0] == "construction-yard");
    CHECK(e.capacity() == 55 && e.stock()[Thermoconc] == 2211 - 89); // paid once
    // Six more yards: demand 70 over the hub's 60.
    for (int i = 0; i < 6; ++i)
        e.addBuilt("vehicle-yard", 0, 0);
    CHECK(e.demandMet() < 1);
    ev = e.advance(1);
    CHECK(count(ev, "power-low") == 1 && count(e.advance(1), "power-low") == 0); // not again at once
    e.addBuilt("power-generator", 0, 0);
    CHECK(e.demandMet() == 1);
    // An order it can't pay for waits and says so.
    Economy poor('F', {}, 20, 50, map);
    poor.addBuilt("colony-hub", 0, 0);
    poor.addBuilt("construction-yard", 0, 0);
    CHECK(poor.order("sickbay", 0, 0, 0, 0) != 0);
    ev = poor.advance(1);
    CHECK(count(ev, "short-resources") == 1 && poor.structures().back().progress == 0);
    // A mine on the deposit digs it dry; the processor refines its ore.
    e.addBuilt("resource-processor", 0, 0);
    CHECK(e.order("mining-station", 0, 0, 0, 0) == 0); // no deposit there
    CHECK(e.order("mining-station", 5 * kTile, 7 * kTile, 5, 7) != 0);
    const int dilithium = e.stock()[Dilithium];
    bool dry = false;
    int at = -1, bt = -1;
    for (int i = 0; i < 2000 && !dry; ++i)
        for (const ScriptEvent& x : e.advance(1))
            if (x.kind == "mine-exhausted")
                dry = true, at = x.a, bt = x.b;
    CHECK(dry && at == 3 * 8 + 4 && bt == 2 * 8 + 4);
    for (int i = 0; i < 200; ++i)
        e.advance(1);
    CHECK(e.stock()[Dilithium] >= dilithium + 199 && e.stock()[Dilithium] <= dilithium + 200 && e.oreStock()[0] < 1);
    // The crew grows, one every 30 s, up to the capacity.
    const int crew = e.crew();
    e.advance(30);
    CHECK(crew > 20 && e.crew() == crew + 1);
    e.advance(3600);
    CHECK(e.crew() == e.capacity());
}

void testWorld() {
    // A 10 x 10 tile plain with a wall of closed tiles down column 5 (open
    // only at row 9), and a cliff (a 3000-unit step) between columns 1 and 2
    // in rows 0-2.
    Terrain t;
    t.width = t.height = 10;
    t.moveClass.assign(100, 1);
    for (int y = 0; y < 9; ++y)
        t.moveClass[size_t(y) * 10 + 5] = 0;
    t.heightAt = [](float x, float y) { return x >= 2 * kTile && y < 3 * kTile ? 3000.0f : 0.0f; };
    World w(t);
    CHECK(!w.passable(5, 0) && w.passable(5, 9) && !w.passable(-1, 0) && !w.passable(10, 0));
    CHECK(!w.canStep(1, 0, 1, 0) && w.canStep(1, 5, 1, 0) && !w.canStep(4, 0, 1, 0));
    CHECK(!w.canStep(4, 9, 1, -1)); // a diagonal past the wall's end
    // Round the wall, through its gap at the top.
    const auto path = w.findPath(3.5f * kTile, 4.5f * kTile, 7.5f * kTile, 4.5f * kTile);
    CHECK(!path.empty() && std::fabs(path.back().first - 7.5f * kTile) < 1 && std::fabs(path.back().second - 4.5f * kTile) < 1);
    bool viaGap = false;
    for (const auto& [x, y] : path)
        viaGap = viaGap || int(y / kTile) == 9;
    CHECK(viaGap);
    // A closed goal moves to an open tile beside it.
    const auto toWall = w.findPath(3.5f * kTile, 4.5f * kTile, 5.5f * kTile, 4.5f * kTile);
    CHECK(!toWall.empty() && int(toWall.back().first / kTile) != 5);

    // Units: selection, orders, groups, arrival.
    const int a = w.addUnit('F', groundScout('F'), 1.5f * kTile, 5.5f * kTile);
    const int b = w.addUnit('F', groundScout('F'), 1.5f * kTile, 6.5f * kTile);
    const int c = w.addUnit('K', groundScout('K'), 8.5f * kTile, 8.5f * kTile);
    CHECK(groundScout('R').model == "geo/rom/rfgs.geo");
    w.select({a, b});
    CHECK(w.selection() == (std::vector<int>{a, b}));
    w.setGroup(1);
    w.clearSelection();
    CHECK(w.selection().empty() && w.selectGroup(1) && w.selection().size() == 2 && !w.selectGroup(2));
    w.moveSelected(3.5f * kTile, 5.5f * kTile);
    CHECK(w.unit(a)->moving() && w.unit(b)->moving() && !w.unit(c)->moving());
    for (int i = 0; i < 200 && w.advance(0.1); ++i) {
    }
    const Unit& ua = *w.unit(a);
    const Unit& ub = *w.unit(b);
    CHECK(!ua.moving() && !ub.moving());
    CHECK(std::hypot(ua.x - 3.5f * kTile, ua.y - 5.5f * kTile) < 2000 && std::hypot(ua.x - ub.x, ua.y - ub.y) > 500);
    float gx = 0, gy = 0;
    CHECK(w.groupCentre(1, gx, gy) && std::fabs(gx - 3.5f * kTile) < 1500 && !w.groupCentre(3, gx, gy));
}

void testModelTextures() {
    std::vector<TextureFile> tex;
    for (const char* name : {"k_bop_a", "k_bop_b", "k_bop_g", "kcs", "kcs_g", "kcs_l", "k_bop2_a", "kwb"})
        tex.push_back({name, "kln", {0, 0}});
    tex.push_back({"kwb", "fed", {0, 0}}); // same name elsewhere: the model's own folder wins
    for (const char* name : {"fapc", "fapc_g", "fapc_l", "fconb", "fconb_a", "fhf_g", "fpdt", "fpdt_s", "fvpy", "fvpy",
                             "landpad"})
        tex.push_back({name, "fed", {0, 0}});
    auto names = [&](const char* model, size_t needed) {
        std::string out;
        for (const TextureFile* t : findModelTextures(model, tex, needed))
            out += (out.empty() ? "" : " ") + (t ? t->stem + (t->folder == "kln" ? "" : "@" + t->folder) : "?");
        return out;
    };
    CHECK(names("geo/kln/k_bop.geo", 3) == "k_bop_a k_bop_b k_bop_g"); // not k_bop2_a
    CHECK(names("geo/kln/k_bop_l.geo", 3) == "k_bop_a k_bop_b k_bop_g"); // falls back without _l
    CHECK(names("geo/kln/kcs_l.geo", 1) == "kcs_l");                     // its own texture first
    CHECK(names("geo/kln/kcs_s.geo", 1) == "?");                          // outlines don't fall back
    CHECK(names("geo/fed/kwb.geo", 1) == "kwb@fed");
    CHECK(names("geo/fed/fhf.geo", 3) == "fhf_g@fed ? ?");       // own textures from 0, _g included
    CHECK(names("geo/fed/fpdt.geo", 2) == "fpdt@fed ?");          // fpdt_s is the outline's
    CHECK(names("geo/fed/fvpy_a.geo", 1) == "fvpy@fed");          // duplicates count once
    CHECK(names("geo/fed/landpd_d.geo", 2) == "landpad@fed ?");   // a shortened name
    CHECK(names("geo/fed/fapc2.geo", 3) == "fapc@fed fapc_g@fed fapc_l@fed"); // a number apart
    CHECK(names("geo/fed/fapc2_l.geo", 1) == "fapc_l@fed");                   // its low-detail texture
    CHECK(names("geo/fed/fconbee.geo", 2) == "fconb@fed fconb_a@fed");        // the texture's name shortened
    // A door: one upright 1000-unit face showing the whole texture, number 1;
    // a roof face (number 2) isn't.
    GeoModel g;
    g.parts.resize(1);
    g.parts[0].vertices = {{0, 0, 0}, {1000, 0, 0}, {1000, 0, 1000}, {0, 0, 1000},
                           {0, 0, 1000}, {1000, 0, 1000}, {1000, 1000, 1000}, {0, 1000, 1000}};
    auto face = [&](uint32_t first, uint32_t slot) {
        GeoModel::Face f;
        for (uint32_t i = 0; i < 4; ++i)
            f.corners.push_back({first + i, float(i == 1 || i == 2), float(i >= 2), {0, 0, 0, 0, slot}});
        g.faces.push_back(f);
    };
    face(4, 0);
    face(0, 1);
    face(4, 2);
    CHECK(isDoorSlot(g, 1) && !isDoorSlot(g, 2) && !isDoorSlot(g, 3));
    std::vector<TextureFile> doors = {{"door3", "fed", {0, 0}}, {"kcdoor", "kln", {0, 0}}};
    std::vector<const TextureFile*> slots(3, nullptr);
    addDoorTextures(g, "geo/fed/fch.geo", doors, slots);
    CHECK(!slots[0] && slots[1] == &doors[0] && !slots[2]);
    doors.push_back({"fch_door", "fed", {0, 0}}); // a model's own door wins
    addDoorTextures(g, "geo/fed/fch.geo", doors, slots);
    CHECK(slots[1] == &doors[2]);
}

void testRasterizer() {
    Surface surf;
    surf.create(16, 16, PixelFormat::RGBA32);
    Rasterizer r;
    r.begin(surf);
    // Two triangles making the square 2..12; every pixel inside is drawn once.
    std::vector<int> hits(16 * 16, 0);
    auto count = [&](float, float, uint8_t* px) {
        ++hits[size_t(px - surf.pixels.data()) / 4];
        return true;
    };
    const ScreenVertex a{2, 2, 1, 0, 0}, b{12, 2, 1, 1, 0}, c{12, 12, 1, 1, 1}, d{2, 12, 1, 0, 1};
    r.drawTriangle(a, b, c, count);
    r.begin(surf); // fresh depth so the second triangle isn't hidden by equal depth
    r.drawTriangle(a, c, d, count);
    bool exact = true;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            exact &= hits[size_t(y * 16 + x)] == ((x >= 2 && x < 12 && y >= 2 && y < 12) ? 1 : 0);
    CHECK(exact);

    // The nearer triangle wins whichever is drawn first; u/v interpolate.
    for (int order = 0; order < 2; ++order) {
        r.begin(surf);
        const ScreenVertex n0{0, 0, 1.0f, 0, 0}, n1{16, 0, 1.0f, 1, 0}, n2{0, 16, 1.0f, 0, 1};
        const ScreenVertex f0{0, 0, 0.5f, 0, 0}, f1{16, 0, 0.5f, 0, 0}, f2{0, 16, 0.5f, 0, 0};
        auto paint = [](uint8_t value) {
            return [value](float, float, uint8_t* px) {
                px[0] = value;
                return true;
            };
        };
        if (order == 0) {
            r.drawTriangle(n0, n1, n2, paint(1));
            r.drawTriangle(f0, f1, f2, paint(2));
        } else {
            r.drawTriangle(f0, f1, f2, paint(2));
            r.drawTriangle(n0, n1, n2, paint(1));
        }
        CHECK(surf.row(2)[2 * 4] == 1);
    }
    float seenU = -1;
    r.begin(surf);
    r.drawTriangle({0, 0, 1, 0, 0}, {16, 0, 1, 1, 0}, {0, 16, 1, 0, 0},
                   [&](float u, float, uint8_t* px) {
                       if (px == surf.row(0) + 8 * 4)
                           seenU = u;
                       return true;
                   });
    CHECK(std::fabs(seenU - 8.5f / 16) < 1e-4f);
}

} // namespace

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_cat_tests";
    testNormalizeName();
    testRead(tmp);
    testRejectsCorruption(tmp);
    testStringTable();
    testTexture();
    testGeo();
    testGmf();
    testMdl();
    testArf();
    testFif();
    testLevels();
    testBriefing();
    testMissionScripts();
    testColony();
    testStarSystem();
    testModelPose();
    testEconomy();
    testWorld();
    testModelTextures();
    testRasterizer();
    std::filesystem::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
