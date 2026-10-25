// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva {

// How many leading bytes of a file are hashed for detection. Hashing only the
// start keeps detection fast even for large archives.
constexpr uint64_t kDetectionHashBytes = 5000;

// One file that must be present for a game variant to match.
struct FileSignature {
    std::string name;     // relative path, matched case-insensitively
    std::string md5;      // of the first kDetectionHashBytes bytes; empty = any
    int64_t size = -1;    // exact file size; -1 = any
};

// One release of a game (language, platform, patch level...).
struct GameVariant {
    std::string gameId;      // stable id, e.g. "newworlds"
    std::string description; // shown to the user
    std::string language;    // "en", "de", ...
    std::string platform;    // "windows", ...
    std::vector<FileSignature> files;
};

struct DetectedGame {
    std::string engineId;
    std::string gameId;
    std::string description;
    std::string language;
    std::string platform;
};

// Return every variant in `table` whose files all match inside `dir`.
std::vector<DetectedGame> detectFromTable(const std::string& engineId,
                                          const std::vector<GameVariant>& table,
                                          const std::filesystem::path& dir);

} // namespace reluva
