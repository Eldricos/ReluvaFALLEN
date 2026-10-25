// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/patch.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

// A vertex's attributes as floats, so they can be blended.
struct Point {
    float v[15]; // pos 3, st 2, lightmap 2, normal 3, color 4 (0-255), unused 1
};

Point toPoint(const BspVertex& b) {
    Point p{};
    for (int i = 0; i < 3; ++i) {
        p.v[i] = b.pos[size_t(i)];
        p.v[7 + i] = b.normal[size_t(i)];
    }
    for (int i = 0; i < 2; ++i) {
        p.v[3 + i] = b.st[size_t(i)];
        p.v[5 + i] = b.lightmap[size_t(i)];
    }
    for (int i = 0; i < 4; ++i)
        p.v[10 + i] = b.color[size_t(i)];
    return p;
}

BspVertex toVertex(const Point& p) {
    BspVertex b;
    for (int i = 0; i < 3; ++i) {
        b.pos[size_t(i)] = p.v[i];
        b.normal[size_t(i)] = p.v[7 + i];
    }
    for (int i = 0; i < 2; ++i) {
        b.st[size_t(i)] = p.v[3 + i];
        b.lightmap[size_t(i)] = p.v[5 + i];
    }
    for (int i = 0; i < 4; ++i)
        b.color[size_t(i)] = static_cast<uint8_t>(std::clamp(std::lround(p.v[10 + i]), 0L, 255L));
    // Blending normals shortens them.
    const float len = std::sqrt(b.normal[0] * b.normal[0] + b.normal[1] * b.normal[1] + b.normal[2] * b.normal[2]);
    if (len > 0)
        for (float& n : b.normal)
            n /= len;
    return b;
}

// Quadratic Bezier: (1-t)^2 a + 2t(1-t) b + t^2 c.
Point bezier(const Point& a, const Point& b, const Point& c, float t) {
    const float wa = (1 - t) * (1 - t), wb = 2 * t * (1 - t), wc = t * t;
    Point r;
    for (int i = 0; i < 15; ++i)
        r.v[i] = wa * a.v[i] + wb * b.v[i] + wc * c.v[i];
    return r;
}

} // namespace

void tessellatePatch(const BspVertex* control, int width, int height, int level, std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices) {
    if (width < 3 || height < 3 || level < 1)
        return;
    const int blocksX = (width - 1) / 2, blocksY = (height - 1) / 2;
    const int cols = blocksX * level + 1, rows = blocksY * level + 1;
    const uint32_t base = static_cast<uint32_t>(vertices.size());

    for (int row = 0; row < rows; ++row) {
        // Which block this row is in, and where in it; the last row belongs
        // to the last block at t = 1.
        const int by = std::min(row / level, blocksY - 1);
        const float tv = float(row - by * level) / float(level);
        for (int col = 0; col < cols; ++col) {
            const int bx = std::min(col / level, blocksX - 1);
            const float tu = float(col - bx * level) / float(level);
            Point across[3];
            for (int j = 0; j < 3; ++j) {
                const BspVertex* r = control + (by * 2 + j) * width + bx * 2;
                across[j] = bezier(toPoint(r[0]), toPoint(r[1]), toPoint(r[2]), tu);
            }
            vertices.push_back(toVertex(bezier(across[0], across[1], across[2], tv)));
        }
    }

    // Wind the triangles clockwise seen from where the normals point: the
    // geometric normal (right-hand rule) should face away from them. Decide
    // once per patch from its first triangle that isn't degenerate.
    auto at = [&](int r, int c) { return base + uint32_t(r * cols + c); };
    bool flip = false;
    for (int r = 0; r + 1 < rows; ++r) {
        bool decided = false;
        for (int c = 0; c + 1 < cols && !decided; ++c) {
            const BspVertex& a = vertices[at(r, c)];
            const BspVertex& b = vertices[at(r + 1, c)];
            const BspVertex& d = vertices[at(r, c + 1)];
            const float e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
            const float e2[3] = {d.pos[0] - a.pos[0], d.pos[1] - a.pos[1], d.pos[2] - a.pos[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
            const float s = n[0] * a.normal[0] + n[1] * a.normal[1] + n[2] * a.normal[2];
            if (std::fabs(s) > 1e-6f) {
                flip = s > 0;
                decided = true;
            }
        }
        if (decided)
            break;
    }
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c + 1 < cols; ++c) {
            const uint32_t a = at(r, c), b = at(r + 1, c), d = at(r, c + 1), e = at(r + 1, c + 1);
            if (flip)
                indices.insert(indices.end(), {a, d, b, b, d, e});
            else
                indices.insert(indices.end(), {a, b, d, b, e, d});
        }
}

float patchBend(const BspVertex* control, int width, int height) {
    auto off = [](const std::array<float, 3>& a, const std::array<float, 3>& m, const std::array<float, 3>& b) {
        float d2 = 0;
        for (size_t k = 0; k < 3; ++k) {
            const float d = m[k] - (a[k] + b[k]) / 2;
            d2 += d * d;
        }
        return std::sqrt(d2);
    };
    float bend = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x + 2 < width; x += 2)
            bend = std::max(bend, off(control[y * width + x].pos, control[y * width + x + 1].pos,
                                      control[y * width + x + 2].pos));
    for (int x = 0; x < width; ++x)
        for (int y = 0; y + 2 < height; y += 2)
            bend = std::max(bend, off(control[y * width + x].pos, control[(y + 1) * width + x].pos,
                                      control[(y + 2) * width + x].pos));
    return bend;
}

int patchLevel(const BspVertex* control, int width, int height, float error) {
    const float bend = patchBend(control, width, height);
    if (bend < 0.5f)
        return 1;
    return std::clamp(int(std::ceil(std::sqrt(bend / (2 * std::max(error, 0.01f))))), 2, 16);
}

} // namespace reluva::idtech3
