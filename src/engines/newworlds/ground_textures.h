// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/geo.h"

#include <string>
#include <vector>

namespace reluva::newworlds {

// Which of a planet's textures each texture slot of a ground tile model gets.
// Neither the maps nor the models say (see docs/formats/newworlds/gmf.md), so
// this is the viewer's own rule, not the game's: each slot gets a role from
// the shape of its faces (high, middle or low flat ground, or slope), and the
// role a texture by the planet's names, preferring one numbered like the
// model (alpin_07's low ground: low07). Holes, cracks, ramps, bridges,
// concrete and water use their own textures. Returns a stem per slot ("" for
// none).
std::vector<std::string> chooseGroundTextures(const std::string& modelStem, const GeoModel& model,
                                              const std::vector<std::string>& planetStems);

} // namespace reluva::newworlds
