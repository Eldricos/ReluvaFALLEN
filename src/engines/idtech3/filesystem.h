// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// How many archives one game folder may add; game folders mounted later
// get priorities above every archive of earlier ones.
constexpr int kPrioritiesPerGameDir = 1000;

// Add one game folder of an install (the base game's, or a mod's) to the
// search set, the way id Tech 3 games search their files (see
// docs/formats/idtech3/pk3.md): loose files first, then the folder's .pk3
// files, later names (compared in upper case) before earlier ones, so
// pak1.pk3 overrides pak0.pk3 and pak_x.pk3 overrides paka.pk3.
//
// `layer` orders game folders: the base game is 0, a mod 1. A .pk3 that
// can't be opened is skipped and reported in `warnings`; a missing folder
// is an error.
Error mountGameDir(SearchSet& set, const std::filesystem::path& install, std::string_view gameDir, int layer,
                   std::vector<std::string>& warnings);

} // namespace reluva::idtech3
