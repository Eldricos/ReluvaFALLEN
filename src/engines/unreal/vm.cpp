// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/vm.h"

#include "engines/unreal/script.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace reluva::unreal {

namespace {

constexpr int kMaxDepth = 250;            // nested calls before a script is stopped
constexpr uint64_t kMaxStatements = 2000000; // in one run of a function
constexpr uint64_t kMaxStateStatements = 20000; // in one tick of state code
constexpr float kPi = 3.14159265358979f;

std::string lower(std::string_view s) { return fs::toLower(std::string(s)); }

VType typeOfProperty(std::string_view cls) {
    const std::string c = lower(cls);
    if (c == "byteproperty") return VType::Byte;
    if (c == "intproperty") return VType::Int;
    if (c == "boolproperty") return VType::Bool;
    if (c == "floatproperty") return VType::Float;
    if (c == "nameproperty") return VType::Name;
    if (c == "strproperty" || c == "stringproperty") return VType::String;
    if (c == "objectproperty" || c == "classproperty") return VType::Object;
    if (c == "structproperty") return VType::Struct;
    return VType::None; // dynamic arrays, maps: not supported
}

} // namespace

// --- Values --------------------------------------------------------------------------

Value Value::byteValue(int v) {
    Value x;
    x.type = VType::Byte;
    x.i = v & 0xff;
    return x;
}
Value Value::intValue(int32_t v) {
    Value x;
    x.type = VType::Int;
    x.i = v;
    return x;
}
Value Value::boolValue(bool v) {
    Value x;
    x.type = VType::Bool;
    x.i = v ? 1 : 0;
    return x;
}
Value Value::floatValue(float v) {
    Value x;
    x.type = VType::Float;
    x.f = v;
    return x;
}
Value Value::nameValue(NameId v) {
    Value x;
    x.type = VType::Name;
    x.i = v;
    return x;
}
Value Value::stringValue(std::string v) {
    Value x;
    x.type = VType::String;
    x.s = std::move(v);
    return x;
}
Value Value::objectValue(VmObject* v) {
    Value x;
    x.type = VType::Object;
    x.o = v;
    return x;
}

int32_t Value::asInt() const {
    switch (type) {
    case VType::Float: return int32_t(f);
    case VType::Byte:
    case VType::Int:
    case VType::Bool:
    case VType::Name: return i;
    case VType::String: return std::atoi(s.c_str());
    default: return 0;
    }
}
float Value::asFloat() const {
    switch (type) {
    case VType::Float: return f;
    case VType::Byte:
    case VType::Int:
    case VType::Bool: return float(i);
    case VType::String: return float(std::atof(s.c_str()));
    default: return 0;
    }
}
bool Value::asBool() const {
    switch (type) {
    case VType::Float: return f != 0;
    case VType::Object: return asObject() != nullptr;
    case VType::String: return !s.empty() && lower(s) != "false" && s != "0";
    case VType::None:
    case VType::Struct: return false;
    default: return i != 0;
    }
}
VmObject* Value::asObject() const { return type == VType::Object && o && !o->deleted ? o : nullptr; }

Value VmProperty::zero() const {
    Value v;
    v.type = type;
    if (type == VType::Struct && structType) {
        v.st = structType;
        v.fields = structType->defaults;
    }
    return v;
}

VmProperty* VmStruct::findProperty(NameId name) const {
    // The most derived first: a subclass can declare a property of the
    // same name again (The Fallen redeclares SoundVolume and others).
    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
        if ((*it)->nameId == name)
            return *it;
    return nullptr;
}

bool VmClass::isA(const VmClass* other) const {
    for (const VmClass* c = this; c; c = c->parent())
        if (c == other)
            return true;
    return false;
}

Value* VmObject::find(Vm& vm, std::string_view property) {
    if (!cls)
        return nullptr;
    VmProperty* p = cls->findProperty(vm.name(property));
    return p && size_t(p->slot) < values.size() ? &values[size_t(p->slot)] : nullptr;
}

const std::string& NativeCall::s(size_t k) const {
    static const std::string empty;
    return k < args.size() ? args[k].s : empty;
}

// --- The VM: names and loading ---------------------------------------------------------

Vm::Vm(const PackageSet& packages) : packages_(packages) {
    name("None"); // id 0
    registerCoreNatives(*this);
}

Vm::~Vm() = default;

NameId Vm::name(std::string_view text) {
    std::string key = lower(text);
    auto it = nameIds_.find(key);
    if (it != nameIds_.end())
        return it->second;
    const NameId id = NameId(names_.size());
    names_.emplace_back(text);
    nameIds_.emplace(std::move(key), id);
    return id;
}

const std::string& Vm::nameText(NameId id) const {
    static const std::string none = "None";
    return id >= 0 && size_t(id) < names_.size() ? names_[size_t(id)] : none;
}

void Vm::reportError(const std::string& what) {
    int& n = stats.errors[what];
    if (n++ == 0 && logger)
        logger("Script error: " + what);
}

VmClass* Vm::findClass(std::string_view qualified) {
    const size_t dot = qualified.rfind('.');
    const std::string pkgName(dot == std::string_view::npos ? std::string_view() : qualified.substr(0, dot));
    const std::string cls = lower(dot == std::string_view::npos ? qualified : qualified.substr(dot + 1));
    for (const std::string& n : packages_.names()) {
        if (!pkgName.empty() && lower(n) != lower(pkgName))
            continue;
        const Package* p = packages_.get(n);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i)
            if (p->exports()[i].classRef == 0 && lower(p->objectName(int32_t(i) + 1)) == cls)
                return classAt(*p, int(i));
    }
    return nullptr;
}

VmObject* Vm::findObject(std::string_view path) {
    // "Package.Name" or "Package.Group.Name".
    const size_t dot = path.find('.');
    if (dot == std::string_view::npos)
        return nullptr;
    const Package* p = packages_.get(std::string(path.substr(0, dot)));
    if (!p)
        return nullptr;
    const std::string rest = lower(path.substr(dot + 1));
    for (size_t i = 0; i < p->exports().size(); ++i)
        if (lower(p->path(int32_t(i) + 1)) == rest)
            return objectAt(*p, int32_t(i) + 1);
    return nullptr;
}

// As UnrealScript's string() of an object gives it: its path, from its
// package down ("M01_KiraL1A.LevelInfo0", "Engine.Actor", "DS9_Objectives.M01_KL1_LibKey"):
// a package's object by its place in it, a class by its package, anything
// else by its Outer's path. A level's actors take the level's package for
// their outer, as the game's scripts read it (GetMissionLevel takes the
// mission's number from string(Level)).
std::string Vm::pathName(const VmObject* o) {
    if (!o)
        return "None";
    if (o->asClass)
        return o->asClass->package ? o->asClass->package->name() + "." + o->asClass->name : o->asClass->name;
    if (o->package && o->exportIndex >= 0)
        return o->package->name() + "." + o->package->path(o->exportIndex + 1);
    const Value* outerValue = const_cast<VmObject*>(o)->find(*this, "Outer");
    const VmObject* outer = outerValue ? outerValue->asObject() : nullptr;
    if (!outer || outer == o)
        return nameText(o->name);
    if (outer->package && outer->exportIndex >= 0 && outer->cls && lower(outer->cls->name) == "level")
        return outer->package->name() + "." + nameText(o->name);
    return pathName(outer) + "." + nameText(o->name);
}

std::string Vm::objectName(const VmObject* o) const {
    if (!o)
        return "None";
    if (o->asClass)
        return o->asClass->name;
    return nameText(o->name);
}

VmClass* Vm::classAt(const Package& package, int exportIndex) {
    VmStruct* s = structAt(package, exportIndex);
    return s && s->kind == VmStruct::Kind::Class ? static_cast<VmClass*>(s) : nullptr;
}

VmStruct* Vm::structAt(const Package& package, int exportIndex) {
    const auto key = std::make_pair(&package, exportIndex);
    if (auto it = structs_.find(key); it != structs_.end())
        return it->second.get();
    if (exportIndex < 0 || size_t(exportIndex) >= package.exports().size())
        return nullptr;
    const Export& e = package.exports()[size_t(exportIndex)];
    if (e.classRef != 0) {
        // A function, state or struct: loaded with the class it's in.
        const std::string cls = lower(package.objectName(e.classRef));
        if (cls != "function" && cls != "state" && cls != "struct")
            return nullptr;
        if (cls == "struct") {
            ScriptObject so;
            if (Error err = readScriptObject(package, exportIndex, so)) {
                reportError(err.message);
                return nullptr;
            }
            auto owned = std::make_unique<VmStruct>();
            VmStruct* st = owned.get();
            st->kind = VmStruct::Kind::Struct;
            st->name = package.objectName(exportIndex + 1);
            st->nameId = name(st->name);
            st->package = &package;
            st->exportIndex = exportIndex;
            structs_.emplace(key, std::move(owned));
            if (e.superRef) {
                const ObjectRef sup = packages_.resolve(package, e.superRef);
                if (sup)
                    st->super = structAt(*sup.package, sup.exportIndex);
                if (st->super) {
                    st->properties = st->super->properties;
                    st->slots = st->super->slots;
                    st->defaults = st->super->defaults;
                }
            }
            loadStruct(*st, so);
            if (lower(package.name()) == "core")
                coreStructs_.emplace(st->nameId, st);
            return st;
        }
        // Up the outers to the class, then look again.
        int32_t outer = e.package;
        for (int guard = 0; guard < 16 && outer > 0; ++guard) {
            const Export& o = package.exports()[size_t(outer - 1)];
            if (o.classRef == 0) {
                classAt(package, outer - 1);
                break;
            }
            outer = o.package;
        }
        auto it = structs_.find(key);
        return it == structs_.end() ? nullptr : it->second.get();
    }

    // A class.
    ScriptObject so;
    if (Error err = readScriptObject(package, exportIndex, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmClass>();
    VmClass* c = owned.get();
    c->kind = VmStruct::Kind::Class;
    c->name = package.objectName(exportIndex + 1);
    c->nameId = name(c->name);
    c->package = &package;
    c->exportIndex = exportIndex;
    c->owner = c;
    c->classFlags = so.classFlags;
    structs_.emplace(key, std::move(owned));
    if (e.superRef) {
        const ObjectRef sup = packages_.resolve(package, e.superRef);
        VmClass* parent = sup ? classAt(*sup.package, sup.exportIndex) : nullptr;
        c->super = parent;
        if (parent) {
            c->properties = parent->properties;
            c->slots = parent->slots;
            c->defaults = parent->defaults;
        }
    }
    loadStruct(*c, so);
    // Its defaults over its parent's.
    for (const Property& p : so.defaults.items) {
        VmProperty* prop = c->findProperty(name(p.name));
        if (!prop) {
            reportError(c->name + "'s defaults: no property " + p.name);
            continue;
        }
        const int at = prop->slot + p.arrayIndex;
        if (p.arrayIndex < 0 || p.arrayIndex >= prop->arrayDim)
            continue;
        c->defaults[size_t(at)] = valueOf(*prop, package, p);
    }
    applyConfig(*c);
    classObject(c);
    return c;
}

// The settings the player's and the game's .ini files hold for a class's
// config properties, over its defaults: a config property from the class's
// own section ([Engine.HUD]), a globalconfig one from the section of the
// class that declares it; User.ini first, then DefUser.ini, then the
// game's own .ini (DS9.ini). An array's elements as Key[n]; structs aren't
// read.
void Vm::applyConfig(VmClass& c) {
    if (!c.package)
        return;
    const std::string own = c.package->name() + "." + c.name;
    for (const VmProperty* prop : c.properties) {
        if (!(prop->flags & (propflag::Config | propflag::GlobalConfig)) || prop->type == VType::Struct)
            continue;
        std::string section = own;
        if (prop->flags & propflag::GlobalConfig)
            for (const VmClass* up = &c; up; up = up->parent())
                if (std::find(up->properties.begin(), up->properties.end(), prop) != up->properties.end() && up->package)
                    section = up->package->name() + "." + up->name;
        for (int k = 0; k < prop->arrayDim; ++k) {
            const std::string key = prop->arrayDim > 1 ? prop->name + "[" + std::to_string(k) + "]" : prop->name;
            const std::string* text = configSetting(section, key);
            if (!text)
                continue;
            Value& v = c.defaults[size_t(prop->slot + k)];
            switch (prop->type) {
            case VType::Bool: v = Value::boolValue(lower(*text) == "true" || *text == "1"); break;
            case VType::Int: v = Value::intValue(std::atoi(text->c_str())); break;
            case VType::Float: v = Value::floatValue(std::strtof(text->c_str(), nullptr)); break;
            case VType::Name: v = Value::nameValue(name(*text)); break;
            case VType::String: v = Value::stringValue(*text); break;
            case VType::Byte: {
                int b = std::atoi(text->c_str());
                if (prop->enumNames)
                    for (size_t e = 0; e < prop->enumNames->size(); ++e)
                        if (lower(nameText((*prop->enumNames)[e])) == lower(*text))
                            b = int(e);
                v = Value::byteValue(b);
                break;
            }
            case VType::Object:
                if (VmObject* o = findObject(*text))
                    v = Value::objectValue(o);
                break;
            default: break;
            }
        }
    }
}

// A setting from the .ini files, in the order applyConfig says; each file
// read once.
const std::string* Vm::configSetting(const std::string& section, const std::string& key) {
    const std::filesystem::path system = packages_.iniFile().parent_path();
    for (const std::filesystem::path& file :
         {system / "User.ini", system / "DefUser.ini", packages_.iniFile()}) {
        const std::string fileKey = file.string();
        auto it = iniFiles_.find(fileKey);
        if (it == iniFiles_.end()) {
            IniFile parsed;
            FILE* f = std::fopen(fileKey.c_str(), "rb");
            if (f) {
                std::string text;
                char buf[4096];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
                    text.append(buf, n);
                std::fclose(f);
                std::map<std::string, std::string>* current = nullptr;
                size_t at = 0;
                while (at < text.size()) {
                    size_t end = text.find('\n', at);
                    if (end == std::string::npos)
                        end = text.size();
                    std::string line = text.substr(at, end - at);
                    at = end + 1;
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                        line.pop_back();
                    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                        current = &parsed[lower(line.substr(1, line.size() - 2))];
                        continue;
                    }
                    const size_t eq = line.find('=');
                    if (current && eq != std::string::npos)
                        current->emplace(lower(line.substr(0, eq)), line.substr(eq + 1));
                }
            }
            it = iniFiles_.emplace(fileKey, std::move(parsed)).first;
        }
        auto sec = it->second.find(lower(section));
        if (sec == it->second.end())
            continue;
        auto value = sec->second.find(lower(key));
        if (value != sec->second.end())
            return &value->second;
    }
    return nullptr;
}

void Vm::loadStruct(VmStruct& s, const ScriptObject& so) {
    const Package& package = *s.package;
    for (int32_t ref = so.children, guard = 0; ref > 0 && guard < 100000; ++guard) {
        const int index = ref - 1;
        ScriptObject child;
        if (Error err = readScriptObject(package, index, child)) {
            reportError(err.message);
            break;
        }
        switch (child.kind) {
        case ScriptObject::Kind::Property: addProperty(s, package, index, child); break;
        case ScriptObject::Kind::Function: {
            VmFunction* f = loadFunction(package, index, &s);
            if (f && (s.kind == VmStruct::Kind::Class || s.kind == VmStruct::Kind::State))
                static_cast<VmState&>(s).functions[f->nameId] = f;
            break;
        }
        case ScriptObject::Kind::State:
            if (s.kind == VmStruct::Kind::Class) {
                auto& cls = static_cast<VmClass&>(s);
                if (VmState* st = loadState(package, index, &cls))
                    cls.states[st->nameId] = st;
            }
            break;
        case ScriptObject::Kind::Struct: structAt(package, index); break;
        default: break;
        }
        ref = child.next;
    }
    s.code = so.code;
    prepareCode(s);
    if (s.kind == VmStruct::Kind::Class || s.kind == VmStruct::Kind::State) {
        auto& st = static_cast<VmState&>(s);
        st.stateFlags = so.stateFlags;
        for (const Expr& x : s.code.exprs)
            for (const auto& [label, offset] : x.labels)
                st.labels[name(package.name(label))] = offset;
    }
}

void Vm::prepareCode(VmStruct& s) {
    s.statementAt.clear();
    for (size_t i = 0; i < s.code.statements.size(); ++i)
        if (s.code.statements[i] >= 0)
            s.statementAt[s.code.exprs[size_t(s.code.statements[i])].offset] = int(i);
    s.resolved.assign(s.code.exprs.size(), nullptr);
}

void Vm::addProperty(VmStruct& s, const Package& package, int index, const ScriptObject& so) {
    propertyStore_.emplace_back();
    VmProperty& p = propertyStore_.back();
    p.name = package.objectName(index + 1);
    p.nameId = name(p.name);
    p.type = typeOfProperty(so.className);
    p.isClass = lower(so.className) == "classproperty";
    p.arrayDim = std::max(1, so.arrayDim);
    p.flags = so.propertyFlags;
    p.slot = s.slots;
    const std::string cls = lower(so.className);
    if (cls == "structproperty" && so.propertyRef) {
        const ObjectRef r = packages_.resolve(package, so.propertyRef);
        if (r)
            p.structType = structAt(*r.package, r.exportIndex);
        if (!p.structType)
            p.type = VType::None;
    }
    if (cls == "byteproperty" && so.propertyRef) {
        const ObjectRef r = packages_.resolve(package, so.propertyRef);
        ScriptObject en;
        if (r && !readScriptObject(*r.package, r.exportIndex, en)) {
            enumStore_.emplace_back();
            for (int32_t n : en.enumNames)
                enumStore_.back().push_back(name(r.package->name(n)));
            p.enumNames = &enumStore_.back();
        }
    }
    s.slots += p.arrayDim;
    s.properties.push_back(&p);
    for (int k = 0; k < p.arrayDim; ++k)
        s.defaults.push_back(p.zero());
    properties_[{&package, index}] = &p;
}

VmFunction* Vm::loadFunction(const Package& package, int index, VmStruct* outer) {
    const auto key = std::make_pair(&package, index);
    if (auto it = structs_.find(key); it != structs_.end())
        return static_cast<VmFunction*>(it->second.get());
    ScriptObject so;
    if (Error err = readScriptObject(package, index, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmFunction>();
    VmFunction* f = owned.get();
    f->kind = VmStruct::Kind::Function;
    f->name = package.objectName(index + 1);
    f->nameId = name(f->name);
    f->package = &package;
    f->exportIndex = index;
    f->flags = so.functionFlags;
    f->native = so.native;
    f->symbol = package.name(so.friendlyName);
    f->outer = outer;
    const VmClass* owner = outer->kind == VmStruct::Kind::Class ? static_cast<VmClass*>(outer)
                           : outer->kind == VmStruct::Kind::State ? static_cast<VmState*>(outer)->owner
                                                                  : nullptr;
    f->qualified = (owner ? owner->name : outer->name) + "." + f->name;
    structs_.emplace(key, std::move(owned));
    loadStruct(*f, so);
    for (VmProperty* p : f->properties) {
        if (p->flags & propflag::ReturnParm)
            f->returnValue = p;
        else if (p->flags & propflag::Parm)
            f->params.push_back(p);
    }
    if (f->native)
        nativeFunctions_[f->native] = f;
    return f;
}

VmState* Vm::loadState(const Package& package, int index, VmClass* owner) {
    const auto key = std::make_pair(&package, index);
    if (auto it = structs_.find(key); it != structs_.end())
        return static_cast<VmState*>(it->second.get());
    ScriptObject so;
    if (Error err = readScriptObject(package, index, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmState>();
    VmState* st = owned.get();
    st->kind = VmStruct::Kind::State;
    st->name = package.objectName(index + 1);
    st->nameId = name(st->name);
    st->package = &package;
    st->exportIndex = index;
    st->owner = owner;
    structs_.emplace(key, std::move(owned));
    const Export& e = package.exports()[size_t(index)];
    if (e.superRef) {
        const ObjectRef sup = packages_.resolve(package, e.superRef);
        VmStruct* s = sup ? structAt(*sup.package, sup.exportIndex) : nullptr;
        if (s && s->kind == VmStruct::Kind::State)
            st->super = s;
    }
    loadStruct(*st, so);
    return st;
}

VmProperty* Vm::propertyAt(const Package& package, int exportIndex) {
    const auto key = std::make_pair(&package, exportIndex);
    if (auto it = properties_.find(key); it != properties_.end())
        return it->second;
    // Load what it's in: up its outers to a struct or class.
    if (exportIndex < 0 || size_t(exportIndex) >= package.exports().size())
        return nullptr;
    const int32_t outer = package.exports()[size_t(exportIndex)].package;
    if (outer > 0)
        structAt(package, outer - 1);
    auto it = properties_.find(key);
    return it == properties_.end() ? nullptr : it->second;
}

VmFunction* Vm::nativeFunction(int number) {
    auto it = nativeFunctions_.find(number);
    if (it != nativeFunctions_.end())
        return it->second;
    if (!nativesScanned_)
        scanNatives();
    it = nativeFunctions_.find(number);
    return it == nativeFunctions_.end() ? nullptr : it->second;
}

// Loads every class declaring numbered natives (Core, Engine...), once.
void Vm::scanNatives() {
    nativesScanned_ = true;
    for (const std::string& n : packages_.names()) {
        const Package* p = packages_.get(n);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i) {
            const Export& e = p->exports()[i];
            if (e.classRef == 0 || lower(p->objectName(e.classRef)) != "function")
                continue;
            ScriptObject so;
            if (readScriptObject(*p, int(i), so) || so.native == 0 || nativeFunctions_.count(so.native))
                continue;
            structAt(*p, int(i));
        }
    }
}

VmObject* Vm::classObject(VmClass* cls) {
    if (cls->object)
        return cls->object;
    auto o = std::make_unique<VmObject>();
    o->asClass = cls;
    o->name = cls->nameId;
    o->package = cls->package;
    o->exportIndex = cls->exportIndex;
    cls->object = o.get();
    objects_.push_back(std::move(o));
    return cls->object;
}

VmObject* Vm::objectAt(const Package& package, int32_t ref) {
    if (ref == 0)
        return nullptr;
    const ObjectRef r = packages_.resolve(package, ref);
    if (!r)
        return nullptr;
    const auto key = std::make_pair(r.package, r.exportIndex);
    if (auto it = exportObjects_.find(key); it != exportObjects_.end())
        return it->second;
    const Export& e = r.package->exports()[size_t(r.exportIndex)];
    if (e.classRef == 0) {
        VmClass* c = classAt(*r.package, r.exportIndex);
        return c ? classObject(c) : nullptr;
    }
    // An object the VM doesn't run: its class's defaults, and where it is.
    const ObjectRef cref = packages_.resolve(*r.package, e.classRef);
    VmClass* cls = cref ? classAt(*cref.package, cref.exportIndex) : nullptr;
    auto o = std::make_unique<VmObject>();
    o->cls = cls;
    if (cls)
        o->values = cls->defaults;
    o->name = name(r.package->objectName(r.exportIndex + 1));
    o->package = r.package;
    o->exportIndex = r.exportIndex;
    VmObject* out = o.get();
    objects_.push_back(std::move(o));
    exportObjects_[key] = out;
    // Its own properties over the defaults: scripts read them (The Fallen's
    // HUD textures keep where they go on the screen in theirs). Textures
    // and anything small; meshes and sounds aren't read whole for it.
    bool texture = false;
    for (const VmClass* c = cls; c && !texture; c = c->parent())
        texture = lower(c->name) == "texture";
    if (cls && (texture || e.serialSize < 65536)) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (!r.package->readExport(r.exportIndex, data)) {
            Reader rd(data, size_t(e.serialOffset));
            if (!beginObject(*r.package, r.exportIndex, rd, props))
                applyProperties(out, *r.package, props);
        }
    }
    return out;
}

VmObject* Vm::create(VmClass* cls, NameId objName, VmObject* outer) {
    auto o = std::make_unique<VmObject>();
    o->cls = cls;
    o->values = cls->defaults;
    o->name = objName ? objName : name(cls->name + std::to_string(nameCounts_[cls]++));
    // Object's native properties, which scripts read.
    if (Value* v = o->find(*this, "Outer"))
        v->o = outer;
    if (Value* v = o->find(*this, "Name"))
        v->i = o->name;
    if (Value* v = o->find(*this, "Class"))
        v->o = classObject(cls);
    for (const VmClass* c = cls; c; c = c->parent())
        if (lower(c->name) == "actor")
            o->isActor = true;
    VmObject* out = o.get();
    objects_.push_back(std::move(o));
    return out;
}

void Vm::adopt(const Package& package, int exportIndex, VmObject* object) {
    exportObjects_[{&package, exportIndex}] = object;
    object->package = &package;
    object->exportIndex = exportIndex;
}

void Vm::applyProperties(VmObject* object, const Package& package, const PropertyList& props) {
    if (!object->cls)
        return;
    for (const Property& p : props.items) {
        VmProperty* prop = object->cls->findProperty(name(p.name));
        if (!prop) {
            reportError(object->cls->name + ": no property " + p.name);
            continue;
        }
        if (p.arrayIndex < 0 || p.arrayIndex >= prop->arrayDim)
            continue;
        object->values[size_t(prop->slot + p.arrayIndex)] = valueOf(*prop, package, p);
    }
}

Value Vm::valueOf(const VmProperty& p, const Package& package, const Property& data) {
    if (p.type == VType::Bool)
        return Value::boolValue(data.boolValue);
    Reader r(data.data);
    Value v = readValue(p, package, r);
    if (!r.ok() || r.remaining() != 0)
        reportError("the value of " + p.name + " (" + std::to_string(data.data.size()) + " bytes) doesn't read as its type");
    return v;
}

// A value as properties and struct fields store it.
Value Vm::readValue(const VmProperty& p, const Package& package, Reader& r) {
    switch (p.type) {
    case VType::Byte: return Value::byteValue(r.u8());
    case VType::Int: return Value::intValue(r.i32());
    case VType::Bool: return Value::boolValue(r.u8() != 0);
    case VType::Float: return Value::floatValue(r.f32());
    case VType::Name: {
        const int32_t n = r.index();
        return Value::nameValue(n >= 0 && size_t(n) < package.names().size() ? name(package.name(n)) : 0);
    }
    case VType::String: return Value::stringValue(r.string(package.version()));
    case VType::Object: return Value::objectValue(objectAt(package, r.index()));
    case VType::Struct: return p.structType ? readStruct(*p.structType, package, r) : Value();
    default: return Value();
    }
}

Value Vm::readStruct(const VmStruct& st, const Package& package, Reader& r) {
    Value v;
    v.type = VType::Struct;
    v.st = &st;
    v.fields = st.defaults;
    for (const VmProperty* p : st.properties)
        for (int k = 0; k < p->arrayDim; ++k)
            v.fields[size_t(p->slot + k)] = readValue(*p, package, r);
    return v;
}

// --- Values, converted -----------------------------------------------------------------

std::string Vm::toString(const Value& v) const {
    char buf[96];
    switch (v.type) {
    case VType::Byte:
    case VType::Int: return std::to_string(v.i);
    case VType::Bool: return v.i ? "True" : "False";
    case VType::Float:
        std::snprintf(buf, sizeof buf, "%.2f", double(v.f));
        return buf;
    case VType::Name: return nameText(v.i);
    case VType::String: return v.s;
    case VType::Object: return const_cast<Vm*>(this)->pathName(v.asObject());
    case VType::Struct:
        if (v.fields.size() == 3 && v.fields[0].type == VType::Float) {
            std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", double(v.fields[0].f), double(v.fields[1].f),
                          double(v.fields[2].f));
            return buf;
        }
        if (v.fields.size() == 3 && v.fields[0].type == VType::Int) {
            std::snprintf(buf, sizeof buf, "%d,%d,%d", v.fields[0].i, v.fields[1].i, v.fields[2].i);
            return buf;
        }
        return v.st ? v.st->name : "struct";
    default: return "";
    }
}

Value Vm::convert(const Value& v, const VmProperty& to) const {
    switch (to.type) {
    case VType::Byte: return Value::byteValue(v.asInt());
    case VType::Int: return Value::intValue(v.asInt());
    case VType::Bool: return Value::boolValue(v.asBool());
    case VType::Float: return Value::floatValue(v.asFloat());
    case VType::Name:
        if (v.type == VType::Name)
            return v;
        if (v.type == VType::String)
            return Value::nameValue(const_cast<Vm*>(this)->name(v.s));
        return Value::nameValue(0);
    case VType::String: return Value::stringValue(toString(v));
    case VType::Object: return Value::objectValue(v.asObject());
    case VType::Struct:
        if (v.type == VType::Struct)
            return v;
        return to.zero();
    default: return v;
    }
}

void Vm::assign(Value& dst, const Value& src) const {
    switch (dst.type) {
    case VType::Byte: dst.i = src.asInt() & 0xff; break;
    case VType::Int: dst.i = src.asInt(); break;
    case VType::Bool: dst.i = src.asBool() ? 1 : 0; break;
    case VType::Float: dst.f = src.asFloat(); break;
    case VType::Name:
        if (src.type == VType::Name)
            dst.i = src.i;
        else if (src.type == VType::String)
            dst.i = const_cast<Vm*>(this)->name(src.s);
        else
            dst.i = 0;
        break;
    case VType::String: dst.s = toString(src); break;
    case VType::Object: dst.o = src.asObject(); break;
    case VType::Struct:
        if (src.type == VType::Struct && (!dst.st || src.fields.size() == dst.fields.size())) {
            dst.fields = src.fields;
            dst.st = src.st;
        }
        break;
    case VType::None: dst = src; break;
    }
}

bool Vm::equal(const Value& a, const Value& b) const {
    const VType t = a.type != VType::None ? a.type : b.type;
    switch (t) {
    case VType::Float: return a.asFloat() == b.asFloat();
    case VType::String: return toString(a) == toString(b);
    case VType::Object: return a.asObject() == b.asObject();
    case VType::Struct:
        if (a.fields.size() != b.fields.size())
            return false;
        for (size_t k = 0; k < a.fields.size(); ++k)
            if (!equal(a.fields[k], b.fields[k]))
                return false;
        return true;
    default: return a.asInt() == b.asInt();
    }
}

bool Vm::isA(const VmObject* o, std::string_view className) {
    if (!o || !o->cls)
        return false;
    const NameId n = name(className);
    for (const VmClass* c = o->cls; c; c = c->parent())
        if (c->nameId == n)
            return true;
    return false;
}

const VmStruct* Vm::coreStruct(std::string_view structName) {
    const NameId n = name(structName);
    if (auto it = coreStructs_.find(n); it != coreStructs_.end())
        return it->second;
    findClass("Core.Object"); // loads Object's structs
    auto it = coreStructs_.find(n);
    return it == coreStructs_.end() ? nullptr : it->second;
}

Value Vm::vectorValue(float x, float y, float z) {
    Value v;
    v.type = VType::Struct;
    v.st = coreStruct("Vector");
    v.fields = {Value::floatValue(x), Value::floatValue(y), Value::floatValue(z)};
    return v;
}

Value Vm::rotatorValue(int32_t pitch, int32_t yaw, int32_t roll) {
    Value v;
    v.type = VType::Struct;
    v.st = coreStruct("Rotator");
    v.fields = {Value::intValue(pitch), Value::intValue(yaw), Value::intValue(roll)};
    return v;
}

std::array<float, 3> Vm::vectorOf(const Value& v) {
    if (v.fields.size() < 3)
        return {0, 0, 0};
    return {v.fields[0].asFloat(), v.fields[1].asFloat(), v.fields[2].asFloat()};
}

std::array<int32_t, 3> Vm::rotatorOf(const Value& v) {
    if (v.fields.size() < 3)
        return {0, 0, 0};
    return {v.fields[0].asInt(), v.fields[1].asInt(), v.fields[2].asInt()};
}

std::string Vm::where() const {
    if (running_.empty())
        return "";
    const VmStruct* s = running_.back();
    if (s->kind == VmStruct::Kind::Function)
        return " in " + static_cast<const VmFunction*>(s)->qualified;
    if (s->kind == VmStruct::Kind::State)
        return " in " + static_cast<const VmState*>(s)->owner->name + "." + s->name + " (state code)";
    return " in " + s->name;
}

VmClass* Vm::defineIntrinsic(std::string_view className, VmClass* parent) {
    const NameId n = name(className);
    if (auto it = intrinsics_.find(n); it != intrinsics_.end())
        return it->second.get();
    auto c = std::make_unique<VmClass>();
    c->kind = VmStruct::Kind::Class;
    c->name = std::string(className);
    c->nameId = n;
    c->owner = c.get();
    c->super = parent;
    if (parent) {
        c->package = parent->package;
        c->properties = parent->properties;
        c->slots = parent->slots;
        c->defaults = parent->defaults;
    }
    VmClass* out = c.get();
    intrinsics_.emplace(n, std::move(c));
    classObject(out);
    return out;
}

VmClass* Vm::intrinsic(std::string_view className) {
    auto it = intrinsics_.find(name(className));
    return it == intrinsics_.end() ? nullptr : it->second.get();
}

void Vm::registerNative(std::string_view qualifiedName, NativeFn fn) {
    natives_[lower(qualifiedName)] = std::move(fn);
}

// --- The interpreter -----------------------------------------------------------------------

class Interpreter {
public:
    explicit Interpreter(Vm& vm) : vm_(vm) {}

    struct Iteration {
        std::vector<Value> items;
        size_t next = 0;
        Value* out = nullptr;
        int body = 0;
    };
    struct Frame {
        const VmStruct* owner = nullptr; // the function or state whose code runs
        VmObject* self = nullptr;
        std::vector<Value>* locals = nullptr;
        Value result;
        std::vector<Iteration> iterations;
        Value switchValue;
        std::deque<Value> temps; // values that aren't variables, for references to them
        uint64_t statements = 0;
    };
    enum class Flow { Return, Stop, Wait, Restart };

    // Runs statements from `pc`; on Wait, `pc` is the statement to resume at.
    Flow run(Frame& f, int& pc) {
        vm_.running_.push_back(f.owner);
        const Flow flow = runStatements(f, pc);
        vm_.running_.pop_back();
        return flow;
    }

    Flow runStatements(Frame& f, int& pc) {
        const Code& code = f.owner->code;
        const int stateSerial = f.self ? f.self->stateSerial : 0;
        const bool stateCode = f.owner->kind == VmStruct::Kind::State || f.owner->kind == VmStruct::Kind::Class;
        while (pc >= 0 && size_t(pc) < code.statements.size()) {
            if (++f.statements > (stateCode ? kMaxStateStatements : kMaxStatements)) {
                vm_.reportError("a loop that doesn't end in " + where(f));
                return Flow::Return;
            }
            ++vm_.stats.statements;
            f.temps.clear();
            const int si = code.statements[size_t(pc)];
            if (si < 0)
                return Flow::Return;
            const Expr& x = code.exprs[size_t(si)];
            switch (x.token) {
            case token::Jump: pc = at(f, uint32_t(x.value)); continue;
            case token::JumpIfNot:
                pc = eval(f, arg(x, 0), f.self).asBool() ? pc + 1 : at(f, uint32_t(x.value));
                continue;
            case token::Return:
                if (!x.args.empty() && x.args[0] >= 0 && code.exprs[size_t(x.args[0])].token != token::Nothing)
                    f.result = eval(f, x.args[0], f.self);
                return Flow::Return;
            case token::Stop: return Flow::Stop;
            case token::Switch:
                f.switchValue = eval(f, arg(x, 0), f.self);
                ++pc;
                continue;
            case token::Case:
                if (x.value == 0xffff || vm_.equal(f.switchValue, eval(f, arg(x, 0), f.self)))
                    ++pc;
                else
                    pc = at(f, uint32_t(x.value));
                continue;
            case token::Iterator: {
                Iteration it;
                it.body = pc + 1;
                iterate(f, arg(x, 0), it);
                const bool empty = it.items.empty() || !it.out;
                if (!empty)
                    vm_.assign(*it.out, it.items[0]);
                it.next = 1;
                f.iterations.push_back(std::move(it));
                pc = empty ? at(f, uint32_t(x.value)) : pc + 1;
                continue;
            }
            case token::IteratorNext:
                if (!f.iterations.empty()) {
                    Iteration& it = f.iterations.back();
                    // Destroyed actors drop out of the list as it goes.
                    while (it.next < it.items.size() && it.items[it.next].type == VType::Object &&
                           !it.items[it.next].asObject())
                        ++it.next;
                    if (it.next < it.items.size()) {
                        vm_.assign(*it.out, it.items[it.next++]);
                        pc = it.body;
                        continue;
                    }
                }
                ++pc;
                continue;
            case token::IteratorPop:
                if (!f.iterations.empty())
                    f.iterations.pop_back();
                ++pc;
                continue;
            case token::GotoLabel: {
                const NameId label = eval(f, arg(x, 0), f.self).i;
                const int to = labelAt(f.owner, label);
                if (to < 0) {
                    // The script's own mistake (DS9_GrigariSentryDrone goes
                    // to 'DetermineAction', a function): reported, and the
                    // code carries on.
                    vm_.reportError("goto a label that isn't there, " + vm_.nameText(label) + ", in " + where(f));
                    ++pc;
                    continue;
                }
                pc = to;
                continue;
            }
            case token::Nothing:
            case token::LabelTable:
            case token::EndFunctionParms: ++pc; continue;
            case token::Assert:
                if (!eval(f, arg(x, 0), f.self).asBool())
                    vm_.reportError("an assertion failed in " + where(f) + " (line " + std::to_string(x.value) + ")");
                ++pc;
                continue;
            default:
                eval(f, si, f.self);
                ++pc;
                if (vm_.pendingLatent_ >= 0) {
                    const float wait = vm_.pendingLatent_;
                    vm_.pendingLatent_ = -1;
                    if (stateCode && f.self) {
                        f.self->sleeping = wait;
                        f.self->latent = true;
                        return Flow::Wait;
                    }
                }
                if (stateCode && f.self && f.self->stateSerial != stateSerial)
                    return Flow::Restart; // GotoState from its own code
                continue;
            }
        }
        return Flow::Return;
    }

    Value eval(Frame& f, int i, VmObject* ctx) {
        if (i < 0)
            return Value();
        const Code& code = f.owner->code;
        const Expr& x = code.exprs[size_t(i)];
        using namespace token;
        if (x.native >= 0)
            return call(f, x, ctx, vm_.nativeFunction(x.native));
        if (x.token >= FirstConversion && x.token <= LastConversion)
            return conversion(x.token, eval(f, arg(x, 0), ctx));
        switch (x.token) {
        case LocalVariable:
        case InstanceVariable:
        case DefaultVariable:
        case ArrayElement:
        case StructMember:
        case BoolVariable: {
            Value* v = ref(f, i, ctx);
            return v ? *v : Value();
        }
        case Context:
        case ClassContext: {
            VmObject* o = eval(f, arg(x, 0), ctx).asObject();
            if (!o) {
                accessedNone(f);
                return Value();
            }
            return eval(f, arg(x, 1), o);
        }
        case Let:
        case LetBool: {
            const Value v = eval(f, arg(x, 1), ctx);
            if (Value* d = ref(f, arg(x, 0), ctx))
                vm_.assign(*d, v);
            return Value();
        }
        case VirtualFunction:
        case GlobalFunction: {
            VmObject* target = ctx ? ctx : f.self;
            const NameId n = vm_.name(f.owner->package->name(x.ref));
            VmFunction* fn = target ? vm_.findFunction(target, n, x.token == GlobalFunction) : nullptr;
            if (!fn) {
                vm_.reportError("no function " + vm_.nameText(n) + " for " +
                                (target && target->cls ? target->cls->name : std::string("None")) + " (in " +
                                where(f) + ")");
                return Value();
            }
            return call(f, x, target, fn);
        }
        case FinalFunction: return call(f, x, ctx ? ctx : f.self, function(f, i, x.ref));
        case IntConst:
        case IntConstByte: return Value::intValue(x.value);
        case ByteConst: return Value::byteValue(x.value);
        case IntZero: return Value::intValue(0);
        case IntOne: return Value::intValue(1);
        case True: return Value::boolValue(true);
        case False: return Value::boolValue(false);
        case FloatConst: return Value::floatValue(x.f[0]);
        case StringConst:
        case UnicodeStringConst: return Value::stringValue(x.text);
        case NameConst: return Value::nameValue(vm_.name(f.owner->package->name(x.ref)));
        case ObjectConst: return Value::objectValue(vm_.objectAt(*f.owner->package, x.ref));
        case VectorConst: return vm_.vectorValue(x.f[0], x.f[1], x.f[2]);
        case RotationConst: return vm_.rotatorValue(x.rot[0], x.rot[1], x.rot[2]);
        case Self: return Value::objectValue(f.self);
        case NoObject: return Value::objectValue(nullptr);
        case Nothing: return Value();
        case Skip:
        case EatString: return eval(f, arg(x, 0), ctx);
        case DynamicCast: {
            VmObject* o = eval(f, arg(x, 0), ctx).asObject();
            VmClass* to = classRef(f, i, x.ref);
            if (!o || !to)
                return Value::objectValue(nullptr);
            if (o->asClass) // casting a class to Class or Object
                return Value::objectValue(lowerName(to) == "class" || lowerName(to) == "object" ? o : nullptr);
            return Value::objectValue(o->cls && o->cls->isA(to) ? o : nullptr);
        }
        case MetaCast: {
            VmObject* o = eval(f, arg(x, 0), ctx).asObject();
            VmClass* to = classRef(f, i, x.ref);
            return Value::objectValue(o && o->asClass && to && o->asClass->isA(to) ? o : nullptr);
        }
        case StructCmpEq:
        case StructCmpNe: {
            const bool same = vm_.equal(eval(f, arg(x, 0), ctx), eval(f, arg(x, 1), ctx));
            return Value::boolValue(x.token == StructCmpEq ? same : !same);
        }
        case New: {
            VmObject* clsObj = eval(f, arg(x, 3), ctx).asObject();
            if (!clsObj || !clsObj->asClass) {
                vm_.reportError("new without a class in " + where(f));
                return Value::objectValue(nullptr);
            }
            VmObject* outer = eval(f, arg(x, 0), ctx).asObject();
            const Value objName = eval(f, arg(x, 1), ctx);
            return Value::objectValue(vm_.create(clsObj->asClass, objName.type == VType::Name ? objName.i : 0,
                                                 outer ? outer : f.self));
        }
        default:
            vm_.reportError("token 0x" + hex(x.token) + " can't be evaluated (in " + where(f) + ")");
            return Value();
        }
    }

    // A reference to a variable, or to a temporary holding the value.
    Value* ref(Frame& f, int i, VmObject* ctx) {
        if (i < 0)
            return temp(f, Value());
        const Expr& x = f.owner->code.exprs[size_t(i)];
        using namespace token;
        switch (x.token) {
        case LocalVariable: {
            VmProperty* p = property(f, i, x.ref);
            if (!p || !f.locals || size_t(p->slot) >= f.locals->size()) {
                vm_.reportError("a local variable that isn't there, in " + where(f));
                return temp(f, Value());
            }
            return &(*f.locals)[size_t(p->slot)];
        }
        case InstanceVariable:
        case DefaultVariable: {
            VmProperty* p = property(f, i, x.ref);
            VmObject* o = ctx ? ctx : f.self;
            if (!p || !o) {
                vm_.reportError("a variable that isn't there, in " + where(f));
                return temp(f, Value());
            }
            std::vector<Value>* values = &o->values;
            if (x.token == DefaultVariable || o->asClass) {
                VmClass* c = o->asClass ? o->asClass : o->cls;
                values = c ? &c->defaults : nullptr;
            }
            if (!values || size_t(p->slot) >= values->size()) {
                vm_.reportError("variable " + p->name + " isn't in " +
                                (o->cls ? o->cls->name : vm_.objectName(o)) + " (in " + where(f) + ")");
                return temp(f, p->zero());
            }
            return &(*values)[size_t(p->slot)];
        }
        case BoolVariable: return ref(f, arg(x, 0), ctx);
        case Context:
        case ClassContext: {
            VmObject* o = eval(f, arg(x, 0), ctx).asObject();
            if (!o) {
                accessedNone(f);
                return temp(f, Value());
            }
            return ref(f, arg(x, 1), o);
        }
        case ArrayElement: {
            const int index = eval(f, arg(x, 0), f.self).asInt();
            const int arrayExpr = arg(x, 1);
            Value* base = ref(f, arrayExpr, ctx);
            const int dim = arrayDim(f, arrayExpr);
            if (index < 0 || index >= dim) {
                vm_.reportError("an array index out of range (" + std::to_string(index) + " of " +
                                std::to_string(dim) + ") in " + where(f));
                return temp(f, base ? *base : Value());
            }
            return base + index;
        }
        case StructMember: {
            Value* base = ref(f, arg(x, 0), ctx);
            VmProperty* p = property(f, i, x.ref);
            if (base && base->type == VType::None) // after None was accessed, already counted
                return temp(f, p ? p->zero() : Value());
            if (!base || !p || size_t(p->slot) >= base->fields.size()) {
                vm_.reportError("a struct member that isn't there, in " + where(f));
                return temp(f, p ? p->zero() : Value());
            }
            return &base->fields[size_t(p->slot)];
        }
        default: return temp(f, eval(f, i, ctx));
        }
    }

    // A call: its arguments evaluated in the caller (out ones by reference),
    // then the function, script or native.
    Value call(Frame& f, const Expr& x, VmObject* target, VmFunction* fn) {
        if (!fn) {
            vm_.reportError("a call to a function that isn't there (native " + std::to_string(x.native) + ") in " +
                            where(f));
            return Value();
        }
        std::vector<Value> args(fn->params.size());
        std::vector<Value*> outs(fn->params.size(), nullptr);
        std::vector<bool> given(fn->params.size(), false);
        // && and || leave their right side unevaluated when the left decides.
        const bool andOp = fn->qualified == "Object.AndAnd_BoolBool", orOp = fn->qualified == "Object.OrOr_BoolBool";
        for (size_t k = 0; k < fn->params.size(); ++k) {
            const int a = k < x.args.size() ? x.args[k] : -1;
            const VmProperty& p = *fn->params[k];
            if (a < 0 || f.owner->code.exprs[size_t(a)].token == token::EndFunctionParms ||
                f.owner->code.exprs[size_t(a)].token == token::Nothing) {
                args[k] = p.zero();
                continue;
            }
            if (k == 1 && (andOp || orOp)) {
                const bool left = args[0].asBool();
                if ((andOp && !left) || (orOp && left))
                    return Value::boolValue(left);
                return Value::boolValue(eval(f, a, f.self).asBool());
            }
            given[k] = true;
            if (p.flags & propflag::OutParm) {
                outs[k] = ref(f, a, f.self);
                args[k] = *outs[k];
            } else {
                args[k] = vm_.convert(eval(f, a, f.self), p);
            }
        }
        return invoke(fn, target, std::move(args), outs, given, nullptr);
    }

    Value invoke(VmFunction* fn, VmObject* self, std::vector<Value> args, const std::vector<Value*>& outs,
                 std::vector<bool> given, std::vector<Value>* items) {
        if (vm_.depth_ >= kMaxDepth) {
            vm_.reportError("calls nested too deep, at " + fn->qualified);
            return fn->returnValue ? fn->returnValue->zero() : Value();
        }
        ++vm_.stats.calls;
        if (given.size() < args.size())
            given.resize(args.size(), true);
        Value result = fn->returnValue ? fn->returnValue->zero() : Value();
        if (fn->flags & funcflag::Native) {
            ++vm_.stats.nativeCalls;
            auto it = vm_.natives_.find(lower(fn->qualified));
            if (it == vm_.natives_.end()) {
                ++vm_.stats.missingNatives[fn->qualified];
                // A latent one still waits a tick, so state code that
                // loops round it doesn't spin.
                if (fn->flags & funcflag::Latent)
                    vm_.pendingLatent_ = 0;
                return result;
            }
            NativeCall c{vm_, self, fn, std::move(args), std::move(given), result, -1, {}};
            ++vm_.depth_;
            it->second(c);
            --vm_.depth_;
            for (size_t k = 0; k < outs.size() && k < c.args.size(); ++k)
                if (outs[k])
                    vm_.assign(*outs[k], c.args[k]);
            if (items)
                *items = std::move(c.items);
            if (c.latentSeconds >= 0)
                vm_.pendingLatent_ = c.latentSeconds;
            if (fn->returnValue)
                return vm_.convert(c.result, *fn->returnValue);
            return c.result;
        }
        Frame frame;
        frame.owner = fn;
        frame.self = self;
        std::vector<Value> locals = fn->defaults;
        for (size_t k = 0; k < fn->params.size() && k < args.size(); ++k)
            vm_.assign(locals[size_t(fn->params[k]->slot)], args[k]);
        frame.locals = &locals;
        ++vm_.depth_;
        int pc = 0;
        run(frame, pc);
        --vm_.depth_;
        vm_.pendingLatent_ = -1; // latent functions only wait in state code
        for (size_t k = 0; k < outs.size() && k < fn->params.size(); ++k)
            if (outs[k])
                vm_.assign(*outs[k], locals[size_t(fn->params[k]->slot)]);
        if (fn->returnValue && frame.result.type != VType::None)
            return vm_.convert(frame.result, *fn->returnValue);
        return result;
    }

private:
    // foreach: the iterator function's results, and its out parameter.
    void iterate(Frame& f, int callExpr, Iteration& it) {
        if (callExpr < 0)
            return;
        const Expr& x = f.owner->code.exprs[size_t(callExpr)];
        VmObject* target = f.self;
        const Expr* call = &x;
        // Through a context (Level.AllActors...).
        if ((x.token == token::Context || x.token == token::ClassContext) && x.args.size() >= 2) {
            target = eval(f, x.args[0], f.self).asObject();
            if (!target) {
                accessedNone(f);
                return;
            }
            call = &f.owner->code.exprs[size_t(x.args[1])];
        }
        VmFunction* fn = nullptr;
        if (call->native >= 0)
            fn = vm_.nativeFunction(call->native);
        else if (call->token == token::FinalFunction)
            fn = function(f, int(call - f.owner->code.exprs.data()), call->ref);
        else if (call->token == token::VirtualFunction)
            fn = vm_.findFunction(target, vm_.name(f.owner->package->name(call->ref)));
        if (!fn) {
            vm_.reportError("foreach over an unknown function in " + where(f));
            return;
        }
        std::vector<Value> args(fn->params.size());
        std::vector<Value*> outs(fn->params.size(), nullptr);
        std::vector<bool> given(fn->params.size(), false);
        for (size_t k = 0; k < fn->params.size(); ++k) {
            const int a = k < call->args.size() ? call->args[k] : -1;
            const VmProperty& p = *fn->params[k];
            if (a < 0 || f.owner->code.exprs[size_t(a)].token == token::EndFunctionParms ||
                f.owner->code.exprs[size_t(a)].token == token::Nothing) {
                args[k] = p.zero();
                continue;
            }
            given[k] = true;
            if (p.flags & propflag::OutParm) {
                outs[k] = ref(f, a, f.self);
                args[k] = *outs[k];
                if (!it.out)
                    it.out = outs[k];
            } else {
                args[k] = vm_.convert(eval(f, a, f.self), p);
            }
        }
        // The out parameters aren't written back: the loop sets the first.
        invoke(fn, target, std::move(args), {}, std::move(given), &it.items);
    }

    Value conversion(uint8_t t, const Value& v) {
        switch (t) {
        case 0x39: { // rotator to vector: the direction it points
            const auto r = Vm::rotatorOf(v);
            const float p = float(r[0]) * 2 * kPi / 65536, y = float(r[1]) * 2 * kPi / 65536;
            return vm_.vectorValue(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
        }
        case 0x3a: case 0x41: case 0x44: case 0x46: case 0x4a: return Value::intValue(v.asInt());
        case 0x3b: case 0x3e: case 0x45: case 0x47: case 0x48: case 0x4b: return Value::boolValue(v.asBool());
        case 0x3c: case 0x3f: case 0x42: case 0x4c: return Value::floatValue(v.asFloat());
        case 0x3d: case 0x40: case 0x43: case 0x49: return Value::byteValue(v.asInt());
        case 0x4d: { // string to vector: "x,y,z"
            float a = 0, b = 0, c = 0;
            std::sscanf(v.s.c_str(), "%f,%f,%f", &a, &b, &c);
            return vm_.vectorValue(a, b, c);
        }
        case 0x4e: {
            int a = 0, b = 0, c = 0;
            std::sscanf(v.s.c_str(), "%d,%d,%d", &a, &b, &c);
            return vm_.rotatorValue(a, b, c);
        }
        case 0x4f: {
            const auto a = Vm::vectorOf(v);
            return Value::boolValue(a[0] != 0 || a[1] != 0 || a[2] != 0);
        }
        case 0x50: { // vector to rotator: pointing along it
            const auto a = Vm::vectorOf(v);
            const float yaw = std::atan2(a[1], a[0]);
            const float pitch = std::atan2(a[2], std::sqrt(a[0] * a[0] + a[1] * a[1]));
            return vm_.rotatorValue(int32_t(pitch * 32768 / kPi) & 0xffff, int32_t(yaw * 32768 / kPi) & 0xffff, 0);
        }
        case 0x51: {
            const auto r = Vm::rotatorOf(v);
            return Value::boolValue(r[0] != 0 || r[1] != 0 || r[2] != 0);
        }
        default:
            if (t >= 0x52 && t <= 0x59)
                return Value::stringValue(vm_.toString(v));
            return v;
        }
    }

    int arg(const Expr& x, size_t k) const { return k < x.args.size() ? x.args[k] : -1; }
    Value* temp(Frame& f, Value v) {
        f.temps.push_back(std::move(v));
        return &f.temps.back();
    }
    int at(Frame& f, uint32_t offset) {
        auto it = f.owner->statementAt.find(offset);
        if (it == f.owner->statementAt.end()) {
            vm_.reportError("a jump into the middle of a statement in " + where(f));
            return -1;
        }
        return it->second;
    }
    int labelAt(const VmStruct* owner, NameId label) {
        if (owner->kind != VmStruct::Kind::State && owner->kind != VmStruct::Kind::Class)
            return -1;
        const auto* st = static_cast<const VmState*>(owner);
        auto it = st->labels.find(label);
        if (it == st->labels.end())
            return -1;
        auto s = owner->statementAt.find(it->second);
        return s == owner->statementAt.end() ? -1 : s->second;
    }
    void accessedNone(Frame& f) {
        ++vm_.stats.accessedNone;
        vm_.stats.errors["Accessed None in " + where(f)]++;
    }
    std::string where(const Frame& f) const {
        if (f.owner->kind == VmStruct::Kind::Function)
            return static_cast<const VmFunction*>(f.owner)->qualified;
        if (f.owner->kind == VmStruct::Kind::State)
            return static_cast<const VmState*>(f.owner)->owner->name + "." + f.owner->name + " (state code)";
        return f.owner->name;
    }
    static std::string hex(uint8_t v) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02x", v);
        return buf;
    }
    std::string lowerName(const VmClass* c) const { return lower(c->name); }

    int arrayDim(Frame& f, int i) {
        if (i < 0)
            return 1;
        const Expr& x = f.owner->code.exprs[size_t(i)];
        if (x.token == token::LocalVariable || x.token == token::InstanceVariable || x.token == token::DefaultVariable) {
            VmProperty* p = property(f, i, x.ref);
            return p ? p->arrayDim : 1;
        }
        if (x.token == token::Context || x.token == token::ClassContext)
            return arrayDim(f, arg(x, 1));
        if (x.token == token::StructMember) {
            VmProperty* p = property(f, i, x.ref);
            return p ? p->arrayDim : 1;
        }
        return 1;
    }

    // An expression's reference, resolved once.
    VmProperty* property(Frame& f, int i, int32_t ref) {
        const void*& cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = vm_.packages().resolve(*f.owner->package, ref);
            cached = r ? vm_.propertyAt(*r.package, r.exportIndex) : nullptr;
        }
        return static_cast<VmProperty*>(const_cast<void*>(cached));
    }
    VmFunction* function(Frame& f, int i, int32_t ref) {
        const void*& cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = vm_.packages().resolve(*f.owner->package, ref);
            VmStruct* s = r ? vm_.structAt(*r.package, r.exportIndex) : nullptr;
            cached = s && s->kind == VmStruct::Kind::Function ? s : nullptr;
        }
        return static_cast<VmFunction*>(const_cast<void*>(cached));
    }
    VmClass* classRef(Frame& f, int i, int32_t ref) {
        const void*& cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = vm_.packages().resolve(*f.owner->package, ref);
            cached = r ? vm_.classAt(*r.package, r.exportIndex) : vm_.intrinsic(f.owner->package->objectName(ref));
        }
        return static_cast<VmClass*>(const_cast<void*>(cached));
    }

    Vm& vm_;
};

// --- Calls, states, ticking -------------------------------------------------------------

VmFunction* Vm::findFunction(VmObject* self, NameId fname, bool global) {
    if (!self)
        return nullptr;
    if (self->asClass) {
        for (VmClass* c = self->asClass; c; c = c->parent())
            if (auto it = c->functions.find(fname); it != c->functions.end())
                return it->second;
        return nullptr;
    }
    if (!global)
        for (VmStruct* s = self->state; s && s->kind == VmStruct::Kind::State; s = s->super) {
            auto* st = static_cast<VmState*>(s);
            if (auto it = st->functions.find(fname); it != st->functions.end())
                return it->second;
        }
    for (VmClass* c = self->cls; c; c = c->parent())
        if (auto it = c->functions.find(fname); it != c->functions.end())
            return it->second;
    return nullptr;
}

Value Vm::invoke(VmFunction* f, VmObject* self, std::vector<Value> args, std::vector<Value*> outs) {
    Interpreter in(*this);
    std::vector<Value> converted(f->params.size());
    std::vector<bool> given(f->params.size(), false);
    for (size_t k = 0; k < f->params.size(); ++k) {
        given[k] = k < args.size();
        converted[k] = k < args.size() ? convert(args[k], *f->params[k]) : f->params[k]->zero();
    }
    outs.resize(f->params.size(), nullptr);
    return in.invoke(f, self, std::move(converted), outs, std::move(given), nullptr);
}

Value Vm::callEvent(VmObject* self, std::string_view function, std::vector<Value> args) {
    if (!self || self->deleted)
        return Value();
    const NameId n = name(function);
    if (self->disabled.count(n))
        return Value();
    VmFunction* f = findFunction(self, n);
    return f ? invoke(f, self, std::move(args)) : Value();
}

VmState* Vm::findState(VmClass* cls, NameId stateName) {
    const bool autoState = lower(nameText(stateName)) == "auto";
    for (VmClass* c = cls; c; c = c->parent()) {
        if (autoState) {
            for (const auto& [n, st] : c->states)
                if (st->stateFlags & 0x2) // auto
                    return st;
            continue;
        }
        if (auto it = c->states.find(stateName); it != c->states.end())
            return it->second;
    }
    return nullptr;
}

void Vm::gotoState(VmObject* self, NameId stateName, NameId label) {
    if (!self || !self->cls || self->deleted)
        return;
    VmState* next = stateName == 0 ? nullptr : findState(self->cls, stateName);
    if (stateName != 0 && !next && lower(nameText(stateName)) != "auto") {
        reportError(self->cls->name + " has no state " + nameText(stateName));
        return;
    }
    if (next != self->state) {
        if (self->state)
            callEvent(self, "EndState");
        self->state = next;
        ++self->stateSerial;
        if (next)
            callEvent(self, "BeginState");
    } else {
        ++self->stateSerial;
    }
    self->codeAt = -1;
    self->codeState = nullptr;
    self->latent = false;
    self->sleeping = 0;
    if (self->state) {
        const NameId want = label ? label : name("Begin");
        for (VmStruct* s = self->state; s && s->kind == VmStruct::Kind::State; s = s->super) {
            auto* st = static_cast<VmState*>(s);
            if (auto it = st->labels.find(want); it != st->labels.end()) {
                if (auto a = s->statementAt.find(it->second); a != s->statementAt.end()) {
                    self->codeState = st; // the state that has the label
                    self->codeAt = a->second;
                }
                break;
            }
        }
    }
}

void Vm::tick(VmObject* self, float seconds) {
    if (!self || self->deleted || !self->cls)
        return;
    if (self->timerRate > 0) {
        self->timerCount += seconds;
        if (self->timerCount >= self->timerRate) {
            if (self->timerLoop)
                self->timerCount -= self->timerRate;
            else
                self->timerRate = 0;
            callEvent(self, "Timer");
        }
    }
    callEvent(self, "Tick", {Value::floatValue(seconds)});
    if (self->latent) {
        self->sleeping -= seconds;
        if (self->sleeping > 0)
            return;
        self->latent = false;
    }
    for (int restarts = 0; restarts < 16 && self->codeState && self->codeAt >= 0 && !self->deleted; ++restarts) {
        Interpreter in(*this);
        Interpreter::Frame f;
        f.owner = self->codeState;
        f.self = self;
        int pc = self->codeAt;
        const int serial = self->stateSerial;
        const Interpreter::Flow flow = in.run(f, pc);
        if (self->stateSerial != serial)
            continue; // it changed state: run the new state's code
        self->codeAt = flow == Interpreter::Flow::Wait ? pc : -1;
        break;
    }
}

} // namespace reluva::unreal
