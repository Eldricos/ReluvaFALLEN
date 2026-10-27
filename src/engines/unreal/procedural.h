// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Textures whose pixels change as the game runs: fire (FireTexture),
// water (WaterTexture, WaveTexture, and WetTexture, which ripples another
// texture), ice (IceTexture, one texture seen through another sliding
// over it) and The Fallen's FliTexture (an FLC animation stored with
// it). The procedural ones store only their settings (fire's sparks,
// water's drops); the pictures are made here, Reluva's own simulation of
// what the settings describe, to be compared with the original running.
// See docs/formats/unreal/texture.md.

#include "common/image.h"
#include "engines/unreal/package_set.h"

#include <memory>
#include <string>
#include <string_view>

namespace reluva::unreal {

class AnimatedTexture {
public:
    virtual ~AnimatedTexture() = default;
    // The current picture (RGBA, the texture's size).
    const image::Image& image() const { return image_; }
    // Moves on by `seconds`; true if the picture changed.
    virtual bool advance(float seconds) = 0;
    // What it is: "fire", "water", "wave", "wet", "ice", "FLC".
    virtual const char* kind() const = 0;
    // For one played frame by frame (an FLC: The Fallen's HUD picks its
    // frames itself): how many frames, how long each shows at rate 1, and
    // the picture showing frame `n` (from 1), its colour 0 see-through if
    // asked; false if it was showing already.
    virtual int frames() const { return 0; }
    virtual float frameSeconds() const { return 0; }
    virtual bool showFrame(int n, bool seeThrough) {
        (void)n;
        (void)seeThrough;
        return false;
    }

protected:
    image::Image image_;
};

// Whether a texture class is one AnimatedTexture plays.
bool isAnimatedTextureClass(std::string_view className);

// A texture's animation, showing its first picture; nullptr (with
// `error` set, if given) when it isn't one or doesn't read.
std::unique_ptr<AnimatedTexture> openAnimatedTexture(const PackageSet& packages, const Package& package,
                                                     int exportIndex, std::string* error = nullptr);

} // namespace reluva::unreal
