// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// Surface flags (the shaders' surfaceparms, as compiled into the map)
// that keep marks off a surface.
namespace surfaceflags {
constexpr int32_t Sky = 0x4, NoImpact = 0x10, NoMarks = 0x20;
} // namespace surfaceflags

// Where a mark (a scorch, a bullet hole) lands on the level: the client
// game gives a polygon and a direction to project it along, and gets back
// the pieces of the level's surfaces under it, clipped to its outline,
// which it then draws as polygons with its own texture coordinates. This
// is what the original's mark fragment call answers; see
// docs/formats/idtech3/eliteforce-syscalls.md.
class MarkSurfaces {
public:
    // The world's surfaces (the map's first model): planar faces,
    // triangle soups and patches, tessellated as they're drawn: straying
    // from round by at most `subdivisions` units (r_subdivisions).
    explicit MarkSurfaces(const Bsp& bsp, float subdivisions = 4);

    struct Fragment {
        int32_t firstPoint = 0, numPoints = 0; // in the points handed back
    };
    // The surfaces facing against `projection` within its reach of the
    // polygon (both ways), each triangle clipped to the polygon's edges
    // swept along it. Appends to `points` and `fragments`, stopping before
    // either would pass its limit.
    void mark(const std::vector<Vec3f>& polygon, const Vec3f& projection, size_t maxPoints, size_t maxFragments,
              std::vector<Vec3f>& points, std::vector<Fragment>& fragments) const;

    size_t triangleCount() const { return triangles_.size(); }

private:
    struct Triangle {
        std::array<Vec3f, 3> corners;
        Vec3f normal; // the way the surface faces
    };
    struct Surface {
        Vec3f mins, maxs;
        size_t first = 0, count = 0; // in triangles_
    };
    std::vector<Triangle> triangles_;
    std::vector<Surface> surfaces_;
};

} // namespace reluva::idtech3
