// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::idtech3::eliteforce {

// The start of every entity, the part the engine reads and writes, as
// Elite Force lays it out (found from the game's own code: what it passes
// to traces and SetBrushModel, and the size of what it copies whole; see
// eliteforce-syscalls.md). An entity state of 204 bytes (Quake III's is
// 208), then entityShared_t at 204, with one field fewer than Quake III's
// before the box (which of linkcount, svFlags and singleClient isn't
// known yet, so only `linked` and `bmodel`, the same either way, are
// written).
namespace ent {
constexpr int32_t Number = 0;       // s.number
constexpr int32_t ModelIndex = 160; // s.modelindex, at Quake III's offset (assumed: the 4 bytes Elite Force lacks come later)
// s.solid at Quake III's offset (assumed as above: Elite Force lacking
// only Quake III's last field, generic1): what the client game draws a
// brush entity by, and predicts collisions with.
constexpr int32_t Solid = 176;
constexpr int32_t kSolidBrushModel = 0xffffff;
constexpr int32_t Linked = 204;
constexpr int32_t BModel = 216;
constexpr int32_t Mins = 220, Maxs = 232;
constexpr int32_t Contents = 244;
constexpr int32_t AbsMin = 248, AbsMax = 260;
constexpr int32_t CurrentOrigin = 272, CurrentAngles = 284;
constexpr int32_t OwnerNum = 296;
constexpr int32_t SharedEnd = 300;
} // namespace ent

constexpr int32_t kEntityNone = 1023, kEntityWorld = 1022;

} // namespace reluva::idtech3::eliteforce
