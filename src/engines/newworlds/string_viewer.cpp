// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/font.h"
#include "common/text.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace reluva::newworlds {

namespace {

constexpr Color kBackground{22, 26, 38};
constexpr Color kIdColor{110, 118, 140};
constexpr Color kTextColor{220, 224, 232};
constexpr Color kHeadingColor{255, 200, 80}; // ~HEADING~
constexpr Color kFormatColor{120, 200, 255}; // printf codes: %d, %%
constexpr int kMargin = 4;

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// One word (or run of spaces) of a string, in its colour; a break forces a
// new line.
struct Piece {
    std::string text;
    Color color;
    bool space = false, lineBreak = false;
};

// Splits a string into pieces: "~HEADING~" on a line of its own, printf
// codes coloured, '\n' as breaks.
std::vector<Piece> piecesOf(const std::string& text) {
    std::vector<Piece> out;
    auto add = [&](const std::string& word, Color c) {
        // Mark printf codes within the word.
        size_t at = 0;
        for (size_t i = 0; i < word.size(); ++i) {
            if (word[i] != '%' || i + 1 >= word.size())
                continue;
            size_t j = i + 1;
            while (j < word.size() && std::isdigit(static_cast<unsigned char>(word[j])))
                ++j;
            if (j < word.size() && (word[j] == 'd' || word[j] == 's' || word[j] == '%' || word[j] == 'i')) {
                if (i > at)
                    out.push_back({word.substr(at, i - at), c});
                out.push_back({word.substr(i, j + 1 - i), kFormatColor});
                at = j + 1;
                i = j;
            }
        }
        if (at < word.size())
            out.push_back({word.substr(at), c});
    };
    std::string word;
    Color color = kTextColor;
    bool heading = false;
    auto flush = [&] {
        if (!word.empty())
            add(word, color);
        word.clear();
    };
    for (char c : text) {
        if (c == '~') {
            flush();
            heading = !heading;
            color = heading ? kHeadingColor : kTextColor;
            if (!heading)
                out.push_back({"", kTextColor, false, true});
        } else if (c == '\n') {
            flush();
            out.push_back({"", kTextColor, false, true});
        } else if (c == ' ' || c == '\t' || c == '\r') {
            flush();
            out.push_back({" ", color, true});
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

} // namespace

StringViewer::StringViewer(System& sys, Surface& screen, const Archives& archives)
    : sys_(sys), screen_(screen), archives_(archives) {
    // misc.cat stores one table (str/eng/frend.stb) with the texture tag, so
    // go by name as well as tag.
    for (size_t a = 0; a < archives_.size(); ++a) {
        const auto& entries = archives_[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].container && (entries[i].tag == cattag::Strings || endsWith(entries[i].name, ".stb")))
                tables_.push_back({a, i});
    }
    fonts_ = loadGameFonts(archives_);
}

int StringViewer::measure(std::string_view text) const {
    if (font_ < 0)
        return int(font::length(text)) * font::kAdvance * scale_;
    return fonts_[size_t(font_)].textWidth(text) * scale_;
}

int StringViewer::lineHeight() const {
    return (font_ < 0 ? font::kLineHeight : fonts_[size_t(font_)].lineHeight) * scale_;
}

int StringViewer::drawSpan(int x, int y, std::string_view text, Color color) {
    if (font_ < 0)
        return font::drawText(screen_, x, y, text, color, scale_);
    return fonts_[size_t(font_)].drawText(screen_, x, y, text, color, scale_);
}

void StringViewer::activate() {
    if (!shown_) {
        shown_ = true;
        show(0);
        return;
    }
    draw();
    updateTitle();
}

int StringViewer::rows() const {
    return std::max(1, (screen_.h - 2 * kMargin) / lineHeight());
}

void StringViewer::handleKey(uint32_t k) {
    const int n = int(tables_.size());
    if (n == 0)
        return;
    const int last = std::max(0, int(lines_.size()) - rows());
    switch (k) {
    case key::Right: show((current_ + 1) % n); return;
    case key::Left: show((current_ + n - 1) % n); return;
    case key::Down: top_ = std::min(top_ + 1, last); break;
    case key::Up: top_ = std::max(top_ - 1, 0); break;
    case key::PageDown: top_ = std::min(top_ + rows() - 1, last); break;
    case key::PageUp: top_ = std::max(top_ - (rows() - 1), 0); break;
    case key::Home: top_ = 0; break;
    case key::End: top_ = last; break;
    case '+':
    case '=':
        scale_ = std::min(scale_ + 1, 3);
        layout();
        break;
    case '-':
        scale_ = std::max(scale_ - 1, 1);
        layout();
        break;
    case 'f':
        // The next font: the built-in one, then each of the game's.
        font_ = font_ + 1 < int(fonts_.size()) ? font_ + 1 : -1;
        layout();
        break;
    default: return;
    }
    draw();
    updateTitle();
}

void StringViewer::show(int index) {
    current_ = index;
    top_ = 0;
    status_.clear();
    const EntryRef& ref = tables_[size_t(index)];
    CatArchive& cat = *archives_[ref.archive];
    std::vector<uint8_t> data;
    if (Error err = cat.read(cat.entries()[ref.entry], data))
        status_ = err.message;
    else if (Error err2 = table_.parse(data))
        status_ = err2.message;
    if (!status_.empty())
        table_ = StringTable{};
    layout();
    draw();
    updateTitle();
}

void StringViewer::layout() {
    lines_.clear();
    const int idWidth = measure("9999 ");
    const int width = std::max(measure("MMMMMMMMMM"), screen_.w - 2 * kMargin - idWidth);
    for (const StringTable::String& s : table_.strings()) {
        Line line{std::to_string(s.id), {}};
        int used = 0; // pixels on this line after the id column
        auto newLine = [&] {
            lines_.push_back(std::move(line));
            line = Line{};
            used = 0;
        };
        for (const Piece& p : piecesOf(s.text)) {
            if (p.lineBreak) {
                if (used > 0)
                    newLine();
                continue;
            }
            const int w = measure(p.text);
            if (p.space) {
                if (used > 0 && used + w <= width)
                    line.spans.push_back({p.text, p.color}), used += w;
                continue;
            }
            if (used > 0 && used + w > width)
                newLine();
            // Words longer than a line are cut, a character at a time.
            std::string part;
            for (size_t i = 0; i < p.text.size();) {
                const size_t from = i;
                text::nextUtf8(p.text, i);
                const std::string ch = p.text.substr(from, i - from);
                if (!part.empty() && used + measure(part + ch) > width) {
                    line.spans.push_back({part, p.color});
                    newLine();
                    part.clear();
                }
                part += ch;
            }
            used += measure(part);
            line.spans.push_back({std::move(part), p.color});
        }
        lines_.push_back(std::move(line));
    }
    top_ = std::min(top_, std::max(0, int(lines_.size()) - rows()));
}

void StringViewer::draw() {
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4)
            row[0] = kBackground.r, row[1] = kBackground.g, row[2] = kBackground.b, row[3] = 255;
    }
    if (!status_.empty()) {
        font::drawText(screen_, kMargin, kMargin, status_, kHeadingColor, scale_);
        return;
    }
    const int idWidth = measure("9999 ");
    for (int r = 0; r < rows() && top_ + r < int(lines_.size()); ++r) {
        const Line& line = lines_[size_t(top_ + r)];
        const int y = kMargin + r * lineHeight();
        // Ids right-aligned in their column.
        drawSpan(kMargin + idWidth - measure(line.id + " "), y, line.id, kIdColor);
        int x = kMargin + idWidth;
        for (const Span& span : line.spans)
            x = drawSpan(x, y, span.text, span.color);
    }
}

void StringViewer::updateTitle() {
    if (tables_.empty())
        return;
    const EntryRef& ref = tables_[size_t(current_)];
    std::string title = "New Worlds strings " + std::to_string(current_ + 1) + "/" + std::to_string(tables_.size()) +
                        ": " + archives_[ref.archive]->entries()[ref.entry].name + " in " +
                        archives_[ref.archive]->path().filename().string() + " (";
    if (!status_.empty()) {
        title += status_ + ")";
    } else {
        title += std::to_string(table_.strings().size()) + " strings)";
        title += font_ < 0 ? " [built-in font]" : " [font " + fonts_[size_t(font_)].name + "]";
        if (int(lines_.size()) > rows())
            title += " lines " + std::to_string(top_ + 1) + "-" +
                     std::to_string(std::min(top_ + rows(), int(lines_.size()))) + " of " +
                     std::to_string(lines_.size());
    }
    sys_.setWindowTitle(title);
}

} // namespace reluva::newworlds
