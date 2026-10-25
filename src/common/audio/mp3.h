// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::audio {

// An MP3 file (MPEG-1, 2 or 2.5, layers I-III), decoded with minimp3
// (third_party/minimp3). An ID3v2 tag at the start is skipped. Fails if the
// data holds no MP3 frames, or if the sample rate or channel count changes
// partway.
Error decodeMp3(const std::vector<uint8_t>& data, Sound& out);

} // namespace reluva::audio
