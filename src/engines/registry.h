// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace reluva {

const std::vector<const MetaEngine*>& allEngines();
const MetaEngine* findEngine(std::string_view id);

// Ask every engine whether it recognises the game in `dir`.
std::vector<DetectedGame> detectGames(const std::filesystem::path& dir);

} // namespace reluva
