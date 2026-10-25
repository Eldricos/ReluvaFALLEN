// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Builds small .zip files for tests: stored entries, or entries whose
// deflated bytes the test supplies.

#pragma once

#include "common/zip.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace reluva::test {

struct ZipFile {
    std::string name;
    std::vector<uint8_t> data;       // uncompressed contents
    std::vector<uint8_t> deflated;   // if not empty: stored with method 8 as these bytes
};

inline std::vector<uint8_t> toBytes(const std::string& s) { return {s.begin(), s.end()}; }

inline std::vector<uint8_t> buildZip(const std::vector<ZipFile>& files, const std::string& comment = {}) {
    std::vector<uint8_t> out, dir;
    auto put16 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(uint8_t(x));
        v.push_back(uint8_t(x >> 8));
    };
    auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) {
        put16(v, x & 0xffff);
        put16(v, x >> 16);
    };
    for (const ZipFile& f : files) {
        const bool deflate = !f.deflated.empty();
        const std::vector<uint8_t>& body = deflate ? f.deflated : f.data;
        const uint32_t crc = crc32(f.data.data(), f.data.size());
        const uint32_t offset = uint32_t(out.size());
        put32(out, 0x04034b50);
        put16(out, 20); put16(out, 0); put16(out, deflate ? 8 : 0); put16(out, 0); put16(out, 0);
        put32(out, crc); put32(out, uint32_t(body.size())); put32(out, uint32_t(f.data.size()));
        put16(out, uint32_t(f.name.size())); put16(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), body.begin(), body.end());

        put32(dir, 0x02014b50);
        put16(dir, 20); put16(dir, 20); put16(dir, 0); put16(dir, deflate ? 8 : 0); put16(dir, 0); put16(dir, 0);
        put32(dir, crc); put32(dir, uint32_t(body.size())); put32(dir, uint32_t(f.data.size()));
        put16(dir, uint32_t(f.name.size())); put16(dir, 0); put16(dir, 0); put16(dir, 0); put16(dir, 0);
        put32(dir, 0); put32(dir, offset);
        dir.insert(dir.end(), f.name.begin(), f.name.end());
    }
    const uint32_t dirOffset = uint32_t(out.size());
    out.insert(out.end(), dir.begin(), dir.end());
    put32(out, 0x06054b50);
    put16(out, 0); put16(out, 0); put16(out, uint32_t(files.size())); put16(out, uint32_t(files.size()));
    put32(out, uint32_t(dir.size())); put32(out, dirOffset); put16(out, uint32_t(comment.size()));
    out.insert(out.end(), comment.begin(), comment.end());
    return out;
}

inline void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

} // namespace reluva::test
