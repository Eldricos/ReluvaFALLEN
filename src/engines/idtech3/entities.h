// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// One entity from a map's entity lump: its key/value pairs in file order.
// Keys are matched ignoring case; a repeated key keeps every value.
struct Entity {
    std::vector<std::pair<std::string, std::string>> pairs;

    // The first value for the key, or `fallback`.
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    bool has(std::string_view key) const;
    std::string classname() const { return get("classname"); }
    // "x y z" as three floats; false if the key is missing or malformed.
    bool vector(std::string_view key, float out[3]) const;
};

// Parse the entity lump: { "key" "value" ... } blocks. See
// docs/formats/idtech3/bsp.md.
Error parseEntities(std::string_view text, std::vector<Entity>& out);

} // namespace reluva::idtech3
