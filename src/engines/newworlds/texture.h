// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::newworlds {

// A decoded texture (chunk tag 0x16). See docs/formats/newworlds/lbp.md.
struct Texture {
    uint32_t format = 0;
    bool colourKeyed = false;  // a palette index is transparent
    uint32_t keyIndex = 0;     // that index, when colourKeyed
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, bytes R, G, B, A; A is 0 for
                                // colour-keyed pixels and 255 otherwise
    std::vector<uint8_t> alpha; // format 6: the alpha layer, width * height bytes
                                // (in model textures, a light map); else empty
};

Error decodeTexture(const std::vector<uint8_t>& data, Texture& out);

} // namespace reluva::newworlds
