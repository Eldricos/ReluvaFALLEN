// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <optional>
#include <string_view>

namespace reluva::idtech3 {

// Splits the text files id Tech 3 games use (shader scripts, the BSP entity
// lump, .arena/.skin/.cfg files) into tokens, as described in
// docs/formats/idtech3/text.md: a token is a "quoted string" (without its
// quotes) or a run of characters up to the next whitespace; // and /* */
// comments are skipped.
class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    // The next token, or nullopt at the end of the text. With
    // crossLines = false, also nullopt (without consuming anything) when
    // the next token is on a later line, for directives that take the
    // rest of their line.
    std::optional<std::string_view> next(bool crossLines = true);
    // The next token without consuming it.
    std::optional<std::string_view> peek(bool crossLines = true);
    // Skip whatever is left of the current line.
    void skipLine();
    // Skip tokens up to and including the "}" matching an already consumed
    // "{". Returns false if the text ends first.
    bool skipBlock();

    int line() const { return line_; } // 1-based line of the last token read
    bool atEnd();

private:
    // Skips whitespace and comments; stops before a newline when
    // crossLines is false. Returns false if it stopped at a newline.
    bool skipSpace(bool crossLines);

    std::string_view text_;
    size_t pos_ = 0;
    int line_ = 1;
};

} // namespace reluva::idtech3
