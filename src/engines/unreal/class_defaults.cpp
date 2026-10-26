// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/class_defaults.h"

#include "common/fs.h"

namespace reluva::unreal {

namespace {

// Whether a property's size fits its type, for types of fixed size.
bool plausible(const Property& p) {
    if (fs::toLower(p.name) == "none")
        return false;
    switch (p.type) {
    case PropertyType::None: return false;
    case PropertyType::Byte: return p.data.size() == 1;
    case PropertyType::Int:
    case PropertyType::Float: return p.data.size() == 4;
    case PropertyType::Bool: return p.data.empty();
    case PropertyType::Object:
    case PropertyType::Name:
    case PropertyType::Class: return !p.data.empty() && p.data.size() <= 5;
    case PropertyType::Vector:
    case PropertyType::Rotator: return p.data.size() == 12;
    case PropertyType::Struct:
        if (p.structName == "Vector" || p.structName == "Rotator")
            return p.data.size() == 12;
        return !p.structName.empty() && fs::toLower(p.structName) != "none";
    default: return true;
    }
}

// A compact index ending exactly at `end`, read from `start`.
bool compactEndingAt(const std::vector<uint8_t>& data, size_t start, size_t end, int32_t& value) {
    Reader r(data.data() + start, end - start);
    value = r.index();
    return r.ok() && r.remaining() == 0;
}

} // namespace

bool findTrailingDefaults(const Package& package, const std::vector<uint8_t>& data, PropertyList& out) {
    const size_t window = std::min<size_t>(data.size(), 65536);
    const auto names = int32_t(package.names().size());
    const auto objects = int32_t(std::max(package.imports().size(), package.exports().size()));
    // Earliest first: later starts that also read are tails of the same list.
    for (size_t start = data.size() - window; start < data.size(); ++start) {
        // Just before: the config name (a compact name index), and before
        // that the class's outer (a compact object reference).
        bool preceded = false;
        for (size_t k = 1; k <= 5 && k <= start && !preceded; ++k) {
            int32_t config = 0;
            if (!compactEndingAt(data, start - k, start, config) || config < 0 || config >= names)
                continue;
            for (size_t j = 1; j <= 5 && j <= start - k; ++j) {
                int32_t within = 0;
                if (compactEndingAt(data, start - k - j, start - k, within) && within >= -objects &&
                    within <= objects) {
                    preceded = true;
                    break;
                }
            }
        }
        if (!preceded)
            continue;
        Reader r(data.data() + start, data.size() - start);
        PropertyList props;
        if (readProperties(package, r, props) || r.remaining() != 0)
            continue;
        bool ok = true;
        for (const Property& p : props.items)
            ok = ok && plausible(p);
        if (!ok)
            continue;
        out = std::move(props);
        return true;
    }
    return false;
}

const PropertyList* ClassDefaults::defaults(const Package& package, int classExport) {
    const auto key = std::make_pair(&package, classExport);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::optional<PropertyList> found;
        std::vector<uint8_t> data;
        PropertyList props;
        if (!package.readExport(classExport, data) && findTrailingDefaults(package, data, props))
            found = std::move(props);
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
