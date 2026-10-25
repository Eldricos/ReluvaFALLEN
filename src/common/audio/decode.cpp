// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/decode.h"

#include "common/audio/mp3.h"
#include "common/audio/wav.h"

#include <cstring>

namespace reluva::audio {

Error decodeSound(const std::vector<uint8_t>& data, Sound& out) {
    if (data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WAVE", 4) == 0)
        return decodeWav(data, out);
    return decodeMp3(data, out);
}

const char* formatName(int format) {
    switch (format) {
    case 1: return "PCM";
    case 0x11: return "IMA ADPCM";
    case kFormatMp3: return "MP3";
    default: return "?";
    }
}

} // namespace reluva::audio
