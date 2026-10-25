// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/string_table.h"
#include "common/text.h"

namespace reluva::newworlds {

static uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

Error StringTable::parse(const std::vector<uint8_t>& data) {
    strings_.clear();
    if (data.size() < 4)
        return Error::fail("string table too small");
    const uint32_t count = rd32(data, 0);
    size_t pos = 4;
    std::vector<uint16_t> units;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 4 > data.size())
            return Error::fail("string table ends before string " + std::to_string(i) + " of " +
                               std::to_string(count));
        const uint32_t id = rd32(data, pos);
        pos += 4;
        units.clear();
        bool terminated = false;
        while (pos + 2 <= data.size()) {
            const uint16_t u = uint16_t(data[pos] | data[pos + 1] << 8);
            pos += 2;
            if (u == 0) {
                terminated = true;
                break;
            }
            units.push_back(u);
        }
        if (!terminated)
            return Error::fail("string " + std::to_string(i) + " (id " + std::to_string(id) + ") is not terminated");
        strings_.push_back({id, text::utf16leToUtf8(units.data(), units.size())});
    }
    if (pos != data.size())
        return Error::fail("string table has " + std::to_string(data.size() - pos) + " bytes after its last string");
    return Error::success();
}

const std::string* StringTable::find(uint32_t id) const {
    for (const String& s : strings_)
        if (s.id == id)
            return &s.text;
    return nullptr;
}

} // namespace reluva::newworlds
