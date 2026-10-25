// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva {

// A .zip archive (also .pk3 and the like), read from its central directory.
// Files stored uncompressed or with deflate are supported, checked against
// their CRC-32 when read; encrypted entries, multi-disk and Zip64 archives
// are not. Folder entries are left out of list().
class ZipArchive : public Archive {
public:
    struct Entry {
        std::string name;         // as stored
        uint16_t method = 0;      // 0 stored, 8 deflate
        uint32_t crc = 0;
        uint32_t compressedSize = 0;
        uint32_t size = 0;
        uint32_t localHeader = 0; // file offset of the local header
    };

    Error open(const std::filesystem::path& path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t>& out) const override;
    std::string describe() const override { return path_.string(); }

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* entry(std::string_view name) const;
    Error read(const Entry& entry, std::vector<uint8_t>& out) const;

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byName_; // normalized name -> entries_ index
};

// CRC-32 as zip uses it (polynomial 0xedb88320), continuing from `crc`.
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

// Inflate raw deflate data (no zlib header) whose size is known. Fails if
// the data is damaged or doesn't come to exactly `size` bytes.
Error inflateRaw(const uint8_t* data, size_t dataSize, size_t size, std::vector<uint8_t>& out);

} // namespace reluva
