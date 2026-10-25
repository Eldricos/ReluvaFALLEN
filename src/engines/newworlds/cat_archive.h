// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::newworlds {

// Chunk tags in .cat archives. See docs/formats/newworlds/cat.md.
namespace cattag {
constexpr uint16_t Group = 0x00;      // container, purpose unknown
constexpr uint16_t Root = 0x01;
constexpr uint16_t Table = 0x05;
constexpr uint16_t Gmf = 0x11;        // .gmf
constexpr uint16_t Texture = 0x16;    // .lbp .mp0-.mp4 .eb0 .eb1 .fbp
constexpr uint16_t Geometry = 0x17;   // .geo
constexpr uint16_t Arf = 0x25;        // .arf
constexpr uint16_t Fif = 0x26;        // .fif glyph table
constexpr uint16_t FontPart = 0x27;   // container inside FontSet: a .fif and its .fbp image
constexpr uint16_t FontSet = 0x28;    // container: one font
constexpr uint16_t TextureSet = 0x29; // container: .mmb + texture levels
constexpr uint16_t Mmb = 0x2a;        // .mmb
constexpr uint16_t Mdl = 0x2c;        // .mdl
constexpr uint16_t Strings = 0x32;    // .stb
constexpr uint16_t Wave = 0x33;       // .wav
} // namespace cattag

bool isContainerTag(uint16_t tag);

struct CatEntry {
    std::string name;         // normalized: lower case, '/' separators, build prefix removed
    std::string originalPath; // as stored, e.g. c:\prjs\game\stc\bin\rsc\dat\pc_win32\str\eng\miss\rom01.stb
    uint16_t tag = 0;
    uint32_t size = 0;        // payload size
    uint64_t offset = 0;      // absolute file position of the payload
    int parent = -1;          // index of the containing container entry, or -1
    bool container = false;
    uint32_t modified = 0;    // Unix time from the stored metadata
};

class CatArchive {
public:
    Error open(const std::filesystem::path& path);

    const std::vector<CatEntry>& entries() const { return entries_; }
    const std::filesystem::path& path() const { return path_; }
    // Look up a file by normalized name, ignoring case and slash direction.
    const CatEntry* find(std::string_view name) const;
    Error read(const CatEntry& entry, std::vector<uint8_t>& out);

    // "c:\prjs\game\stc\bin\rsc\dat\pc_win32\str\eng\miss\rom01.stb" -> "str/eng/miss/rom01.stb"
    static std::string normalizeName(std::string_view originalPath);

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<CatEntry> entries_;
    std::unordered_map<std::string, size_t> byName_;
};

} // namespace reluva::newworlds
