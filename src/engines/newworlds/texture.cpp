// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/texture.h"

#include <cstddef>
#include <string>

namespace reluva::newworlds {

namespace {

constexpr uint32_t kPaletted = 1;          // one layer: palette indices
constexpr uint32_t kPalettedWithAlpha = 6; // two layers: palette indices, then alpha
constexpr uint32_t kMaxSide = 4096;

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

} // namespace

Error decodeTexture(const std::vector<uint8_t>& data, Texture& out) {
    out = Texture{};
    if (data.size() < 8)
        return Error::fail("texture too small");
    out.format = rd32(data, 0);
    int layers;
    if (out.format == kPaletted)
        layers = 1;
    else if (out.format == kPalettedWithAlpha)
        layers = 2;
    else
        return Error::fail("unsupported texture format " + std::to_string(out.format));

    const uint32_t paletteCount = rd32(data, 4);
    if (paletteCount > 256)
        return Error::fail("palette has " + std::to_string(paletteCount) + " entries");
    const size_t palette = 8, header = palette + size_t(paletteCount) * 4;
    if (header + 4 > data.size())
        return Error::fail("texture ends inside its header");
    // After the palette: u32 0, width, height; or u32 1, key index, width,
    // height for textures with a transparent palette index.
    const uint32_t flag = rd32(data, header);
    size_t sizeAt = header + 4;
    if (flag == 1) {
        out.colourKeyed = true;
        if (header + 8 > data.size())
            return Error::fail("texture ends inside its header");
        out.keyIndex = rd32(data, header + 4);
        sizeAt = header + 8;
    } else if (flag != 0) {
        return Error::fail("unknown value " + std::to_string(flag) + " after the palette");
    }
    if (sizeAt + 8 > data.size())
        return Error::fail("texture ends inside its header");
    const uint32_t w = rd32(data, sizeAt), h = rd32(data, sizeAt + 4);
    const size_t indices = sizeAt + 8;

    if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide)
        return Error::fail("texture size " + std::to_string(w) + "x" + std::to_string(h) + " out of range");
    const size_t pixels = size_t(w) * h;
    const size_t alpha = indices + pixels;
    if (indices + pixels * size_t(layers) != data.size())
        return Error::fail("texture data is " + std::to_string(data.size()) + " bytes, expected " +
                           std::to_string(indices + pixels * size_t(layers)));

    out.width = int(w);
    out.height = int(h);
    out.rgba.resize(pixels * 4);
    if (layers == 2)
        out.alpha.assign(data.begin() + std::ptrdiff_t(alpha), data.begin() + std::ptrdiff_t(alpha + pixels));
    else
        out.alpha.clear();
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t index = data[indices + i];
        uint8_t* px = &out.rgba[i * 4];
        if (index < paletteCount) {
            const uint8_t* entry = &data[palette + size_t(index) * 4]; // blue, green, red, 0
            px[0] = entry[2];
            px[1] = entry[1];
            px[2] = entry[0];
        } else {
            px[0] = px[1] = px[2] = 0;
        }
        px[3] = out.colourKeyed && index == out.keyIndex ? 0 : 255;
    }
    return Error::success();
}

} // namespace reluva::newworlds
