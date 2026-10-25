// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/sounds.h"

#include "common/audio/decode.h"

namespace reluva::idtech3 {

std::shared_ptr<const audio::Sound> SoundLibrary::get(std::string_view name) {
    const std::string key = normalizeArchiveName(name);
    if (key.empty())
        return nullptr;
    if (const auto it = sounds_.find(key); it != sounds_.end())
        return it->second;
    std::shared_ptr<const audio::Sound> result;
    std::vector<uint8_t> data;
    if (Error err = files_.read(key, data)) {
        problems_.push_back(std::string(name) + ": " + err.message);
    } else {
        auto sound = std::make_shared<audio::Sound>();
        if (Error derr = audio::decodeSound(data, *sound))
            problems_.push_back(std::string(name) + ": " + derr.message);
        else if (sound->frames() == 0)
            problems_.push_back(std::string(name) + ": no samples");
        else
            result = std::move(sound);
    }
    sounds_.emplace(key, result);
    return result;
}

} // namespace reluva::idtech3
