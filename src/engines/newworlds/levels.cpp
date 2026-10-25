// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/levels.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace reluva::newworlds {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// "F1,2,Power Corrupts" -> a level; false if the line isn't one.
bool parseLine(std::string_view line, Level& out) {
    const size_t a = line.find(','), b = a == std::string_view::npos ? a : line.find(',', a + 1);
    if (b == std::string_view::npos)
        return false;
    const std::string_view name = trim(line.substr(0, a)), players = trim(line.substr(a + 1, b - a - 1));
    if (name.empty() || players.empty())
        return false;
    int n = 0;
    for (char c : players) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
        n = n * 10 + (c - '0');
    }
    out = Level{std::string(name), n, std::string(trim(line.substr(b + 1))), false};
    return true;
}

} // namespace

Error parseLevelList(std::string_view text, std::vector<Level>& out) {
    out.clear();
    size_t lineNo = 0;
    while (!text.empty()) {
        ++lineNo;
        const size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty())
            continue;
        Level level;
        if (line.substr(0, 2) == "//") {
            if (parseLine(trim(line.substr(2)), level)) {
                level.commentedOut = true;
                out.push_back(std::move(level));
            }
            continue;
        }
        if (!parseLine(line, level))
            return Error::fail("level.dat line " + std::to_string(lineNo) + " isn't \"name,players,title\"");
        out.push_back(std::move(level));
    }
    return Error::success();
}

const Level* levelForMap(const std::vector<Level>& levels, std::string_view mapName) {
    std::string_view stem = mapName;
    if (const size_t slash = stem.find_last_of("/\\"); slash != std::string_view::npos)
        stem.remove_prefix(slash + 1);
    if (const size_t dot = stem.rfind('.'); dot != std::string_view::npos)
        stem = stem.substr(0, dot);
    for (bool commented : {false, true})
        for (const Level& l : levels)
            if (l.commentedOut == commented && sameName(l.shortName, stem))
                return &l;
    return nullptr;
}

namespace {

char upper(char c) { return char(std::toupper(static_cast<unsigned char>(c))); }

bool isJoint(std::string_view shortName) { return shortName.size() > 1 && upper(shortName[0]) == 'J'; }

// The mission's number as two digits ("F1" -> "01"); empty if it has none.
std::string twoDigits(std::string_view shortName) {
    std::string_view digits = shortName.substr(std::min<size_t>(1, shortName.size()));
    if (digits.empty() || digits.size() > 2)
        return {};
    for (char c : digits)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return {};
    return digits.size() == 1 ? "0" + std::string(digits) : std::string(digits);
}

const char* sidePrefix(char side) {
    switch (upper(side)) {
    case 'F': return "fed";
    case 'K': return "klg";
    case 'R': return "rom";
    default: return nullptr;
    }
}

} // namespace

std::vector<char> missionSides(std::string_view shortName) {
    if (isJoint(shortName))
        return {'F', 'K', 'R'};
    // The bonus missions: C9 the Federation's, C10 the Klingons', C11 the
    // Romulans' (level.dat's titles say so).
    if (sameName(shortName, "C10"))
        return {'K'};
    if (sameName(shortName, "C11"))
        return {'R'};
    if (!shortName.empty() && (upper(shortName[0]) == 'K' || upper(shortName[0]) == 'R') &&
        !twoDigits(shortName).empty())
        return {upper(shortName[0])};
    return {'F'};
}

std::string missionTableName(std::string_view shortName, char side) {
    const std::string folder = "str/eng/miss/";
    if (sameName(shortName, "test"))
        return folder + "tst.stb";
    const std::string number = twoDigits(shortName);
    if (number.empty())
        return {};
    const char* prefix = sidePrefix(side);
    switch (upper(shortName[0])) {
    case 'F':
    case 'K':
    case 'R':
        prefix = sidePrefix(shortName[0]);
        return folder + prefix + number + ".stb";
    case 'J': return prefix ? folder + "jnt" + number + prefix + ".stb" : std::string();
    case 'C': return folder + "cvr03.stb";
    default: return {};
    }
}

std::vector<std::string> missionMapNames(std::string_view shortName) {
    std::string lower;
    for (char c : shortName)
        lower += char(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::string> names = {"map/" + lower + ".gmf"};
    if (lower.size() > 2 && lower.compare(lower.size() - 2, 2, "14") == 0)
        names.push_back("map/" + lower.substr(0, lower.size() - 2) + "12.gmf");
    // The bonus missions have no map of their own name; their recordings'
    // mines and ore readings lie on these (play-c9.md, c10, c11).
    if (lower == "c9")
        names = {"map/f3.gmf", "map/j3.gmf"};
    else if (lower == "c10")
        names = {"map/f2.gmf"};
    else if (lower == "c11")
        names = {"map/f1.gmf"};
    return names;
}

bool isMissionClip(std::string_view stem, std::string_view shortName, char side) {
    std::string prefix(shortName);
    if (isJoint(shortName))
        prefix += side;
    if (stem.size() < prefix.size() + 3 || stem.size() > prefix.size() + 4 ||
        !sameName(stem.substr(0, prefix.size()), prefix))
        return false;
    for (size_t i = prefix.size(); i < prefix.size() + 3; ++i)
        if (!std::isdigit(static_cast<unsigned char>(stem[i])))
            return false;
    return stem.size() == prefix.size() + 3 || std::isalpha(static_cast<unsigned char>(stem.back()));
}

std::string planetTheme(std::string_view archiveName) {
    static constexpr std::pair<const char*, const char*> kThemes[] = {
        {"alpwrld.cat", "AlpineStart"}, {"deswrld.cat", "DesertStart"}, {"icewrld.cat", "IceStart"},
        {"vlcwrld.cat", "LavaStart"},   {"redwrld.cat", "MarsStart"},   {"alnwrld.cat", "MountainStart"}};
    for (const auto& [archive, theme] : kThemes)
        if (sameName(archiveName, archive))
            return theme;
    return {};
}

} // namespace reluva::newworlds
