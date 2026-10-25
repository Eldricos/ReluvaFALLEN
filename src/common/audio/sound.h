// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::audio {

// A decoded sound: signed 16-bit samples, interleaved when stereo. Every
// decoder in common/audio produces one.
struct Sound {
    int rate = 0;     // samples per second
    int channels = 0; // 1 or 2
    int format = 0;   // the source's format: a WAV format tag (1 PCM, 0x11 IMA ADPCM), or kFormatMp3
    std::vector<int16_t> samples;
    size_t frames() const { return channels ? samples.size() / size_t(channels) : 0; }
};

// Sound::format for MP3 sources (not a WAV tag in use: those are 16-bit).
constexpr int kFormatMp3 = 0x10055;

} // namespace reluva::audio
