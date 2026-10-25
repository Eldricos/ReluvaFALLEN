// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/games.h"

#include "common/fs.h"

namespace reluva::unreal {

const std::vector<GameInfo>& games() {
    static const std::vector<GameInfo> list = {
        {"ds9fallen", "Star Trek: Deep Space Nine - The Fallen", "DS9.ini"},
        {"ue1", "Unreal Engine 1 game", ""},
    };
    return list;
}

const GameInfo* findGame(std::string_view gameId) {
    for (const GameInfo& g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

namespace {

// Built with `reluva --hash`: the size and the MD5 of the first
// kDetectionHashBytes bytes of each file.
//
// Detection keys on the executable and Engine.u, which a patch would
// both replace.
//
// TODO: add The Fallen's other releases (other patch levels, languages,
// Mac) from owned copies. Until then they're found by name below.
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {
        // Windows install, packages of versions 71-73 (reported October 2026).
        {"ds9fallen", "Star Trek: Deep Space Nine - The Fallen", "en", "windows",
         {{"System/DS9.exe", "0bbf37d2d1b0d023fafb3f727f975507", 604291},
          {"System/Engine.u", "4127f15b637f34af3a23efc7758b9d47", 4356401}}},
    };
    return table;
}

// The Fallen's executable is System/DS9.exe; its settings are
// System/DS9.ini (TrekCore's support pages tell players to edit it),
// made from System/Default.ini when the game first runs.
const std::vector<GameVariant>& fallbackTable() {
    static const std::vector<GameVariant> table = {
        {"ds9fallen", "Star Trek: Deep Space Nine - The Fallen (unknown release)", "", "windows",
         {{"System/Core.u", "", -1}, {"System/Engine.u", "", -1}, {"System/DS9.ini", "", -1}}},
        {"ds9fallen", "Star Trek: Deep Space Nine - The Fallen (unknown release)", "", "windows",
         {{"System/Core.u", "", -1}, {"System/Engine.u", "", -1}, {"System/DS9.exe", "", -1}}},
    };
    return table;
}

} // namespace

std::vector<DetectedGame> detect(const char* engineId, const std::filesystem::path& dir) {
    auto found = detectFromTable(engineId, detectionTable(), dir);
    if (!found.empty())
        return found;
    found = detectFromTable(engineId, fallbackTable(), dir);
    if (!found.empty()) {
        found.resize(1); // both fallbacks can match the same install
        return found;
    }
    if (fs::findFile(dir, "System/Core.u") && fs::findFile(dir, "System/Engine.u"))
        return {{engineId, "ue1", "Unreal Engine 1 game (unidentified)", "", ""}};
    return {};
}

} // namespace reluva::unreal
