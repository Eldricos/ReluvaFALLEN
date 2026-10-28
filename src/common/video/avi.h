// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// AVI files (RIFF 'AVI '): their first video stream's frames, still
// compressed, and their first audio stream if it's PCM. Used for The
// Fallen's intro (Cinepak video, 16-bit stereo PCM). See
// docs/formats/common/avi.md.

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::video {

struct Avi {
    int width = 0, height = 0;
    double framesPerSecond = 0;
    std::string codec;          // the video stream's handler or compression ("cvid")
    std::vector<std::vector<uint8_t>> frames; // each video chunk, in order (an empty one repeats the last)
    audio::Sound audio;         // the PCM, all of it; empty if there's none (or it isn't PCM)
};

Error readAvi(const std::vector<uint8_t>& data, Avi& out);
Error readAvi(const std::filesystem::path& file, Avi& out);

} // namespace reluva::video
