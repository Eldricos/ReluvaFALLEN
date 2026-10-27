// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The native functions Core's Object declares: operators for each type,
// maths, strings, and states. Each is written from its declaration (its
// name, parameters and flags, as `reluva-tool ue-script <game> Object`
// shows them).

#include "engines/unreal/vm.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace reluva::unreal {

namespace {

constexpr float kPi = 3.14159265358979f;

using Vec = std::array<float, 3>;
using Rot = std::array<int32_t, 3>;

std::mt19937& random() {
    static std::mt19937 r(12345); // repeatable runs
    return r;
}
float frand() { return std::uniform_real_distribution<float>(0, 1)(random()); }

Vec vec(const Value& v) { return Vm::vectorOf(v); }
Rot rot(const Value& v) { return Vm::rotatorOf(v); }
Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// A rotator's axes: forward, right, up (Unreal's left-handed space).
std::array<Vec, 3> axes(const Rot& r) {
    const float p = float(r[0]) * 2 * kPi / 65536, y = float(r[1]) * 2 * kPi / 65536,
                rl = float(r[2]) * 2 * kPi / 65536;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(rl),
                cr = std::cos(rl);
    return {Vec{cp * cy, cp * sy, sp}, Vec{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
            Vec{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
}

int32_t unwind(int32_t a) {
    a &= 0xffff;
    return a > 32767 ? a - 65536 : a;
}

std::string lower(const std::string& s) { return fs::toLower(s); }

} // namespace

void registerCoreNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(std::string("Object.") + name, std::move(fn)); };
    auto I = [](int32_t v) { return Value::intValue(v); };
    auto F = [](float v) { return Value::floatValue(v); };
    auto B = [](bool v) { return Value::boolValue(v); };

    // --- int
    reg("Subtract_PreInt", [=](NativeCall& c) { c.result = I(-c.i(0)); });
    reg("Complement_PreInt", [=](NativeCall& c) { c.result = I(~c.i(0)); });
    reg("Multiply_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) * c.i(1)); });
    reg("Divide_IntInt", [=](NativeCall& c) {
        if (c.i(1) == 0)
            c.vm.reportError("an integer divided by zero");
        c.result = I(c.i(1) ? c.i(0) / c.i(1) : 0);
    });
    reg("Add_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) + c.i(1)); });
    reg("Subtract_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) - c.i(1)); });
    reg("LessLess_IntInt", [=](NativeCall& c) { c.result = I(int32_t(uint32_t(c.i(0)) << (c.i(1) & 31))); });
    reg("GreaterGreater_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) >> (c.i(1) & 31)); });
    reg("GreaterGreaterGreater_IntInt", [=](NativeCall& c) { c.result = I(int32_t(uint32_t(c.i(0)) >> (c.i(1) & 31))); });
    reg("Less_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) < c.i(1)); });
    reg("Greater_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) > c.i(1)); });
    reg("LessEqual_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) <= c.i(1)); });
    reg("GreaterEqual_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) >= c.i(1)); });
    reg("EqualEqual_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) == c.i(1)); });
    reg("NotEqual_IntInt", [=](NativeCall& c) { c.result = B(c.i(0) != c.i(1)); });
    reg("And_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) & c.i(1)); });
    reg("Xor_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) ^ c.i(1)); });
    reg("Or_IntInt", [=](NativeCall& c) { c.result = I(c.i(0) | c.i(1)); });
    // Assignment operators: the out parameter changes, and is the result.
    auto setI = [=](NativeCall& c, int32_t v) { c.result = c.args[0] = I(v); };
    reg("MultiplyEqual_IntFloat", [=](NativeCall& c) { setI(c, int32_t(float(c.i(0)) * c.f(1))); });
    reg("DivideEqual_IntFloat", [=](NativeCall& c) { setI(c, c.f(1) != 0 ? int32_t(float(c.i(0)) / c.f(1)) : 0); });
    reg("AddEqual_IntInt", [=](NativeCall& c) { setI(c, c.i(0) + c.i(1)); });
    reg("SubtractEqual_IntInt", [=](NativeCall& c) { setI(c, c.i(0) - c.i(1)); });
    reg("AddAdd_PreInt", [=](NativeCall& c) { setI(c, c.i(0) + 1); });
    reg("SubtractSubtract_PreInt", [=](NativeCall& c) { setI(c, c.i(0) - 1); });
    reg("AddAdd_Int", [=](NativeCall& c) {
        const int32_t old = c.i(0);
        c.args[0] = I(old + 1);
        c.result = I(old);
    });
    reg("SubtractSubtract_Int", [=](NativeCall& c) {
        const int32_t old = c.i(0);
        c.args[0] = I(old - 1);
        c.result = I(old);
    });
    reg("Rand", [=](NativeCall& c) {
        c.result = I(c.i(0) > 0 ? int32_t(random()() % uint32_t(c.i(0))) : 0);
    });
    reg("Min", [=](NativeCall& c) { c.result = I(std::min(c.i(0), c.i(1))); });
    reg("Max", [=](NativeCall& c) { c.result = I(std::max(c.i(0), c.i(1))); });
    reg("Clamp", [=](NativeCall& c) { c.result = I(std::max(c.i(1), std::min(c.i(0), c.i(2)))); });

    // --- byte
    auto setByte = [](NativeCall& c, int32_t v) { c.result = c.args[0] = Value::byteValue(v); };
    reg("MultiplyEqual_ByteByte", [=](NativeCall& c) { setByte(c, c.i(0) * c.i(1)); });
    reg("DivideEqual_ByteByte", [=](NativeCall& c) { setByte(c, c.i(1) ? c.i(0) / c.i(1) : 0); });
    reg("AddEqual_ByteByte", [=](NativeCall& c) { setByte(c, c.i(0) + c.i(1)); });
    reg("SubtractEqual_ByteByte", [=](NativeCall& c) { setByte(c, c.i(0) - c.i(1)); });
    reg("AddAdd_PreByte", [=](NativeCall& c) { setByte(c, c.i(0) + 1); });
    reg("SubtractSubtract_PreByte", [=](NativeCall& c) { setByte(c, c.i(0) - 1); });
    reg("AddAdd_Byte", [=](NativeCall& c) {
        const int32_t old = c.i(0);
        c.args[0] = Value::byteValue(old + 1);
        c.result = Value::byteValue(old);
    });
    reg("SubtractSubtract_Byte", [=](NativeCall& c) {
        const int32_t old = c.i(0);
        c.args[0] = Value::byteValue(old - 1);
        c.result = Value::byteValue(old);
    });

    // --- bool
    reg("Not_PreBool", [=](NativeCall& c) { c.result = B(!c.b(0)); });
    reg("EqualEqual_BoolBool", [=](NativeCall& c) { c.result = B(c.b(0) == c.b(1)); });
    reg("NotEqual_BoolBool", [=](NativeCall& c) { c.result = B(c.b(0) != c.b(1)); });
    reg("AndAnd_BoolBool", [=](NativeCall& c) { c.result = B(c.b(0) && c.b(1)); });
    reg("XorXor_BoolBool", [=](NativeCall& c) { c.result = B(c.b(0) != c.b(1)); });
    reg("OrOr_BoolBool", [=](NativeCall& c) { c.result = B(c.b(0) || c.b(1)); });

    // --- float
    reg("Subtract_PreFloat", [=](NativeCall& c) { c.result = F(-c.f(0)); });
    reg("MultiplyMultiply_FloatFloat", [=](NativeCall& c) { c.result = F(std::pow(c.f(0), c.f(1))); });
    reg("Multiply_FloatFloat", [=](NativeCall& c) { c.result = F(c.f(0) * c.f(1)); });
    reg("Divide_FloatFloat", [=](NativeCall& c) {
        if (c.f(1) == 0)
            c.vm.reportError("a float divided by zero");
        c.result = F(c.f(1) != 0 ? c.f(0) / c.f(1) : 0);
    });
    reg("Percent_FloatFloat", [=](NativeCall& c) { c.result = F(c.f(1) != 0 ? std::fmod(c.f(0), c.f(1)) : 0); });
    reg("Add_FloatFloat", [=](NativeCall& c) { c.result = F(c.f(0) + c.f(1)); });
    reg("Subtract_FloatFloat", [=](NativeCall& c) { c.result = F(c.f(0) - c.f(1)); });
    reg("Less_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) < c.f(1)); });
    reg("Greater_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) > c.f(1)); });
    reg("LessEqual_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) <= c.f(1)); });
    reg("GreaterEqual_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) >= c.f(1)); });
    reg("EqualEqual_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) == c.f(1)); });
    reg("NotEqual_FloatFloat", [=](NativeCall& c) { c.result = B(c.f(0) != c.f(1)); });
    reg("ComplementEqual_FloatFloat", [=](NativeCall& c) { c.result = B(std::fabs(c.f(0) - c.f(1)) < 1e-4f); });
    auto setF = [=](NativeCall& c, float v) { c.result = c.args[0] = F(v); };
    reg("MultiplyEqual_FloatFloat", [=](NativeCall& c) { setF(c, c.f(0) * c.f(1)); });
    reg("DivideEqual_FloatFloat", [=](NativeCall& c) { setF(c, c.f(1) != 0 ? c.f(0) / c.f(1) : 0); });
    reg("AddEqual_FloatFloat", [=](NativeCall& c) { setF(c, c.f(0) + c.f(1)); });
    reg("SubtractEqual_FloatFloat", [=](NativeCall& c) { setF(c, c.f(0) - c.f(1)); });
    reg("Abs", [=](NativeCall& c) { c.result = F(std::fabs(c.f(0))); });
    reg("Sin", [=](NativeCall& c) { c.result = F(std::sin(c.f(0))); });
    reg("Cos", [=](NativeCall& c) { c.result = F(std::cos(c.f(0))); });
    reg("Tan", [=](NativeCall& c) { c.result = F(std::tan(c.f(0))); });
    reg("Atan", [=](NativeCall& c) { c.result = F(std::atan(c.f(0))); });
    reg("Exp", [=](NativeCall& c) { c.result = F(std::exp(c.f(0))); });
    reg("Loge", [=](NativeCall& c) { c.result = F(c.f(0) > 0 ? std::log(c.f(0)) : 0); });
    reg("Sqrt", [=](NativeCall& c) { c.result = F(c.f(0) > 0 ? std::sqrt(c.f(0)) : 0); });
    reg("Square", [=](NativeCall& c) { c.result = F(c.f(0) * c.f(0)); });
    reg("FRand", [=](NativeCall& c) { c.result = F(frand()); });
    reg("RandRange", [=](NativeCall& c) { c.result = F(c.f(0) + (c.f(1) - c.f(0)) * frand()); });
    reg("FMin", [=](NativeCall& c) { c.result = F(std::min(c.f(0), c.f(1))); });
    reg("FMax", [=](NativeCall& c) { c.result = F(std::max(c.f(0), c.f(1))); });
    reg("FClamp", [=](NativeCall& c) { c.result = F(std::max(c.f(1), std::min(c.f(0), c.f(2)))); });
    reg("Lerp", [=](NativeCall& c) { c.result = F(c.f(1) + (c.f(2) - c.f(1)) * c.f(0)); });
    reg("Smerp", [=](NativeCall& c) {
        const float a = c.f(0);
        c.result = F(c.f(1) + (c.f(2) - c.f(1)) * (3 * a * a - 2 * a * a * a));
    });

    // --- vector
    auto V = [&vm](const Vec& v) { return vm.vectorValue(v[0], v[1], v[2]); };
    auto R = [&vm](int32_t p, int32_t y, int32_t r) { return vm.rotatorValue(p, y, r); };
    reg("Subtract_PreVector", [=](NativeCall& c) { c.result = V(mul(vec(c.args[0]), -1)); });
    reg("Multiply_VectorFloat", [=](NativeCall& c) { c.result = V(mul(vec(c.args[0]), c.f(1))); });
    reg("Multiply_FloatVector", [=](NativeCall& c) { c.result = V(mul(vec(c.args[1]), c.f(0))); });
    reg("Multiply_VectorVector", [=](NativeCall& c) {
        const Vec a = vec(c.args[0]), b = vec(c.args[1]);
        c.result = V({a[0] * b[0], a[1] * b[1], a[2] * b[2]});
    });
    reg("Divide_VectorFloat", [=](NativeCall& c) { c.result = V(mul(vec(c.args[0]), c.f(1) != 0 ? 1 / c.f(1) : 0)); });
    reg("Add_VectorVector", [=](NativeCall& c) { c.result = V(add(vec(c.args[0]), vec(c.args[1]))); });
    reg("Subtract_VectorVector", [=](NativeCall& c) { c.result = V(sub(vec(c.args[0]), vec(c.args[1]))); });
    reg("GreaterGreater_VectorRotator", [=](NativeCall& c) { // local to world
        const Vec v = vec(c.args[0]);
        const auto ax = axes(rot(c.args[1]));
        c.result = V(add(add(mul(ax[0], v[0]), mul(ax[1], v[1])), mul(ax[2], v[2])));
    });
    reg("LessLess_VectorRotator", [=](NativeCall& c) { // world to local
        const Vec v = vec(c.args[0]);
        const auto ax = axes(rot(c.args[1]));
        c.result = V({dot(v, ax[0]), dot(v, ax[1]), dot(v, ax[2])});
    });
    reg("EqualEqual_VectorVector", [=](NativeCall& c) { c.result = B(vec(c.args[0]) == vec(c.args[1])); });
    reg("NotEqual_VectorVector", [=](NativeCall& c) { c.result = B(vec(c.args[0]) != vec(c.args[1])); });
    reg("Dot_VectorVector", [=](NativeCall& c) { c.result = F(dot(vec(c.args[0]), vec(c.args[1]))); });
    reg("Cross_VectorVector", [=](NativeCall& c) { c.result = V(cross(vec(c.args[0]), vec(c.args[1]))); });
    auto setV = [=](NativeCall& c, const Vec& v) { c.result = c.args[0] = V(v); };
    reg("MultiplyEqual_VectorFloat", [=](NativeCall& c) { setV(c, mul(vec(c.args[0]), c.f(1))); });
    reg("MultiplyEqual_VectorVector", [=](NativeCall& c) {
        const Vec a = vec(c.args[0]), b = vec(c.args[1]);
        setV(c, {a[0] * b[0], a[1] * b[1], a[2] * b[2]});
    });
    reg("DivideEqual_VectorFloat", [=](NativeCall& c) { setV(c, mul(vec(c.args[0]), c.f(1) != 0 ? 1 / c.f(1) : 0)); });
    reg("AddEqual_VectorVector", [=](NativeCall& c) { setV(c, add(vec(c.args[0]), vec(c.args[1]))); });
    reg("SubtractEqual_VectorVector", [=](NativeCall& c) { setV(c, sub(vec(c.args[0]), vec(c.args[1]))); });
    reg("VSize", [=](NativeCall& c) { c.result = F(std::sqrt(dot(vec(c.args[0]), vec(c.args[0])))); });
    reg("VSize2D", [=](NativeCall& c) {
        const Vec v = vec(c.args[0]);
        c.result = F(std::sqrt(v[0] * v[0] + v[1] * v[1]));
    });
    reg("VSizeSquared", [=](NativeCall& c) { c.result = F(dot(vec(c.args[0]), vec(c.args[0]))); });
    reg("Normal", [=](NativeCall& c) {
        const Vec v = vec(c.args[0]);
        const float l = std::sqrt(dot(v, v));
        c.result = V(l > 0 ? mul(v, 1 / l) : Vec{0, 0, 0});
    });
    reg("Invert", [=](NativeCall& c) { // transposes the three axes
        const Vec x = vec(c.args[0]), y = vec(c.args[1]), z = vec(c.args[2]);
        c.args[0] = V({x[0], y[0], z[0]});
        c.args[1] = V({x[1], y[1], z[1]});
        c.args[2] = V({x[2], y[2], z[2]});
    });
    reg("VRand", [=](NativeCall& c) {
        Vec v;
        do
            v = {frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
        while (dot(v, v) > 1 || dot(v, v) < 1e-4f);
        c.result = V(mul(v, 1 / std::sqrt(dot(v, v))));
    });
    reg("MirrorVectorByNormal", [=](NativeCall& c) {
        const Vec v = vec(c.args[0]), n = vec(c.args[1]);
        c.result = V(sub(v, mul(n, 2 * dot(v, n))));
    });

    // --- rotator
    reg("EqualEqual_RotatorRotator", [=](NativeCall& c) { c.result = B(rot(c.args[0]) == rot(c.args[1])); });
    reg("NotEqual_RotatorRotator", [=](NativeCall& c) { c.result = B(rot(c.args[0]) != rot(c.args[1])); });
    auto scaled = [](const Rot& r, float s) { return Rot{int32_t(float(r[0]) * s), int32_t(float(r[1]) * s), int32_t(float(r[2]) * s)}; };
    reg("Multiply_RotatorFloat", [=](NativeCall& c) { const Rot r = scaled(rot(c.args[0]), c.f(1)); c.result = R(r[0], r[1], r[2]); });
    reg("Multiply_FloatRotator", [=](NativeCall& c) { const Rot r = scaled(rot(c.args[1]), c.f(0)); c.result = R(r[0], r[1], r[2]); });
    reg("Multiply_RotatorRotator", [=](NativeCall& c) {
        const Rot a = rot(c.args[0]), b = rot(c.args[1]);
        c.result = R(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
    });
    reg("Divide_RotatorFloat", [=](NativeCall& c) {
        const Rot r = scaled(rot(c.args[0]), c.f(1) != 0 ? 1 / c.f(1) : 0);
        c.result = R(r[0], r[1], r[2]);
    });
    auto setR = [=](NativeCall& c, const Rot& r) { c.result = c.args[0] = R(r[0], r[1], r[2]); };
    reg("MultiplyEqual_RotatorFloat", [=](NativeCall& c) { setR(c, scaled(rot(c.args[0]), c.f(1))); });
    reg("DivideEqual_RotatorFloat", [=](NativeCall& c) { setR(c, scaled(rot(c.args[0]), c.f(1) != 0 ? 1 / c.f(1) : 0)); });
    reg("Add_RotatorRotator", [=](NativeCall& c) {
        const Rot a = rot(c.args[0]), b = rot(c.args[1]);
        c.result = R(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
    });
    reg("Subtract_RotatorRotator", [=](NativeCall& c) {
        const Rot a = rot(c.args[0]), b = rot(c.args[1]);
        c.result = R(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    });
    reg("AddEqual_RotatorRotator", [=](NativeCall& c) {
        const Rot a = rot(c.args[0]), b = rot(c.args[1]);
        setR(c, {a[0] + b[0], a[1] + b[1], a[2] + b[2]});
    });
    reg("SubtractEqual_RotatorRotator", [=](NativeCall& c) {
        const Rot a = rot(c.args[0]), b = rot(c.args[1]);
        setR(c, {a[0] - b[0], a[1] - b[1], a[2] - b[2]});
    });
    reg("GetAxes", [=](NativeCall& c) {
        const auto ax = axes(rot(c.args[0]));
        for (size_t k = 0; k < 3; ++k)
            c.args[k + 1] = V(ax[k]);
    });
    reg("GetUnAxes", [=](NativeCall& c) { // the axes of the inverse rotation
        const auto ax = axes(rot(c.args[0]));
        for (size_t k = 0; k < 3; ++k)
            c.args[k + 1] = V({ax[0][k], ax[1][k], ax[2][k]});
    });
    reg("RotRand", [=](NativeCall& c) {
        c.result = R(int32_t(random()() & 0xffff), int32_t(random()() & 0xffff),
                     c.b(0) ? int32_t(random()() & 0xffff) : 0);
    });
    reg("OrthoRotation", [=](NativeCall& c) {
        const Vec x = vec(c.args[0]), y = vec(c.args[1]), z = vec(c.args[2]);
        const float k = 32768 / kPi;
        c.result = R(int32_t(std::atan2(x[2], std::sqrt(x[0] * x[0] + x[1] * x[1])) * k),
                     int32_t(std::atan2(x[1], x[0]) * k), int32_t(std::atan2(-y[2], z[2]) * k));
    });
    reg("Normalize", [=](NativeCall& c) {
        const Rot r = rot(c.args[0]);
        c.result = R(unwind(r[0]), unwind(r[1]), unwind(r[2]));
    });

    // --- strings
    auto S = [](std::string v) { return Value::stringValue(std::move(v)); };
    reg("Concat_StrStr", [=](NativeCall& c) { c.result = S(c.s(0) + c.s(1)); });
    reg("At_StrStr", [=](NativeCall& c) { c.result = S(c.s(0) + " " + c.s(1)); });
    reg("Less_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) < c.s(1)); });
    reg("Greater_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) > c.s(1)); });
    reg("LessEqual_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) <= c.s(1)); });
    reg("GreaterEqual_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) >= c.s(1)); });
    reg("EqualEqual_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) == c.s(1)); });
    reg("NotEqual_StrStr", [=](NativeCall& c) { c.result = B(c.s(0) != c.s(1)); });
    reg("ComplementEqual_StrStr", [=](NativeCall& c) { c.result = B(lower(c.s(0)) == lower(c.s(1))); });
    reg("Len", [=](NativeCall& c) { c.result = I(int32_t(c.s(0).size())); });
    reg("InStr", [=](NativeCall& c) {
        const size_t at = c.s(0).find(c.s(1));
        c.result = I(at == std::string::npos ? -1 : int32_t(at));
    });
    reg("Mid", [=](NativeCall& c) {
        const std::string& s = c.s(0);
        int32_t from = std::max(0, c.i(1));
        const int32_t count = c.given.size() > 2 && c.given[2] ? c.i(2) : int32_t(s.size());
        if (size_t(from) >= s.size() || count <= 0) {
            c.result = S("");
            return;
        }
        c.result = S(s.substr(size_t(from), size_t(count)));
    });
    reg("Left", [=](NativeCall& c) { c.result = S(c.s(0).substr(0, size_t(std::max(0, c.i(1))))); });
    reg("Right", [=](NativeCall& c) {
        const std::string& s = c.s(0);
        const size_t n = std::min(s.size(), size_t(std::max(0, c.i(1))));
        c.result = S(s.substr(s.size() - n));
    });
    reg("Caps", [=](NativeCall& c) {
        std::string s = c.s(0);
        for (char& ch : s)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        c.result = S(s);
    });
    reg("Chr", [=](NativeCall& c) { c.result = S(std::string(1, char(c.i(0)))); });
    reg("Asc", [=](NativeCall& c) { c.result = I(c.s(0).empty() ? 0 : static_cast<unsigned char>(c.s(0)[0])); });

    // --- objects and names
    reg("EqualEqual_ObjectObject", [=](NativeCall& c) { c.result = B(c.o(0) == c.o(1)); });
    reg("NotEqual_ObjectObject", [=](NativeCall& c) { c.result = B(c.o(0) != c.o(1)); });
    reg("EqualEqual_NameName", [=](NativeCall& c) { c.result = B(c.i(0) == c.i(1)); });
    reg("NotEqual_NameName", [=](NativeCall& c) { c.result = B(c.i(0) != c.i(1)); });
    reg("Log", [=](NativeCall& c) {
        if (c.vm.logger)
            c.vm.logger((c.given.size() > 1 && c.given[1] ? c.vm.nameText(c.i(1)) : std::string("ScriptLog")) + ": " +
                        c.s(0));
    });
    reg("Warn", [=](NativeCall& c) {
        if (c.vm.logger)
            c.vm.logger("ScriptWarning: " + c.s(0));
    });
    reg("Localize", [=](NativeCall& c) { c.result = S(""); });
    reg("GotoState", [=](NativeCall& c) {
        const NameId state = c.given.size() > 0 && c.given[0] ? c.i(0) : 0;
        c.vm.gotoState(c.self, state, c.given.size() > 1 && c.given[1] ? c.i(1) : 0);
    });
    reg("IsInState", [=](NativeCall& c) {
        bool in = false;
        for (VmStruct* s = c.self ? c.self->state : nullptr; s && s->kind == VmStruct::Kind::State; s = s->super)
            in = in || s->nameId == c.i(0);
        c.result = B(in);
    });
    reg("GetStateName", [=](NativeCall& c) {
        c.result = Value::nameValue(c.self && c.self->state ? c.self->state->nameId : 0);
    });
    reg("Enable", [=](NativeCall& c) {
        if (c.self)
            c.self->disabled.erase(c.i(0));
    });
    reg("Disable", [=](NativeCall& c) {
        if (c.self)
            c.self->disabled.insert(c.i(0));
    });
    reg("IsA", [=](NativeCall& c) {
        bool is = false;
        const VmClass* start = c.self ? (c.self->asClass ? nullptr : c.self->cls) : nullptr;
        for (const VmClass* k = start; k; k = k->parent())
            is = is || k->nameId == c.i(0);
        c.result = B(is);
    });
    reg("ClassIsChildOf", [=](NativeCall& c) {
        VmObject* a = c.o(0);
        VmObject* b = c.o(1);
        c.result = B(a && b && a->asClass && b->asClass && a->asClass->isA(b->asClass));
    });
    reg("GetEnum", [=](NativeCall& c) {
        VmObject* e = c.o(0);
        c.result = Value::nameValue(0);
        ScriptObject so;
        if (e && e->package && !readScriptObject(*e->package, e->exportIndex, so) && c.i(1) >= 0 &&
            size_t(c.i(1)) < so.enumNames.size())
            c.result = Value::nameValue(c.vm.name(e->package->name(so.enumNames[size_t(c.i(1))])));
    });
    reg("DynamicLoadObject", [=](NativeCall& c) {
        VmObject* o = c.vm.findObject(c.s(0));
        if (!o && !(c.given.size() > 2 && c.b(2)))
            c.vm.reportError("DynamicLoadObject couldn't find " + c.s(0));
        c.result = Value::objectValue(o);
    });
    reg("GetPropertyText", [=](NativeCall& c) {
        Value* v = c.self ? c.self->find(c.vm, c.s(0)) : nullptr;
        c.result = S(v ? c.vm.toString(*v) : "");
    });
    reg("SetPropertyText", [=](NativeCall& c) {
        if (Value* v = c.self ? c.self->find(c.vm, c.s(0)) : nullptr) {
            if (v->type == VType::Object)
                v->o = c.vm.findObject(c.s(1));
            else
                c.vm.assign(*v, Value::stringValue(c.s(1)));
        }
    });
    for (const char* none : {"SaveConfig", "StaticSaveConfig", "ResetConfig", "SetFlags", "ClearFlags"})
        reg(none, [](NativeCall&) {});
}

} // namespace reluva::unreal
