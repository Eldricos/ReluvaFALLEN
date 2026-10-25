// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/font.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>

namespace reluva::newworlds {

namespace {

constexpr Color kBackground{22, 26, 38};
constexpr Color kLabelColor{110, 118, 140};
constexpr Color kTextColor{235, 238, 245};
constexpr Color kBoxColor{90, 60, 60};
constexpr int kMargin = 6;

const char* const kSamples[] = {
    "STAR TREK: NEW WORLDS",
    "The quick brown fox jumps over the lazy dog.",
    "Stardate 8987.8. Priority One Communication received.",
    "0123456789 Done: 100% Caf\xc3\xa9 \xc2\xa9 \xc2\xbd",
};

} // namespace

FontViewer::FontViewer(System& sys, Surface& screen, const Archives& archives)
    : sys_(sys), screen_(screen), fonts_(loadGameFonts(archives)) {}

void FontViewer::activate() {
    draw();
    updateTitle();
}

void FontViewer::handleKey(uint32_t k) {
    const int n = int(fonts_.size());
    if (n == 0)
        return;
    switch (k) {
    case key::Right: current_ = (current_ + 1) % n; break;
    case key::Left: current_ = (current_ + n - 1) % n; break;
    case '+':
    case '=': scale_ = std::min(scale_ + 1, 4); break;
    case '-': scale_ = std::max(scale_ - 1, 1); break;
    case 'b': boxes_ = !boxes_; break;
    default: return;
    }
    draw();
    updateTitle();
}

void FontViewer::draw() {
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4)
            row[0] = kBackground.r, row[1] = kBackground.g, row[2] = kBackground.b, row[3] = 255;
    }
    if (fonts_.empty())
        return;
    const GameFont& f = fonts_[size_t(current_)];
    const int line = f.lineHeight * scale_ + 2;
    int y = kMargin;
    // Every glyph, in code order, wrapped at the screen's edge.
    font::drawText(screen_, kMargin, y, "Characters " + std::to_string(f.first) + "-" +
                                            std::to_string(f.first + f.glyphs.size() - 1) + ":",
                   kLabelColor);
    y += font::kLineHeight + 2;
    int x = kMargin;
    for (size_t i = 0; i < f.glyphs.size(); ++i) {
        const GameFont::Glyph& g = f.glyphs[i];
        if (g.height == 0)
            continue;
        if (x + g.width * scale_ > screen_.w - kMargin)
            x = kMargin, y += line;
        if (boxes_)
            for (int by = 0; by < g.height * scale_; ++by)
                for (int bx = 0; bx < g.width * scale_; ++bx)
                    if (y + by < screen_.h && x + bx < screen_.w) {
                        uint8_t* p = screen_.row(y + by) + (x + bx) * 4;
                        if ((bx + by) % 2 == 0 && (bx == 0 || by == 0 || bx == g.width * scale_ - 1 ||
                                                   by == g.height * scale_ - 1))
                            p[0] = kBoxColor.r, p[1] = kBoxColor.g, p[2] = kBoxColor.b;
                    }
        // Latin-1 codes are their own UTF-8 code points.
        const uint32_t cp = f.first + uint32_t(i);
        std::string ch;
        if (cp < 0x80)
            ch = char(cp);
        else
            ch = {char(0xc0 | (cp >> 6)), char(0x80 | (cp & 0x3f))};
        x = f.drawText(screen_, x, y, ch, kTextColor, scale_);
    }
    y += line + 8;
    font::drawText(screen_, kMargin, y, "Sample:", kLabelColor);
    y += font::kLineHeight + 2;
    for (const char* sample : kSamples) {
        f.drawText(screen_, kMargin, y, sample, kTextColor, scale_);
        y += line;
    }
}

void FontViewer::updateTitle() {
    if (fonts_.empty())
        return;
    const GameFont& f = fonts_[size_t(current_)];
    size_t drawn = 0;
    for (const auto& g : f.glyphs)
        drawn += g.height > 0;
    sys_.setWindowTitle("New Worlds fonts " + std::to_string(current_ + 1) + "/" + std::to_string(fonts_.size()) +
                        ": " + f.name + " in " + f.archive + " (" + std::to_string(drawn) + " characters, " +
                        std::to_string(f.lineHeight) + " pixels high, image " + std::to_string(f.imageWidth) + "x" +
                        std::to_string(f.imageHeight) + ")");
}

} // namespace reluva::newworlds
