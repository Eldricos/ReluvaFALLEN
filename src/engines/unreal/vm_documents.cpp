// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's documents and objectives, for a running level
// (ScriptLevel): the text files in the game's int folder (Document.int,
// the readable documents; each map's own .int, its objectives and title)
// read section by section for CTVE_Document: which documents there are
// (FindDocument), a section's settings as the actor's properties
// (LocalizeDocument), and its lines handed to the actor's events to draw
// (LoadDocument). See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "common/fs.h"

#include <fstream>
#include <iterator>

namespace reluva::unreal {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
        ++start;
    return s.substr(start);
}

} // namespace

// The game's int folder: beside System (its text files: dialogue tables,
// documents, the maps' strings), else System itself.
std::filesystem::path ScriptLevel::intFolder() const {
    const std::filesystem::path system = vm_.packages().iniFile().parent_path();
    for (const std::filesystem::path& folder : {system.parent_path() / "int", system.parent_path() / "Int"}) {
        std::error_code ec;
        if (std::filesystem::is_directory(folder, ec))
            return folder;
    }
    return system;
}

// A section of an .int file (found ignoring case): its keys and values in
// order, as they're written ("Text=..." many times over). Sections are
// "[Name]" lines; other lines without "=" (the rows of asterisks some
// files start with) are left out. Read once a file.
const std::vector<std::pair<std::string, std::string>>* ScriptLevel::intSection(const std::string& file,
                                                                                const std::string& section) {
    const std::string fileKey = fs::toLower(file);
    auto it = intFiles_.find(fileKey);
    if (it == intFiles_.end()) {
        IntFile parsed;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(intFolder(), ec)) {
            if (fs::toLower(entry.path().filename().string()) != fileKey)
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::vector<std::pair<std::string, std::string>>* current = nullptr;
            size_t at = 0;
            while (at < text.size()) {
                size_t end = text.find('\n', at);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = trimmed(text.substr(at, end - at));
                at = end + 1;
                if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                    current = &parsed[fs::toLower(line.substr(1, line.size() - 2))];
                    continue;
                }
                const size_t eq = line.find('=');
                if (current && eq != std::string::npos)
                    current->push_back({trimmed(line.substr(0, eq)), trimmed(line.substr(eq + 1))});
            }
            break;
        }
        it = intFiles_.emplace(fileKey, std::move(parsed)).first;
    }
    auto found = it->second.find(fs::toLower(section));
    return found == it->second.end() ? nullptr : &found->second;
}

void registerDocumentNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // GetMapTitle(): the map's name ("M01_KiraL1A"): its .int's, as
    // CTVE_Document and the HUD take it.
    reg("Actor.GetMapTitle", [](NativeCall& c) {
        c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : std::string());
    });
    // FindDocument(string theSectionName): a document in Document.int of
    // that name is there: FoundDocument (the actor then lists it, if it's
    // for this mission).
    reg("CTVE_Document.FindDocument", [](NativeCall& c) {
        if (c.vm.level && c.self && c.vm.level->intSection("Document.int", c.s(0)))
            c.vm.callEvent(c.self, "FoundDocument", {Value::stringValue(c.s(0))});
    });
    // LocalizeDocument(string DocumentName, string theFileName): the
    // section's settings as the actor's properties of the same names
    // (Title, MissionLevel, bOnlyThisMission, bCritical, bActive), each
    // converted to its type. Its lines (Text, Color, IMAGE) are for
    // LoadDocument.
    reg("CTVE_Document.LocalizeDocument", [](NativeCall& c) {
        const auto* section = c.vm.level && c.self ? c.vm.level->intSection(c.s(1), c.s(0)) : nullptr;
        if (!section)
            return;
        for (const auto& [key, value] : *section) {
            const VmProperty* p = c.self->cls->findProperty(c.vm.name(key));
            if (!p || size_t(p->slot) >= c.self->values.size())
                continue;
            Value& v = c.self->values[size_t(p->slot)];
            switch (p->type) {
            case VType::Byte:
            case VType::Int: v = Value::intValue(std::atoi(value.c_str())); break;
            case VType::Float: v = Value::floatValue(std::strtof(value.c_str(), nullptr)); break;
            case VType::Bool: v = Value::boolValue(fs::toLower(value) == "true" || value == "1"); break;
            case VType::String: v = Value::stringValue(value); break;
            default: break;
            }
        }
    });
    // LoadDocument(string DocumentName, string theFileName): the section's
    // lines in order, each to the actor's event that draws it: Color
    // (SetColor), Text (LoadText: a line), IMAGE ("Package.Texture
    // Justify=Left": DefineImage; the HUD takes 2 as right and 3 as centred,
    // anything else as left), Title
    // (SetTitle).
    reg("CTVE_Document.LoadDocument", [](NativeCall& c) {
        const auto* section = c.vm.level && c.self ? c.vm.level->intSection(c.s(1), c.s(0)) : nullptr;
        if (!section)
            return;
        for (const auto& [key, value] : *section) {
            const std::string k = fs::toLower(key);
            if (k == "color") {
                c.vm.callEvent(c.self, "SetColor", {Value::stringValue(value)});
            } else if (k == "text") {
                c.vm.callEvent(c.self, "LoadText", {Value::stringValue(value)});
            } else if (k == "title") {
                c.vm.callEvent(c.self, "SetTitle", {Value::stringValue(value)});
            } else if (k == "image") {
                const size_t space = value.find(' ');
                const std::string name = value.substr(0, space);
                const std::string options = space == std::string::npos ? std::string() : fs::toLower(value.substr(space));
                const int just = options.find("justify=center") != std::string::npos   ? 3
                                 : options.find("justify=right") != std::string::npos ? 2
                                                                                       : 0;
                c.vm.callEvent(c.self, "DefineImage", {Value::stringValue(name), Value::byteValue(just), Value::byteValue(1)});
            }
        }
    });
}

} // namespace reluva::unreal
