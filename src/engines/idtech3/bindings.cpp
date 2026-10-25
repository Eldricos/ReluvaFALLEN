// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/bindings.h"

#include "common/fs.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::idtech3 {

namespace {

// The names of keys that aren't printable characters, as config files
// write them, by key number.
struct KeyName {
    int32_t key;
    const char* name;
};
constexpr KeyName kKeyNames[] = {
    {q3key::Tab, "TAB"},         {q3key::Enter, "ENTER"},      {q3key::Escape, "ESCAPE"},
    {q3key::Space, "SPACE"},     {q3key::Backspace, "BACKSPACE"}, {129, "CAPSLOCK"},
    {q3key::Pause, "PAUSE"},     {q3key::Up, "UPARROW"},       {q3key::Down, "DOWNARROW"},
    {q3key::Left, "LEFTARROW"},  {q3key::Right, "RIGHTARROW"}, {q3key::Alt, "ALT"},
    {q3key::Ctrl, "CTRL"},       {q3key::Shift, "SHIFT"},      {q3key::Insert, "INS"},
    {q3key::Delete, "DEL"},      {q3key::PageDown, "PGDN"},    {q3key::PageUp, "PGUP"},
    {q3key::Home, "HOME"},       {q3key::End, "END"},          {160, "KP_HOME"},
    {161, "KP_UPARROW"},         {162, "KP_PGUP"},             {163, "KP_LEFTARROW"},
    {164, "KP_5"},               {165, "KP_RIGHTARROW"},       {166, "KP_END"},
    {167, "KP_DOWNARROW"},       {168, "KP_PGDN"},             {q3key::KpEnter, "KP_ENTER"},
    {170, "KP_INS"},             {171, "KP_DEL"},              {172, "KP_SLASH"},
    {173, "KP_MINUS"},           {174, "KP_PLUS"},             {175, "KP_NUMLOCK"},
    {176, "KP_STAR"},            {177, "KP_EQUALS"},           {q3key::Mouse1, "MOUSE1"},
    {q3key::Mouse2, "MOUSE2"},   {q3key::Mouse3, "MOUSE3"},    {q3key::Mouse4, "MOUSE4"},
    {q3key::Mouse5, "MOUSE5"},   {q3key::WheelDown, "MWHEELDOWN"}, {q3key::WheelUp, "MWHEELUP"},
    {';', "SEMICOLON"},
};

} // namespace

int32_t toQ3Key(uint32_t k) {
    switch (k) {
    case key::Tab: return q3key::Tab;
    case key::Return: return q3key::Enter;
    case key::KpEnter: return q3key::KpEnter;
    case key::Escape: return q3key::Escape;
    case key::Space: return q3key::Space;
    case key::Backspace: return q3key::Backspace;
    case key::Delete: return q3key::Delete;
    case key::Up: return q3key::Up;
    case key::Down: return q3key::Down;
    case key::Left: return q3key::Left;
    case key::Right: return q3key::Right;
    case key::Insert: return q3key::Insert;
    case key::Home: return q3key::Home;
    case key::End: return q3key::End;
    case key::PageUp: return q3key::PageUp;
    case key::PageDown: return q3key::PageDown;
    case key::LShift:
    case key::RShift: return q3key::Shift;
    case key::LCtrl:
    case key::RCtrl: return q3key::Ctrl;
    case key::LAlt:
    case key::RAlt: return q3key::Alt;
    default: break;
    }
    if (k >= key::F1 && k < key::F1 + 12)
        return q3key::F1 + int32_t(k - key::F1);
    if (k > 32 && k < 127)
        return int32_t(k);
    return 0;
}

int32_t toQ3Button(MouseButton b) {
    switch (b) {
    case MouseButton::Left: return q3key::Mouse1;
    case MouseButton::Right: return q3key::Mouse2;
    case MouseButton::Middle: return q3key::Mouse3;
    default: return 0;
    }
}

std::string KeyBindings::keyName(int32_t key) {
    for (const KeyName& n : kKeyNames)
        if (n.key == key)
            return n.name;
    if (key >= q3key::F1 && key < q3key::F1 + 15)
        return "F" + std::to_string(key - q3key::F1 + 1);
    if (key > 32 && key < 127)
        return std::string(1, char(key));
    // Others by number, as the original writes them.
    char hex[8];
    std::snprintf(hex, sizeof hex, "0x%02x", unsigned(key & 0xff));
    return hex;
}

int32_t KeyBindings::keyNumber(std::string_view name) {
    if (name.empty())
        return -1;
    if (name.size() == 1) {
        const auto c = static_cast<unsigned char>(name[0]);
        // Letters are bound in lower case.
        return c >= 'A' && c <= 'Z' ? int32_t(c - 'A' + 'a') : int32_t(c);
    }
    const std::string lower = fs::toLower(name);
    for (const KeyName& n : kKeyNames)
        if (lower == fs::toLower(n.name))
            return n.key;
    if (lower.size() >= 2 && lower[0] == 'f') {
        const int f = std::atoi(lower.c_str() + 1);
        if (f >= 1 && f <= 15)
            return q3key::F1 + f - 1;
    }
    if (lower.size() == 4 && lower[0] == '0' && lower[1] == 'x')
        return int32_t(std::strtol(lower.c_str() + 2, nullptr, 16));
    return -1;
}

const std::string& KeyBindings::get(int32_t key) const {
    static const std::string none;
    return key >= 0 && key < q3key::Count ? commands_[size_t(key)] : none;
}

void KeyBindings::set(int32_t key, std::string command) {
    if (key < 0 || key >= q3key::Count || commands_[size_t(key)] == command)
        return;
    commands_[size_t(key)] = std::move(command);
    changed = true;
}

void KeyBindings::clear() {
    for (std::string& c : commands_)
        if (!c.empty()) {
            c.clear();
            changed = true;
        }
}

bool KeyBindings::empty() const {
    for (const std::string& c : commands_)
        if (!c.empty())
            return false;
    return true;
}

std::vector<std::string> splitCommand(std::string_view line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (true) {
        while (i < line.size() && static_cast<unsigned char>(line[i]) <= ' ')
            ++i;
        if (i >= line.size() || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
            return words;
        std::string word;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"')
                word += line[i++];
            if (i < line.size())
                ++i;
        } else {
            while (i < line.size() && static_cast<unsigned char>(line[i]) > ' ')
                word += line[i++];
        }
        words.push_back(std::move(word));
    }
}

std::vector<std::string> splitCommands(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"')
            quoted = !quoted;
        if (c == '/' && !quoted && i + 1 < text.size() && text[i + 1] == '/') {
            // A comment, to the end of the line.
            while (i + 1 < text.size() && text[i + 1] != '\n')
                ++i;
            continue;
        }
        if (c == '\n' || c == '\r' || (c == ';' && !quoted)) {
            if (c != ';')
                quoted = false;
            if (!splitCommand(current).empty())
                out.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!splitCommand(current).empty())
        out.push_back(current);
    return out;
}

void KeyBindings::execute(std::string_view text, CvarStore& cvars, int* ignored,
                          const std::function<bool(const std::string&, std::string&)>& readFile, int depth) {
    for (const std::string& line : splitCommands(text)) {
        const std::vector<std::string> w = splitCommand(line);
        const std::string verb = fs::toLower(w[0]);
        if (verb == "bind" && w.size() >= 3) {
            const int32_t key = keyNumber(w[1]);
            if (key >= 0) {
                // The rest of the line, unquoted words joined.
                std::string command = w[2];
                for (size_t i = 3; i < w.size(); ++i)
                    command += " " + w[i];
                set(key, command);
                continue;
            }
        } else if (verb == "unbind" && w.size() >= 2 && keyNumber(w[1]) >= 0) {
            set(keyNumber(w[1]), {});
            continue;
        } else if (verb == "unbindall") {
            clear();
            continue;
        } else if ((verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") && w.size() >= 3) {
            const int32_t flags = verb == "seta"   ? CvarStore::Archive
                                  : verb == "sets" ? CvarStore::ServerInfo
                                  : verb == "setu" ? CvarStore::UserInfo
                                                   : 0;
            std::string value = w[2];
            for (size_t i = 3; i < w.size(); ++i)
                value += " " + w[i];
            cvars.create(w[1], value, flags);
            cvars.set(w[1], value);
            continue;
        } else if (verb == "exec" && w.size() >= 2 && readFile && depth < 8) {
            std::string name = w[1], contents;
            if (name.find('.') == std::string::npos)
                name += ".cfg";
            if (readFile(name, contents))
                execute(contents, cvars, ignored, readFile, depth + 1);
            continue;
        }
        if (ignored)
            ++*ignored;
    }
}

std::string KeyBindings::write() const {
    std::string out = "unbindall\n";
    for (int32_t key = 0; key < q3key::Count; ++key) {
        const std::string& c = commands_[size_t(key)];
        if (c.empty())
            continue;
        // The quote mark by number, as it can't be written quoted.
        const std::string name = key == '"' ? "0x22" : keyName(key);
        out += "bind " + name + " \"" + c + "\"\n";
    }
    return out;
}

} // namespace reluva::idtech3
