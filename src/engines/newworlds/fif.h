// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/newworlds/model_textures.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::newworlds {

// One of the game's fonts: a .fif (chunk tag 0x26, the glyph table) and the
// .fbp image of the same name next to it (tag 0x16, a texture). See
// docs/formats/newworlds/fif.md.
struct GameFont {
    struct Glyph {
        int x = 0, y = 0, width = 0, height = 0; // in the image; height 0: none
    };
    std::string name;    // "fnt/final12"
    std::string archive; // the file it came from, e.g. "glblgui.cat"
    uint32_t first = 0;  // the code of glyphs[0] (32); codes are Latin-1
    int size = 0;        // lineHeight - 2 in every font seen; use unknown
    int lineHeight = 0;
    std::vector<Glyph> glyphs;
    // The image's brightness, used as each pixel's coverage (the glyphs are
    // white, edges grey, on black).
    int imageWidth = 0, imageHeight = 0;
    std::vector<uint8_t> coverage;

    // The glyph a code point is drawn with: its own, else (for a code beyond
    // Latin-1) its plain form, else '?'; nullptr if the font has none.
    const Glyph* glyph(uint32_t codePoint) const;
    int textWidth(std::string_view utf8) const;
    // Blends UTF-8 text in the colour into an RGBA32 surface with its
    // top-left corner at (x, y); returns the x after the last character.
    int drawText(Surface& surface, int x, int y, std::string_view utf8, Color color, int scale = 1) const;
};

// Reads a .fif glyph table.
Error parseFif(const std::vector<uint8_t>& data, GameFont& out);
// Adds the image from a .fbp.
Error setFontImage(const std::vector<uint8_t>& fbp, GameFont& font);

// Every font in the archives, once per name (fedgui.cat stores fe5pt four
// times). Fonts that don't read are left out.
std::vector<GameFont> loadGameFonts(const Archives& archives);

} // namespace reluva::newworlds
