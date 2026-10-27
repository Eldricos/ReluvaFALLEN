// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/class_defaults.h"

#include "engines/unreal/script.h"


namespace reluva::unreal {

const PropertyList* ClassDefaults::defaults(const Package& package, int classExport) {
    const auto key = std::make_pair(&package, classExport);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::optional<PropertyList> found;
        ScriptObject cls;
        if (!readScriptObject(package, classExport, cls) && cls.kind == ScriptObject::Kind::Class)
            found = std::move(cls.defaults);
        ++(found ? found_ : missing_);
        it = cache_.emplace(key, std::move(found)).first;
    }
    return it->second ? &*it->second : nullptr;
}

Error ClassDefaults::actorProperties(const Package& package, int index, ActorProperties& out) {
    out.layers.clear();
    ActorProperties::Layer own;
    own.package = &package;
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    if (Error err = beginObject(package, index, r, own.props))
        return err;
    out.layers.push_back(std::move(own));
    // Up the class chain: each class's defaults, then its parent's.
    const Package* pkg = &package;
    int32_t cls = package.exports()[size_t(index)].classRef;
    for (int depth = 0; depth < 32 && cls != 0; ++depth) {
        const ObjectRef def = packages_.resolve(*pkg, cls);
        if (!def)
            break;
        if (const PropertyList* d = defaults(*def.package, def.exportIndex))
            out.layers.push_back({def.package, *d});
        pkg = def.package;
        cls = pkg->exports()[size_t(def.exportIndex)].superRef;
    }
    return Error::success();
}

const Property* ActorProperties::find(std::string_view name, int arrayIndex, const Package** from) const {
    for (const Layer& l : layers)
        if (const Property* p = l.props.find(name, arrayIndex)) {
            if (from)
                *from = l.package;
            return p;
        }
    return nullptr;
}

uint8_t ActorProperties::byte(std::string_view name, uint8_t fallback, int arrayIndex) const {
    const Property* p = find(name, arrayIndex);
    return p && !p->data.empty() ? p->data[0] : fallback;
}

float ActorProperties::number(std::string_view name, float fallback) const {
    for (const Layer& l : layers)
        if (l.props.find(name))
            return l.props.number(name, fallback);
    return fallback;
}

bool ActorProperties::boolean(std::string_view name, bool fallback) const {
    const Property* p = find(name);
    return p && p->type == PropertyType::Bool ? p->boolValue : fallback;
}

bool ActorProperties::vector(std::string_view name, std::array<float, 3>& out, int arrayIndex) const {
    for (const Layer& l : layers)
        if (l.props.find(name, arrayIndex))
            return l.props.vector(name, out, arrayIndex);
    return false;
}

bool ActorProperties::rotator(std::string_view name, Rotator& out) const {
    for (const Layer& l : layers)
        if (l.props.find(name))
            return l.props.rotator(name, out);
    return false;
}

std::string ActorProperties::nameValue(std::string_view name) const {
    for (const Layer& l : layers)
        if (l.props.find(name))
            return l.package->name(l.props.nameIndex(name));
    return {};
}

ObjectRef ActorProperties::object(const PackageSet& packages, std::string_view name, int arrayIndex) const {
    for (const Layer& l : layers)
        if (l.props.find(name, arrayIndex))
            return packages.resolve(*l.package, l.props.object(name, arrayIndex));
    return {};
}

} // namespace reluva::unreal
