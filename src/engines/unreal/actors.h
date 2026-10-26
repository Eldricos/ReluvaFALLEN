// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What a level's actors add to its look, read from their saved
// properties: movers (doors, lifts: brushes kept out of the level's BSP,
// drawn from their own polygons) and the sky zone (the room the sky is
// seen from). See docs/formats/unreal/actors.md.

#include "engines/unreal/model.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/properties.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::unreal {

// A brush polygon (a Polys object's), in the brush's own coordinates.
struct Poly {
    Vec3f base{}, normal{}, textureU{}, textureV{};
    std::vector<Vec3f> vertices;
    uint32_t polyFlags = 0;
    int32_t actor = 0, texture = 0; // object references
    int32_t itemName = 0;           // name index
    int32_t link = 0, brushPoly = 0;
    int16_t panU = 0, panV = 0;
};

// Reads a Polys object; `trailing`, if given, is set to the bytes left
// after the last polygon (none expected).
Error readPolys(const Package& package, int exportIndex, std::vector<Poly>& out, size_t* trailing = nullptr);

// An export's properties (its state frame skipped).
Error readActorProperties(const Package& package, int exportIndex, PropertyList& out);

// Whether an export's class is `className` or a subclass of it, following
// the classes' super references through the packages that define them.
bool isA(const PackageSet& packages, const Package& package, int exportIndex, std::string_view className);

struct Mover {
    std::string name;
    std::vector<Poly> polys;
    // Placement: a brush point v goes to
    // location + postScale * (rotation * (mainScale * (v - prePivot))).
    Vec3f location{}, prePivot{};
    Rotator rotation;
    Vec3f mainScale{1, 1, 1}, postScale{1, 1, 1};
    // Keyframes, relative to the base (the location and rotation less
    // the current key's); the mover is saved at keyNum.
    int numKeys = 1, keyNum = 0;
    std::array<Vec3f, 8> keyPos{};
    std::array<Rotator, 8> keyRot{};
    float moveTime = 1; // seconds between keys
    Vec3f basePosition() const;
    Rotator baseRotation() const;
};

// Every mover in a level package that reads; problems go in `errors`.
std::vector<Mover> findMovers(const PackageSet& packages, const Package& package, std::vector<std::string>* errors);

// The sky zone's viewpoint: the first SkyZoneInfo (or subclass) actor's
// location. nullopt when the level has none.
std::optional<Vec3f> findSkyZone(const PackageSet& packages, const Package& package);

// A rotator's axes in Unreal's space: forward, right, up.
std::array<Vec3f, 3> rotationAxes(const Rotator& r);

} // namespace reluva::unreal
