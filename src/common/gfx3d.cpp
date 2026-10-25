// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/gfx3d.h"

#include <cmath>

namespace reluva::gfx {

namespace {

Vec3 normalize(const Vec3& v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return len > 0 ? Vec3{v[0] / len, v[1] / len, v[2] / len} : v;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a[size_t(k * 4 + row)] * b[size_t(col * 4 + k)];
            r[size_t(col * 4 + row)] = s;
        }
    return r;
}

Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    // Camera looks down -Z; z_clip/w_clip runs 0 at zNear to 1 at zFar.
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = zFar / (zNear - zFar);
    m[11] = -1;
    m[14] = zNear * zFar / (zNear - zFar);
    return m;
}

Mat4 lookAlong(const Vec3& eye, const Vec3& forward, const Vec3& up) {
    const Vec3 f = normalize(forward);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 m = identity();
    for (int i = 0; i < 3; ++i) {
        m[size_t(i * 4 + 0)] = s[size_t(i)];
        m[size_t(i * 4 + 1)] = u[size_t(i)];
        m[size_t(i * 4 + 2)] = -f[size_t(i)];
    }
    m[12] = -dot(s, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
    return m;
}

Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    float out[4];
    for (int row = 0; row < 4; ++row)
        out[row] = m[size_t(row)] * p[0] + m[size_t(4 + row)] * p[1] + m[size_t(8 + row)] * p[2] + m[size_t(12 + row)];
    const float w = out[3] != 0 ? out[3] : 1;
    return {out[0] / w, out[1] / w, out[2] / w};
}

float evaluate(const Wave& wave, float time) {
    // Must match wave() in the backend's vertex shader.
    constexpr float kTau = 6.28318530718f;
    const float x = wave.phase + time * wave.frequency;
    const float f = x - std::floor(x);
    float v = 0;
    switch (wave.func) {
    case WaveFunc::Sin: v = std::sin(f * kTau); break;
    case WaveFunc::Triangle: v = f < 0.25f ? 4 * f : f < 0.75f ? 2 - 4 * f : 4 * f - 4; break;
    case WaveFunc::Square: v = f < 0.5f ? 1.0f : -1.0f; break;
    case WaveFunc::Sawtooth: v = f; break;
    case WaveFunc::InverseSawtooth: v = 1 - f; break;
    case WaveFunc::Noise: {
        // Smoothly interpolated values in -1..1 at whole numbers.
        auto hash = [](float n) {
            const float h = std::sin(n * 12.9898f) * 43758.5453f;
            return (h - std::floor(h)) * 2 - 1;
        };
        const float i = std::floor(x);
        v = hash(i) + (hash(i + 1) - hash(i)) * (f * f * (3 - 2 * f));
        break;
    }
    }
    return wave.base + wave.amplitude * v;
}

} // namespace reluva::gfx
