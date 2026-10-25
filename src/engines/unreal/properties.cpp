// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/properties.h"

#include "common/fs.h"

#include <cstdio>
#include <cstring>

namespace reluva::unreal {

const char* typeName(PropertyType type) {
    static const char* const kNames[] = {"None", "Byte",  "Int",    "Bool",  "Float",   "Object", "Name", "String",
                                         "Class", "Array", "Struct", "Vector", "Rotator", "Str",    "Map",  "FixedArray"};
    return kNames[static_cast<int>(type) & 15];
}

const Property* PropertyList::find(std::string_view name, int arrayIndex) const {
    const std::string wanted = fs::toLower(name);
    for (const Property& p : items)
        if (p.arrayIndex == arrayIndex && fs::toLower(p.name) == wanted)
            return &p;
    return nullptr;
}

namespace {

Reader dataReader(const Property& p) { return Reader(p.data); }

} // namespace

int32_t PropertyList::integer(std::string_view name, int32_t fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.size() < 4)
        return fallback;
    Reader r = dataReader(*p);
    return r.i32();
}

float PropertyList::number(std::string_view name, float fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.size() < 4)
        return fallback;
    Reader r = dataReader(*p);
    return r.f32();
}

uint8_t PropertyList::byte(std::string_view name, uint8_t fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    return p && !p->data.empty() ? p->data[0] : fallback;
}

bool PropertyList::boolean(std::string_view name, bool fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    return p && p->type == PropertyType::Bool ? p->boolValue : fallback;
}

int32_t PropertyList::object(std::string_view name, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.empty())
        return 0;
    Reader r = dataReader(*p);
    const int32_t v = r.index();
    return r.ok() ? v : 0;
}

int32_t PropertyList::nameIndex(std::string_view name, int32_t fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.empty())
        return fallback;
    Reader r = dataReader(*p);
    const int32_t v = r.index();
    return r.ok() ? v : fallback;
}

bool PropertyList::vector(std::string_view name, std::array<float, 3>& out, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.size() < 12)
        return false;
    Reader r = dataReader(*p);
    for (float& f : out)
        f = r.f32();
    return true;
}

bool PropertyList::rotator(std::string_view name, Rotator& out, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p || p->data.size() < 12)
        return false;
    Reader r = dataReader(*p);
    out.pitch = r.i32();
    out.yaw = r.i32();
    out.roll = r.i32();
    return true;
}

std::string PropertyList::string(std::string_view name, int version, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    if (!p)
        return {};
    if (p->type == PropertyType::String) {
        // A fixed-size buffer, NUL-terminated.
        std::string s(p->data.begin(), p->data.end());
        return s.substr(0, s.find('\0'));
    }
    Reader r = dataReader(*p);
    return r.string(version);
}

Error readProperties(const Package& package, Reader& r, PropertyList& out) {
    out.items.clear();
    // Bounded, so a damaged object can't run away.
    for (int count = 0; count < 100000; ++count) {
        const int32_t nameIdx = r.index();
        if (!r.ok())
            return Error::fail("properties run past the end of the object");
        if (nameIdx < 0 || size_t(nameIdx) >= package.names().size())
            return Error::fail("property name " + std::to_string(nameIdx) + " isn't in the name table");
        const std::string& name = package.name(nameIdx);
        if (fs::toLower(name) == "none")
            return Error::success();

        // The info byte: type (bits 0-3), size code (bits 4-6), and bit 7:
        // a Bool's value, or else that an array index follows.
        const uint8_t info = r.u8();
        Property p;
        p.name = name;
        p.type = static_cast<PropertyType>(info & 15);
        if (p.type == PropertyType::Struct) {
            const int32_t s = r.index();
            p.structName = package.name(s);
        }
        size_t size = 0;
        switch ((info >> 4) & 7) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        case 2: size = 4; break;
        case 3: size = 12; break;
        case 4: size = 16; break;
        case 5: size = r.u8(); break;
        case 6: size = r.u16(); break;
        default: size = r.u32(); break;
        }
        if (p.type == PropertyType::Bool) {
            p.boolValue = (info & 0x80) != 0;
            size = 0;
        } else if (info & 0x80) {
            // 1, 2 or 4 bytes, told apart by the top bits of the first.
            const uint8_t b = r.u8();
            if ((b & 0x80) == 0)
                p.arrayIndex = b;
            else if ((b & 0xc0) == 0x80)
                p.arrayIndex = (b & 0x7f) << 8 | r.u8();
            else
                p.arrayIndex = int((uint32_t(b & 0x3f) << 24 | uint32_t(r.u8()) << 16 | uint32_t(r.u8()) << 8 | r.u8()));
        }
        if (!r.ok() || size > r.remaining())
            return Error::fail("property " + name + " runs past the end of the object");
        p.data.resize(size);
        r.bytes(p.data.data(), size);
        out.items.push_back(std::move(p));
    }
    return Error::fail("too many properties");
}

Error beginObject(const Package& package, int index, Reader& r, PropertyList& out) {
    if (index < 0 || size_t(index) >= package.exports().size())
        return Error::fail("no export " + std::to_string(index));
    const Export& e = package.exports()[size_t(index)];
    if (e.classRef == 0)
        return Error::fail(package.name(e.objectName) + " is a class");
    if (e.flags & objflag::HasStack) {
        // The state the object is in: its node and state node, the
        // events it ignores, its latent action, and where in the node's
        // code it is (only when it's in one).
        const int32_t node = r.index();
        r.index(); // state node
        r.u64();   // probe mask
        r.i32();   // latent action
        if (node != 0)
            r.index(); // code offset
    }
    if (Error err = readProperties(package, r, out))
        return Error::fail(package.name(e.objectName) + ": " + err.message);
    return Error::success();
}

std::string describe(const Package& package, const Property& p) {
    Reader r(p.data);
    char buf[128];
    switch (p.type) {
    case PropertyType::Bool:
        return p.boolValue ? "true" : "false";
    case PropertyType::Byte:
        return p.data.empty() ? "?" : std::to_string(p.data[0]);
    case PropertyType::Int:
        return std::to_string(r.i32());
    case PropertyType::Float:
        std::snprintf(buf, sizeof buf, "%g", double(r.f32()));
        return buf;
    case PropertyType::Object:
    case PropertyType::Class: {
        const int32_t ref = r.index();
        return ref == 0 ? "None" : package.className(ref) + "'" + package.path(ref) + "'";
    }
    case PropertyType::Name:
        return package.name(r.index());
    case PropertyType::Str:
        return "\"" + r.string(package.version()) + "\"";
    case PropertyType::String: {
        const std::string s(p.data.begin(), p.data.end());
        return "\"" + s.substr(0, s.find('\0')) + "\"";
    }
    default:
        break;
    }
    if ((p.type == PropertyType::Struct && (p.structName == "Vector" || p.structName == "Rotator")) ||
        p.type == PropertyType::Vector || p.type == PropertyType::Rotator) {
        const bool rot = p.structName == "Rotator" || p.type == PropertyType::Rotator;
        if (p.data.size() >= 12) {
            if (rot) {
                const int32_t a = r.i32(), b = r.i32(), c = r.i32();
                std::snprintf(buf, sizeof buf, "(Pitch=%d,Yaw=%d,Roll=%d)", a, b, c);
            } else {
                const float a = r.f32(), b = r.f32(), c = r.f32();
                std::snprintf(buf, sizeof buf, "(X=%g,Y=%g,Z=%g)", double(a), double(b), double(c));
            }
            return buf;
        }
    }
    return std::to_string(p.data.size()) + " bytes";
}

} // namespace reluva::unreal
