// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/dialog_table.h"

#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace reluva::unreal {

namespace {

// The files are Windows-1252: its quotes, dashes and ellipsis as plain
// ASCII, the rest of the upper half as Latin-1 in UTF-8.
std::string fromWindows1252(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const auto c = uint8_t(ch);
        if (c < 0x80) {
            out += ch;
        } else if (c == 0x91 || c == 0x92) {
            out += '\'';
        } else if (c == 0x93 || c == 0x94) {
            out += '"';
        } else if (c == 0x96 || c == 0x97) {
            out += '-';
        } else if (c == 0x85) {
            out += "...";
        } else if (c >= 0xa0) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && s[start] == ' ')
        ++start;
    return s.substr(start);
}

// A spreadsheet's quoting: a field in quotes, its own quotes doubled.
std::string unquoted(std::string s) {
    s = trimmed(std::move(s));
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
        for (size_t at = s.find("\"\""); at != std::string::npos; at = s.find("\"\"", at + 1))
            s.erase(at, 1);
    }
    return s;
}

} // namespace

// Rows of tab-separated columns: the record's speaker code and number,
// mission, type, speaker, listener, line ID, line, direction, pause, next
// line, return line, tag, event, condition, camera, the speaker's and
// listener's animations and faces. Rows without a record (the header,
// the Missions and Speakers lines, blank spacers) are left out.
void parseDialogTable(const std::string& text, const std::string& table, std::vector<DialogLine>& out) {
    size_t at = 0;
    int rowNumber = -1; // the rows that aren't blank, from 0 (as the engine numbers them)
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string row = text.substr(at, end - at);
        at = end + 1;
        if (row.find_first_not_of(" \r\n") == std::string::npos)
            continue;
        ++rowNumber;
        std::vector<std::string> c, raw;
        size_t from = 0;
        while (true) {
            const size_t tab = row.find('\t', from);
            raw.push_back(fromWindows1252(row.substr(from, tab == std::string::npos ? std::string::npos : tab - from)));
            c.push_back(unquoted(raw.back()));
            if (tab == std::string::npos)
                break;
            from = tab + 1;
        }
        if (c.size() < 8 || c[0].empty() || c[1].empty() || c[0] == "REC ID" || c[0] == "Missions" || c[0] == "Speakers" ||
            c[0] == "Types" || c[0] == "Faces")
            continue;
        c.resize(20);
        DialogLine l;
        l.table = table;
        l.record = c[0] + c[1];
        l.mission = c[2];
        l.type = c[3];
        l.speaker = c[4];
        l.listener = c[5];
        l.lineId = c[6];
        l.text = c[7];
        l.row = rowNumber;
        l.rawText = raw.size() > 7 && !(trimmed(raw[7]).size() >= 2 && trimmed(raw[7]).front() == '"') ? raw[7] : c[7];
        while (!l.rawText.empty() && (l.rawText.back() == '\r' || l.rawText.back() == '\n'))
            l.rawText.pop_back();
        l.rawCondition = raw.size() > 14 ? raw[14] : std::string();
        // (Quoted, the quotes taken off as the engine does, its spaces kept.)
        if (const size_t q = l.rawCondition.find('"'); q != std::string::npos && l.rawCondition.rfind('"') > q)
            l.rawCondition = l.rawCondition.substr(q + 1, l.rawCondition.rfind('"') - q - 1);
        while (!l.rawCondition.empty() && (l.rawCondition.back() == '\r' || l.rawCondition.back() == '\n'))
            l.rawCondition.pop_back();
        l.direction = c[8];
        l.pause = c[9].empty() ? 0 : std::strtof(c[9].c_str(), nullptr);
        l.next = c[10];
        l.back = c[11];
        l.tag = c[12];
        l.event = c[13];
        l.condition = c[14];
        l.camera = c[15];
        l.speakerAnim = c[16];
        l.listenerAnim = c[17];
        l.speakerFace = c[18];
        l.listenerFace = c[19];
        out.push_back(std::move(l));
    }
}

Error readDialogTables(const std::filesystem::path& folder, std::vector<DialogLine>& out) {
    out.clear();
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        const std::string name = entry.path().filename().string();
        const std::string lower = fs::toLower(name);
        if (lower.size() > 13 && lower.substr(lower.size() - 13) == "_dialogue.txt")
            files.push_back(entry.path());
    }
    if (ec)
        return Error::fail(folder.string() + ": " + ec.message());
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        parseDialogTable(text, file.stem().string(), out);
    }
    return files.empty() ? Error::fail(folder.string() + ": no dialogue tables") : Error::success();
}

bool dialogKeyMatches(const std::string& pattern, const std::string& key) {
    if (pattern.size() != key.size())
        return false;
    for (size_t i = 0; i < key.size(); ++i)
        if (pattern[i] != '?' && std::toupper(uint8_t(pattern[i])) != std::toupper(uint8_t(key[i])))
            return false;
    return true;
}

} // namespace reluva::unreal
