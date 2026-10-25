// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// A model (.md3, "IDP3" version 15): surfaces of triangles whose vertices
// move from frame to frame, and tags (named points with axes) where other
// models attach. See docs/formats/idtech3/md3.md.

using Vec3 = std::array<float, 3>;

struct Md3Frame {
    Vec3 mins{}, maxs{};
    Vec3 origin{};
    float radius = 0;
    std::string name;
};

struct Md3Tag {
    std::string name;
    Vec3 origin{};
    std::array<Vec3, 3> axis{}; // the tag's x, y and z axes in the model's space
};

struct Md3Vertex {
    Vec3 pos{};
    Vec3 normal{};
};

struct Md3Surface {
    std::string name;
    std::vector<std::string> shaders; // usually one
    std::vector<uint32_t> indices;    // three per triangle, into one frame's vertices
    std::vector<std::array<float, 2>> st;
    int vertexCount = 0;              // per frame
    std::vector<Md3Vertex> vertices;  // frame by frame: frame f's vertex i is [f * vertexCount + i]
};

struct Md3Model {
    std::string name;
    std::vector<Md3Frame> frames;
    int tagCount = 0;          // per frame
    std::vector<Md3Tag> tags;  // frame by frame
    std::vector<Md3Surface> surfaces;

    // The tag of that name in a frame, or nullptr.
    const Md3Tag* tag(int frame, std::string_view name) const;
};

// Parse a whole .md3 file, checking every count and offset.
Error parseMd3(const std::vector<uint8_t>& data, Md3Model& out);
// Parse a whole .mdr file (Raven's skeletal models, "RDM5" version 2;
// see docs/formats/idtech3/mdr.md), posed into an Md3Model: each frame's
// vertices placed by their bones, each frame's tags from theirs. `lod`
// picks the level of detail (0 the most detailed; past the last, the last).
Error parseMdr(const std::vector<uint8_t>& data, Md3Model& out, int lod = 0);
// Either, by the file's first bytes.
Error parseModel(const std::vector<uint8_t>& data, Md3Model& out, int lod = 0);

// A .skin file: which shader each surface uses, as lines of
// "surfacename,shader" (tag lines and empty shaders are ignored).
// Surface names are lower-cased.
std::map<std::string, std::string> parseSkin(std::string_view text);

} // namespace reluva::idtech3
