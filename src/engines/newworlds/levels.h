// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::newworlds {

// A mission listed in level.dat, the main menu's list. See
// docs/formats/newworlds/level.md.
struct Level {
    std::string shortName; // "F1": also the map's name (map/f1.gmf)
    int maxPlayers = 0;
    std::string title;      // "Power Corrupts"
    bool commentedOut = false; // a "//" line (C9-C11, an early J1)
};

// Reads level.dat's text: "short name,max players,title" per line, "//"
// starting a comment. Commented-out lines that still look like a level are
// kept, marked.
Error parseLevelList(std::string_view text, std::vector<Level>& out);

// The level whose short name is the map's file stem ("map/f1.gmf" -> F1),
// ignoring case; listed levels before commented-out ones; nullptr if none.
const Level* levelForMap(const std::vector<Level>& levels, std::string_view mapName);

// What a mission's short name ties together (see docs/formats/newworlds/level.md
// and media.md). A side is 'F', 'K' or 'R'.

// The sides a mission is played from: all three for a joint mission (J1),
// else the campaign's ('F' for F1; the Federation for the others).
std::vector<char> missionSides(std::string_view shortName);
// Its string table in the side's misc archive: "str/eng/miss/fed01.stb" for
// F1, "jnt01klg.stb" for J1 as the Klingons, "cvr03.stb" for the C
// missions, "tst.stb" for Test; empty if none is known (E1-E3).
std::string missionTableName(std::string_view shortName, char side);
// The maps to try, best first: "map/f1.gmf"; mission 14 then 12's map, whose
// world its briefing names; the bonus missions the maps their recordings
// fit (C11 f1, C10 f2, C9 f3 or j3).
std::vector<std::string> missionMapNames(std::string_view shortName);
// Its voice clips (Media/FedVO/F1005.mp3, .../KlingVO/J1K005.mp3): the stem
// starts with the short name, a joint mission's then the side's letter, and
// goes on with three digits and perhaps a letter (K12009A). Ignores case.
bool isMissionClip(std::string_view stem, std::string_view shortName, char side);
// The music for a planet archive's maps ("icewrld.cat" -> "IceStart", the
// stem of Media/Music/IceStart.mp3); empty if unknown.
std::string planetTheme(std::string_view archiveName);

} // namespace reluva::newworlds
