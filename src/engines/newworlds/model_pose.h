// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/arf.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/mdl.h"
#include "engines/newworlds/model_textures.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::newworlds {

// Moving a model's parts: the crew's and construction vehicles' .arf
// animations, and the turrets and guns of tanks, artillery and defences.
// A geometry part belongs to one of the .mdl's nodes (GeoModel::Part::node,
// named by the .mdl's parts list: turret=1, gun=2); see
// docs/formats/newworlds/mdl.md#moving-parts.

// Where a part is moved to: v' = to + m × (v − from); normals turn by m.
struct PartPose {
    bool moved = false;
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    GeoModel::Vec3 from{0, 0, 0}, to{0, 0, 0};

    GeoModel::Vec3 turn(const GeoModel::Vec3& v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
    GeoModel::Vec3 apply(const GeoModel::Vec3& v) const {
        if (!moved)
            return v;
        const GeoModel::Vec3 r = turn({v.x - from.x, v.y - from.y, v.z - from.z});
        return {to.x + r.x, to.y + r.y, to.z + r.z};
    }
};
using Pose = std::vector<PartPose>; // one per geometry part

// An .arf animation: its frames (a transform per animated part each) and
// the .mdl state it plays (walk, stand, manipulate).
struct Animation {
    std::string name, state;
    std::vector<std::vector<PartTransform>> frames;
};

// The metamodels and animations in a set of archives, found by model.
class Metamodels {
public:
    explicit Metamodels(const Archives& archives);
    // A model's .mdl: the same folder and name under mdl/ (geo/kln/k_bop.geo:
    // mdl/kln/k_bop.mdl), or without the underscores (geo/map/ice/ice_07.geo:
    // mdl/ice/ice07.mdl), or without a suffix (k_bop_l: k_bop).
    bool find(const std::string& modelName, MdlModel& out) const;
    // The .arf sets that fit it (arf.md#the-sets-in-every-actor-archive),
    // those named like the model first, then those in its folder.
    std::vector<Animation> animations(const std::string& modelName, const MdlModel& mdl) const;

private:
    const Archives& archives_;
    std::map<std::string, EntryRef> metamodels_; // by "folder/stem"
};

// An animation frame's pose: each part moved by the animated part of its
// node's name, from the .mdl's reference pose (arf.md#posing-a-model).
Pose framePose(const GeoModel& geo, const MdlModel& mdl, const std::vector<PartTransform>& frame);

// Whether the model has a turret (turret or turret_1 ... in its parts).
bool hasTurret(const MdlModel& mdl);
// Its turrets turned by yaw (radians, anticlockwise from above) about the
// upright through their turret_axis points, and their guns, turning with
// them, raised by pitch (radians, muzzles up) about the line across them
// through their gun_elevate points. The guns point along −y.
Pose turretPose(const GeoModel& geo, const MdlModel& mdl, float yaw, float pitch);

} // namespace reluva::newworlds
