// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::newworlds {

// A .stb string table (tag 0x32). See docs/formats/newworlds/stb.md.
class StringTable {
public:
    struct String {
        uint32_t id;
        std::string text; // UTF-8
    };

    Error parse(const std::vector<uint8_t>& data);

    const std::vector<String>& strings() const { return strings_; }
    // Text for an id, or nullptr if there's none.
    const std::string* find(uint32_t id) const;

private:
    std::vector<String> strings_;
};

} // namespace reluva::newworlds
