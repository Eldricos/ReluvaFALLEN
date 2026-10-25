// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Sounds (Sound objects, in .uax packages and in maps): a whole sound file,
// almost always a RIFF WAVE, kept inside the object. Music (.umx) is stored
// the same way, as tracker modules. See docs/formats/unreal/sound.md.

#include "common/audio/sound.h"
#include "engines/unreal/package.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::unreal {

struct EmbeddedFile {
    std::string format; // the object's Format name: "WAV", "IT", "S3M"...
    std::vector<uint8_t> data;
    // A few bytes that aren't a sound: The Fallen keeps some sounds as
    // the 4 bytes "X \r\n", with nothing to play.
    bool isPlaceholder() const;
};

// The file a Sound or Music object holds.
Error readEmbeddedFile(const Package& package, int exportIndex, EmbeddedFile& out);

// Decodes a Sound's file; when it can't, the error says what the file is
// (its format name, size and first bytes), to work out unknown kinds.
Error decodeEmbeddedSound(const EmbeddedFile& file, audio::Sound& out);

} // namespace reluva::unreal
