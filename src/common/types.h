// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reluva {

// Result of an operation that can fail with a human-readable reason.
struct Error {
    bool ok = true;
    std::string message;

    static Error success() { return {}; }
    static Error fail(std::string msg) { return {false, std::move(msg)}; }
    explicit operator bool() const { return !ok; } // true when there is an error
};

struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

enum class PixelFormat {
    CLUT8,  // 8-bit palette indices, colours set with System::setPalette
    RGBA32, // 4 bytes per pixel in memory order R, G, B, A
};

inline int bytesPerPixel(PixelFormat f) { return f == PixelFormat::CLUT8 ? 1 : 4; }

// A simple owned pixel buffer, used as the game's framebuffer.
struct Surface {
    int w = 0, h = 0, pitch = 0;
    PixelFormat format = PixelFormat::CLUT8;
    std::vector<uint8_t> pixels;

    void create(int width, int height, PixelFormat fmt) {
        w = width;
        h = height;
        format = fmt;
        pitch = width * bytesPerPixel(fmt);
        pixels.assign(static_cast<size_t>(pitch) * height, 0);
    }

    uint8_t* row(int y) { return pixels.data() + static_cast<size_t>(y) * pitch; }
    const uint8_t* row(int y) const { return pixels.data() + static_cast<size_t>(y) * pitch; }
};

} // namespace reluva
