// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/fs.h"
#include "common/md5.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace reluva::fs {

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static std::optional<stdfs::path> findEntry(const stdfs::path& dir, const std::string& lowerName) {
    std::error_code ec;
    for (stdfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (toLower(it->path().filename().string()) == lowerName)
            return it->path();
    }
    return std::nullopt;
}

static std::optional<stdfs::path> findPath(const stdfs::path& dir, std::string_view relative) {
    stdfs::path current = dir;
    size_t start = 0;
    while (start <= relative.size()) {
        size_t slash = relative.find('/', start);
        if (slash == std::string_view::npos)
            slash = relative.size();
        std::string_view part = relative.substr(start, slash - start);
        if (!part.empty()) {
            auto found = findEntry(current, toLower(part));
            if (!found)
                return std::nullopt;
            current = *found;
        }
        start = slash + 1;
    }
    return current;
}

std::optional<stdfs::path> findFile(const stdfs::path& dir, std::string_view relative) {
    auto found = findPath(dir, relative);
    std::error_code ec;
    if (!found || !stdfs::is_regular_file(*found, ec))
        return std::nullopt;
    return found;
}

std::optional<stdfs::path> findDirectory(const stdfs::path& dir, std::string_view relative) {
    auto found = findPath(dir, relative);
    std::error_code ec;
    if (!found || !stdfs::is_directory(*found, ec))
        return std::nullopt;
    return found;
}

std::optional<std::string> md5File(const stdfs::path& file, uint64_t maxBytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;

    Md5 md5;
    std::vector<char> buf(64 * 1024);
    uint64_t remaining = maxBytes ? maxBytes : UINT64_MAX;
    while (remaining > 0 && in) {
        auto want = static_cast<std::streamsize>(std::min<uint64_t>(buf.size(), remaining));
        in.read(buf.data(), want);
        std::streamsize got = in.gcount();
        if (got <= 0)
            break;
        md5.update(buf.data(), static_cast<size_t>(got));
        remaining -= static_cast<uint64_t>(got);
    }
    if (in.bad())
        return std::nullopt;
    return md5.hexDigest();
}

} // namespace reluva::fs
