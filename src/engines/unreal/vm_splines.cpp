// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's splines (CTVE_Spline, and its camera directors and
// animated movers): where along its chain a spline is at a time, which
// its scripts ask of `GetSplineData` each tick to move the camera or an
// actor. As Engine.dll's ACTVE_Spline::GetData and ComputeRotation do it
// (read for what they do, written here anew): see docs/engines/unreal_vm.md.

#include "engines/unreal/actors.h"
#include "engines/unreal/vm_level.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;
using Rot = std::array<int32_t, 3>;

constexpr float kPi = 3.14159265358979f;
enum Focus { kFocSpline, kFocDirection, kFocActor, kFocWorld, kFocOrient, kFocActorFollow };

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

// A node's settings, as its properties (and its class's defaults) hold them.
class Node {
public:
    Node(Vm& vm, VmObject* o) : vm_(vm), obj(o) {}
    float f(const char* name) const {
        const Value* v = obj->find(vm_, name);
        return v ? v->asFloat() : 0;
    }
    int i(const char* name) const {
        const Value* v = obj->find(vm_, name);
        return v ? v->asInt() : 0;
    }
    bool b(const char* name) const {
        const Value* v = obj->find(vm_, name);
        return v && v->asBool();
    }
    Vec vec(const char* name) const {
        const Value* v = obj->find(vm_, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    }
    Rot rot(const char* name) const {
        const Value* v = obj->find(vm_, name);
        return v ? Vm::rotatorOf(*v) : Rot{};
    }
    VmObject* next() const {
        const Value* v = obj->find(vm_, "Next");
        return v ? v->asObject() : nullptr;
    }

    Vm& vm_;
    VmObject* obj;
};

// The cubic from p0 to p1 leaving and arriving along t0 and t1, or with
// Interpolation linear the straight line: `u` 0-1 of the way.
Vec hermite(bool linear, float u, const Vec& p0, const Vec& p1, const Vec& t0, const Vec& t1) {
    Vec p{};
    if (linear) {
        for (int k = 0; k < 3; ++k)
            p[k] = p0[k] + (p1[k] - p0[k]) * u;
        return p;
    }
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = 3 * u2 - 2 * u3, h11 = u3 - u2;
    for (int k = 0; k < 3; ++k)
        p[k] = h00 * p0[k] + h01 * p1[k] + h10 * t0[k] + h11 * t1[k];
    return p;
}

int32_t angleOf(float radians) { return int32_t(std::lround(radians * 32768.0f / kPi)); }

// The way a vector points (FVector::Rotation): yaw and pitch, no roll.
Rot facing(const Vec& d) {
    return {angleOf(std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]))), angleOf(std::atan2(d[1], d[0])), 0};
}

// v turned by r: a local offset into the world.
Vec turned(const Vec& v, const Rot& r) {
    const auto axes = rotationAxes({r[0], r[1], r[2]});
    Vec out{};
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 3; ++j)
            out[size_t(k)] += v[size_t(j)] * axes[size_t(j)][size_t(k)];
    return out;
}

// The node's focal actor: the one it found before if that's still tagged
// FocusActorTag, else the first actor so tagged (kept in FocalActor).
VmObject* focalActor(const Node& n) {
    Vm& vm = n.vm_;
    Value* kept = n.obj->find(vm, "FocalActor");
    VmObject* focal = kept ? kept->asObject() : nullptr;
    const int tag = n.i("FocusActorTag");
    auto tagOf = [&](VmObject* o) {
        const Value* t = o ? o->find(vm, "Tag") : nullptr;
        return t ? t->asInt() : -1;
    };
    if (focal && tagOf(focal) == tag)
        return focal;
    if (vm.level)
        for (VmObject* o : vm.level->actors())
            if (o && tagOf(o) == tag) {
                if (kept)
                    *kept = Value::objectValue(o);
                return o;
            }
    return focal;
}

// Which way a node faces `alpha` of the way along its segment, at `pos`
// (ComputeRotation), by its Focus; then rolled by RollDegrees, changing
// evenly to the next node's.
Rot nodeFacing(const Node& n, float alpha, const Vec& pos) {
    Vm& vm = n.vm_;
    VmObject* nextObj = n.next();
    int roll = 0;
    Rot r{};
    bool done = true;
    switch (n.i("Focus")) {
    case kFocDirection:
        r = n.rot("Rotation");
        roll = r[2];
        break;
    case kFocWorld: r = facing(sub(n.vec("FocusOffset"), pos)); break;
    case kFocOrient: r = facing(n.vec("FocusOffset")); break;
    case kFocActor:
    case kFocActorFollow:
        if (VmObject* focal = focalActor(n)) {
            const Node a(vm, focal);
            const Vec target = add(add(a.vec("Location"), turned(n.vec("FocusOffset"), a.rot("Rotation"))), a.vec("PrePivot"));
            r = facing(sub(target, pos));
        } else {
            done = false;
        }
        break;
    default: done = false; break;
    }
    if (!done) {
        // Along the curve: towards a point a hundredth further on (or from
        // one a hundredth back, past halfway).
        const Node next(vm, nextObj ? nextObj : n.obj);
        const Vec p0 = n.vec("Location"), p1 = next.vec("Location"), t0 = n.vec("Tangent"), t1 = next.vec("Tangent");
        const bool linear = n.i("Interpolation") == 1;
        r = alpha < 0.5f ? facing(sub(hermite(linear, alpha + 0.01f, p0, p1, t0, t1), pos))
                         : facing(sub(pos, hermite(linear, alpha - 0.01f, p0, p1, t0, t1)));
    }
    const float degrees = n.f("RollDegrees") + (nextObj ? (Node(vm, nextObj).f("RollDegrees") - n.f("RollDegrees")) : 0) * alpha;
    r[2] = int32_t(float(roll) + degrees * 182.044f);
    return r;
}

} // namespace

void registerSplineNatives(Vm& vm) {
    // GetSplineData(NewTime, out TimeAlpha, out TimeSpline, out TimePos, out TimeRot)
    vm.registerNative("CTVE_Spline.GetSplineData", [](NativeCall& c) {
        if (!c.self)
            return;
        Vm& vm = c.vm;
        const Node head(vm, c.self);

        // The segment: count off each node's Duration from the head,
        // following Next (round again on a closed chain), until the time
        // falls within one or the chain ends. Each node waits its Pause
        // first; the rest of its Duration is the travel.
        VmObject* seg = c.self;
        float alpha = 0;
        if (head.next()) {
            float t = c.args[0].asFloat();
            for (int guard = 0; guard < 100000; ++guard) {
                const Node n(vm, seg);
                if (t - n.f("Duration") <= 0 || !n.next())
                    break;
                t -= n.f("Duration");
                seg = n.next();
            }
            const Node n(vm, seg);
            const float travel = n.f("Duration") - n.f("Pause");
            t -= n.f("Pause");
            if (t > 0 && n.next()) {
                const float v0 = n.f("Speed"), v1 = Node(vm, n.next()).f("Speed");
                const float ave = n.f("AveSpeed"), length = n.f("Length");
                if (ave != 0 && ave != v0 && length != 0) {
                    // The speed changing evenly from this node's to the
                    // next's (AveSpeed, saved, is Length / the travel time),
                    // ExtraDuration stretching it out.
                    const float dist = v0 * t + (v1 - v0) * ave * t * t * 0.5f / length;
                    alpha = dist * (t / (t + dist / length * n.f("ExtraDuration"))) / length;
                } else {
                    alpha = travel != 0 ? t / travel : 1;
                }
                alpha = std::clamp(alpha, 0.0f, 1.0f);
            }
        }

        const Node n(vm, seg);
        VmObject* nextObj = n.next();
        Vec pos{};
        if (n.i("Focus") == kFocActorFollow) {
            // Following an actor: at it, offset by FocusOffset turned with
            // it, and FocusFollowOffset (turned too with bFocusFollowLocal).
            if (VmObject* focal = focalActor(n)) {
                const Node a(vm, focal);
                const Rot ar = a.rot("Rotation");
                const Vec follow = n.b("bFocusFollowLocal") ? turned(n.vec("FocusFollowOffset"), ar) : n.vec("FocusFollowOffset");
                pos = add(add(a.vec("Location"), turned(n.vec("FocusOffset"), ar)), follow);
            } else {
                pos = n.vec("Location");
            }
        } else {
            const Node next(vm, nextObj ? nextObj : seg);
            pos = hermite(n.i("Interpolation") == 1, alpha, n.vec("Location"), next.vec("Location"), n.vec("Tangent"),
                          next.vec("Tangent"));
        }

        Rot rot = nodeFacing(n, alpha, pos);
        // Turning smoothly into the next node's facing when it asks
        // (bSmoothRotate), unless both look along the curve or at the same
        // actor.
        if (nextObj) {
            const Node next(vm, nextObj);
            const int fa = n.i("Focus"), fb = next.i("Focus");
            const bool sameActor = fa == kFocActor && fb == kFocActor &&
                                   n.obj->find(vm, "FocalActor") && next.obj->find(vm, "FocalActor") &&
                                   n.obj->find(vm, "FocalActor")->asObject() == next.obj->find(vm, "FocalActor")->asObject();
            if (next.b("bSmoothRotate") && !sameActor && !(fa == kFocSpline && fb == kFocSpline)) {
                const Rot to = nodeFacing(next, 0, next.vec("Location"));
                const Rotator r = slerpRotators({rot[0], rot[1], rot[2]}, {to[0], to[1], to[2]}, alpha);
                rot = {r.pitch, r.yaw, r.roll};
            }
        }
        // The head's LocalRotation on top.
        const Rot local = head.rot("LocalRotation");
        for (size_t k = 0; k < 3; ++k)
            rot[k] += local[k];

        c.args[1] = Value::floatValue(alpha);
        c.args[2] = Value::objectValue(seg);
        c.args[3] = vm.vectorValue(pos[0], pos[1], pos[2]);
        c.args[4] = vm.rotatorValue(rot[0], rot[1], rot[2]);
    });
}

} // namespace reluva::unreal
