// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Unreal Engine 1 engine's readers, on packages built here.

#include "common/audio/decode.h"
#include "common/audio/wav.h"
#include "data/tone440_mp3.h"
#include "engines/registry.h"
#include "engines/unreal/actors.h"
#include "engines/unreal/class_defaults.h"
#include "engines/unreal/games.h"
#include "engines/unreal/mesh.h"
#include "engines/unreal/lighting.h"
#include "engines/unreal/procedural.h"
#include "engines/unreal/model.h"
#include "engines/unreal/package.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/properties.h"
#include "engines/unreal/sound.h"
#include "engines/unreal/texture.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::unreal;

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

// Little-endian output, with the engine's compact integers.
struct Bytes {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back(uint8_t(v)); }
    void u16(int v) { u8(v & 0xff), u8((v >> 8) & 0xff); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            u8(int((v >> (8 * i)) & 0xff));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void index(int32_t v) {
        uint32_t a = uint32_t(v < 0 ? -v : v);
        uint8_t first = uint8_t((v < 0 ? 0x80 : 0) | (a & 0x3f));
        a >>= 6;
        if (a)
            first |= 0x40;
        u8(first);
        while (a) {
            uint8_t next = uint8_t(a & 0x7f);
            a >>= 7;
            if (a)
                next |= 0x80;
            u8(next);
        }
    }
    void str(const std::string& s, int version) {
        if (version >= 64)
            index(int32_t(s.size() + 1));
        b.insert(b.end(), s.begin(), s.end());
        u8(0);
    }
    void bytes(const std::vector<uint8_t>& d) { b.insert(b.end(), d.begin(), d.end()); }
    void vec(float x, float y, float z) { f32(x), f32(y), f32(z); }
};

// Builds a package: names are added as used, exports' data written by the
// test with the names' indices.
struct PackageWriter {
    int version = 69;
    std::vector<std::string> names{"None"};
    struct Imp {
        int32_t classPackage, className, package, objectName;
    };
    struct Exp {
        int32_t classRef, superRef, package, objectName;
        uint32_t flags;
        std::vector<uint8_t> data;
    };
    std::vector<Imp> imports;
    std::vector<Exp> exports;

    int32_t name(const std::string& n) {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == n)
                return int32_t(i);
        names.push_back(n);
        return int32_t(names.size() - 1);
    }
    // Returns the object reference.
    int32_t addImport(const std::string& pkg, const std::string& cls, int32_t outer, const std::string& obj) {
        imports.push_back({name(pkg), name(cls), outer, name(obj)});
        return -int32_t(imports.size());
    }
    int32_t addExport(int32_t cls, int32_t outer, const std::string& obj, std::vector<uint8_t> data,
                      uint32_t flags = 0) {
        exports.push_back({cls, 0, outer, name(obj), flags, std::move(data)});
        return int32_t(exports.size());
    }
    std::vector<uint8_t> build() const {
        Bytes body;
        // Header written last, at a fixed size; the data follows it.
        const size_t headerSize = 36 + (version >= 68 ? 16 + 4 + 8 : 8);
        std::vector<int32_t> offsets;
        for (const Exp& e : exports) {
            offsets.push_back(int32_t(headerSize + body.b.size()));
            body.bytes(e.data);
        }
        const auto nameOffset = uint32_t(headerSize + body.b.size());
        for (const std::string& n : names) {
            body.str(n, version);
            body.u32(0x70010);
        }
        const auto importOffset = uint32_t(headerSize + body.b.size());
        for (const Imp& i : imports) {
            body.index(i.classPackage);
            body.index(i.className);
            body.i32(i.package);
            body.index(i.objectName);
        }
        const auto exportOffset = uint32_t(headerSize + body.b.size());
        for (size_t k = 0; k < exports.size(); ++k) {
            const Exp& e = exports[k];
            body.index(e.classRef);
            body.index(e.superRef);
            body.i32(e.package);
            body.index(e.objectName);
            body.u32(e.flags);
            body.index(int32_t(e.data.size()));
            if (!e.data.empty())
                body.index(offsets[k]);
        }
        Bytes h;
        h.u32(kPackageTag);
        h.u16(version);
        h.u16(0);
        h.u32(1);
        h.u32(uint32_t(names.size()));
        h.u32(nameOffset);
        h.u32(uint32_t(exports.size()));
        h.u32(exportOffset);
        h.u32(uint32_t(imports.size()));
        h.u32(importOffset);
        if (version >= 68) {
            for (int i = 0; i < 16; ++i)
                h.u8(i);
            h.u32(1);
            h.u32(uint32_t(exports.size()));
            h.u32(uint32_t(names.size()));
        } else {
            h.u32(0);
            h.u32(0);
        }
        h.bytes(body.b);
        return h.b;
    }
};

// A property tag for a value of `size` bytes (sizes the short codes cover).
void tag(Bytes& o, PackageWriter& w, const std::string& name, PropertyType type, size_t size, int arrayIndex = 0,
         const std::string& structName = {}) {
    o.index(w.name(name));
    int code = size <= 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 12 ? 3 : size == 16 ? 4 : 5;
    o.u8(int(type) | code << 4 | (arrayIndex ? 0x80 : 0));
    if (type == PropertyType::Struct)
        o.index(w.name(structName));
    if (code == 5)
        o.u8(int(size));
    if (arrayIndex)
        o.u8(arrayIndex);
}

void endProps(Bytes& o, PackageWriter& w) { o.index(w.name("None")); }

} // namespace

static void testCompactIndex() {
    for (int32_t v : {0, 1, -1, 63, 64, -64, 127, 8191, 8192, 1000000, -1000000, 0x7ffffff}) {
        Bytes b;
        b.index(v);
        Reader r(b.b);
        CHECK(r.index() == v && r.ok() && r.remaining() == 0);
    }
    const std::vector<uint8_t> truncated{0x40};
    Reader shortRead(truncated);
    shortRead.index();
    CHECK(!shortRead.ok());
}

static void testPackageTables() {
    for (int version : {61, 69}) {
        PackageWriter w;
        w.version = version;
        const int32_t core = w.addImport("Core", "Package", 0, "Engine");
        const int32_t texClass = w.addImport("Core", "Class", core, "Texture");
        const int32_t group = w.addExport(0, 0, "Walls", {}); // a class stands in for a group
        Bytes d;
        endProps(d, w);
        w.addExport(texClass, group, "Brick", d.b);
        Package p;
        CHECK(!p.openMemory(w.build(), "Test"));
        CHECK(p.version() == version && p.exports().size() == 2 && p.imports().size() == 2);
        CHECK(p.path(2) == "Walls.Brick" && p.className(2) == "Texture" && p.className(1) == "Class");
        CHECK(p.importPackage(1) == "Engine" && p.path(texClass) == "Engine.Texture");
        CHECK(p.findExport("brick", "TEXTURE") == 1 && p.findExport("brick", "Sound") == -1);
        CHECK(p.exportsOfClass("texture") == std::vector<int>{1});
    }
    Package bad;
    CHECK(bad.openMemory({1, 2, 3, 4}, "x"));
    std::vector<uint8_t> cut = PackageWriter{}.build();
    cut.resize(cut.size() - 3);
    CHECK(bad.openMemory(cut, "x"));
}

static void testProperties() {
    PackageWriter w;
    Bytes o;
    tag(o, w, "Count", PropertyType::Int, 4);
    o.i32(-7);
    tag(o, w, "Scale", PropertyType::Float, 4);
    o.f32(1.5f);
    tag(o, w, "bOn", PropertyType::Bool, 0);
    o.b.back() |= 0x80; // the value, in the info byte
    tag(o, w, "Kind", PropertyType::Byte, 1);
    o.u8(3);
    tag(o, w, "Tag", PropertyType::Name, 1);
    o.index(w.name("Door"));
    tag(o, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    o.vec(1, 2, 3);
    tag(o, w, "Rotation", PropertyType::Struct, 12, 0, "Rotator");
    o.i32(0), o.i32(16384), o.i32(0);
    tag(o, w, "Slots", PropertyType::Int, 4, 2);
    o.i32(42);
    Bytes s;
    s.str("hello", 69);
    tag(o, w, "Message", PropertyType::Str, s.b.size());
    o.bytes(s.b);
    endProps(o, w);
    o.u8(0xAB); // the class's own data
    // With a state frame first.
    Bytes withStack;
    withStack.index(5), withStack.index(5), withStack.u64(0), withStack.i32(0), withStack.index(12);
    withStack.bytes(o.b);
    w.addExport(0, 0, "Plain", o.b);
    const int32_t cls = w.addImport("Engine", "Class", 0, "Actor");
    w.exports[0].classRef = cls;
    w.addExport(cls, 0, "Stacked", withStack.b, objflag::HasStack);
    Package p;
    CHECK(!p.openMemory(w.build(), "Props"));
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> data;
        CHECK(!p.readExport(i, data));
        Reader r(data);
        PropertyList props;
        CHECK(!beginObject(p, i, r, props));
        CHECK(props.items.size() == 9);
        CHECK(props.integer("count") == -7 && props.number("Scale") == 1.5f && props.boolean("bOn"));
        CHECK(props.byte("Kind") == 3 && p.name(props.nameIndex("Tag")) == "Door");
        std::array<float, 3> v{};
        CHECK(props.vector("Location", v) && v[0] == 1 && v[1] == 2 && v[2] == 3);
        Rotator rot;
        CHECK(props.rotator("Rotation", rot) && rot.yaw == 16384);
        CHECK(props.integer("Slots", 0, 2) == 42 && props.integer("Slots", 9, 0) == 9);
        CHECK(props.string("Message", 69) == "hello");
        CHECK(r.u8() == 0xAB && r.ok());
    }
}

// A package with a 4x2 P8 texture (masked) and its palette.
static std::vector<uint8_t> texturePackage() {
    PackageWriter w;
    const int32_t engine = w.addImport("Core", "Package", 0, "Engine");
    const int32_t texClass = w.addImport("Core", "Class", engine, "Texture");
    const int32_t palClass = w.addImport("Core", "Class", engine, "Palette");
    Bytes pal;
    endProps(pal, w);
    pal.index(256);
    for (int i = 0; i < 256; ++i)
        pal.u8(i), pal.u8(255 - i), pal.u8(7), pal.u8(0);
    const int32_t palRef = w.addExport(palClass, 0, "BrickPal", pal.b);
    Bytes tex;
    tag(tex, w, "Palette", PropertyType::Object, 1);
    tex.index(palRef);
    tag(tex, w, "USize", PropertyType::Int, 4);
    tex.i32(4);
    tag(tex, w, "VSize", PropertyType::Int, 4);
    tex.i32(2);
    tag(tex, w, "bMasked", PropertyType::Bool, 0);
    tex.b.back() |= 0x80;
    endProps(tex, w);
    tex.index(1); // one mipmap
    tex.i32(0); // where its data ends (the reader doesn't need it)
    tex.index(8);
    for (int i = 0; i < 8; ++i)
        tex.u8(i);
    tex.i32(4), tex.i32(2), tex.u8(2), tex.u8(1);
    w.addExport(texClass, 0, "Brick", tex.b);
    return w.build();
}

static void testTextures() {
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    const Package* p = set.get("walls");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, p->findExport("Brick"), img, &info));
    CHECK(img.w == 4 && img.h == 2 && info.masked && info.format == 0 && info.mipCount == 1);
    CHECK(img.pixel(0, 0)[3] == 0);                                           // colour 0 is see-through
    CHECK(img.pixel(1, 0)[0] == 1 && img.pixel(1, 0)[1] == 254 && img.pixel(1, 0)[3] == 255);
    CHECK(img.pixel(3, 1)[0] == 7 && img.pixel(3, 1)[2] == 7);

    // A map importing it: resolved through the set, by group-less path.
    PackageWriter map;
    const int32_t walls = map.addImport("Core", "Package", 0, "Walls");
    const int32_t ref = map.addImport("Engine", "Texture", walls, "Brick");
    map.addImport("Engine", "Texture", walls, "Missing");
    set.addMemory("Map", ".unr", map.build());
    const Package* m = set.get("Map");
    CHECK(m && m->exports().empty());
    if (!m)
        return;
    const ObjectRef found = set.resolve(*m, ref);
    CHECK(found && found.package == p && found.exportIndex == p->findExport("Brick"));
    CHECK(!set.resolve(*m, -3));
    CHECK(!set.resolve(*m, 0) && !set.resolve(*m, 5));
    CHECK(set.get("Nothing") == nullptr && set.names(".utx") == std::vector<std::string>{"Walls"});
}

static void testSounds() {
    audio::Sound tone;
    tone.rate = 11025;
    tone.channels = 1;
    for (int i = 0; i < 100; ++i)
        tone.samples.push_back(int16_t(i * 100));
    const std::vector<uint8_t> wav = audio::encodePcmWav(tone);
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Sound");
    Bytes o;
    endProps(o, w);
    o.index(w.name("WAV"));
    o.i32(0);
    o.index(int32_t(wav.size()));
    o.bytes(wav);
    w.addExport(cls, 0, "Beep", o.b);
    Package p;
    CHECK(!p.openMemory(w.build(), "Sfx"));
    EmbeddedFile f;
    CHECK(!readEmbeddedFile(p, 0, f));
    CHECK(f.format == "WAV" && f.data == wav);
    audio::Sound back;
    CHECK(!audio::decodeSound(f.data, back) && back.samples == tone.samples);
}

static void testModel() {
    // One square floor polygon, 128 units across, at z = 0.
    PackageWriter w;
    const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
    const int32_t modelClass = w.addImport("Engine", "Class", 0, "Model");
    const int32_t tex = w.addExport(texClass, 0, "Floor", {});
    // A light 64 units above the floor's corner, and the zone's ambient light.
    const int32_t lightClass = w.addImport("Engine", "Class", 0, "Light");
    const int32_t zoneClass = w.addImport("Engine", "Class", 0, "ZoneInfo");
    Bytes la;
    tag(la, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    la.vec(0, 0, 64);
    tag(la, w, "LightBrightness", PropertyType::Byte, 1);
    la.u8(128);
    tag(la, w, "LightRadius", PropertyType::Byte, 1);
    la.u8(9); // 250 units
    endProps(la, w);
    const int32_t light = w.addExport(lightClass, 0, "Light0", la.b);
    Bytes za;
    tag(za, w, "AmbientBrightness", PropertyType::Byte, 1);
    za.u8(16);
    endProps(za, w);
    const int32_t zone = w.addExport(zoneClass, 0, "ZoneInfo0", za.b);
    Bytes o;
    endProps(o, w);
    o.vec(0, 0, 0), o.vec(128, 128, 0), o.u8(1); // box
    o.vec(64, 64, 0), o.f32(90);                // sphere
    o.index(3);                                 // vectors: normal, texture U, V
    o.vec(0, 0, 1), o.vec(1, 0, 0), o.vec(0, 1, 0);
    o.index(4); // points
    o.vec(0, 0, 0), o.vec(128, 0, 0), o.vec(128, 128, 0), o.vec(0, 128, 0);
    o.index(1); // nodes
    o.f32(0), o.f32(0), o.f32(1), o.f32(0);
    o.u64(1), o.u8(0);
    o.index(0), o.index(0), o.index(-1), o.index(-1), o.index(-1), o.index(-1), o.index(-1);
    o.u8(0), o.u8(0), o.u8(4), o.i32(-1), o.i32(-1);
    o.index(1); // surfs
    o.index(tex), o.u32(0), o.index(0), o.index(0), o.index(1), o.index(2), o.index(0), o.index(0);
    o.u16(16), o.u16(0), o.index(0);
    o.index(4); // verts
    for (int i = 0; i < 4; ++i)
        o.index(i), o.index(-1);
    o.i32(0), o.i32(1);
    // Lighting: the zone, no polys, one lightmap of 5x5 samples 32 units
    // apart (the floor's 128), one light's bits (all lit but the far
    // corner), no bounds, hulls or leaves, the light list, two flags.
    o.index(zone), o.u64(1), o.u64(1);
    o.index(0);
    o.index(1);
    o.i32(0), o.vec(0, 0, 0), o.index(5), o.index(5), o.f32(32), o.f32(32), o.i32(0);
    o.index(5);
    o.u8(0x1f), o.u8(0x1f), o.u8(0x1f), o.u8(0x1f), o.u8(0x0f);
    o.index(0), o.index(0), o.index(0);
    o.index(2), o.index(light), o.index(0);
    o.i32(0), o.i32(1);
    w.addExport(modelClass, 0, "Model1", o.b);
    // A brush's model, without nodes.
    Bytes brush;
    endProps(brush, w);
    for (int i = 0; i < 6; ++i)
        brush.f32(0);
    brush.u8(0);
    for (int i = 0; i < 4; ++i)
        brush.f32(0);
    for (int i = 0; i < 5; ++i)
        brush.index(0);
    brush.i32(0), brush.i32(0);
    w.addExport(modelClass, 0, "Model2", brush.b);

    Package p;
    CHECK(!p.openMemory(w.build(), "Level"));
    Model m;
    std::string err;
    CHECK(findLevelModel(p, &m, &err) == 3 && err.empty());
    CHECK(m.nodes.size() == 1 && m.surfs.size() == 1 && m.verts.size() == 4 && m.zoneCount == 1);
    const std::vector<Polygon> polys = levelPolygons(m);
    CHECK(polys.size() == 1);
    if (polys.size() == 1) {
        CHECK(polys[0].points.size() == 4 && polys[0].normal[2] == 1);
        CHECK(polys[0].texels[2][0] == 144 && polys[0].texels[2][1] == 128); // panned 16 in U
    }
    CHECK(m.hasLighting() && m.zones.size() == 1 && m.lightMaps.size() == 1 && m.lights.size() == 2 &&
          m.trailingBytes == 0);
    CHECK((lightColor(0, 0, 128) == Vec3f{1, 0, 0}) && (lightColor(128, 0, 64) == Vec3f{0, 0.5f, 0.5f}));
    CHECK((lightColor(42, 255, 128) == Vec3f{1, 1, 1})); // saturation 255: white
    PackageSet set;
    const LevelLights lights = gatherLights(set, p, m);
    CHECK(lights.sources.size() == 2 && lights.unreadable == 0 && lights.sources[0].radius == 250);
    CHECK(lights.zoneAmbient.size() == 1 && lights.zoneAmbient[0][0] == 0.125f);
    const LightmapAtlas atlas = buildLightmaps(m, lights);
    CHECK(atlas.pages.size() == 1 && atlas.stats.lightmaps == 1 && atlas.stats.lit == 1);
    CHECK(atlas.stats.fromBounds == 0 && atlas.stats.badFit == 0);
    if (atlas.pages.size() == 1) {
        const auto uv = atlas.uv(0, {0, 0, 0}), far = atlas.uv(0, {128, 128, 0});
        CHECK(uv[0] * 1024 == 1.5f && uv[1] * 1024 == 1.5f && far[0] * 1024 == 5.5f);
        const image::Image& page = atlas.pages[0];
        // Under the light: ambient 0.125 + (1 - 64 / 250), stored halved.
        CHECK(std::abs(int(page.pixel(1, 1)[0]) - 111) <= 1);
        CHECK(page.pixel(5, 5)[0] == 16);                        // in shadow: ambient alone
        CHECK(page.pixel(0, 0)[0] == page.pixel(1, 1)[0]);       // the border repeats the edge
        CHECK(page.pixel(3, 1)[0] < page.pixel(1, 1)[0]);        // further from the light, darker
    }
    // A light four times as bright: capped at the texture's own colours
    // (1, stored halved), not brighter.
    LevelLights bright = lights;
    for (float& c : bright.sources[0].color)
        c *= 4;
    const LightmapAtlas capped = buildLightmaps(m, bright);
    if (capped.pages.size() == 1)
        CHECK(capped.pages[0].pixel(1, 1)[0] == 128 && capped.pages[0].pixel(5, 5)[0] == 16);
    // The same light pulsing: its share kept apart, redrawn by its level.
    CHECK(lights.animated.empty() && atlas.animated.empty());
    LevelLights pulsing = lights;
    pulsing.sources[0].type = kLightPulse;
    pulsing.sources[0].animated = 0;
    pulsing.animated = {pulsing.sources[0]};
    const LightmapAtlas changing = buildLightmaps(m, pulsing);
    CHECK(changing.animated.size() == 1);
    if (changing.animated.size() == 1 && atlas.pages.size() == 1) {
        const AnimatedLightmap& a = changing.animated[0];
        CHECK(a.page == 0 && a.x == 0 && a.y == 0 && a.w == 7 && a.h == 7 && a.lights.size() == 1);
        std::vector<uint8_t> full, off;
        a.draw({1.0f}, full);
        a.draw({0.0f}, off);
        CHECK(full.size() == 7 * 7 * 4 && full[(1 * 7 + 1) * 4] == atlas.pages[0].pixel(1, 1)[0]);
        CHECK(off[(1 * 7 + 1) * 4] == 16); // out: the ambient light alone
        // Drawn into the page as at time 0: the pulse's 0.6.
        CHECK(std::abs(int(changing.pages[0].pixel(1, 1)[0]) - int(16 + (111 - 16) * 0.6f)) <= 1);
    }
    LightSource l;
    l.type = kLightPulse;
    l.period = 2;
    CHECK(std::abs(lightLevel(l, 0.5f) - 0.99f) < 1e-4f && std::abs(lightLevel(l, 1.5f) - 0.21f) < 1e-4f);
    l.type = kLightBlink;
    CHECK(lightLevel(l, 0.5f) == 1 && lightLevel(l, 1.5f) == 0);
    l.type = kLightSteady;
    CHECK(lightLevel(l, 1.5f) == 1);
    // A model whose BSP doesn't hold together: refused, or read without it.
    {
        PackageWriter bad = w;
        // The node's vertex count (4) at its place in Model1's data: make
        // it 9, past the 4 vertices.
        std::vector<uint8_t>& d = bad.exports[3].data;
        const size_t nodesAt = 1 + 25 + 16 + 1 + 36 + 1 + 48 + 1 + 16 + 8 + 1 + 7;
        CHECK(d[nodesAt + 2] == 4);
        d[nodesAt + 2] = 9;
        Package pb;
        CHECK(!pb.openMemory(bad.build(), "Bad"));
        Model strict, lenient;
        CHECK(readModel(pb, 3, strict));
        CHECK(!readModel(pb, 3, lenient, false) && !lenient.geometryError.empty() && lenient.nodes.empty() &&
              lenient.hasLighting() && lenient.lightMaps.size() == 1);
    }
    // Invisible surfaces aren't drawn.
    m.surfs[0].polyFlags = polyflag::Invisible;
    CHECK(levelPolygons(m).empty());
}

static void testSearchPathsAndDetection() {
    const std::string ini = "[Engine.Engine]\nPaths=nope\n[Core.System]\nPurgeCacheDays=30\n"
                            "Paths=..\\System\\*.u\r\nPaths=../Maps/*.unr\n  paths = ..\\Textures\\*.utx \n[Other]\n";
    CHECK(parseSearchPaths(ini) == (std::vector<std::string>{"..\\System\\*.u", "../Maps/*.unr", "..\\Textures\\*.utx"}));

    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_test";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "system");
    std::filesystem::create_directories(tmp / "TEXTURES");
    std::ofstream(tmp / "system" / "CORE.U") << "x";
    std::ofstream(tmp / "system" / "Engine.u") << "x";
    CHECK(detect("unreal", tmp).size() == 1 && detect("unreal", tmp)[0].gameId == "ue1");
    std::ofstream(tmp / "system" / "ds9.ini") << "[Core.System]\nPaths=*.u\nPaths=..\\Textures\\*.utx\n";
    const auto found = detect("unreal", tmp);
    CHECK(found.size() == 1 && found[0].gameId == "ds9fallen");
    {
        const std::vector<uint8_t> pkg = texturePackage();
        std::ofstream(tmp / "TEXTURES" / "Walls.utx", std::ios::binary)
            .write(reinterpret_cast<const char*>(pkg.data()), std::streamsize(pkg.size()));
    }
    PackageSet set;
    std::vector<std::string> warnings;
    CHECK(!set.open(tmp, "DS9.ini", &warnings));
    CHECK(set.names() == (std::vector<std::string>{"CORE", "Engine", "Walls"}));
    const Package* walls = set.get("walls");
    CHECK(walls && walls->findExport("Brick") >= 0);
    CHECK(set.get("core") == nullptr && !set.error("core").empty()); // not a real package
    if (walls) {
        image::Image img;
        CHECK(!decodeTexture(set, *walls, walls->findExport("Brick"), img));
    }
    std::filesystem::remove_all(tmp);
    CHECK(findEngine("unreal") != nullptr);
}

static void testCompressedTexture() {
    // An empty P8 mipmap, and the pixels in the bHasComp list as DXT1.
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Texture");
    Bytes o;
    tag(o, w, "bHasComp", PropertyType::Bool, 0);
    o.b.back() |= 0x80;
    tag(o, w, "CompFormat", PropertyType::Byte, 1);
    o.u8(3);
    endProps(o, w);
    o.index(1);
    o.i32(0), o.index(0), o.i32(4), o.i32(4), o.u8(2), o.u8(2);
    o.index(1);
    o.i32(0), o.index(8);
    o.u16(0xf800), o.u16(0x001f), o.u32(0x55555555); // red and blue; every pixel index 1 (blue)
    o.i32(4), o.i32(4), o.u8(2), o.u8(2);
    w.addExport(cls, 0, "Shiny", o.b);
    // One with no pixels at all.
    Bytes empty;
    endProps(empty, w);
    empty.index(1);
    empty.i32(0), empty.index(0), empty.i32(256), empty.i32(256), empty.u8(8), empty.u8(8);
    w.addExport(cls, 0, "Blank", empty.b);
    PackageSet set;
    set.addMemory("Comp", ".utx", w.build());
    const Package* p = set.get("Comp");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 4 && info.format == 3 && img.pixel(2, 3)[2] == 255 && img.pixel(2, 3)[0] == 0);
    const Error err = decodeTexture(set, *p, 1, img);
    CHECK(err && err.message.find("no pixels stored") != std::string::npos);
}

static void testSoundErrors() {
    EmbeddedFile f;
    f.format = "OGG";
    f.data = {'O', 'g', 'g', 'S', 0, 2};
    f.data.resize(20);
    audio::Sound s;
    const Error err = decodeEmbeddedSound(f, s);
    CHECK(!f.isPlaceholder());
    CHECK(err && err.message.find("format OGG, 20 bytes starting 4f 67 67 53 00 02 00 00 (\"OggS\")") == 0);
    // The Fallen's empty sounds.
    f.format = "wav";
    f.data = {'X', ' ', '\r', '\n'};
    CHECK(f.isPlaceholder() && decodeEmbeddedSound(f, s).message.find("placeholder") != std::string::npos);
}

static void testStreamedSounds() {
    // A placeholder Sound, and the MP3 of its name in the mp3 folder.
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_mp3";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "System");
    std::filesystem::create_directories(tmp / "Sounds");
    std::filesystem::create_directories(tmp / "mp3");
    std::ofstream(tmp / "System" / "Default.ini") << "[Core.System]\nPaths=..\\Sounds\\*.uax\n";
    std::ofstream(tmp / "mp3" / "orb_thme.mp3", std::ios::binary)
        .write(reinterpret_cast<const char*>(kTone440Mp3), sizeof kTone440Mp3);
    PackageWriter w;
    const int32_t cls = w.addImport("Engine", "Class", 0, "Sound");
    for (const char* name : {"Orb_Thme", "Missing"}) {
        Bytes o;
        endProps(o, w);
        o.index(w.name("wav"));
        o.i32(0);
        o.index(4);
        o.u8('X'), o.u8(' '), o.u8('\r'), o.u8('\n');
        w.addExport(cls, 0, name, o.b);
    }
    {
        const std::vector<uint8_t> pkg = w.build();
        std::ofstream(tmp / "Sounds" / "Trks.uax", std::ios::binary)
            .write(reinterpret_cast<const char*>(pkg.data()), std::streamsize(pkg.size()));
    }
    PackageSet set;
    CHECK(!set.open(tmp, ""));
    const Package* p = set.get("Trks");
    CHECK(p != nullptr);
    if (p) {
        audio::Sound s;
        std::string from;
        CHECK(!loadSound(set, *p, 0, s, &from));
        CHECK(s.format == audio::kFormatMp3 && s.rate == 22050 && s.frames() > 0);
        CHECK(std::filesystem::path(from).filename() == "orb_thme.mp3");
        const Error err = loadSound(set, *p, 1, s, &from);
        CHECK(err && err.message.find("no mp3/Missing.mp3") != std::string::npos);
    }
    std::filesystem::remove_all(tmp);
}

static void testProceduralTextures() {
    // A FireTexture and a WetTexture that store no pixels, over the
    // masked P8 texture and palette of texturePackage().
    PackageWriter w;
    const int32_t walls = w.addImport("Core", "Package", 0, "Walls");
    const int32_t brick = w.addImport("Engine", "Texture", walls, "Brick");
    const int32_t pal = w.addImport("Engine", "Palette", walls, "BrickPal");
    const int32_t fire = w.addImport("Fire", "Class", 0, "FireTexture");
    const int32_t wet = w.addImport("Fire", "Class", 0, "WetTexture");
    auto empty = [&](int32_t ref, const char* prop, int width, int height) {
        Bytes o;
        tag(o, w, prop, PropertyType::Object, 1);
        o.index(ref);
        endProps(o, w);
        o.index(1);
        o.i32(0), o.index(0), o.i32(width), o.i32(height), o.u8(0), o.u8(0);
        return o.b;
    };
    w.addExport(fire, 0, "Flames", empty(pal, "Palette", 2, 4));
    w.addExport(wet, 0, "Ripples", empty(brick, "SourceTexture", 8, 4));
    w.addExport(fire, 0, "Nothing", empty(0, "Palette", 2, 2));
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    set.addMemory("Fx", ".utx", w.build());
    const Package* p = set.get("Fx");
    CHECK(p != nullptr);
    if (!p)
        return;
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 2 && img.h == 4 && info.standIn.find("FireTexture") == 0);
    CHECK(img.pixel(0, 0)[0] == 0 && img.pixel(1, 3)[0] == 255 && img.pixel(1, 3)[3] == 255); // dark to bright
    CHECK(!decodeTexture(set, *p, 1, img, &info));
    CHECK(img.w == 8 && img.h == 4 && info.standIn.find("Walls.Brick") != std::string::npos);
    CHECK(img.pixel(2, 0)[0] == 1 && img.pixel(7, 3)[0] == 7); // Brick's pixels, scaled up
    const Error err = decodeTexture(set, *p, 2, img, &info);
    CHECK(err && err.message.find("no stand-in") != std::string::npos);
}

static void testAnimatedTextures() {
    // A FliTexture of two 4x2 frames and the ring frame back to the
    // first, a FireTexture with one spark, and a WaterTexture with a drop,
    // all through texturePackage()'s palette (red = the index).
    PackageWriter w;
    const int32_t walls = w.addImport("Core", "Package", 0, "Walls");
    const int32_t pal = w.addImport("Engine", "Palette", walls, "BrickPal");
    const int32_t fli = w.addImport("DS9", "Class", 0, "FliTexture");
    const int32_t fire = w.addImport("Fire", "Class", 0, "FireTexture");
    const int32_t water = w.addImport("Fire", "Class", 0, "WaterTexture");
    const int32_t ice = w.addImport("Fire", "Class", 0, "IceTexture");
    const int32_t brick = w.addImport("Engine", "Texture", walls, "Brick");
    auto start = [&](Bytes& o) {
        tag(o, w, "Palette", PropertyType::Object, 1);
        o.index(pal);
    };
    auto mips = [](Bytes& o, int width, int height) {
        o.index(1);
        o.i32(0), o.index(0), o.i32(width), o.i32(height), o.u8(0), o.u8(0);
    };
    auto frame = [](Bytes& out, const std::vector<std::pair<int, std::vector<uint8_t>>>& chunks) {
        Bytes f;
        for (const auto& [type, data] : chunks) {
            f.u32(uint32_t(data.size() + 6));
            f.u16(type);
            for (uint8_t b : data)
                f.u8(b);
        }
        out.u32(uint32_t(f.b.size() + 16 + 2)); // padded by 2, as some of The Fallen's are
        out.u16(0xf1fa);
        out.u16(int(chunks.size()));
        for (int i = 0; i < 8; ++i)
            out.u8(0);
        out.b.insert(out.b.end(), f.b.begin(), f.b.end());
        out.u16(0);
    };
    Bytes flc;
    frame(flc, {{16, {0, 1, 2, 3, 4, 5, 6, 7}}});     // the picture as is
    frame(flc, {{7, {1, 0, 1, 0, 1, 1, 9, 9}}});     // line 0: skip 1, a word of 9s
    frame(flc, {{7, {1, 0, 1, 0, 1, 1, 1, 2}}});     // and back
    Bytes f;
    start(f);
    endProps(f, w);
    mips(f, 4, 2);
    f.f32(2.0f / 30), f.f32(1), f.u16(2), f.u16(4), f.u16(2), f.u16(0), f.u32(uint32_t(flc.b.size()));
    f.b.insert(f.b.end(), flc.b.begin(), flc.b.end());
    w.addExport(fli, 0, "Blink", f.b);
    Bytes fb;
    start(fb);
    endProps(fb, w);
    mips(fb, 8, 8);
    fb.index(1);
    for (int b : {99, 200, 3, 4, 0, 0, 0, 0}) // a spark type that just heats its spot
        fb.u8(b);
    w.addExport(fire, 0, "Flame", fb.b);
    Bytes wb;
    start(wb);
    tag(wb, w, "NumDrops", PropertyType::Int, 4);
    wb.i32(1);
    tag(wb, w, "Drops", PropertyType::Struct, 8, 0, "ADrop");
    for (int b : {1, 200, 2, 2, 0, 0, 0, 16}) // a spot rising and falling
        wb.u8(b);
    endProps(wb, w);
    mips(wb, 8, 8);
    w.addExport(water, 0, "Pool", wb.b);
    // Brick through itself, the source sliding a pixel a second.
    Bytes ib;
    tag(ib, w, "SourceTexture", PropertyType::Object, 1);
    ib.index(brick);
    tag(ib, w, "GlassTexture", PropertyType::Object, 1);
    ib.index(brick);
    tag(ib, w, "HorizPanSpeed", PropertyType::Byte, 1);
    ib.u8(132);
    tag(ib, w, "MoveIce", PropertyType::Bool, 0);
    ib.b.back() |= 0x80;
    endProps(ib, w);
    mips(ib, 4, 2);
    w.addExport(ice, 0, "Frost", ib.b);
    PackageSet set;
    set.addMemory("Walls", ".utx", texturePackage());
    set.addMemory("Fx", ".utx", w.build());
    const Package* p = set.get("Fx");
    CHECK(p != nullptr);
    if (!p)
        return;
    std::string err;
    auto blink = openAnimatedTexture(set, *p, 0, &err);
    CHECK(blink && err.empty());
    if (blink) {
        CHECK(std::string(blink->kind()) == "FLC" && blink->image().w == 4 && blink->image().h == 2);
        CHECK(blink->image().pixel(1, 0)[0] == 1 && blink->image().pixel(3, 1)[0] == 7);
        CHECK(!blink->advance(0.01f));                           // a frame lasts 1/30 s
        CHECK(blink->advance(0.03f) && blink->image().pixel(1, 0)[0] == 9 && blink->image().pixel(2, 0)[0] == 9);
        CHECK(blink->image().pixel(3, 0)[0] == 3);
        CHECK(blink->advance(1.0f / 30) && blink->image().pixel(1, 0)[0] == 1); // the ring frame: the first again
        CHECK(blink->advance(1.0f / 30) && blink->image().pixel(1, 0)[0] == 9);
    }
    auto flame = openAnimatedTexture(set, *p, 1, &err);
    CHECK(flame != nullptr);
    if (flame) {
        CHECK(std::string(flame->kind()) == "fire");
        CHECK(flame->image().pixel(3, 4)[0] == 200);  // the spark's spot
        CHECK(flame->image().pixel(4, 4)[0] > 0 && flame->image().pixel(4, 4)[0] < 200); // spreading, cooling
        CHECK(flame->image().pixel(0, 0)[0] == 0);
    }
    auto pool = openAnimatedTexture(set, *p, 2, &err);
    CHECK(pool != nullptr);
    if (pool) {
        CHECK(std::string(pool->kind()) == "water" && pool->image().w == 8);
        std::vector<uint8_t> before = pool->image().rgba;
        CHECK(pool->advance(0.1f) && pool->image().rgba != before);
    }
    auto frost = openAnimatedTexture(set, *p, 3, &err);
    CHECK(frost != nullptr);
    if (frost) {
        CHECK(std::string(frost->kind()) == "ice" && frost->image().w == 4 && frost->image().h == 2);
        auto run = [&](float seconds) {
            for (int k = 0; k < int(seconds * 30 + 0.5f); ++k)
                frost->advance(1.0f / 30);
        };
        run(0.5f);
        const std::vector<uint8_t> start = frost->image().rgba;
        run(1);
        CHECK(frost->image().rgba != start); // slid a pixel
        run(3);
        CHECK(frost->image().rgba == start); // the whole width: as it was
    }
    // A plain texture isn't one.
    CHECK(!isAnimatedTextureClass("Texture") && isAnimatedTextureClass("WetTexture"));
    // As a still picture, the FLC's first frame.
    image::Image img;
    TextureInfo info;
    CHECK(!decodeTexture(set, *p, 0, img, &info));
    CHECK(img.w == 4 && img.pixel(1, 0)[0] == 1 && info.standIn.find("FLC") != std::string::npos);
}

static void testMapExtension() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_maps";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "System");
    std::filesystem::create_directories(tmp / "Maps");
    // No DS9.ini yet: the game makes it from Default.ini.
    std::ofstream(tmp / "System" / "Default.ini") << "[Core.System]\nPaths=..\\Maps\\*.dsm\n";
    std::ofstream(tmp / "Maps" / "Ops.dsm") << "x";
    std::ofstream(tmp / "Maps" / "Other.unr") << "x";
    PackageSet set;
    CHECK(!set.open(tmp, "DS9.ini"));
    CHECK(set.iniFile().filename() == "Default.ini");
    CHECK(set.mapNames() == std::vector<std::string>{"Ops"} && set.isMap("ops") && !set.isMap("nothing"));
    std::filesystem::remove_all(tmp);
}

static void testActors() {
    // Classes defined here: Brush, Mover extending it, ElevatorMover
    // extending that, SkyZoneInfo; a square brush polygon; an elevator
    // using it with two keyframes; a sky zone.
    PackageWriter w;
    const int32_t brushClass = w.addExport(0, 0, "Brush", {});
    const int32_t moverClass = w.addExport(0, 0, "Mover", {});
    w.exports.back().superRef = brushClass;
    const int32_t elevatorClass = w.addExport(0, 0, "ElevatorMover", {});
    w.exports.back().superRef = moverClass;
    const int32_t skyClass = w.addExport(0, 0, "SkyZoneInfo", {});
    const int32_t polysClass = w.addImport("Engine", "Class", 0, "Polys");
    const int32_t modelClass = w.addImport("Engine", "Class", 0, "Model");

    Bytes polys;
    endProps(polys, w);
    polys.i32(1), polys.i32(1);
    polys.index(4);
    polys.vec(0, 0, 0), polys.vec(0, 0, 1), polys.vec(1, 0, 0), polys.vec(0, 1, 0);
    polys.vec(0, 0, 0), polys.vec(64, 0, 0), polys.vec(64, 64, 0), polys.vec(0, 64, 0);
    polys.u32(0), polys.index(0), polys.index(0), polys.index(0), polys.index(0), polys.index(0);
    polys.u16(8), polys.u16(0);
    const int32_t polysRef = w.addExport(polysClass, 0, "Polys0", polys.b);

    Bytes model; // a brush's model: no geometry of its own, then its Polys
    endProps(model, w);
    for (int i = 0; i < 6; ++i)
        model.f32(0);
    model.u8(0);
    for (int i = 0; i < 4; ++i)
        model.f32(0);
    for (int i = 0; i < 5; ++i)
        model.index(0);
    model.i32(0), model.i32(0);
    model.index(polysRef);
    model.index(0), model.index(0), model.index(0), model.index(0), model.index(0), model.index(0);
    model.i32(0), model.i32(0);
    const int32_t modelRef = w.addExport(modelClass, 0, "Model5", model.b);

    Bytes mover;
    tag(mover, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    mover.vec(10, 20, 30);
    tag(mover, w, "KeyPos", PropertyType::Struct, 12, 1, "Vector");
    mover.vec(0, 0, 64);
    tag(mover, w, "MoveTime", PropertyType::Float, 4);
    mover.f32(2);
    tag(mover, w, "Brush", PropertyType::Object, 1);
    mover.index(modelRef);
    endProps(mover, w);
    w.addExport(elevatorClass, 0, "ElevatorMover0", mover.b);

    Bytes sky;
    tag(sky, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    sky.vec(1000, 2000, 3000);
    endProps(sky, w);
    w.addExport(skyClass, 0, "SkyZoneInfo0", sky.b);

    PackageSet set;
    set.addMemory("Level", ".unr", w.build());
    const Package* p = set.get("Level");
    CHECK(p != nullptr);
    if (!p)
        return;
    const int elevator = p->findExport("ElevatorMover0"), skyZone = p->findExport("SkyZoneInfo0");
    CHECK(isA(set, *p, elevator, "mover") && isA(set, *p, elevator, "Brush") && !isA(set, *p, skyZone, "Mover"));

    std::vector<Poly> list;
    size_t trailing = 1;
    CHECK(!readPolys(*p, int(polysRef) - 1, list, &trailing) && trailing == 0);
    CHECK(list.size() == 1 && list[0].vertices.size() == 4 && list[0].normal[2] == 1 && list[0].panU == 8);

    std::vector<std::string> errors;
    const std::vector<Mover> movers = findMovers(set, *p, &errors);
    CHECK(errors.empty() && movers.size() == 1);
    if (movers.size() == 1) {
        const Mover& m = movers[0];
        CHECK(m.polys.size() == 1 && m.numKeys == 2 && m.keyNum == 0 && m.moveTime == 2);
        CHECK((m.basePosition() == Vec3f{10, 20, 30}) && (m.keyPos[1] == Vec3f{0, 0, 64}));
        CHECK((m.mainScale == Vec3f{1, 1, 1}));
    }
    const auto skyAt = findSkyZone(set, *p);
    CHECK(skyAt && (*skyAt == Vec3f{1000, 2000, 3000}));

    // Yaw a quarter turn: forward is +Y, right is -X.
    const auto axes = rotationAxes({0, 16384, 0});
    CHECK(std::abs(axes[0][1] - 1) < 1e-5f && std::abs(axes[1][0] + 1) < 1e-5f && std::abs(axes[2][2] - 1) < 1e-5f);
}

// A two-frame triangle mesh as the package's first export (its data at file
// offset 64, which its lazy arrays' end offsets need), classic or LOD.
// `importBlock`: as The Fallen stores them, the vertices not in their lazy
// array but in a block after the textures (1: one array of all frames'
// vertices; 2: one per frame).
static std::vector<uint8_t> meshData(PackageWriter& w, bool lod, int32_t textureRef, int importBlock = 0) {
    const size_t base = 64;
    Bytes o;
    endProps(o, w);
    for (int i = 0; i < 6; ++i)
        o.f32(0);
    o.u8(1);
    o.f32(0), o.f32(0), o.f32(0), o.f32(10);
    auto packed = [](int x, int y, int z) {
        return uint32_t(x & 0x7ff) | uint32_t(y & 0x7ff) << 11 | uint32_t(z & 0x3ff) << 22;
    };
    // A lazy array: the file offset just past it, the count, the elements.
    auto lazy = [&](int count, const std::vector<uint8_t>& elements) {
        Bytes c;
        c.index(count);
        o.u32(uint32_t(base + o.b.size() + 4 + c.b.size() + elements.size()));
        o.bytes(c.b);
        o.bytes(elements);
    };
    Bytes verts;
    for (int z : {0, 5}) {
        verts.u32(packed(-3, 0, z));
        verts.u32(packed(10, 0, z));
        verts.u32(packed(0, 10, z));
    }
    if (importBlock)
        lazy(0, {});
    else
        lazy(6, verts.b);
    Bytes tris;
    if (!lod) {
        tris.u16(0), tris.u16(1), tris.u16(2);
        tris.u8(0), tris.u8(0), tris.u8(128), tris.u8(0), tris.u8(0), tris.u8(128);
        tris.u32(0), tris.i32(0);
    }
    lazy(lod ? 0 : 1, tris.b);
    o.index(1); // animations
    o.index(w.name("Wave")), o.index(w.name("None")), o.i32(0), o.i32(2), o.index(0), o.f32(10);
    lazy(0, {}); // connects
    for (int i = 0; i < 6; ++i)
        o.f32(0);
    o.u8(1);
    o.f32(0), o.f32(0), o.f32(0), o.f32(10);
    lazy(0, {}); // vertex links
    o.index(1), o.index(textureRef);
    if (importBlock) {
        // Some of the block's other contents (names, counts), then the
        // vertex arrays: (count, count, 1) and the packed vertices.
        o.u32(1), o.u16(4), o.u8('n'), o.u8('a'), o.u8('m'), o.u8('e'), o.u32(3), o.f32(0.5f);
        if (importBlock == 4 || importBlock == 5) {
            // A skeleton: "root", no vertices, placed 10 up, and its one
            // child "b", placed 10 up from it, with the first frame's
            // vertices (by Scale 2) 20 below it, their weights and indices.
            auto bone = [&](const char* name, bool verts, bool child) {
                o.u16(uint16_t(std::strlen(name)));
                for (const char* c = name; *c; ++c)
                    o.u8(uint8_t(*c));
                o.u32(1), o.u32(1), o.u32(8), o.u32(0);
                const uint32_t k = verts ? 3 : 0;
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.vec(-6, 0, -20), o.vec(20, 0, -20), o.vec(0, 20, -20);
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.f32(1), o.f32(1), o.f32(1);
                o.u32(k), o.u32(k), o.u32(128);
                if (verts)
                    o.u32(0), o.u32(1), o.u32(2);
                for (int m = 0; m < 2; ++m) {
                    o.vec(1, 0, 0), o.vec(0, 1, 0), o.vec(0, 0, 1), o.vec(0, 0, m ? 10.0f : 0.0f);
                    o.u32(m ? 4 : 7);
                }
                // A clip, without keys (5: the child's clips not understood).
                if (importBlock == 5 && !child)
                    o.u32(1), o.u32(2), o.u32(1);
                else
                    o.u32(1), o.u32(1), o.u32(1);
                o.u32(0), o.u32(0), o.u32(32), o.u32(0), o.u32(0), o.u32(32), o.u32(0), o.u32(0);
                o.u32(child ? 1 : 0), o.u32(child ? 1 : 0), o.u32(4); // children
            };
            // After a bone's children: a matrix, 7, 10 floats and two
            // arrays of i32s.
            auto last = [&](int links) {
                o.vec(1, 0, 0), o.vec(0, 1, 0), o.vec(0, 0, 1), o.vec(0, 0, 0);
                o.u32(7);
                for (int k = 0; k < 10; ++k)
                    o.f32(0);
                for (int a = 0; a < 2; ++a) {
                    o.u32(uint32_t(links)), o.u32(uint32_t(links)), o.u32(1);
                    for (int k = 0; k < links; ++k)
                        o.u32(3);
                }
            };
            bone("root", false, true);
            bone("b", true, false);
            last(1);
            last(0);
            // What follows the bones: body parts' names.
            o.u32(1), o.u32(1), o.u32(10), o.u16(8);
            for (char c : std::string("FullBody"))
                o.u8(uint8_t(c));
        } else if (importBlock == 1) {
            o.u32(6), o.u32(6), o.u32(1);
            o.bytes(verts.b);
        } else {
            // 2: one array per frame; 3: only the first frame's (a keyframed mesh).
            for (int f = 0; f < (importBlock == 3 ? 1 : 2); ++f) {
                o.u32(3), o.u32(3), o.u32(1);
                o.bytes(std::vector<uint8_t>(verts.b.begin() + f * 12, verts.b.begin() + f * 12 + 12));
                o.u32(0), o.u32(64);
            }
        }
        o.u32(14), o.u32(14), o.u32(256);
    }
    o.index(0), o.index(0);
    o.i32(3), o.i32(2);
    o.u32(0), o.u32(0);
    o.vec(2, 2, 2), o.vec(0, 0, 0), o.i32(0), o.i32(0), o.i32(0);
    o.i32(0), o.i32(0);
    o.index(0); // texture levels of detail
    if (lod) {
        o.index(0), o.index(0);
        o.index(1), o.u16(0), o.u16(1), o.u16(2), o.u16(0); // a face: wedges, material
        o.index(0);
        o.index(3); // wedges: vertex, u, v
        o.u16(0), o.u8(0), o.u8(0), o.u16(1), o.u8(128), o.u8(0), o.u16(2), o.u8(0), o.u8(128);
        o.index(1), o.u32(polyflag::Masked), o.i32(0); // a material
        o.index(0);
        o.i32(3), o.i32(0);
        for (int i = 0; i < 6; ++i)
            o.f32(0);
        o.index(0);
        o.i32(3);
    }
    return o.b;
}

static void testMeshes() {
    for (bool lod : {false, true}) {
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, lod ? "LodMesh" : "Mesh");
        const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
        w.addExport(meshClass, 0, "Triangle", meshData(w, lod, 2));
        w.addExport(texClass, 0, "Skin", {});
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err);
        if (err) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            continue;
        }
        CHECK(m.lod == lod && m.frameVerts == 3 && m.animFrames == 2 && m.verts.size() == 6 && m.vertexBytes == 4);
        CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[5] == Vec3f{0, 10, 5}));
        CHECK(m.tris.size() == 1 && m.tris[0].vertex[2] == 2 && m.tris[0].uv[1][0] == 0.5f);
        CHECK(m.tris[0].polyFlags == (lod ? polyflag::Masked : 0u));
        CHECK(m.textures.size() == 1 && m.textures[0] == 2 && (m.scale == Vec3f{2, 2, 2}) && m.trailingBytes == 0);
        const MeshAnimSeq* wave = m.findAnim("wave");
        CHECK(wave && wave->numFrames == 2 && wave->rate == 10);
    }
}

static void testAnimationSet() {
    // A shared animation set: a Mesh with a skeleton ("root" and its child
    // "b", no vertices) and one clip, whose only key turns "b" a quarter
    // turn about Z. A character built on bones of the same names borrows it.
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "Mesh");
    const int32_t lodClass = w.addImport("Engine", "Class", 0, "LodMesh");
    Bytes s;
    endProps(s, w);
    auto bone = [&](const char* name, bool key, bool child) {
        s.u16(uint16_t(std::strlen(name)));
        for (const char* c = name; *c; ++c)
            s.u8(uint8_t(*c));
        s.u32(1), s.u32(1), s.u32(8), s.u32(0);
        for (int a = 0; a < 3; ++a)
            s.u32(0), s.u32(0), s.u32(128);
        for (int m = 0; m < 2; ++m) {
            s.vec(1, 0, 0), s.vec(0, 1, 0), s.vec(0, 0, 1), s.vec(0, 0, m ? 10.0f : 0.0f);
            s.u32(m ? 4 : 7);
        }
        s.u32(1), s.u32(1), s.u32(1);    // a clip
        s.u32(0), s.u32(0), s.u32(32);   // no position keys
        s.u32(key ? 1 : 0), s.u32(key ? 1 : 0), s.u32(32);
        if (key) {
            // A quarter turn about Z: x, y 0 (1023.5 of 2047), z sin 45
            // degrees, w - 1 = cos 45 degrees - 1.
            const uint32_t z = uint32_t(std::lround((0.70710678 + 1) / 2 * 1023));
            s.f32(0), s.u32(1023u | 1023u << 11 | z << 22), s.u16(uint16_t(int16_t(std::lround((0.70710678 - 1) * 32767))));
        }
        s.u32(0), s.u32(0);
        s.u32(child ? 1 : 0), s.u32(child ? 1 : 0), s.u32(4);
    };
    auto last = [&] {
        s.vec(1, 0, 0), s.vec(0, 1, 0), s.vec(0, 0, 1), s.vec(0, 0, 0);
        s.u32(7);
        for (int k = 0; k < 10; ++k)
            s.f32(0);
        for (int a = 0; a < 2; ++a)
            s.u32(0), s.u32(0), s.u32(1);
    };
    bone("root", false, true);
    bone("b", true, false);
    last();
    last();
    // The character first: its data holds file offsets for the first export.
    w.addExport(lodClass, 0, "Character", meshData(w, true, 2, 4));
    w.addExport(meshClass, 0, "Male", s.b);
    Package p;
    CHECK(!p.openMemory(w.build(), "Shared"));
    AnimationSet set;
    const Error err = readAnimationSet(p, 1, set);
    CHECK(!err && set.frames() == 1 && set.clipFor(0, 1) == 0 && set.clipFor(0, 2) == -1);
    Mesh m;
    CHECK(!readMesh(p, 0, m) && m.bones == 2);
    MeshAnimSeq seq;
    seq.name = "Turned";
    CHECK(!borrowAnimation(p, 0, set, 0, seq, m));
    CHECK(m.animFrames == 3 && m.verts.size() == 9 && m.findAnim("Turned") && m.findAnim("Turned")->startFrame == 2);
    if (m.verts.size() == 9) {
        // (-3, 0, 0) turned a quarter about Z: (0, +-3, 0).
        const Vec3f& v = m.verts[6];
        CHECK(std::fabs(v[0]) < 0.05f && std::fabs(std::fabs(v[1]) - 3) < 0.05f && std::fabs(v[2]) < 0.05f);
    }
}

static void testImportedMeshBlock() {
    // The Fallen's layout, both ways of keeping the vertices.
    for (int block : {1, 2}) {
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, block));
        w.addExport(texClass, 0, "Skin", {});
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err);
        if (err) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            continue;
        }
        CHECK(m.importBlock > 0 && m.frameVerts == 3 && m.animFrames == 2 && m.verts.size() == 6);
        CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[5] == Vec3f{0, 10, 5}) && m.tris.size() == 1);
        CHECK((m.scale == Vec3f{2, 2, 2}) && m.trailingBytes == 0 && m.findAnim("Wave"));
        CHECK(m.keyFrames == 0 && m.importAxes && m.importAxesGuessed); // no float copy to fit
    }
    {
        // Only the first frame's vertices: used for both frames.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 3));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        CHECK(!readMesh(p, 0, m) && m.keyFrames == 1 && m.verts.size() == 6 && m.verts[3] == m.verts[0]);
    }
    {
        // No packed vertices, a bone: its pose, for both frames.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 4));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err && m.bones == 2 && m.verts.size() == 6);
        if (!err && m.verts.size() == 6)
            CHECK((m.verts[0] == Vec3f{-3, 0, 0}) && (m.verts[2] == Vec3f{0, 10, 0}) && m.verts[5] == m.verts[2] &&
                  m.importAxes && !m.importAxesGuessed);
        const auto lines = describeBones(p, 0);
        CHECK(!lines.empty() && lines.back().find("all in one tree") != std::string::npos);
    }
    {
        // A bone whose clips aren't understood is still found, and placed
        // under its parent.
        PackageWriter w;
        const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
        w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2, 5));
        Package p;
        CHECK(!p.openMemory(w.build(), "Meshes"));
        Mesh m;
        const Error err = readMesh(p, 0, m);
        CHECK(!err && m.bones == 2 && m.verts.size() == 6 && (m.verts[0] == Vec3f{-3, 0, 0}));
        const auto lines = describeBones(p, 0);
        CHECK(!lines.empty() && lines.back().find("not understood") != std::string::npos);
    }
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "Mesh");
    w.addExport(meshClass, 0, "Triangle", meshData(w, false, 0, 1));
    Package p;
    CHECK(!p.openMemory(w.build(), "Meshes"));
    // A Mesh with the block and its triangles where they usually are reads
    // too (The Fallen's own have none there, and are refused for now).
    Mesh m;
    CHECK(!readMesh(p, 0, m) && m.importBlock > 0 && m.tris.size() == 1 && m.verts.size() == 6);
}

static void testClassDefaultsAndMeshActors() {
    // A class "Lamp" whose data is some script-like bytes, the class's
    // outer and config name, then its defaults: drawn as a mesh, twice
    // the size. A placed Lamp sets only its location.
    PackageWriter w;
    const int32_t meshClass = w.addImport("Engine", "Class", 0, "LodMesh");
    const int32_t texClass = w.addImport("Engine", "Class", 0, "Texture");
    const int32_t meshRef = w.addExport(meshClass, 0, "Triangle", meshData(w, true, 2));
    w.addExport(texClass, 0, "Skin", {});
    Bytes cls;
    for (int i = 0; i < 40; ++i)
        cls.u8(i);
    cls.index(-1);                 // within: an object
    cls.index(w.name("System"));   // config name
    tag(cls, w, "DrawType", PropertyType::Byte, 1);
    cls.u8(2);
    tag(cls, w, "Mesh", PropertyType::Object, 1);
    cls.index(meshRef);
    tag(cls, w, "DrawScale", PropertyType::Float, 4);
    cls.f32(2);
    endProps(cls, w);
    const int32_t lampClass = w.addExport(0, 0, "Lamp", cls.b);
    Bytes actor;
    tag(actor, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    actor.vec(1, 2, 3);
    endProps(actor, w);
    w.addExport(lampClass, 0, "Lamp0", actor.b);

    PackageSet set;
    set.addMemory("Level", ".unr", w.build());
    const Package* p = set.get("Level");
    CHECK(p != nullptr);
    if (!p)
        return;
    ClassDefaults defaults(set);
    const PropertyList* d = defaults.defaults(*p, int(lampClass) - 1);
    CHECK(d && d->items.size() == 3 && d->byte("DrawType") == 2 && d->number("DrawScale") == 2);
    MeshActorStats stats;
    const std::vector<MeshActor> actors = findMeshActors(set, *p, defaults, &stats);
    CHECK(actors.size() == 1 && stats.meshes == 1 && stats.fromDefaults == 1);
    if (actors.size() == 1) {
        CHECK(actors[0].mesh.package == p && actors[0].mesh.exportIndex == int(meshRef) - 1);
        CHECK(actors[0].drawScale == 2 && (actors[0].location == Vec3f{1, 2, 3}));
    }
    // Without the config name before them, the whole list isn't taken (a
    // tail of it can be, where other bytes happen to look like one: in a
    // class the true start comes first, so the earliest is kept).
    std::vector<uint8_t> bare(cls.b.begin() + 40 + 2, cls.b.end());
    PropertyList tail;
    CHECK(!findTrailingDefaults(*p, bare, tail) || tail.items.size() < 3);
}

int main() {
    testCompactIndex();
    testPackageTables();
    testProperties();
    testTextures();
    testSounds();
    testModel();
    testSearchPathsAndDetection();
    testCompressedTexture();
    testSoundErrors();
    testProceduralTextures();
    testAnimatedTextures();
    testStreamedSounds();
    testActors();
    testMeshes();
    testImportedMeshBlock();
    testAnimationSet();
    testClassDefaultsAndMeshActors();
    testMapExtension();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("All Unreal tests passed\n");
    return 0;
}
