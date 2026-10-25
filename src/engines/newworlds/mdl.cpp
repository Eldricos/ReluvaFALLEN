// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/mdl.h"

#include <cctype>
#include <cmath>

namespace reluva::newworlds {

namespace {

constexpr size_t kNameSize = 32;
constexpr size_t kEntrySize = kNameSize + 4; // name, then a u32 value
constexpr uint32_t kMaxEntries = 4096;

struct Reader {
    const std::vector<uint8_t>& d;
    bool has(size_t at, size_t bytes) const { return at <= d.size() && bytes <= d.size() - at; }
    uint32_t u32(size_t o) const {
        return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    }
    std::string name(size_t o) const {
        std::string s;
        for (size_t i = 0; i < kNameSize && d[o + i]; ++i)
            s += char(d[o + i]);
        return s;
    }
    // A plausible name: 1-31 printable characters starting with a letter.
    bool isName(size_t o) const {
        if (!has(o, kNameSize))
            return false;
        const std::string s = name(o);
        if (s.empty() || s.size() >= kNameSize || !std::isalpha(static_cast<unsigned char>(s[0])))
            return false;
        for (char c : s)
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126)
                return false;
        return true;
    }
};

// Entries (name, u32 value) up to one named "terminate".
Error readEntries(const Reader& r, size_t& p, std::vector<MdlModel::Name>& out) {
    for (uint32_t i = 0; i <= kMaxEntries; ++i) {
        if (!r.has(p, kEntrySize))
            return Error::fail("model ends inside a name list");
        MdlModel::Name n{r.name(p), int32_t(r.u32(p + kNameSize))};
        p += kEntrySize;
        if (n.name == "terminate")
            return Error::success();
        out.push_back(std::move(n));
    }
    return Error::fail("name list without an end");
}

} // namespace

// Layout: char[32] title; four lists, each a u32 count and entries (char[32]
// name, u32 value) up to "terminate"; after the first list there can be
// leftovers (memory addresses) when its count is larger than the entries
// written, so the next list is found by its count and first name; then
// (usually) u32 0, the state count and the states as entries (their names;
// an unnamed state leaves the list empty).
Error parseMdl(const std::vector<uint8_t>& data, MdlModel& out) {
    out = MdlModel{};
    const Reader r{data};
    if (!r.has(0, kNameSize + 4))
        return Error::fail("model too small");
    out.title = r.name(0);
    size_t p = kNameSize;
    std::vector<MdlModel::Name>* lists[4] = {&out.parts, &out.points, &out.places, &out.animated};
    for (int t = 0; t < 4; ++t) {
        if (t == 1) {
            // Skip leftovers: find a count followed by a name or an empty list.
            size_t q = p;
            while (r.has(q, 4 + kEntrySize) && !(r.u32(q) <= kMaxEntries && r.isName(q + 4)))
                q += 4;
            if (!r.has(q, 4 + kEntrySize))
                return Error::fail("second name list not found after " + std::to_string(p));
            p = q;
        }
        if (!r.has(p, 4))
            return Error::fail("model ends before name list " + std::to_string(t));
        p += 4; // the count, which can include deleted entries
        if (Error err = readEntries(r, p, *lists[t]))
            return err;
    }
    // Usually a u32 0 before the state count (not in crewman or conbee).
    if (!r.isName(p + 4) && r.isName(p + 8))
        p += 4;
    if (!r.has(p, 4))
        return Error::fail("model ends before its states");
    const uint32_t stateCount = r.u32(p);
    p += 4;
    if (stateCount > kMaxEntries)
        return Error::fail("model has " + std::to_string(stateCount) + " states");
    // The states; the list ends at "terminate" or after the count (karazi
    // has no "terminate" after its one state).
    for (uint32_t i = 0; i < stateCount; ++i) {
        if (!r.has(p, kEntrySize))
            return Error::fail("model ends inside its states");
        const std::string name = r.name(p);
        p += kEntrySize;
        if (name == "terminate")
            break;
        out.states.push_back(name);
    }
    // Animated models: after the states, u32 count, pairs of numbers (part
    // and node?) and a few values, then the reference pose, one transform per
    // animated part; found by its first position chunk.
    if (!out.animated.empty()) {
        for (size_t q = p; r.has(q, kTransformSize * out.animated.size()); ++q) {
            PartTransform t;
            if (!readTransform(data, q, t) || std::fabs(t.scale - 1) > 0.01f)
                continue;
            for (size_t i = 0; i < out.animated.size(); ++i) {
                if (!readTransform(data, q + i * kTransformSize, t))
                    return Error::fail("reference pose at " + std::to_string(q) + " ends after " + std::to_string(i));
                out.reference.push_back(t);
            }
            break;
        }
    }
    return Error::success();
}

} // namespace reluva::newworlds
