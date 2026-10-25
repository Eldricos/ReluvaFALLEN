// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string_view>

namespace reluva::font {

// A small built-in bitmap font (Reluva's own, drawn for it) for tools and
// data viewers that need to show text: printable ASCII in 5x9 cells (7 rows
// above the baseline, 2 below), with common accented and typographic
// characters drawn as their plain ASCII forms and anything else as '?'.
constexpr int kAdvance = 6;     // pixels per character, before scaling
constexpr int kLineHeight = 11; // pixels per line, before scaling

// Draws UTF-8 text with its top-left corner at (x, y) into an RGBA32
// surface, clipped to it; returns the x after the last character.
int drawText(Surface& surface, int x, int y, std::string_view utf8, Color color, int scale = 1);

// The number of characters drawText draws for the text.
size_t length(std::string_view utf8);

} // namespace reluva::font
