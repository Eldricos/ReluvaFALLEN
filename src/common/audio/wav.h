// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

// A RIFF WAVE file: 8- and 16-bit PCM and IMA ADPCM (format 0x11, as in
// Star Trek: New Worlds; see docs/formats/newworlds/wav.md).
Error decodeWav(const std::vector<uint8_t>& data, Sound& out);

// A 16-bit PCM WAV file of the sound, for checking it elsewhere.
std::vector<uint8_t> encodePcmWav(const Sound& sound);

} // namespace reluva::audio
