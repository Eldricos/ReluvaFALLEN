// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// A player model's animations (its animation.cfg): each a run of frames
// played at a speed, some looping. See docs/formats/idtech3/md3.md.
struct PlayerAnimation {
    std::string name; // BOTH_DEATH1, TORSO_STAND, LEGS_RUN... (from the line's comment)
    enum class Part { Both, Torso, Legs } part = Part::Both;
    // Frames in the part's own model: the legs model has no torso
    // frames, so its animations start that many earlier than the file says.
    int first = 0, count = 1;
    int loop = 0; // the last this many frames repeat once played through; 0: it stops on its last
    float fps = 10;

    // The frame `seconds` into the animation, as a whole frame and the
    // fraction of the way to the next.
    void frameAt(float seconds, int& frame, int& next, float& fraction) const;
};

// The animations in an animation.cfg's text: lines of first frame, count,
// looping frames and frames per second, named by their comments; other
// lines (sex, soundpath, comments) left out. The part each belongs to
// comes from its name; the legs' animations are moved back by the gap
// between the first torso and the first legs animation.
std::vector<PlayerAnimation> parseAnimationConfig(std::string_view text);

} // namespace reluva::idtech3
