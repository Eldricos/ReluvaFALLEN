// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::newworlds {

// A .geo model (chunk tag 0x17). See docs/formats/newworlds/geo.md; parts of
// the format are still unknown and are skipped.
struct GeoModel {
    struct Vec3 {
        float x, y, z;
    };
    struct Corner {
        uint32_t vertex;    // index into the face's part's vertices
        float u, v;         // texture coordinates, 0..1
        uint32_t extra[5];  // extra[0]: the corner's normal, in the part's second
                            // normal list; in parts above 0, extra[4] is the part
                            // number except in a face's last corner
    };
    struct Face {
        uint32_t groupA = 0, groupB = 0; // header values: the previous face's part, and unknown
        uint32_t part = 0;               // which part the face belongs to
        std::vector<Corner> corners;
    };
    struct Edge {
        uint32_t v0, v1;       // the part's vertices
        uint32_t faceA, faceB; // the faces on either side (0-based, model-wide)
    };
    // A separately numbered piece of the model (most models have one).
    struct Part {
        uint32_t node = 0; // which of the .mdl's nodes (parts list) it is: 0-12 in crewman
        std::vector<Vec3> vertices;
        std::vector<std::vector<Vec3>> normalLists;
        std::vector<Edge> edges;
    };

    // A named point (a turret's axis, a gun's muzzle, a docking point); the
    // .mdl names them in order (see docs/formats/newworlds/mdl.md).
    struct Attachment {
        float frame[10];  // its orientation (9 floats, row by row) and a scale (1), as in .arf frames
        Vec3 position;
    };

    float centreX = 0, centreY = 0, radius = 0;
    uint32_t nodeCount = 0;  // the .mdl's part names (first value of the attachment block)
    uint32_t placeCount = 0; // the .mdl's named places (muzzle flashes, bays)
    std::vector<Attachment> attachments;
    std::vector<Face> faces;
    std::vector<Part> parts;
};

Error parseGeo(const std::vector<uint8_t>& data, GeoModel& out);

// A ground tile model's heights along its four sides (0 east, 1 north,
// 2 west, 3 south, after turning it rotation quarter turns anticlockwise),
// at 5 even points from the side's low-coordinate end, interpolated from
// the vertices on the side; a side with no vertices isn't present. Used to
// see where neighbouring tiles meet.
struct TileSides {
    bool present[4] = {};
    float z[4][5] = {};
};
constexpr float kGroundTileSize = 8192; // ground models span -4096..4096 in x and y
TileSides tileSides(const GeoModel& geo, int rotation);

} // namespace reluva::newworlds
