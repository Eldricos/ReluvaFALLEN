// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/games.h"

namespace reluva::idtech3 {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        {"stvef", "Star Trek: Voyager - Elite Force", "baseEF"},
    };
    return list;
}

const GameInfo* findGame(std::string_view gameId) {
    for (const GameInfo& g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// Built with `reluva --hash`: each entry needs the size and the MD5 of the
// first kDetectionHashBytes bytes.
//
// Detection keys on baseEF/pak0.pk3, which holds the game's data; the
// executable, which patches replace, is only required to exist.
//
// TODO: add the other releases (Expansion Pack, Mac, other languages) from
// owned copies. Until then they're found by fallbackTable().
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        // Windows install with only pak0.pk3 in baseEF (reported October 2026).
        {"stvef", "Star Trek: Voyager - Elite Force", "en", "windows",
         {{"baseEF/pak0.pk3", "f31871f2891b1e9d6e8d1654ea93352d", 566234769}, {"stvoy.exe", "", -1}}},
    };
    return table;
}

// The same games by file names alone, for releases whose hashes aren't in
// detectionTable() yet. Used only when nothing there matches.
const std::vector<GameVariant>& fallbackTable() {
    static const std::vector<GameVariant> table = {
        {"stvef", "Star Trek: Voyager - Elite Force (unknown release)", "", "windows",
         {{"baseEF/pak0.pk3", "", -1}, {"stvoy.exe", "", -1}}},
    };
    return table;
}

} // namespace reluva::idtech3
