// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/rasterizer.h"

#include <cmath>
#include <vector>

// Small 3D helpers shared by the viewers: vectors, an orbiting camera, and
// near-plane clipping. World space is right-handed with z up.

namespace reluva::newworlds {

struct Vec {
    float x, y, z;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec normalize(Vec a) {
    const float len = std::sqrt(dot(a, a));
    return len > 0 ? a * (1.0f / len) : Vec{0, 0, 0};
}

// A polygon corner in view space (x right, y up, z away from the camera).
struct ViewVertex {
    float x, y, z, u, v;
    float s = 1; // light level
};

// A camera looking at `centre` from `distance` away, turned by yaw about z and
// raised by pitch, with a perspective projection onto a w x h screen.
struct OrbitCamera {
    Vec eye, right, up, forward;
    float focal, nearZ, halfW, halfH;

    OrbitCamera(Vec centre, float yaw, float pitch, float distance, int w, int h)
        : focal(float(h) * 1.2f), nearZ(distance * 0.01f), halfW(float(w) / 2), halfH(float(h) / 2) {
        const Vec toCamera{std::cos(pitch) * std::sin(yaw), -std::cos(pitch) * std::cos(yaw), std::sin(pitch)};
        eye = centre + toCamera * distance;
        forward = toCamera * -1.0f;
        right = normalize(cross(forward, Vec{0, 0, 1}));
        up = cross(right, forward);
    }
    ViewVertex toView(Vec p, float u = 0, float v = 0) const {
        const Vec d = p - eye;
        return {dot(d, right), dot(d, up), dot(d, forward), u, v};
    }
    Vec directionToView(Vec n) const { return {dot(n, right), dot(n, up), dot(n, forward)}; }
    ScreenVertex project(const ViewVertex& p) const {
        return {halfW + focal * p.x / p.z, halfH - focal * p.y / p.z, 1.0f / p.z, p.u, p.v, p.s};
    }
};

// Keep the part of the polygon with z >= nearZ (Sutherland-Hodgman).
inline std::vector<ViewVertex> clipNear(const std::vector<ViewVertex>& in, float nearZ) {
    std::vector<ViewVertex> out;
    for (size_t i = 0; i < in.size(); ++i) {
        const ViewVertex& a = in[i];
        const ViewVertex& b = in[(i + 1) % in.size()];
        const bool aIn = a.z >= nearZ, bIn = b.z >= nearZ;
        if (aIn)
            out.push_back(a);
        if (aIn != bIn) {
            const float t = (nearZ - a.z) / (b.z - a.z);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, nearZ, a.u + (b.u - a.u) * t,
                           a.v + (b.v - a.v) * t, a.s + (b.s - a.s) * t});
        }
    }
    return out;
}

} // namespace reluva::newworlds
