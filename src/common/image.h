// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <vector>

namespace reluva::image {

// A decoded picture: rows top to bottom, 4 bytes per pixel in memory order
// R, G, B, A (opaque formats get A = 255).
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;

    uint8_t* pixel(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * w + x) * 4; }
    const uint8_t* pixel(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * w + x) * 4; }
};

// Decode a TGA (uncompressed or RLE; true-colour, grey or colour-mapped,
// either row order), JPEG (baseline or progressive), PNG or BMP file, told
// apart by their contents. Uses stb_image (third_party/stb).
Error decode(const std::vector<uint8_t>& data, Image& out);

// Copy the image into an RGBA32 surface with its top-left corner at (x, y),
// clipped to it, blending by alpha over what's there.
void blit(Surface& dst, const Image& img, int x, int y);

} // namespace reluva::image
