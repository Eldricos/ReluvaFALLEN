// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/sound.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// A game's sounds by name ("sound/interface/button1.wav"), read from its
// search path and decoded on first use (WAV or MP3, common/audio), then
// kept. A sound that can't be read or decoded is remembered as missing
// rather than tried again.
class SoundLibrary {
public:
    explicit SoundLibrary(const SearchSet& files) : files_(files) {}

    // The sound, or nullptr (missing or undecodable).
    std::shared_ptr<const audio::Sound> get(std::string_view name);

    size_t loaded() const { return sounds_.size(); }
    // Names that failed, with why.
    const std::vector<std::string>& problems() const { return problems_; }

private:
    const SearchSet& files_;
    std::unordered_map<std::string, std::shared_ptr<const audio::Sound>> sounds_; // by normalized name
    std::vector<std::string> problems_;
};

} // namespace reluva::idtech3
