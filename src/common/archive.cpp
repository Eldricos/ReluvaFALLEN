// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/archive.h"

#include <algorithm>
#include <fstream>
#include <map>

namespace reluva {

std::string normalizeArchiveName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '\\')
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c == '/' && (out.empty() || out.back() == '/'))
            continue; // leading or doubled separators
        out.push_back(c);
    }
    return out;
}

// --- DirectoryArchive ------------------------------------------------------------

Error DirectoryArchive::open(const std::filesystem::path& dir) {
    dir_ = dir;
    files_.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return Error::fail("Not a folder: " + dir.string());
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::string rel = it->path().lexically_relative(dir).generic_string();
        files_.emplace(normalizeArchiveName(rel), it->path());
    }
    if (ec)
        return Error::fail("Cannot list " + dir.string() + ": " + ec.message());
    return Error::success();
}

std::vector<std::string> DirectoryArchive::list() const {
    std::vector<std::string> names;
    names.reserve(files_.size());
    for (const auto& [key, path] : files_)
        names.push_back(path.lexically_relative(dir_).generic_string());
    return names;
}

bool DirectoryArchive::has(std::string_view name) const {
    return files_.count(normalizeArchiveName(name)) != 0;
}

Error DirectoryArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    auto it = files_.find(normalizeArchiveName(name));
    if (it == files_.end())
        return Error::fail(std::string(name) + " not found in " + dir_.string());
    std::ifstream in(it->second, std::ios::binary | std::ios::ate);
    if (!in)
        return Error::fail("Cannot open " + it->second.string());
    const std::streamoff size = in.tellg();
    out.resize(static_cast<size_t>(size));
    in.seekg(0);
    if (size > 0 && !in.read(reinterpret_cast<char*>(out.data()), size))
        return Error::fail("Cannot read " + it->second.string());
    return Error::success();
}

// --- SearchSet --------------------------------------------------------------------

void SearchSet::add(std::unique_ptr<Archive> archive, int priority) {
    // Before the first member of lower or equal priority: newest first
    // among equals.
    auto at = std::find_if(members_.begin(), members_.end(),
                           [priority](const Member& m) { return m.priority <= priority; });
    members_.insert(at, Member{std::move(archive), priority});
}

const Archive* SearchSet::find(std::string_view name) const {
    for (const Member& m : members_)
        if (m.archive->has(name))
            return m.archive.get();
    return nullptr;
}

Error SearchSet::read(std::string_view name, std::vector<uint8_t>& out) const {
    if (const Archive* a = find(name))
        return a->read(name, out);
    return Error::fail(std::string(name) + " not found");
}

std::vector<std::string> SearchSet::list() const {
    std::map<std::string, std::string> names; // normalized -> spelling of the first archive
    for (const Member& m : members_)
        for (std::string& n : m.archive->list())
            names.try_emplace(normalizeArchiveName(n), std::move(n));
    std::vector<std::string> out;
    out.reserve(names.size());
    for (auto& [key, name] : names)
        out.push_back(std::move(name));
    return out;
}

std::vector<const Archive*> SearchSet::archives() const {
    std::vector<const Archive*> out;
    for (const Member& m : members_)
        out.push_back(m.archive.get());
    return out;
}

} // namespace reluva
