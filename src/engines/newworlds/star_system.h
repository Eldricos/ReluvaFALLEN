// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/geo.h"
#include "engines/newworlds/string_table.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::newworlds {

// The briefings' star systems: glblgame.cat's geo/solsys/ models, one per
// system (karazin, taal, ...), and the planet pictures in lbp/txr/solsys/.
// See docs/formats/newworlds/geo.md#star-systems.

// The system a mission's string table names most often, among the models'
// names (compared as letters only, ignoring case: "F'Thorc" is fthorc);
// empty if it names none.
std::string starSystemName(const StringTable& table, const std::vector<std::string>& systems);

// What a system model's pieces are, read from its shape: a cylinder of
// starfield round everything, the star (a stack of slices all numbered 0),
// and its planets (stacks of seven square slices, each slice twice: a quad
// in the planet's own texture number and two triangles, its back, in 0); a
// ringed planet has six slices and a ring.
struct StarSystemLayout {
    struct Body {
        GeoModel::Vec3 centre;
        float size = 0;
        int slot = -1;     // its texture number (0 for the star)
        int ringSlot = -1; // a ringed planet's ring's
        bool star = false;
    };
    std::vector<Body> bodies;
    std::vector<int> backdropSlots; // the starfield's texture numbers, in order
    // Per face: the texture number to draw it with (a planet's back faces
    // take the planet's), or -1 for a face not to draw; and the body it's
    // part of, or -1 for the starfield.
    std::vector<int> faceSlot, faceBody;
};
StarSystemLayout readStarSystem(const GeoModel& model);

// Texture stems (in lbp/txr/solsys/) for each texture number: stars1-4 for
// the starfield, sun for the star, the mission's world (planetWorld: "ice",
// "desert", ...) for its first planet, the ring pictures (alien3_r,
// alien7_r, red_r) with their planets, and others from a fixed list. Which
// the game gives each system is in STNW.exe; this is Reluva's choice, from
// the colours recordings show (geo.md#star-systems).
// system: the model's name, for the stars the recordings show red
// (bezouk, sakuntul: sun3, the reddest).
std::vector<std::string> starSystemTextures(const StarSystemLayout& layout, std::string_view planetWorld,
                                            std::string_view system = {});

// The solsys picture for a planet archive's world ("icewrld.cat" -> "ice");
// empty if unknown.
std::string planetPicture(std::string_view archiveName);

// The body to look at: the mission's planet (the first planet, by texture
// number), else the star; -1 if there's none.
int missionBody(const StarSystemLayout& layout);

} // namespace reluva::newworlds
