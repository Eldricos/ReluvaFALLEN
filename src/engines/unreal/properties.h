// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// An object's data starts with its properties: the values that differ
// from its class's defaults, each tagged with its name and type, ending at
// the name "None". What follows is the class's own data (a texture's
// pixels, a level's geometry). See docs/formats/unreal/package.md.

#include "engines/unreal/package.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::unreal {

enum class PropertyType : uint8_t {
    None = 0, Byte = 1, Int = 2, Bool = 3, Float = 4, Object = 5, Name = 6, String = 7,
    Class = 8, Array = 9, Struct = 10, Vector = 11, Rotator = 12, Str = 13, Map = 14, FixedArray = 15,
};
const char* typeName(PropertyType type);

struct Property {
    std::string name;
    PropertyType type = PropertyType::None;
    int arrayIndex = 0;
    std::string structName; // Struct properties: the struct's name ("Vector", "Rotator"...)
    bool boolValue = false; // Bool properties keep their value in the tag
    std::vector<uint8_t> data;
};

struct Rotator {
    int32_t pitch = 0, yaw = 0, roll = 0; // 65536 to a full turn
};

class PropertyList {
public:
    std::vector<Property> items;

    const Property* find(std::string_view name, int arrayIndex = 0) const;

    // A property's value read as the type asked for; `fallback` when it's
    // missing (the class's default, which packages don't store) or too short.
    int32_t integer(std::string_view name, int32_t fallback = 0, int arrayIndex = 0) const;
    float number(std::string_view name, float fallback = 0, int arrayIndex = 0) const;
    uint8_t byte(std::string_view name, uint8_t fallback = 0, int arrayIndex = 0) const;
    bool boolean(std::string_view name, bool fallback = false, int arrayIndex = 0) const;
    // Object and Class properties: an object reference into the package.
    int32_t object(std::string_view name, int arrayIndex = 0) const;
    // Name properties: the name table index.
    int32_t nameIndex(std::string_view name, int32_t fallback = -1, int arrayIndex = 0) const;
    // Struct properties holding a Vector (or Vector properties).
    bool vector(std::string_view name, std::array<float, 3>& out, int arrayIndex = 0) const;
    bool rotator(std::string_view name, Rotator& out, int arrayIndex = 0) const;
    // Str and String properties.
    std::string string(std::string_view name, int version, int arrayIndex = 0) const;
};

// Reads tagged properties up to "None".
Error readProperties(const Package& package, Reader& r, PropertyList& out);

// Reads the start of an export's data up to the class's own data: its
// state frame, if it has one, and its properties. Classes (whose data is
// laid out differently) are refused.
Error beginObject(const Package& package, int exportIndex, Reader& r, PropertyList& out);

// A property's value as text, for viewers and tools.
std::string describe(const Package& package, const Property& p);

} // namespace reluva::unreal
