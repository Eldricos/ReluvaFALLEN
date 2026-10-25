// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/lexer.h"

namespace reluva::idtech3 {

bool Lexer::skipSpace(bool crossLines) {
    while (pos_ < text_.size()) {
        const char c = text_[pos_];
        if (c == '\n') {
            if (!crossLines)
                return false;
            ++line_;
            ++pos_;
        } else if (static_cast<unsigned char>(c) <= ' ') {
            ++pos_;
        } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
            while (pos_ < text_.size() && text_[pos_] != '\n')
                ++pos_;
        } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
            // A block comment counts as crossing the lines it spans.
            const size_t close = text_.find("*/", pos_ + 2);
            const size_t stop = close == std::string_view::npos ? text_.size() : close + 2;
            for (size_t i = pos_; i < stop; ++i)
                if (text_[i] == '\n') {
                    if (!crossLines) {
                        pos_ = i;
                        return false;
                    }
                    ++line_;
                }
            pos_ = stop;
        } else {
            break;
        }
    }
    return true;
}

std::optional<std::string_view> Lexer::next(bool crossLines) {
    if (!skipSpace(crossLines) || pos_ >= text_.size())
        return std::nullopt;
    if (text_[pos_] == '"') {
        const size_t start = ++pos_;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\n')
                ++line_;
            ++pos_;
        }
        std::string_view tok = text_.substr(start, pos_ - start);
        if (pos_ < text_.size())
            ++pos_; // closing quote
        return tok;
    }
    const size_t start = pos_;
    while (pos_ < text_.size() && static_cast<unsigned char>(text_[pos_]) > ' ')
        ++pos_;
    return text_.substr(start, pos_ - start);
}

std::optional<std::string_view> Lexer::peek(bool crossLines) {
    const size_t pos = pos_;
    const int line = line_;
    auto tok = next(crossLines);
    pos_ = pos;
    line_ = line;
    return tok;
}

void Lexer::skipLine() {
    while (pos_ < text_.size() && text_[pos_] != '\n')
        ++pos_;
}

bool Lexer::skipBlock() {
    int depth = 1;
    while (auto tok = next()) {
        if (*tok == "{")
            ++depth;
        else if (*tok == "}" && --depth == 0)
            return true;
    }
    return false;
}

bool Lexer::atEnd() {
    skipSpace(true);
    return pos_ >= text_.size();
}

} // namespace reluva::idtech3
