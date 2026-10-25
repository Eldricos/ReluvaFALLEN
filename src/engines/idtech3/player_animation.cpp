// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/player_animation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace reluva::idtech3 {

void PlayerAnimation::frameAt(float seconds, int& frame, int& next, float& fraction) const {
    const float f = std::max(seconds, 0.0f) * fps;
    int n = int(std::floor(f));
    fraction = f - float(n);
    // Past the end: round the looping frames, or held on the last.
    auto wrap = [&](int k) {
        if (k < count)
            return k;
        if (loop > 0 && loop <= count)
            return count - loop + (k - count) % loop;
        return count - 1;
    };
    if (n >= count && loop <= 0)
        fraction = 0;
    frame = first + wrap(n);
    next = first + wrap(n + 1);
}

std::vector<PlayerAnimation> parseAnimationConfig(std::string_view text) {
    std::vector<PlayerAnimation> out;
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::string name;
        if (const size_t comment = line.find("//"); comment != std::string::npos) {
            std::istringstream c(line.substr(comment + 2));
            c >> name;
            line.resize(comment);
        }
        std::istringstream words(line);
        int first = 0, count = 0, loop = 0;
        float fps = 0;
        if (!(words >> first >> count >> loop >> fps))
            continue; // sex, soundpath, blank lines
        PlayerAnimation a;
        a.name = name;
        a.part = name.rfind("TORSO_", 0) == 0  ? PlayerAnimation::Part::Torso
                 : name.rfind("LEGS_", 0) == 0 ? PlayerAnimation::Part::Legs
                                               : PlayerAnimation::Part::Both;
        a.first = first;
        a.count = std::max(count, 1);
        a.loop = loop;
        a.fps = fps > 0 ? fps : 10;
        out.push_back(a);
    }
    // The legs model has no torso frames: its animations come that much earlier.
    int firstTorso = -1, firstLegs = -1;
    for (const PlayerAnimation& a : out) {
        if (a.part == PlayerAnimation::Part::Torso && firstTorso < 0)
            firstTorso = a.first;
        if (a.part == PlayerAnimation::Part::Legs && firstLegs < 0)
            firstLegs = a.first;
    }
    if (firstTorso >= 0 && firstLegs > firstTorso)
        for (PlayerAnimation& a : out)
            if (a.part == PlayerAnimation::Part::Legs)
                a.first -= firstLegs - firstTorso;
    return out;
}

} // namespace reluva::idtech3
