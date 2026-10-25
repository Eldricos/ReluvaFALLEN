// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva {

// INI-style settings file. The [reluva] section holds global settings; every
// other section is a game target (one installed game the user added).
class Config {
public:
    static constexpr const char* kGlobalSection = "reluva";

    Error load(const std::filesystem::path& path);
    Error save() const;

    std::string get(const std::string& section, const std::string& key,
                    const std::string& fallback = {}) const;
    void set(const std::string& section, const std::string& key, const std::string& value);

    bool hasSection(const std::string& section) const;
    void removeSection(const std::string& section);
    std::vector<std::string> targets() const;

private:
    std::filesystem::path path_;
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

} // namespace reluva
