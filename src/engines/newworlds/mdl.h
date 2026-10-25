// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/newworlds/arf.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::newworlds {

// A .mdl "metamodel" (chunk tag 0x2c): the names of a model's parts and
// points, and its states. See docs/formats/newworlds/mdl.md. The geometry
// that follows (each state's model, as in the .geo files) isn't read here.
struct MdlModel {
    struct Name {
        std::string name;
        int32_t value = 0; // -1 for none
    };
    std::string title;          // e.g. "klnbirdofpreymetamodel"
    std::vector<Name> parts;    // nodes: main, turret_1, gun_1, head, pelvis, ...
    std::vector<Name> points;   // the model's attachments, by number: turret_axis_1, BAY_0_LOCK, PORTAL
    std::vector<Name> places;   // muzzle_flash_1, BAY_0, VEHICLE_DOCK
    std::vector<Name> animated; // the parts animations move (crewman's limbs, conbee's arm)
    std::vector<std::string> states; // normal / low detail, constructed / deconstructed, stand / walk
    // For animated models: where each animated part is in the pose the
    // geometry is stored in (the first records after the states).
    std::vector<PartTransform> reference;
};

Error parseMdl(const std::vector<uint8_t>& data, MdlModel& out);

} // namespace reluva::newworlds
