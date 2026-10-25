// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

// Decodes a sound file of any format common/audio reads, chosen by its
// contents: RIFF WAVE (wav.h), else MP3 (mp3.h).
Error decodeSound(const std::vector<uint8_t>& data, Sound& out);

// A short name for a Sound's source format: "PCM", "IMA ADPCM", "MP3", ...
const char* formatName(int format);

} // namespace reluva::audio
