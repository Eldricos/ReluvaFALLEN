// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/detection.h"
#include "common/system.h"
#include "common/types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

// A game the user has added, as stored in the config file.
struct GameTarget {
    std::string target;   // config section name, unique per install
    std::string engineId;
    std::string gameId;
    std::string description;
    std::filesystem::path path; // game data directory
    // Where the engine may keep files for this game (settings, saves); may
    // not exist yet. Set by the frontend.
    std::filesystem::path savePath;
};

// A running game. Created by its MetaEngine, run once, then destroyed.
class Engine {
public:
    virtual ~Engine() = default;

    // Runs the game until the player quits or asks to return to the launcher.
    virtual Error run() = 0;
};

// Static description of an engine: its name, how to recognise its games,
// and how to create an instance.
class MetaEngine {
public:
    virtual ~MetaEngine() = default;

    virtual const char* id() const = 0;
    virtual const char* name() const = 0;

    virtual std::vector<DetectedGame> detect(const std::filesystem::path& dir) const = 0;
    virtual std::unique_ptr<Engine> create(System& system, const GameTarget& target) const = 0;
};

} // namespace reluva
