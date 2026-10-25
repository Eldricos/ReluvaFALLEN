// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/gmf.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace reluva::newworlds {

namespace {

constexpr uint16_t kTileLinks = 21; // record types, in the order they're stored
constexpr uint16_t kTile = 20;
constexpr uint16_t kBlock = 19;
constexpr size_t kGridOffset = 18;  // in a type-20 body, after its header values
constexpr size_t kCellEntrySize = 10;
constexpr size_t kPaletteSize = 1024;

struct Reader {
    const std::vector<uint8_t>& d;
    bool has(size_t at, size_t bytes) const { return at <= d.size() && bytes <= d.size() - at; }
    uint16_t u16(size_t o) const { return uint16_t(d[o] | d[o + 1] << 8); }
    uint32_t u32(size_t o) const {
        return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    }
};

std::string hex(size_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%zx", v);
    return buf;
}

} // namespace

// Layout: u16 counts of type-21, type-20 and type-19 records, u16 width and
// height in blocks; the records (u16 type, u16 length, u16 0, body), all
// type 21, then 20, then 19; u32 N and N u32 list entries (placed objects,
// each cell's list ended by ffffffff); one 10-byte entry per grid cell (u16
// block, u16 ?, u16 ?, u32 where its list starts); a 1024-byte colour table (u32 B, G, R, 0
// per terrain code, ending with a zero entry).
Error parseGmf(const std::vector<uint8_t>& data, GmfMap& out) {
    out = GmfMap{};
    const Reader r{data};
    if (!r.has(0, 10))
        return Error::fail("map too small");
    const uint16_t counts[3] = {r.u16(0), r.u16(2), r.u16(4)};
    out.width = r.u16(6);
    out.height = r.u16(8);
    if (counts[0] != counts[1])
        return Error::fail("map has " + std::to_string(counts[0]) + " type-21 but " + std::to_string(counts[1]) +
                           " type-20 records");
    const uint16_t types[3] = {kTileLinks, kTile, kBlock};
    size_t p = 10;
    for (int k = 0; k < 3; ++k) {
        for (unsigned i = 0; i < counts[k]; ++i) {
            if (!r.has(p, 6) || r.u16(p) != types[k] || !r.has(p + 6, r.u16(p + 2)))
                return Error::fail("type-" + std::to_string(types[k]) + " record " + std::to_string(i) + " at " +
                                   hex(p) + " doesn't fit");
            const size_t body = p + 6, length = r.u16(p + 2);
            if (types[k] == kTile) {
                if (length < kGridOffset + 64 || r.u32(body) != i)
                    return Error::fail("tile record " + std::to_string(i) + " at " + hex(p) + " is malformed");
                GmfMap::Tile t;
                t.model = r.u32(body + 8);
                t.flags = r.u32(body + 12);
                t.rotation = int(t.flags >> 14 & 3);
                t.underlay = (t.flags >> 24 & 1) != 0;
                const size_t layerBytes = std::min<size_t>(t.layers.size(), length - kGridOffset) / 64 * 64;
                std::memcpy(t.layers.data(), &data[body + kGridOffset], layerBytes);
                out.tiles.push_back(t);
            } else if (types[k] == kBlock) {
                if (length < 22)
                    return Error::fail("block record " + std::to_string(i) + " at " + hex(p) + " is too short");
                std::array<uint32_t, 4> b{};
                for (int c = 0; c < 4; ++c) {
                    b[size_t(c)] = r.u32(body + 6 + 4 * size_t(c));
                    if (b[size_t(c)] >= counts[1])
                        return Error::fail("block " + std::to_string(i) + " uses tile " + std::to_string(b[size_t(c)]) +
                                           " of " + std::to_string(counts[1]));
                }
                out.blocks.push_back(b);
            }
            p = body + length;
        }
    }

    // The lists of placed objects, then the grid.
    if (!r.has(p, 4))
        return Error::fail("map ends before its lists");
    const size_t lists = r.u32(p);
    p += 4;
    if (lists > data.size() / 4 || !r.has(p, lists * 4))
        return Error::fail("map's lists run past its end");
    const size_t listStart = p;
    p += lists * 4;
    const size_t cellCount = size_t(out.width) * size_t(out.height);
    if (!r.has(p, cellCount * kCellEntrySize))
        return Error::fail("map ends inside its grid at " + hex(p));
    for (size_t i = 0; i < cellCount; ++i, p += kCellEntrySize) {
        GmfMap::Cell c{r.u16(p), r.u16(p + 2)};
        if (c.block >= out.blocks.size())
            return Error::fail("grid cell " + std::to_string(i) + " uses block " + std::to_string(c.block) + " of " +
                               std::to_string(out.blocks.size()));
        out.cells.push_back(c);
        // The cell's list of objects, ended by ffffffff.
        size_t at = r.u32(p + 6);
        if (at >= lists)
            return Error::fail("grid cell " + std::to_string(i) + "'s list starts at " + std::to_string(at) + " of " +
                               std::to_string(lists));
        for (uint32_t e; at < lists && (e = r.u32(listStart + 4 * at)) != 0xffffffff; ++at) {
            GmfMap::Scenery o;
            o.bx = uint16_t(i % size_t(out.width));
            o.by = uint16_t(i / size_t(out.width));
            o.x = uint8_t(e & 15);
            o.y = uint8_t(e >> 4 & 15);
            o.extra = uint8_t(e >> 8 & 63);
            o.variant = uint8_t(e >> 14 & 3);
            o.type = uint8_t(e >> 16 & 7);
            o.turn = uint8_t(e >> 19 & 15);
            out.scenery.push_back(o);
        }
    }
    for (size_t i = 0; i < kPaletteSize / 4 && r.has(p, 4); ++i, p += 4) {
        if (i > 0 && r.u32(p) == 0)
            break;
        out.palette.push_back({data[p + 2], data[p + 1], data[p]});
    }
    return Error::success();
}

int tileAt(const GmfMap& map, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= 2 * map.width || ty >= 2 * map.height)
        return -1;
    static constexpr int kSlotAt[2][2] = {{0, 3}, {1, 2}}; // [x][y] within the block
    const auto& block = map.blocks[map.cells[size_t(ty / 2) * size_t(map.width) + size_t(tx / 2)].block];
    return int(block[size_t(kSlotAt[tx % 2][ty % 2])]);
}

std::vector<size_t> tileModelOrder(const std::vector<std::string>& geoNames) {
    auto stem = [](const std::string& name) {
        const size_t slash = name.rfind('/'), dot = name.rfind('.');
        const size_t from = slash == std::string::npos ? 0 : slash + 1;
        return name.substr(from, (dot == std::string::npos || dot < from ? name.size() : dot) - from);
    };
    auto isLod = [](const std::string& name) { return name.find("/lod/") != std::string::npos; };
    // The base family: the one with a model numbered _26 (ice_26, alpin_26,
    // vol_26; alien_ goes on to 29).
    std::string base;
    for (const auto& n : geoNames) {
        const std::string s = stem(n);
        if (!isLod(n) && s.size() > 3 && s.compare(s.size() - 3, 3, "_26") == 0)
            base = s.substr(0, s.size() - 3);
    }
    // The fixed places, each with the names that can fill it ("" for none).
    // Found by fitting every map of all six planets: each tile's terrain
    // layers trace its model's height, and neighbouring tiles' edges meet
    // (99% of edges, up from 90% with the earlier table; see gmf.md). The
    // flat models (base 17-20 and 23-26, concrete, hole_02) can't be told
    // apart that way, so their places are less sure.
    std::vector<std::vector<std::string>> places;
    char number[8];
    auto baseTile = [&](int i) {
        std::snprintf(number, sizeof number, "_%02d", i);
        return base + number;
    };
    for (int i = 1; i <= 16; ++i)
        places.push_back({baseTile(i)});              // 0-15
    places.push_back({"hole_01"});                    // 16: a plateau at the top level
    places.push_back({baseTile(21)});                 // 17: not used by any map seen
    places.push_back({baseTile(22)});                 // 18: the third corner slope
    places.push_back({baseTile(20)});                 // 19
    places.push_back({baseTile(18)});                 // 20
    for (const char* n : {"crack_03", "crack_04", "crack_05", "crack_06", "", "crack_08"})
        places.push_back({n});                        // 21-26: crack_0N at 18 + N
    for (const char* n : {"brid_01", "brid_02", "ramp", "concrete", "hole_03", "hole_02", "concact"})
        places.push_back({n});                        // 27-33
    places.push_back({baseTile(19)});                 // 34
    for (int i = 23; i <= 26; ++i)
        places.push_back({baseTile(i)});              // 35-38
    places.push_back({baseTile(17)});                 // 39
    places.push_back({""});                           // 40: flat ground, model unknown
    for (const char* n : {"ball_01", "ball_02", "ball_03"})
        places.push_back({n});                        // 41-43 (alnwrld)
    places.push_back({"water_01"});                   // 44-47: alpwrld's water; alnwrld's
    places.push_back({"water_02", baseTile(27)});     // shallow alien_27-29 (their order
    places.push_back({"water_03", baseTile(28)});     // isn't sure)
    places.push_back({"water_04", baseTile(29)});
    places.push_back({"mount_01"});                   // 48-49
    places.push_back({"mount_02"});
    places.push_back({"arche_01"});                   // 50-51 (deswrld)
    places.push_back({"needle_0"});
    places.push_back({"icecave1", "lavafall"});       // 52 (icewrld, vlcwrld)
    places.push_back({"icecave2"});                   // 53
    places.push_back({"finger_1"});                   // 54 (redwrld)

    std::vector<size_t> order;
    std::vector<bool> used(geoNames.size());
    for (const auto& names : places) {
        size_t found = kNoModel;
        for (size_t i = 0; i < geoNames.size() && found == kNoModel && !base.empty(); ++i)
            for (const auto& n : names)
                if (!used[i] && !isLod(geoNames[i]) && stem(geoNames[i]) == n)
                    found = i;
        if (found != kNoModel)
            used[found] = true;
        order.push_back(found);
    }
    // Then the lod/ models, and last the rest (placed objects' models: tree,
    // rock, ...), which no map's tiles use.
    for (int lod = 1; lod >= 0; --lod)
        for (size_t i = 0; i < geoNames.size(); ++i)
            if (!used[i] && isLod(geoNames[i]) == bool(lod))
                order.push_back(i);
    return order;
}

} // namespace reluva::newworlds
