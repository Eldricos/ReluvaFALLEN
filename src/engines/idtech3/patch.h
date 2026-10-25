// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// Turns a patch surface's control points into triangles. The grid is
// width x height control points, every 3x3 block (sharing its edges) a
// biquadratic Bezier patch; see docs/formats/idtech3/bsp.md. Each block is
// cut into `level` x `level` quads. Every vertex attribute is
// interpolated. Triangles are wound like the map's polygons: clockwise
// seen from the side the normals face.
//
// Appends to `vertices`; `indices` refer to `vertices` (offset included).
void tessellatePatch(const BspVertex* control, int width, int height, int level, std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices);

// How much a patch bends: the farthest any row or column of three
// control points has its middle one off the line between its ends, in
// units. The curve passes half that far off the line, so cut into n
// pieces it strays from them by about bend / (2 n^2).
float patchBend(const BspVertex* control, int width, int height);
// The cuts per 3x3 block for a patch to stray from round by at most
// `error` units, 1 to 16; the original draws with r_subdivisions as the
// error (4 by default).
int patchLevel(const BspVertex* control, int width, int height, float error);

} // namespace reluva::idtech3
