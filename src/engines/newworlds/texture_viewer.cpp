// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/texture.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>
#include <cstring>

namespace reluva::newworlds {

namespace {

constexpr int kFilterCount = 4;
constexpr const char* kFilterNames[kFilterCount] = {"all", "failed", "colour-keyed", "with alpha"};

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

} // namespace

TextureViewer::TextureViewer(System& sys, Surface& screen, const Archives& archives)
    : sys_(sys), screen_(screen), archives_(archives) {
    for (size_t a = 0; a < archives_.size(); ++a) {
        const auto& entries = archives_[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].tag == cattag::Texture && !endsWith(entries[i].name, ".stb"))
                textures_.push_back({{a, i}});
    }
}

void TextureViewer::activate() {
    if (!classified_) {
        classify();
        classified_ = true;
        applyFilter(0);
        return;
    }
    if (visible_.empty())
        applyFilter(filter_);
    else
        show(current_);
}

void TextureViewer::handleKey(uint32_t k) {
    if (k == 'f') {
        applyFilter((filter_ + 1) % kFilterCount);
        return;
    }
    const int n = int(visible_.size());
    if (n == 0)
        return;
    switch (k) {
    case key::Right: show((current_ + 1) % n); break;
    case key::Left: show((current_ + n - 1) % n); break;
    case key::PageDown: show(std::min(current_ + 10, n - 1)); break;
    case key::PageUp: show(std::max(current_ - 10, 0)); break;
    case key::Home: show(0); break;
    case key::End: show(n - 1); break;
    default: break;
    }
}

// Decode every texture once so the filters know which is which.
void TextureViewer::classify() {
    std::vector<uint8_t> data;
    Texture tex;
    for (TextureRef& t : textures_) {
        CatArchive& cat = *archives_[t.ref.archive];
        t.ok = !cat.read(cat.entries()[t.ref.entry], data) && !decodeTexture(data, tex);
        t.colourKeyed = t.ok && tex.colourKeyed;
        t.alpha = t.ok && tex.format == 6;
    }
}

void TextureViewer::applyFilter(int filter) {
    filter_ = filter;
    visible_.clear();
    for (size_t i = 0; i < textures_.size(); ++i) {
        const TextureRef& t = textures_[i];
        const bool keep =
            filter == 0 || (filter == 1 && !t.ok) || (filter == 2 && t.colourKeyed) || (filter == 3 && t.alpha);
        if (keep)
            visible_.push_back(i);
    }
    if (visible_.empty()) {
        drawCheckerboard();
        sys_.setWindowTitle(std::string("New Worlds textures [") + kFilterNames[filter_] +
                            "]: none (press F for the next filter, Tab for models)");
        return;
    }
    show(0);
}

void TextureViewer::show(int index) {
    current_ = index;
    const EntryRef& ref = textures_[visible_[size_t(index)]].ref;
    CatArchive& cat = *archives_[ref.archive];
    const auto& entry = cat.entries()[ref.entry];

    drawCheckerboard();
    std::string status;
    std::vector<uint8_t> data;
    Texture tex;
    if (Error err = cat.read(entry, data)) {
        status = err.message;
    } else if (Error err2 = decodeTexture(data, tex)) {
        status = err2.message;
        fill(0, 0, screen_.w, screen_.h, 80, 0, 0); // dark red: couldn't decode
    } else {
        drawTexture(tex);
        status = "format " + std::to_string(tex.format) + ", " + std::to_string(tex.width) + "x" +
                 std::to_string(tex.height) +
                 (tex.colourKeyed ? ", colour key " + std::to_string(tex.keyIndex) : std::string());
    }
    sys_.setWindowTitle(std::string("New Worlds textures [") + kFilterNames[filter_] + "] " +
                        std::to_string(index + 1) + "/" + std::to_string(visible_.size()) + ": " + entry.name +
                        " (" + status + ")");
}

void TextureViewer::fill(int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = y0; y < y0 + h; ++y) {
        uint8_t* row = screen_.row(y) + x0 * 4;
        for (int x = 0; x < w; ++x, row += 4) {
            row[0] = r;
            row[1] = g;
            row[2] = b;
            row[3] = 255;
        }
    }
}

void TextureViewer::drawCheckerboard() {
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            row[0] = row[1] = row[2] = v;
            row[3] = 255;
        }
    }
}

// Draw centred, scaled up by the largest whole factor that fits (or down by
// halves), blended over the checkerboard using the alpha layer.
void TextureViewer::drawTexture(const Texture& tex) {
    const int sw = screen_.w, sh = screen_.h;
    int up = 1, down = 1;
    while (tex.width * (up + 1) <= sw - 20 && tex.height * (up + 1) <= sh - 20)
        ++up;
    while (tex.width / down > sw || tex.height / down > sh)
        down *= 2;
    const int w = tex.width * up / down, h = tex.height * up / down;
    const int x0 = (sw - w) / 2, y0 = (sh - h) / 2;
    for (int y = 0; y < h; ++y) {
        const int sy = y * down / up;
        uint8_t* dst = screen_.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, dst += 4) {
            const uint8_t* src = &tex.rgba[(size_t(sy) * size_t(tex.width) + size_t(x * down / up)) * 4];
            // Colour key, then the alpha layer if there is one.
            const size_t i = size_t(sy) * size_t(tex.width) + size_t(x * down / up);
            const int a = src[3] && !tex.alpha.empty() ? tex.alpha[i] : src[3];
            for (int c = 0; c < 3; ++c)
                dst[c] = uint8_t((src[c] * a + dst[c] * (255 - a)) / 255);
        }
    }
}

} // namespace reluva::newworlds
