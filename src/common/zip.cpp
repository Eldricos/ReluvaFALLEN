// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout from PKWARE's published APPNOTE.TXT (the .ZIP file format
// specification), sections 4.3.7 (local file header), 4.3.12 (central
// directory header) and 4.3.16 (end of central directory record).

#include "common/zip.h"

#include "stb_image.h" // inflate only; image.cpp holds the implementation

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace reluva {

namespace {

constexpr uint32_t kLocalSig = 0x04034b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kEndSig = 0x06054b50;
constexpr size_t kLocalSize = 30;
constexpr size_t kCentralSize = 46;
constexpr size_t kEndSize = 22;
constexpr size_t kMaxComment = 0xffff;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAt(std::ifstream& in, uint64_t offset, uint8_t* out, size_t size) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size)));
}

} // namespace

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

Error inflateRaw(const uint8_t* data, size_t dataSize, size_t size, std::vector<uint8_t>& out) {
    constexpr size_t kMax = static_cast<size_t>(std::numeric_limits<int>::max());
    if (dataSize > kMax || size > kMax)
        return Error::fail("Compressed data too large");
    out.resize(size);
    if (size == 0)
        return Error::success();
    const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), static_cast<int>(size),
                                                     reinterpret_cast<const char*>(data), static_cast<int>(dataSize));
    if (got < 0 || static_cast<size_t>(got) != size)
        return Error::fail("Damaged deflate data");
    return Error::success();
}

Error ZipArchive::open(const std::filesystem::path& path) {
    path_ = path;
    entries_.clear();
    byName_.clear();
    const std::string where = path.string();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return Error::fail("Cannot open " + where);
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    if (fileSize < kEndSize)
        return Error::fail(where + " is too small to be a zip archive");

    // The end record is the last thing in the file, followed only by a
    // comment of up to 64 KiB: search backwards for its signature.
    const size_t tail = static_cast<size_t>(std::min<uint64_t>(fileSize, kEndSize + kMaxComment));
    std::vector<uint8_t> buf(tail);
    if (!readAt(in, fileSize - tail, buf.data(), tail))
        return Error::fail("Cannot read " + where);
    const uint8_t* end = nullptr;
    for (size_t i = tail - kEndSize + 1; i-- > 0;) {
        if (le32(&buf[i]) == kEndSig && i + kEndSize + le16(&buf[i + 20]) <= tail) {
            end = &buf[i];
            break;
        }
    }
    if (!end)
        return Error::fail(where + " is not a zip archive (no end of central directory)");
    if (le16(end + 4) != 0 || le16(end + 6) != 0 || le16(end + 8) != le16(end + 10))
        return Error::fail(where + ": multi-disk zip archives are not supported");
    const uint16_t count = le16(end + 10);
    const uint32_t dirSize = le32(end + 12);
    const uint32_t dirOffset = le32(end + 16);
    if (count == 0xffff || dirSize == 0xffffffff || dirOffset == 0xffffffff)
        return Error::fail(where + ": Zip64 archives are not supported");
    if (uint64_t(dirOffset) + dirSize > fileSize)
        return Error::fail(where + ": central directory runs past the end of the file");

    std::vector<uint8_t> dir(dirSize);
    if (dirSize && !readAt(in, dirOffset, dir.data(), dirSize))
        return Error::fail("Cannot read " + where);
    size_t at = 0;
    for (uint16_t n = 0; n < count; ++n) {
        if (at + kCentralSize > dir.size() || le32(&dir[at]) != kCentralSig)
            return Error::fail(where + ": damaged central directory");
        const uint8_t* h = &dir[at];
        const uint16_t flags = le16(h + 8);
        const size_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
        if (at + kCentralSize + nameLen + extraLen + commentLen > dir.size())
            return Error::fail(where + ": damaged central directory");
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(h + kCentralSize), nameLen);
        e.method = le16(h + 10);
        e.crc = le32(h + 16);
        e.compressedSize = le32(h + 20);
        e.size = le32(h + 24);
        e.localHeader = le32(h + 42);
        at += kCentralSize + nameLen + extraLen + commentLen;

        if (e.name.empty() || e.name.back() == '/')
            continue; // a folder
        if (flags & 1)
            return Error::fail(where + ": " + e.name + " is encrypted");
        if (uint64_t(e.localHeader) + kLocalSize + e.compressedSize > fileSize)
            return Error::fail(where + ": " + e.name + " runs past the end of the file");
        // The first entry of a name wins, as zip tools disagree otherwise.
        if (byName_.try_emplace(normalizeArchiveName(e.name), entries_.size()).second)
            entries_.push_back(std::move(e));
    }
    return Error::success();
}

std::vector<std::string> ZipArchive::list() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const Entry& e : entries_)
        names.push_back(e.name);
    return names;
}

const ZipArchive::Entry* ZipArchive::entry(std::string_view name) const {
    auto it = byName_.find(normalizeArchiveName(name));
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

bool ZipArchive::has(std::string_view name) const { return entry(name) != nullptr; }

Error ZipArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    const Entry* e = entry(name);
    if (!e)
        return Error::fail(std::string(name) + " not found in " + path_.string());
    return read(*e, out);
}

Error ZipArchive::read(const Entry& e, std::vector<uint8_t>& out) const {
    const std::string where = path_.string() + ": " + e.name;
    // Opened per read so a const archive can be read from several threads.
    std::ifstream in(path_, std::ios::binary);
    uint8_t local[kLocalSize];
    if (!in || !readAt(in, e.localHeader, local, kLocalSize) || le32(local) != kLocalSig)
        return Error::fail(where + ": damaged local header");
    // The local header's name and extra field can differ in length from the
    // central directory's; the data follows the local ones.
    const uint64_t dataAt = uint64_t(e.localHeader) + kLocalSize + le16(local + 26) + le16(local + 28);
    std::vector<uint8_t> raw(e.compressedSize);
    if (e.compressedSize && !readAt(in, dataAt, raw.data(), raw.size()))
        return Error::fail(where + ": truncated");

    if (e.method == 0) {
        if (e.compressedSize != e.size)
            return Error::fail(where + ": stored sizes disagree");
        out = std::move(raw);
    } else if (e.method == 8) {
        if (Error err = inflateRaw(raw.data(), raw.size(), e.size, out))
            return Error::fail(where + ": " + err.message);
    } else {
        return Error::fail(where + ": unsupported compression method " + std::to_string(e.method));
    }
    if (crc32(out.data(), out.size()) != e.crc)
        return Error::fail(where + ": CRC mismatch");
    return Error::success();
}

} // namespace reluva
