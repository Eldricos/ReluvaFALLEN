// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/game_save.h"

#include "engines/unreal/properties.h"

#include <cstring>
#include <fstream>

namespace reluva::unreal {

bool isPackageFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    uint8_t tag[4] = {};
    return f.read(reinterpret_cast<char*>(tag), 4) && tag[0] == 0xC1 && tag[1] == 0x83 && tag[2] == 0x2A &&
           tag[3] == 0x9E;
}

// The Level object (ULevel's serialisation, version 73, with The Fallen's
// additions): its properties; the actor list (a count, the room for them,
// the references); the URL (protocol, host, map and portal strings, the
// options, the port, whether it's valid); the model; the reach list; the
// level's time when written; the first deleted actor; 16 text blocks; the
// travel information (string pairs); then The Fallen's: the ghosts, the
// dialogue engine and the sounds playing; then an i32.
Error readLevelExtras(const Package& package, LevelExtras& out) {
    out = {};
    const std::vector<int> levels = package.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(package.name() + ": no Level object");
    const int index = levels.front();
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    const int v = package.version();
    auto fail = [&](const char* what) { return Error::fail(package.name() + ": the Level object's " + what + " don't read"); };
    const int32_t count = r.i32();
    r.i32();
    if (!r.ok() || count < 0 || size_t(count) > r.remaining())
        return fail("actors");
    for (int32_t i = 0; i < count && r.ok(); ++i)
        out.actors.push_back(r.index());
    r.string(v); // protocol
    r.string(v); // host
    out.urlMap = r.string(v);
    out.urlPortal = r.string(v);
    for (int32_t n = r.index(); n > 0 && r.ok(); --n)
        r.string(v); // options
    r.i32();         // port
    r.i32();         // valid
    r.index();       // model
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.i32();
        r.index();
        r.index();
        r.i32();
        r.i32();
        r.i32();
        r.u8();
    }
    if (!r.ok())
        return fail("URL and reach list");
    r.f32();   // its time
    r.index(); // first deleted
    for (int k = 0; k < 16; ++k)
        r.index(); // text blocks
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.string(v);
        r.string(v);
    }
    if (v < 69 || !r.ok())
        return r.ok() ? Error::success() : fail("travel information");
    for (int32_t n = r.index(); n > 0 && r.ok(); --n)
        out.ghosts.push_back(r.index());
    if (v < 70 || !r.ok())
        return r.ok() ? Error::success() : fail("ghosts");
    // The dialogue engine: whether it's set up; its missions, types and
    // speakers (an id and a name each); its lines; two lists of what's
    // under way; then (from 71) its faces, its cameras and a count.
    r.i32();
    auto nameTable = [&] {
        for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
            r.u16();
            r.string(v);
        }
    };
    for (int t = 0; t < 3; ++t)
        nameTable();
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        LevelExtras::Line line;
        uint16_t fields[7];
        for (uint16_t& f : fields)
            f = r.u16();
        line.said = fields[2];
        r.string(v); // voice
        line.key = r.string(v);
        r.string(v); // text
        r.f32();     // pause
        r.string(v); // next
        r.string(v); // return
        r.index();   // tag
        r.string(v); // event
        r.string(v); // condition
        if (v > 70) {
            r.index();
            r.u16(); // camera
            for (int k = 0; k < 4; ++k)
                r.index();
        }
        out.lines.push_back(std::move(line));
    }
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.u16();
        r.u16();
        r.i32();
        r.index();
        if (v > 71)
            r.i32();
    }
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.u16();
        r.index();
        if (v > 71)
            r.i32();
    }
    if (v > 70) {
        nameTable(); // faces
        for (int32_t n = r.index(); n > 0 && r.ok(); --n)
            r.skip(13 * 4); // cameras
        r.i32();
    }
    if (!r.ok())
        return fail("dialogue");
    // The sounds playing: the actor, the driver's id, the sound's path, its
    // volume, radius and pitch, and when it started.
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        LevelExtras::Sound s;
        s.actor = r.index();
        s.id = r.i32();
        s.path = r.string(v);
        s.volume = r.f32();
        s.radius = r.f32();
        s.pitch = r.f32();
        s.start = r.f32();
        out.sounds.push_back(std::move(s));
    }
    return r.ok() ? Error::success() : fail("sounds");
}

// A C3DMESHSTATE: no properties; a magic number (0xFFFFDE80), its format
// (2) and the C3D library's version (22); the mesh's name; an i32; the IK
// targets; an i32; then the groups, the bones' adjustments and what's
// attached, and the owner's name. Its strings are a 16-bit length and the
// letters (no NUL); its arrays a count, the count again and a growth step.
Error readMeshState(const Package& package, int exportIndex, MeshState& out) {
    out = {};
    std::vector<uint8_t> data;
    if (Error err = package.readExport(exportIndex, data))
        return err;
    Reader r(data, size_t(package.exports()[size_t(exportIndex)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, exportIndex, r, props))
        return err;
    auto str16 = [&] {
        const uint16_t n = r.u16();
        std::string s(n, '\0');
        if (n && !r.bytes(s.data(), n))
            s.clear();
        return s;
    };
    auto array = [&] {
        const int32_t n = r.i32();
        r.i32();
        r.i32();
        return r.ok() && n >= 0 ? n : 0;
    };
    if (r.u32() != 0xFFFFDE80u)
        return Error::fail("a C3D mesh state of the older format");
    r.i32(); // format
    r.i32(); // library version
    out.mesh = str16();
    r.i32();
    for (int32_t n = array(); n > 0 && r.ok(); --n) { // IK targets
        r.i32();
        str16();
        r.i32();
        str16();
        r.skip(24);
    }
    r.i32();
    for (int32_t n = array(); n > 0 && r.ok(); --n) {
        MeshStateGroup g;
        g.group = str16();
        g.sequence = str16();
        g.source = r.i32();
        g.frame = r.f32();
        g.type = r.i32();
        g.state = r.i32();
        g.rate = r.f32();
        g.weight = r.f32();
        g.fromWeight = r.f32();
        g.fromFrame = r.f32();
        g.elapsed = r.f32();
        r.f32();
        g.duration = r.f32();
        r.f32();
        g.pendingSequence = str16();
        g.pendingFrame = r.f32();
        g.pendingWeight = r.f32();
        g.pendingTween = r.f32();
        g.bone = str16();
        for (int32_t& c : g.clear)
            c = r.i32();
        out.groups.push_back(std::move(g));
    }
    return r.ok() ? Error::success() : Error::fail("a C3D mesh state cut short");
}

bool meshStateGroupSpan(const std::vector<uint8_t>& data, size_t& begin, size_t& end) {
    Reader r(data);
    r.index(); // no properties
    auto str16 = [&] { r.skip(r.u16()); };
    auto array = [&] {
        const int32_t n = r.i32();
        r.i32();
        r.i32();
        return r.ok() && n >= 0 ? n : 0;
    };
    if (r.u32() != 0xFFFFDE80u)
        return false;
    r.i32();
    r.i32();
    str16();
    r.i32();
    for (int32_t n = array(); n > 0 && r.ok(); --n) {
        r.i32();
        str16();
        r.i32();
        str16();
        r.skip(24);
    }
    r.i32();
    begin = r.pos();
    for (int32_t n = array(); n > 0 && r.ok(); --n) {
        str16();
        str16();
        r.skip(48);
        str16();
        r.skip(12);
        str16();
        r.skip(12);
    }
    end = r.pos();
    return r.ok();
}

bool levelModelSpan(const std::vector<uint8_t>& data, int version, size_t& begin, size_t& end) {
    Reader r(data);
    r.index(); // its properties: none, the None that ends them
    const int32_t count = r.i32();
    r.i32();
    for (int32_t i = 0; i < count && r.ok(); ++i)
        r.index();
    for (int k = 0; k < 4; ++k)
        r.string(version);
    for (int32_t n = r.index(); n > 0 && r.ok(); --n)
        r.string(version);
    r.i32();
    r.i32();
    begin = r.pos();
    r.index();
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.i32();
        r.index();
        r.index();
        r.i32();
        r.i32();
        r.i32();
        r.u8();
    }
    end = r.pos();
    return r.ok();
}

} // namespace reluva::unreal
