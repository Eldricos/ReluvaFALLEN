// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

namespace reluva::newworlds {

// A vertex after projection to the screen.
struct ScreenVertex {
    float x, y; // pixels; pixel centres are at +0.5
    float invZ; // 1 / view-space depth: larger is nearer
    float u, v; // texture coordinates
    float s = 1; // light level, interpolated for smooth shading
};

// Software triangle rasteriser with a depth buffer, drawing into an RGBA32
// surface. A top-left fill rule draws each pixel of a shared edge once, and
// texture coordinates (and light levels) are interpolated with perspective
// correction.
class Rasterizer {
public:
    void begin(Surface& target) {
        target_ = &target;
        depth_.assign(size_t(target.w) * size_t(target.h), 0.0f);
    }

    // shade(u, v, rgba), or shade(u, v, light, rgba), sets the pixel's colour
    // (4 bytes, R G B A; it may blend with what's there) and returns whether to
    // record its depth: false for a transparent or translucent texel, so it
    // doesn't hide faces drawn later.
    template <typename Shade>
    void drawTriangle(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c, Shade&& shade) {
        const ScreenVertex* v0 = &a;
        const ScreenVertex* v1 = &b;
        const ScreenVertex* v2 = &c;
        float area = edge(*v0, *v1, v2->x, v2->y);
        if (area == 0 || !std::isfinite(area))
            return;
        if (area < 0) {
            std::swap(v1, v2);
            area = -area;
        }
        const int w = target_->w, h = target_->h;
        const int x0 = std::max(0, int(std::floor(std::min({v0->x, v1->x, v2->x}))));
        const int x1 = std::min(w - 1, int(std::ceil(std::max({v0->x, v1->x, v2->x}))));
        const int y0 = std::max(0, int(std::floor(std::min({v0->y, v1->y, v2->y}))));
        const int y1 = std::min(h - 1, int(std::ceil(std::max({v0->y, v1->y, v2->y}))));
        const bool tl0 = topLeft(*v1, *v2), tl1 = topLeft(*v2, *v0), tl2 = topLeft(*v0, *v1);
        const float inv = 1.0f / area;
        for (int y = y0; y <= y1; ++y) {
            const float py = float(y) + 0.5f;
            uint8_t* row = target_->row(y);
            float* depthRow = &depth_[size_t(y) * size_t(w)];
            for (int x = x0; x <= x1; ++x) {
                const float px = float(x) + 0.5f;
                const float w0 = edge(*v1, *v2, px, py);
                const float w1 = edge(*v2, *v0, px, py);
                const float w2 = edge(*v0, *v1, px, py);
                if (w0 < 0 || w1 < 0 || w2 < 0 || (w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2))
                    continue;
                const float b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
                const float invZ = b0 * v0->invZ + b1 * v1->invZ + b2 * v2->invZ;
                if (invZ <= depthRow[x])
                    continue;
                const float u = (b0 * v0->u * v0->invZ + b1 * v1->u * v1->invZ + b2 * v2->u * v2->invZ) / invZ;
                const float v = (b0 * v0->v * v0->invZ + b1 * v1->v * v1->invZ + b2 * v2->v * v2->invZ) / invZ;
                bool opaque;
                if constexpr (std::is_invocable_v<Shade, float, float, float, uint8_t*>) {
                    const float light =
                        (b0 * v0->s * v0->invZ + b1 * v1->s * v1->invZ + b2 * v2->s * v2->invZ) / invZ;
                    opaque = shade(u, v, light, row + x * 4);
                } else {
                    opaque = shade(u, v, row + x * 4);
                }
                if (opaque)
                    depthRow[x] = invZ;
            }
        }
    }

private:
    // Twice the signed area of (p, q, (x, y)); its gradient points into the
    // triangle when the triangle's area is positive.
    static float edge(const ScreenVertex& p, const ScreenVertex& q, float x, float y) {
        return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
    }
    // With y pointing down: a top edge is horizontal with the inside below it,
    // a left edge has the inside to its right.
    static bool topLeft(const ScreenVertex& p, const ScreenVertex& q) {
        const float dx = q.x - p.x, dy = q.y - p.y;
        return dy < 0 || (dy == 0 && dx > 0);
    }

    Surface* target_ = nullptr;
    std::vector<float> depth_;
};

} // namespace reluva::newworlds
