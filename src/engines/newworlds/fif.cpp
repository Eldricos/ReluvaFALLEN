// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/fif.h"

#include "common/text.h"
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/texture.h"

#include <algorithm>
#include <cstring>

namespace reluva::newworlds {

namespace {

constexpr size_t kHeaderSize = 24; // "FONTPROP", u16 first, u16 count, u32 size, u32 height, u32 ?
constexpr size_t kGlyphSize = 16;  // u32 x, y, width, height

int32_t s32(const std::vector<uint8_t>& d, size_t o) {
    return int32_t(uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24);
}

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

Error parseFif(const std::vector<uint8_t>& data, GameFont& out) {
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "FONTPROP", 8) != 0)
        return Error::fail("not a FONTPROP font");
    out.first = uint32_t(data[8] | data[9] << 8);
    const size_t count = size_t(data[10] | data[11] << 8);
    out.size = s32(data, 12);
    out.lineHeight = s32(data, 16);
    if (data.size() != kHeaderSize + count * kGlyphSize)
        return Error::fail("font of " + std::to_string(count) + " glyphs is " + std::to_string(data.size()) +
                           " bytes, not " + std::to_string(kHeaderSize + count * kGlyphSize));
    out.glyphs.clear();
    for (size_t i = 0; i < count; ++i) {
        const size_t o = kHeaderSize + i * kGlyphSize;
        GameFont::Glyph g{s32(data, o), s32(data, o + 4), s32(data, o + 8), s32(data, o + 12)};
        if (g.x < 0 || g.y < 0 || g.width < 0 || g.height < 0)
            return Error::fail("glyph " + std::to_string(out.first + i) + " has a negative position or size");
        out.glyphs.push_back(g);
    }
    return Error::success();
}

Error setFontImage(const std::vector<uint8_t>& fbp, GameFont& font) {
    Texture t;
    if (Error err = decodeTexture(fbp, t))
        return err;
    for (const GameFont::Glyph& g : font.glyphs)
        if (g.height > 0 && (g.x + g.width > t.width || g.y + g.height > t.height))
            return Error::fail("a glyph lies outside the " + std::to_string(t.width) + "x" + std::to_string(t.height) +
                               " image");
    font.imageWidth = t.width;
    font.imageHeight = t.height;
    font.coverage.resize(size_t(t.width) * size_t(t.height));
    for (size_t i = 0; i < font.coverage.size(); ++i)
        font.coverage[i] = std::max({t.rgba[i * 4], t.rgba[i * 4 + 1], t.rgba[i * 4 + 2]});
    return Error::success();
}

const GameFont::Glyph* GameFont::glyph(uint32_t codePoint) const {
    auto own = [&](uint32_t c) -> const Glyph* {
        if (c < first || c - first >= glyphs.size() || glyphs[c - first].height == 0)
            return nullptr;
        return &glyphs[c - first];
    };
    if (const Glyph* g = own(codePoint))
        return g;
    if (const Glyph* g = own(text::toLatin1(codePoint)))
        return g;
    return own('?');
}

int GameFont::textWidth(std::string_view utf8) const {
    int w = 0;
    for (size_t i = 0; i < utf8.size();)
        if (const Glyph* g = glyph(text::nextUtf8(utf8, i)))
            w += g->width;
    return w;
}

int GameFont::drawText(Surface& surface, int x, int y, std::string_view utf8, Color color, int scale) const {
    if (surface.format != PixelFormat::RGBA32 || coverage.empty())
        return x;
    for (size_t i = 0; i < utf8.size();) {
        const Glyph* g = glyph(text::nextUtf8(utf8, i));
        if (!g)
            continue;
        for (int gy = 0; gy < g->height; ++gy)
            for (int gx = 0; gx < g->width; ++gx) {
                const int c = coverage[size_t(g->y + gy) * size_t(imageWidth) + size_t(g->x + gx)];
                if (c == 0)
                    continue;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        const int px = x + gx * scale + dx, py = y + gy * scale + dy;
                        if (px < 0 || py < 0 || px >= surface.w || py >= surface.h)
                            continue;
                        uint8_t* p = surface.row(py) + px * 4;
                        p[0] = uint8_t(p[0] + (color.r - p[0]) * c / 255);
                        p[1] = uint8_t(p[1] + (color.g - p[1]) * c / 255);
                        p[2] = uint8_t(p[2] + (color.b - p[2]) * c / 255);
                    }
            }
        x += g->width * scale;
    }
    return x;
}

std::vector<GameFont> loadGameFonts(const Archives& archives) {
    std::vector<GameFont> fonts;
    for (const auto& cat : archives)
        for (const CatEntry& e : cat->entries()) {
            if (e.container || !endsWith(e.name, ".fif"))
                continue;
            const std::string stem = e.name.substr(0, e.name.size() - 4);
            if (std::any_of(fonts.begin(), fonts.end(), [&](const GameFont& f) { return f.name == stem; }))
                continue;
            const CatEntry* image = cat->find(stem + ".fbp");
            std::vector<uint8_t> fif, fbp;
            GameFont font;
            if (!image || cat->read(e, fif) || cat->read(*image, fbp) || parseFif(fif, font) ||
                setFontImage(fbp, font))
                continue;
            font.name = stem;
            font.archive = cat->path().filename().string();
            fonts.push_back(std::move(font));
        }
    return fonts;
}

} // namespace reluva::newworlds
