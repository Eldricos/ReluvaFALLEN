// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva::unreal {

// What the shared Unreal Engine 1 code needs to know about one game.
struct GameInfo {
    const char* gameId;  // stable id stored in the config
    const char* name;    // shown to the user
    const char* iniName; // its settings in System/, which list where packages are (else Default.ini)
};

const std::vector<GameInfo>& games();
const GameInfo* findGame(std::string_view gameId);

// The games in a folder: known releases by their files, else any game
// with the engine's own packages (System/Core.u and Engine.u) as an
// unidentified Unreal Engine 1 game, so its data can still be looked at.
std::vector<DetectedGame> detect(const char* engineId, const std::filesystem::path& dir);

} // namespace reluva::unreal
