// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Classes' default properties, and actors' properties with them. A placed
// actor stores only the properties that differ from its class's defaults;
// a class stores only those that differ from its parent's. The defaults
// are the last thing in a class's data, after its script (script.h reads
// it). See docs/formats/unreal/actors.md.

#include "engines/unreal/package_set.h"
#include "engines/unreal/properties.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reluva::unreal {

// An actor's properties: its own, then its class's defaults and each
// parent's, most specific first. Object references are resolved in the
// package each property came from.
class ActorProperties {
public:
    struct Layer {
        const Package* package = nullptr;
        PropertyList props;
    };
    std::vector<Layer> layers;

    const Property* find(std::string_view name, int arrayIndex = 0, const Package** from = nullptr) const;
    uint8_t byte(std::string_view name, uint8_t fallback = 0, int arrayIndex = 0) const;
    float number(std::string_view name, float fallback = 0) const;
    bool boolean(std::string_view name, bool fallback = false) const;
    bool vector(std::string_view name, std::array<float, 3>& out, int arrayIndex = 0) const;
    bool rotator(std::string_view name, Rotator& out) const;
    std::string nameValue(std::string_view name) const; // Name properties
    ObjectRef object(const PackageSet& packages, std::string_view name, int arrayIndex = 0) const;
};

class ClassDefaults {
public:
    explicit ClassDefaults(const PackageSet& packages) : packages_(packages) {}

    // A class export's own default properties; nullptr if they weren't found.
    const PropertyList* defaults(const Package& package, int classExport);

    // An actor's properties with its classes' defaults under them.
    Error actorProperties(const Package& package, int exportIndex, ActorProperties& out);

    int found() const { return found_; }
    int missing() const { return missing_; }

private:
    const PackageSet& packages_;
    std::map<std::pair<const Package*, int>, std::optional<PropertyList>> cache_;
    int found_ = 0, missing_ = 0;
};

} // namespace reluva::unreal
