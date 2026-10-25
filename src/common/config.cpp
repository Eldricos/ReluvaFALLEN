// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/config.h"

#include <fstream>

namespace reluva {

static std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

Error Config::load(const std::filesystem::path& path) {
    path_ = path;
    sections_.clear();

    std::ifstream in(path);
    if (!in)
        return Error::success(); // a missing file just means a fresh config

    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            sections_[section];
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty())
            continue;
        sections_[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return Error::success();
}

Error Config::save() const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out)
        return Error::fail("Cannot write " + path_.string());
    for (const auto& [name, values] : sections_) {
        out << '[' << name << "]\n";
        for (const auto& [k, v] : values)
            out << k << '=' << v << '\n';
        out << '\n';
    }
    return out ? Error::success() : Error::fail("Error writing " + path_.string());
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& fallback) const {
    auto s = sections_.find(section);
    if (s == sections_.end())
        return fallback;
    auto v = s->second.find(key);
    return v == s->second.end() ? fallback : v->second;
}

void Config::set(const std::string& section, const std::string& key, const std::string& value) {
    sections_[section][key] = value;
}

bool Config::hasSection(const std::string& section) const { return sections_.count(section) != 0; }

void Config::removeSection(const std::string& section) { sections_.erase(section); }

std::vector<std::string> Config::targets() const {
    std::vector<std::string> out;
    for (const auto& [name, values] : sections_)
        if (name != kGlobalSection)
            out.push_back(name);
    return out;
}

} // namespace reluva
