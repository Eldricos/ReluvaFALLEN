// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's splines (CTVE_Spline, and its camera directors and
// animated movers): where along its chain a spline is at a time, which
// its scripts ask of `GetSplineData` each tick to move the camera or an
// actor. Reluva's reading of what UnrealEd saves with each node (its
// start time, the time and length of the segment after it, its tangent,
// the chain's average speeds), not The Fallen's code: see
// docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;
constexpr int kSamples = 16; // per segment, measuring its length
enum Focus { kFocSpline, kFocDirection, kFocActor, kFocWorld, kFocOrient, kFocActorFollow };

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec scale(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float length(const Vec& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

struct Node {
    VmObject* obj = nullptr;
    Vec location{}, tangent{};
    std::array<int32_t, 3> rotation{};
    float timeLine = 0, duration = 0, pause = 0, speed = 0;
    bool linear = false;
    int focus = kFocDirection;
};

class Spline {
public:
    explicit Spline(Vm& vm) : vm_(vm) {}

    float f(VmObject* o, const char* name) {
        const Value* v = o->find(vm_, name);
        return v ? v->asFloat() : 0;
    }
    Node node(VmObject* o) {
        Node n;
        n.obj = o;
        if (const Value* v = o->find(vm_, "Location"))
            n.location = Vm::vectorOf(*v);
        if (const Value* v = o->find(vm_, "Tangent"))
            n.tangent = Vm::vectorOf(*v);
        if (const Value* v = o->find(vm_, "Rotation"))
            n.rotation = Vm::rotatorOf(*v);
        n.timeLine = f(o, "TimeLine");
        n.duration = f(o, "Duration");
        n.pause = f(o, "Pause");
        n.speed = f(o, "Speed");
        if (const Value* v = o->find(vm_, "Interpolation"))
            n.linear = v->asInt() == 1;
        if (const Value* v = o->find(vm_, "Focus"))
            n.focus = v->asInt();
        return n;
    }
    VmObject* next(VmObject* o) {
        const Value* v = o->find(vm_, "Next");
        return v ? v->asObject() : nullptr;
    }

private:
    Vm& vm_;
};

// A point on the segment from a to b, `u` 0-1: a cubic through the two
// nodes leaving and arriving along their tangents, or a straight line.
Vec curve(const Node& a, const Node& b, float u) {
    if (a.linear)
        return add(a.location, scale(sub(b.location, a.location), u));
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    Vec p{};
    for (int k = 0; k < 3; ++k)
        p[k] = h00 * a.location[k] + h10 * a.tangent[k] + h01 * b.location[k] + h11 * b.tangent[k];
    return p;
}

// The curve's parameter a fraction `s` of the way along it by length.
float byLength(const Node& a, const Node& b, float s) {
    if (a.linear || s <= 0 || s >= 1)
        return std::clamp(s, 0.0f, 1.0f);
    std::array<float, kSamples + 1> run{};
    Vec prev = a.location;
    for (int k = 1; k <= kSamples; ++k) {
        const Vec p = curve(a, b, float(k) / kSamples);
        run[size_t(k)] = run[size_t(k - 1)] + length(sub(p, prev));
        prev = p;
    }
    const float want = s * run[kSamples];
    for (int k = 1; k <= kSamples; ++k)
        if (run[size_t(k)] >= want) {
            const float span = run[size_t(k)] - run[size_t(k - 1)];
            const float t = span > 0 ? (want - run[size_t(k - 1)]) / span : 0;
            return (float(k - 1) + t) / kSamples;
        }
    return 1;
}

int32_t angleOf(float radians) { return int32_t(std::lround(radians * 32768.0f / kPi)); }

std::array<int32_t, 3> facing(const Vec& d) {
    return {angleOf(std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]))), angleOf(std::atan2(d[1], d[0])), 0};
}

// From one angle toward another the shorter way round.
int32_t turn(int32_t from, int32_t to, float u) {
    const int32_t diff = int32_t(int16_t(uint16_t((to - from) & 0xffff)));
    return from + int32_t(std::lround(float(diff) * u));
}

} // namespace

void registerSplineNatives(Vm& vm) {
    // GetSplineData(NewTime, out TimeAlpha, out TimeSpline, out TimePos, out TimeRot)
    vm.registerNative("CTVE_Spline.GetSplineData", [](NativeCall& c) {
        if (!c.self)
            return;
        Vm& vm = c.vm;
        Spline sp(vm);
        // The chain from this node (its head), until it ends or comes back round.
        std::vector<Node> chain{sp.node(c.self)};
        bool loops = false;
        for (VmObject* o = sp.next(c.self); o && chain.size() < 1000; o = sp.next(o)) {
            if (o == c.self) {
                loops = true;
                break;
            }
            const Value* head = o->find(vm, "bHeadOfChain");
            if (head && head->asBool())
                break;
            chain.push_back(sp.node(o));
        }
        const float maxTime = sp.f(c.self, "MaxTime");
        const float t = std::max(0.0f, c.args[0].asFloat());
        size_t i = 0;
        while (i + 1 < chain.size() && chain[i + 1].timeLine <= t)
            ++i;
        const bool last = i + 1 >= chain.size();
        const Node& a = chain[i];
        const Node* b = last ? (loops && chain.size() > 1 ? &chain[0] : nullptr) : &chain[i + 1];

        // How far along the segment: each node first waits its pause,
        // then travels, its speed changing evenly from the node's to the
        // next's (UnrealEd's average speed, saved, is their mean). The
        // saved duration counts the pause too: CM10_SWK_L3's Sisko waits
        // 8 seconds at a node whose duration is 10.1, its 178 units at
        // speed 85 the other 2.1.
        float s = 0;
        if (b) {
            const float end = last ? maxTime : b->timeLine;
            const float span = std::max(0.0f, end - a.timeLine);
            const float travel = a.duration > 0 ? std::clamp(a.duration - a.pause, 0.0f, span)
                                                : std::max(0.0f, span - a.pause);
            const float wait = span - travel;
            const float tau = travel > 0 ? std::clamp((t - a.timeLine - wait) / travel, 0.0f, 1.0f) : 1.0f;
            const float v0 = a.speed, v1 = b->speed;
            s = v0 + v1 > 0 ? (v0 * tau + (v1 - v0) * tau * tau / 2) / ((v0 + v1) / 2) : tau;
            s = std::clamp(s, 0.0f, 1.0f);
        }
        const float u = b ? byLength(a, *b, s) : 0;
        const Vec pos = b ? curve(a, *b, u) : a.location;

        // Facing, by the segment's focus.
        std::array<int32_t, 3> rot = a.rotation;
        auto alongCurve = [&] {
            if (!b)
                return a.rotation;
            const float e = 0.01f;
            const Vec d = sub(curve(a, *b, std::min(1.0f, u + e)), curve(a, *b, std::max(0.0f, u - e)));
            return length(d) > 1e-4f ? facing(d) : a.rotation;
        };
        auto between = [&] {
            if (!b)
                return a.rotation;
            return std::array<int32_t, 3>{turn(a.rotation[0], b->rotation[0], s), turn(a.rotation[1], b->rotation[1], s),
                                          turn(a.rotation[2], b->rotation[2], s)};
        };
        VmObject* focal = nullptr;
        if (const Value* v = a.obj->find(vm, "FocalActor"))
            focal = v->asObject();
        switch (a.focus) {
        case kFocSpline:
        case kFocOrient:
            rot = alongCurve();
            break;
        case kFocActor:
        case kFocActorFollow: {
            // The actor tagged FocusActorTag, found once.
            const Value* tag = a.obj->find(vm, "FocusActorTag");
            if (!focal && tag && tag->asInt() != vm.name("None") && vm.level)
                for (VmObject* o : vm.level->actors()) {
                    const Value* ot = o ? o->find(vm, "Tag") : nullptr;
                    if (ot && ot->i == tag->i) {
                        focal = o;
                        if (Value* fv = a.obj->find(vm, "FocalActor"))
                            *fv = Value::objectValue(o);
                        break;
                    }
                }
            if (focal) {
                Vec target = Vm::vectorOf(*focal->find(vm, "Location"));
                if (const Value* pp = focal->find(vm, "PrePivot"))
                    target = add(target, Vm::vectorOf(*pp));
                if (const Value* off = a.obj->find(vm, "FocusOffset")) {
                    // The offset turns with the actor (as the scripts' own `>>` does).
                    const Vec o = Vm::vectorOf(*off);
                    const float yaw = float(Vm::rotatorOf(*focal->find(vm, "Rotation"))[1]) * kPi / 32768.0f;
                    target = add(target, {o[0] * std::cos(yaw) - o[1] * std::sin(yaw), o[0] * std::sin(yaw) + o[1] * std::cos(yaw), o[2]});
                }
                const Vec d = sub(target, pos);
                rot = length(d) > 1e-4f ? facing(d) : between();
            } else {
                rot = between();
            }
            break;
        }
        case kFocWorld:
            if (const Value* off = a.obj->find(vm, "FocusOffset")) {
                const Vec d = sub(Vm::vectorOf(*off), pos);
                rot = length(d) > 1e-4f ? facing(d) : between();
            }
            break;
        default: // kFocDirection: the way each node points, turning from one to the next
            rot = between();
            break;
        }
        if (const Value* local = a.obj->find(vm, "LocalRotation")) {
            const auto l = Vm::rotatorOf(*local);
            for (int k = 0; k < 3; ++k)
                rot[size_t(k)] += l[size_t(k)];
        }
        const float roll = sp.f(a.obj, "RollDegrees") + (b ? (sp.f(b->obj, "RollDegrees") - sp.f(a.obj, "RollDegrees")) * s : 0);
        rot[2] += int32_t(std::lround(roll * 65536.0f / 360.0f));

        c.args[1] = Value::floatValue(s);
        c.args[2] = Value::objectValue(a.obj);
        c.args[3] = vm.vectorValue(pos[0], pos[1], pos[2]);
        c.args[4] = vm.rotatorValue(rot[0], rot[1], rot[2]);
    });
}

} // namespace reluva::unreal
