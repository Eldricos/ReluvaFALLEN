// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The packages a game can load, found as the engine finds them: by name,
// in the folders its .ini's [Core.System] Paths lines list
// ("../Textures/*.utx"), opened when first asked for. Objects one package
// imports are looked up in the package they come from.

#include "engines/unreal/package.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::unreal {

// The Paths a game's .ini gives, relative to its System folder; these when
// it gives none.
std::vector<std::string> defaultSearchPaths();
// The Paths= lines of an .ini's [Core.System] section, in order.
std::vector<std::string> parseSearchPaths(std::string_view ini);

// An export found through a package set.
struct ObjectRef {
    const Package* package = nullptr;
    int exportIndex = -1;
    explicit operator bool() const { return package && exportIndex >= 0; }
};

class PackageSet {
public:
    // Indexes the packages under a game folder: its System folder's .ini
    // (`iniName` first, else any with Paths lines) says where to look.
    // Problems that don't stop the game are added to `warnings`.
    Error open(const std::filesystem::path& gameDir, std::string_view iniName,
               std::vector<std::string>* warnings = nullptr);
    // Adds one package, from disk or from memory (tests); the first of a
    // name wins, as with the search path.
    void addFile(const std::filesystem::path& file);
    void addMemory(std::string name, std::string extension, std::vector<uint8_t> data);

    // The package of that name (no extension, any case), opened on first
    // use; nullptr if there's none or it won't open (see error()).
    const Package* get(std::string_view name) const;
    std::string error(std::string_view name) const;

    // Where an object reference made in `from` leads: an export of `from`,
    // or for an import the export it names in the package it comes from.
    ObjectRef resolve(const Package& from, int32_t ref) const;

    // Every package's name, sorted, optionally only those with the
    // extension (".utx").
    std::vector<std::string> names(std::string_view extension = {}) const;
    std::string extension(std::string_view name) const;
    // Maps: packages with the extension the search path gives its Maps
    // folder (The Fallen's are .dsm), or .unr.
    bool isMap(std::string_view name) const;
    std::vector<std::string> mapNames() const;
    const std::vector<std::string>& searchPaths() const { return paths_; }
    std::filesystem::path iniFile() const { return ini_; }
    // The game folder open() was given (empty for sets built by hand).
    const std::filesystem::path& gameDir() const { return gameDir_; }

private:
    struct Entry {
        std::string name;      // as spelled on disk
        std::string extension; // lower case, with the dot
        std::filesystem::path file;
        std::vector<uint8_t> memory;
        mutable std::unique_ptr<Package> package;
        mutable bool tried = false;
        mutable std::string error;
    };
    std::map<std::string, Entry> entries_; // by lower-case name
    std::vector<std::string> paths_;
    std::vector<std::string> mapExtensions_{".unr"};
    std::filesystem::path ini_;
    std::filesystem::path gameDir_;
};

} // namespace reluva::unreal
