// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva {

// A collection of named files: a folder on disk, a .zip/.pk3, or an engine's
// own container format. Names use '/' separators and are matched ignoring
// case, as the games that look files up expect.
class Archive {
public:
    virtual ~Archive() = default;

    // Every file's name, in the archive's own spelling.
    virtual std::vector<std::string> list() const = 0;
    virtual bool has(std::string_view name) const = 0;
    virtual Error read(std::string_view name, std::vector<uint8_t>& out) const = 0;
    // Where the files come from, for messages ("baseEF/pak0.pk3").
    virtual std::string describe() const = 0;
};

// "Textures\\Base\\Floor.TGA" -> "textures/base/floor.tga": the key archives
// use to look names up.
std::string normalizeArchiveName(std::string_view name);

// The loose files under a folder on disk, subfolders included. The folder is
// listed once when opened; files added afterwards aren't seen.
class DirectoryArchive : public Archive {
public:
    Error open(const std::filesystem::path& dir);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t>& out) const override;
    std::string describe() const override { return dir_.string(); }

private:
    std::filesystem::path dir_;
    std::unordered_map<std::string, std::filesystem::path> files_; // normalized name -> file
};

// Several archives searched as one, like a game's search path: a name is
// read from the archive with the highest priority that has it; archives of
// equal priority are searched newest first.
class SearchSet {
public:
    void add(std::unique_ptr<Archive> archive, int priority);
    void clear() { members_.clear(); }

    bool has(std::string_view name) const { return find(name) != nullptr; }
    // The archive a name would be read from, or nullptr.
    const Archive* find(std::string_view name) const;
    Error read(std::string_view name, std::vector<uint8_t>& out) const;
    // Every name in the set once, sorted by normalized name, in the spelling
    // of the archive that would supply it.
    std::vector<std::string> list() const;

    size_t size() const { return members_.size(); }
    // The archives in search order (highest priority first).
    std::vector<const Archive*> archives() const;

private:
    struct Member {
        std::unique_ptr<Archive> archive;
        int priority;
    };
    std::vector<Member> members_; // kept in search order
};

} // namespace reluva
