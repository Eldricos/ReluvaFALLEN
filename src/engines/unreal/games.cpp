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
// TODO: add The Fallen's releases (retail 1.0, the patches, other
// languages, Mac) from owned copies. Until then it's found by name below.
const std::vector<GameVariant>& detectionTable() {
    static const std::vector<GameVariant> table = {};
    return table;
}

// The Fallen's settings are System/DS9.ini (TrekCore's support pages tell
// players to edit it); its executable is expected to be System/DS9.exe.
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
