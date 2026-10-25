// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Sounds (Sound objects, in .uax packages and in maps): a whole sound file,
// almost always a RIFF WAVE, kept inside the object. Music (.umx) is stored
// the same way, as tracker modules. The Fallen keeps its music as MP3
// files in its mp3 folder instead, each with a placeholder Sound of the
// same name. See docs/formats/unreal/sound.md.

#include "common/audio/sound.h"
#include "engines/unreal/package.h"
#include "engines/unreal/package_set.h"

#include <cstdint>
#include <filesystem>
#include <optional>
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

// Where a placeholder Sound's audio is: mp3/<its name>.mp3 in the game
// folder, found ignoring case; nullopt if there's none.
std::optional<std::filesystem::path> streamedFile(const PackageSet& packages, const Package& package,
                                                  int exportIndex);

// The sound a Sound object plays: its embedded file, or for a placeholder
// the MP3 streamedFile() finds. `from`, if given, is set to where it came
// from (the file, for streamed ones; empty for embedded).
Error loadSound(const PackageSet& packages, const Package& package, int exportIndex, audio::Sound& out,
                std::string* from = nullptr);

} // namespace reluva::unreal
