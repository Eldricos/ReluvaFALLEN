// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/game_ui.h"

#include "common/text.h"

#include <algorithm>
#include <cctype>

namespace reluva::newworlds {

namespace {

bool loadPicture(const Archives& archives, const std::string& name, Texture& out) {
    for (const auto& a : archives)
        if (const CatEntry* e = a->find(name)) {
            std::vector<uint8_t> data;
            return !a->read(*e, data) && !decodeTexture(data, out);
        }
    return false;
}

// The Romulan recordings' faces by section, matched to the portraits
// (gui.md#the-in-game-interface): a section's officer, usually this one.
constexpr std::pair<const char*, const char*> kRomulanFaces[] = {
    {"Operations", "r_c17"}, {"Engineering", "r_c19"}, {"Intelligence", "r_c18"},
    {"Science", "r_c21"},    {"Security", "r_c20"},
};

// Sections in the order the side's crew portraits are handed out.
constexpr const char* kSections[] = {"Command",      "Operations",   "Engineering",   "Science",
                                     "Medical",      "Security",     "Intelligence",  "Communications"};

// Puts a texture's pixel down, blended by its alpha layer when it has one
// (the frame pieces' edges are smoothed that way).
void putPixel(uint8_t* o, const Texture& t, size_t i) {
    const uint8_t* p = &t.rgba[i * 4];
    if (p[3] == 0)
        return;
    const int a = t.alpha.empty() ? 255 : t.alpha[i];
    for (int c = 0; c < 3; ++c)
        o[c] = uint8_t((p[c] * a + o[c] * (255 - a)) / 255);
}

} // namespace

bool GameUi::load(const Archives& archives, char side) {
    side_ = side;
    const std::string folder = side == 'K' ? "kln" : side == 'R' ? "rom" : "fed";
    const std::string base = "lbp/gui/" + folder + "/";
    loaded_ = loadPicture(archives, base + "corner.lbp", corner_) && loadPicture(archives, base + "border.lbp", border_);
    if (!loaded_)
        return false;
    loadPicture(archives, base + "wallpapr.lbp", wallpaper_);
    for (const char* n : {"logo32", "shldicon", "pwricon", "popnicon", "closebut", "vcr"}) {
        Texture t;
        if (loadPicture(archives, base + n + ".lbp", t))
            icons_[n] = std::move(t);
    }
    // Crew portraits: f_c1, k_c1, r_c1 ... (the large ones, 64 x 60).
    const char prefix = side == 'K' ? 'k' : side == 'R' ? 'r' : 'f';
    for (int i = 1; i <= 40 && crew_.size() < std::size(kSections); ++i) {
        Texture t;
        if (loadPicture(archives, base + "ptrt/crew/lg/" + prefix + "_c" + std::to_string(i) + ".lbp", t))
            crew_.push_back(std::move(t));
    }
    if (side == 'R')
        for (const auto& [section, stem] : kRomulanFaces) {
            Texture t;
            if (loadPicture(archives, base + "ptrt/crew/lg/" + stem + ".lbp", t))
                faces_[section] = std::move(t);
        }
    for (const char* n : {"taubat", "metar", "hubrin", "orion"}) {
        Texture t;
        if (loadPicture(archives, base + "ptrt/crew/lg/" + n + ".lbp", t))
            natives_[n] = std::move(t);
    }
    // Fonts: fe8pt/kl8pt/rm8pt and fe5pt/kl5pt/rm5pt.
    fonts_ = loadGameFonts(archives);
    const std::string f = side == 'K' ? "kl" : side == 'R' ? "rm" : "fe";
    for (const GameFont& g : fonts_) {
        if (g.name == "fnt/" + f + "5pt")
            small_ = &g;
        if (g.name == "fnt/" + f + "8pt")
            large_ = &g;
    }
    if (side == 'K')
        title_ = {240, 150, 40}, text_ = {90, 170, 255};
    return true;
}

void GameUi::blit(Surface& s, const Texture& t, int dx, int dy, bool flipX, bool flipY, bool transpose) const {
    const int w = transpose ? t.height : t.width, h = transpose ? t.width : t.height;
    for (int y = 0; y < h; ++y) {
        if (dy + y < 0 || dy + y >= s.h)
            continue;
        uint8_t* row = s.row(dy + y);
        for (int x = 0; x < w; ++x) {
            if (dx + x < 0 || dx + x >= s.w)
                continue;
            int sx = transpose ? y : x, sy = transpose ? x : y;
            if (flipX)
                sx = t.width - 1 - sx;
            if (flipY)
                sy = t.height - 1 - sy;
            putPixel(row + (dx + x) * 4, t, size_t(sy) * size_t(t.width) + size_t(sx));
        }
    }
}

void GameUi::drawPicture(Surface& s, const Texture& t, int x0, int y0, int w, int h) const {
    if (t.rgba.empty() || w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y) {
        if (y0 + y < 0 || y0 + y >= s.h)
            continue;
        uint8_t* row = s.row(y0 + y);
        const int sy = y * t.height / h;
        for (int x = 0; x < w; ++x) {
            if (x0 + x < 0 || x0 + x >= s.w)
                continue;
            putPixel(row + (x0 + x) * 4, t, size_t(sy) * size_t(t.width) + size_t(x * t.width / w));
        }
    }
}

void GameUi::drawPanel(Surface& s, int x, int y, int w, int h, std::string_view title) const {
    const int c = corner_.width, b = border_.height; // 12 and 12
    // The inside: the side's wallpaper tile, darkened.
    for (int yy = y + b; yy < y + h - b; ++yy) {
        if (yy < 0 || yy >= s.h)
            continue;
        uint8_t* row = s.row(yy);
        for (int xx = x + b; xx < x + w - b; ++xx) {
            if (xx < 0 || xx >= s.w)
                continue;
            uint8_t* o = row + xx * 4;
            if (!wallpaper_.rgba.empty()) {
                const uint8_t* p = &wallpaper_.rgba[(size_t((yy - y) % wallpaper_.height) * size_t(wallpaper_.width) +
                                                     size_t((xx - x) % wallpaper_.width)) *
                                                    4];
                o[0] = uint8_t(p[0] / 2), o[1] = uint8_t(p[1] / 2), o[2] = uint8_t(p[2] / 2);
            } else {
                o[0] = o[1] = o[2] = 0;
            }
        }
    }
    // The edges: the border tile along the top, flipped along the bottom,
    // turned along the sides; the corner piece in each corner.
    for (int xx = x + c; xx < x + w - c; xx += border_.width) {
        blit(s, border_, xx, y, false, false, false);
        blit(s, border_, xx, y + h - b, false, true, false);
    }
    for (int yy = y + c; yy < y + h - c; yy += border_.width) {
        blit(s, border_, x, yy, false, false, true);
        blit(s, border_, x + w - b, yy, false, true, true);
    }
    blit(s, corner_, x, y, false, false, false);
    blit(s, corner_, x + w - c, y, true, false, false);
    blit(s, corner_, x, y + h - c, false, true, false);
    blit(s, corner_, x + w - c, y + h - c, true, true, false);
    // The title, centred in the top bar.
    if (!title.empty()) {
        const int tw = large_ ? large_->textWidth(title) : 0;
        drawLarge(s, x + (w - tw) / 2, y + 1, title, title_);
    }
}

int GameUi::drawSmall(Surface& s, int x, int y, std::string_view text, Color c) const {
    return small_ ? small_->drawText(s, x, y, text, c) : x;
}

int GameUi::drawLarge(Surface& s, int x, int y, std::string_view text, Color c) const {
    return large_ ? large_->drawText(s, x, y, text, c) : drawSmall(s, x, y, text, c);
}

int GameUi::smallWidth(std::string_view text) const { return small_ ? small_->textWidth(text) : 0; }

int GameUi::smallHeight() const { return small_ ? small_->lineHeight : 9; }

std::vector<std::string> GameUi::wrapSmall(std::string_view text, int width) const {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        const size_t space = std::min(text.find(' ', i), text.size());
        const std::string word(text.substr(i, space - i));
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && smallWidth(next) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
        i = space + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

const Texture* GameUi::portraitFor(std::string_view sender, std::string_view natives) const {
    for (const auto& [section, face] : faces_)
        if (sender.rfind(section, 0) == 0)
            return &face;
    for (size_t i = 0; i < std::size(kSections); ++i)
        if (sender.rfind(kSections[i], 0) == 0)
            return i < crew_.size() ? &crew_[i] : nullptr;
    if (sender.rfind("Incoming", 0) == 0) {
        const auto it = natives_.find(std::string(natives));
        if (it != natives_.end())
            return &it->second;
    }
    return icon("logo32");
}

const Texture* GameUi::icon(std::string_view name) const {
    const auto it = icons_.find(std::string(name));
    return it == icons_.end() ? nullptr : &it->second;
}

} // namespace reluva::newworlds
