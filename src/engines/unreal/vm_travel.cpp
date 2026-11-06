// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Travelling between levels with the player's things, as the engine does
// it (UGameEngine's travel and ULevel::SpawnPlayActor, read from
// Engine.dll; the code is Reluva's own). Leaving with items, the player's
// pawn and each actor along its Inventory chain that has bTravel are
// written out as text, a block each:
//
//   Class=PLYR.DS9_Kira Name=DS9_Kira0
//   {
//   Health=80
//   Weapon=DS9_PhaserRifle0
//   }
//
// with only their travel properties, and only those that differ from
// their class's defaults (an actor reference as the actor's name). On
// arrival, the pawn's block goes onto the pawn just logged in, whatever
// class it was; each other block's class is spawned where the pawn is,
// owned by it; then the properties are put back, an actor reference to
// whichever travelled actor had that name.

#include "engines/unreal/vm_level.h"

#include <cstdio>

namespace reluva::unreal {

namespace {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return std::string(s.substr(a, b - a));
}

bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t k = 0; k < a.size(); ++k)
        if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k])))
            return false;
    return true;
}

// A struct's "(A=1,B=2)" split at its top-level commas.
std::vector<std::string> fieldsOf(std::string_view text) {
    std::vector<std::string> out;
    std::string t = trim(text);
    if (t.size() < 2 || t.front() != '(' || t.back() != ')')
        return out;
    int depth = 0;
    bool quoted = false;
    std::string cur;
    for (size_t k = 1; k + 1 < t.size(); ++k) {
        const char c = t[k];
        if (c == '"')
            quoted = !quoted;
        else if (!quoted && c == '(')
            ++depth;
        else if (!quoted && c == ')')
            --depth;
        if (c == ',' && depth == 0 && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

} // namespace

std::string ScriptLevel::travelText(const VmProperty& p, const Value& v) {
    switch (p.type) {
    case VType::Bool: return v.asBool() ? "True" : "False";
    case VType::Byte: return std::to_string(v.i & 255);
    case VType::Int: return std::to_string(v.i);
    case VType::Float: {
        char b[32];
        std::snprintf(b, sizeof b, "%.6f", double(v.f));
        return b;
    }
    case VType::Name: return v.i ? vm_.nameText(v.i) : "None";
    case VType::String: {
        std::string out = "\"";
        for (char c : v.s)
            out += c == '"' ? '\'' : c;
        return out + "\"";
    }
    case VType::Object: {
        VmObject* o = v.asObject();
        if (!o)
            return "None";
        return o->isActor ? vm_.objectName(o) : vm_.pathName(o);
    }
    case VType::Struct: {
        std::string out = "(";
        bool first = true;
        if (v.st)
            for (const VmProperty* f : v.st->properties)
                for (int k = 0; k < f->arrayDim; ++k) {
                    const size_t at = size_t(f->slot + k);
                    if (at >= v.fields.size())
                        continue;
                    out += first ? "" : ",";
                    first = false;
                    out += f->name + (f->arrayDim > 1 ? "[" + std::to_string(k) + "]" : "") + "=" +
                           travelText(*f, v.fields[at]);
                }
        return out + ")";
    }
    default: return {};
    }
}

bool ScriptLevel::travelValue(const VmProperty& p, const std::string& text, Value& v,
                              const std::map<std::string, VmObject*>& byName) {
    const std::string t = trim(text);
    switch (p.type) {
    case VType::Bool: v = Value::boolValue(same(t, "true") || t == "1"); return true;
    case VType::Byte: v = Value::byteValue(std::atoi(t.c_str())); return true;
    case VType::Int: v = Value::intValue(std::atoi(t.c_str())); return true;
    case VType::Float: v = Value::floatValue(std::strtof(t.c_str(), nullptr)); return true;
    case VType::Name: v = Value::nameValue(same(t, "None") ? 0 : vm_.name(t)); return true;
    case VType::String:
        v = Value::stringValue(t.size() >= 2 && t.front() == '"' && t.back() == '"' ? t.substr(1, t.size() - 2) : t);
        return true;
    case VType::Object: {
        if (same(t, "None")) {
            v = Value::objectValue(nullptr);
            return true;
        }
        // One of the travelled actors, by the name it had; else (not an
        // actor) the object by its path. One not found is left alone.
        for (const auto& [name, actor] : byName)
            if (same(name, t)) {
                v = Value::objectValue(actor);
                return true;
            }
        if (p.objectClass && p.objectClass->isA(vm_.findClass("Actor")))
            return false;
        if (VmObject* o = vm_.findObject(t)) {
            v = Value::objectValue(o);
            return true;
        }
        return false;
    }
    case VType::Struct: {
        if (!p.structType)
            return false;
        if (v.type != VType::Struct)
            v = p.zero();
        for (const std::string& field : fieldsOf(t)) {
            const size_t eq = field.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(std::string_view(field).substr(0, eq));
            int index = 0;
            if (const size_t br = key.find('['); br != std::string::npos) {
                index = std::atoi(key.c_str() + br + 1);
                key = trim(std::string_view(key).substr(0, br));
            }
            for (const VmProperty* f : p.structType->properties)
                if (same(f->name, key) && index < f->arrayDim && size_t(f->slot + index) < v.fields.size())
                    travelValue(*f, field.substr(eq + 1), v.fields[size_t(f->slot + index)], byName);
        }
        return true;
    }
    default: return false;
    }
}

std::string ScriptLevel::exportTravel() {
    std::string out;
    std::set<VmObject*> seen;
    for (VmObject* a = player_; a && !a->deleted && seen.insert(a).second;) {
        if (pb(a, "bTravel")) {
            out += "Class=" + vm_.pathName(a->cls->object) + " Name=" + vm_.objectName(a) + "\r\n{\r\n";
            for (const VmProperty* p : a->cls->properties) {
                if (!(p->flags & propflag::Travel))
                    continue;
                for (int k = 0; k < p->arrayDim; ++k) {
                    const size_t at = size_t(p->slot + k);
                    if (at >= a->values.size() || at >= a->cls->defaults.size())
                        continue;
                    const std::string now = travelText(*p, a->values[at]);
                    if (now == travelText(*p, a->cls->defaults[at]))
                        continue; // as its class has it: not written
                    out += p->name + (p->arrayDim > 1 ? "[" + std::to_string(k) + "]" : "") + "=" + now + "\r\n";
                }
            }
            out += "}\r\n";
        }
        const Value* next = prop(a, "Inventory");
        a = next ? next->asObject() : nullptr;
    }
    return out;
}

std::vector<VmObject*> ScriptLevel::acceptTravel(const std::string& text) {
    struct Block {
        VmObject* actor = nullptr;
        std::vector<std::string> lines;
    };
    std::vector<Block> blocks;
    std::map<std::string, VmObject*> byName;
    VmClass* playerPawn = vm_.findClass("PlayerPawn");
    size_t at = 0;
    Block* open = nullptr;
    bool skipping = false;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = trim(std::string_view(text).substr(at, end - at));
        at = end + 1;
        if (line.rfind("Class=", 0) == 0) {
            const size_t name = line.find(" Name=");
            const std::string className = trim(line.substr(6, name == std::string::npos ? std::string::npos : name - 6));
            const std::string actorName = name == std::string::npos ? std::string() : trim(line.substr(name + 6));
            VmClass* cls = vm_.findClass(className);
            open = nullptr;
            skipping = !cls;
            if (!cls) {
                if (vm_.logger)
                    vm_.logger("Cannot accept travelling class " + className);
                continue;
            }
            VmObject* actor = nullptr;
            if (playerPawn && cls->isA(playerPawn)) {
                actor = player_; // the player's own: onto the pawn logged in
            } else {
                const Value* loc = prop(player_, "Location");
                const Value* rot = prop(player_, "Rotation");
                actor = spawn(cls, player_, 0, loc, rot);
            }
            if (!actor)
                continue;
            blocks.push_back({actor, {}});
            open = &blocks.back();
            byName[actorName] = actor;
        } else if (line == "{" || line == "}") {
            if (line == "}")
                open = nullptr, skipping = false;
        } else if (open && !skipping && !line.empty()) {
            open->lines.push_back(line);
        }
    }
    // Their travel properties put back, by name (any case), the index
    // within its array.
    for (Block& b : blocks)
        for (const std::string& line : b.lines) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(std::string_view(line).substr(0, eq));
            int index = 0;
            if (const size_t br = key.find('['); br != std::string::npos) {
                index = std::atoi(key.c_str() + br + 1);
                key = trim(std::string_view(key).substr(0, br));
            }
            for (const VmProperty* p : b.actor->cls->properties)
                if ((p->flags & propflag::Travel) && same(p->name, key) && index >= 0 && index < p->arrayDim &&
                    size_t(p->slot + index) < b.actor->values.size()) {
                    Value v = b.actor->values[size_t(p->slot + index)];
                    if (travelValue(*p, line.substr(eq + 1), v, byName))
                        vm_.assign(b.actor->values[size_t(p->slot + index)], v);
                    break;
                }
        }
    std::vector<VmObject*> accepted;
    for (const Block& b : blocks)
        accepted.push_back(b.actor);
    return accepted;
}

} // namespace reluva::unreal
