// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's own physics modes (past PHYS_MovingBrush), for a running
// level (ScriptLevel): how each kind of effect moves its copies (its
// CTVE_MultiMesh list) and, for a few, itself; and the flight and
// interpolation modes some actors use. Each is Reluva's reading of what
// the effect's scripts set and leave undone; the original code isn't
// known. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"
#include "engines/unreal/actors.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;

// The Fallen's physics modes (Actor.EPhysics).
enum : int {
    kPhysInterpolating = 8,
    kPhysCtveInterpolating = 9,
    kPhysTrailer = 12,
    kPhysParticle = 16,
    kPhysNewtonian = 17,
    kPhysWeather = 18,
    kPhysBubble = 19,
    kPhysSpark = 21,
    kPhysFanParticle = 22,
    kPhysRotatingYaw = 23,
    kPhysRotatingPitch = 24,
    kPhysSteamJet = 25,
    kPhysHotSpark = 26,
    kPhysExhaustJet = 27,
    kPhysPlasmaThrower = 28,
    kPhysEmPulseRifle = 29,
    kPhysEmPulseRifleQuad = 30,
    kPhysPipePath = 20,
    kPhysDisintegration = 31,
    kPhysSwarmObject = 33,
    kPhysWaterRing = 34,
    kPhysFish = 35,
    kPhysSmoke = 36,
    kPhysLava = 37,
    kPhysGasCloudM4 = 38,
    kPhysBeamSustain = 39,
    kPhysBeamFadeTailCatchup = 40,
    kPhysBeamFadeTailStationary = 41,
    kPhysPlasmaJet = 42,
    kPhysExpTrail = 43,
    kPhysExpCore = 44,
    kPhysExpRing = 45,
    kPhysExpSmoke = 46,
    kPhysExpCinder = 47,
    kPhysExpParticle = 48,
    kPhysExpColumn = 49,
    kPhysFireGas = 50,
    kPhysSpiralJet = 51,
    kPhysDisintegrationPhaser = 52,
    kPhysCtveSparks = 53,
    kPhysFishInTank = 54,
};

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec& a) { return std::sqrt(dot(a, a)); }
Vec normal(const Vec& a) {
    const float l = length(a);
    return l > 1e-6f ? mul(a, 1 / l) : Vec{0, 0, 0};
}

std::mt19937& random() {
    static std::mt19937 r(24680); // repeatable runs
    return r;
}
float frand() { return std::uniform_real_distribution<float>(0, 1)(random()); }
Vec vrand() {
    for (;;) {
        const Vec v{frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1};
        const float l = length(v);
        if (l > 1e-3f && l <= 1)
            return mul(v, 1 / l);
    }
}

int32_t units(float radians) { return int32_t(std::lround(radians * 32768 / kPi)); }

} // namespace

// Each level starts the same, whatever ran before it (ue-run -all).
void reseedEffectPhysicsRandom() { random().seed(24680); }

// --- An effect's copies ---------------------------------------------------------------------

// A copy's field, read and written.
float ScriptLevel::copyFloat(VmObject* mm, const char* name, float otherwise) {
    const Value* v = prop(mm, name);
    return v ? v->asFloat() : otherwise;
}
Vec ScriptLevel::copyVec(VmObject* mm, const char* name) {
    const Value* v = prop(mm, name);
    return v ? Vm::vectorOf(*v) : Vec{};
}
void ScriptLevel::setCopyFloat(VmObject* mm, const char* name, float v) {
    if (Value* p = prop(mm, name))
        *p = Value::floatValue(v);
}
void ScriptLevel::setCopyVec(VmObject* mm, const char* name, const Vec& v) {
    if (Value* p = prop(mm, name))
        vm_.setVector(*p, v[0], v[1], v[2]);
}

// How much of a copy's life is gone (0-1): its LifeSpan against what it
// had when first seen (a copy let go and made again starts over). 0 for
// one that lasts.
float ScriptLevel::copyAge(VmObject* mm) {
    const float life = copyFloat(mm, "LifeSpan");
    if (life <= 0)
        return 0;
    float& start = copyStarts_[mm];
    if (life > start)
        start = life;
    return std::clamp(1 - life / start, 0.0f, 1.0f);
}

// A copy's step: its velocity changed by its acceleration (`accelerate`)
// and anything `extra`, then moved by it (`move`); its life counted down
// and let go (Delete) when it runs out. False if it's gone.
bool ScriptLevel::stepCopy(VmObject* mm, float dt, bool accelerate, bool move, const Vec& extra) {
    Value* loc = prop(mm, "Location");
    Value* vel = prop(mm, "Velocity");
    if (loc && vel) {
        Vec v = Vm::vectorOf(*vel);
        Vec a = extra;
        if (accelerate)
            a = add(a, copyVec(mm, "Acceleration"));
        if (a[0] != 0 || a[1] != 0 || a[2] != 0) {
            v = add(v, mul(a, dt));
            vm_.setVector(*vel, v[0], v[1], v[2]);
        }
        if (move && (v[0] != 0 || v[1] != 0 || v[2] != 0)) {
            const Vec l = add(Vm::vectorOf(*loc), mul(v, dt));
            vm_.setVector(*loc, l[0], l[1], l[2]);
        }
    }
    return ageCopy(mm, dt);
}

// Its life counted down; let go when it runs out. False if it's gone.
bool ScriptLevel::ageCopy(VmObject* mm, float dt) {
    Value* life = prop(mm, "LifeSpan");
    if (!life || life->asFloat() <= 0)
        return true; // lasts
    copyAge(mm);
    life->f -= dt;
    if (life->f > 0)
        return true;
    life->f = 0;
    copyStarts_.erase(mm);
    vm_.callEvent(mm, "Delete");
    return false;
}

// Each tick an actor's copies move and age, as its physics mode has them.
// Most fly by their velocity and acceleration and go when their lives
// run out; the modes differ in what else they do (grow, fade, rise to a
// surface, wander in a tank) and some leave the moving to the scripts.
void ScriptLevel::moveMultiMeshes(VmObject* actor, float dt) {
    if (dt <= 0)
        return;
    const Value* head = prop(actor, "MultiMesh");
    VmObject* base = head ? head->asObject() : nullptr;
    if (!base)
        return;
    const Value* physicsValue = prop(actor, "Physics");
    const int physics = physicsValue ? physicsValue->asInt() : 0;
    const std::vector<VmObject*> copies = multiMeshes(actor);
    if (copies.empty())
        return;
    auto f = [&](VmObject* o, const char* name, float otherwise = 0) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? v->asFloat() : otherwise;
    };
    auto b = [&](VmObject* o, const char* name) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v && v->asBool();
    };
    auto vec = [&](VmObject* o, const char* name) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    const Value* ownerValue = prop(actor, "Owner");
    VmObject* owner = ownerValue ? ownerValue->asObject() : nullptr;
    const Vec origin = vec(actor, "Location");

    // As Engine.dll's phys* functions for The Fallen's effect modes (read
    // November 2026): each walks the copies itself. Most fade in the last
    // second (or half) of a copy's LifeSpan, not over its whole life.
    auto set = [&](VmObject* o, const char* name, float v) { setCopyFloat(o, name, v); };
    auto setv = [&](VmObject* o, const char* name, const Vec& v) { setCopyVec(o, name, v); };
    auto gone = [&](VmObject* mm) {
        copyStarts_.erase(mm);
        vm_.callEvent(mm, "Delete");
    };
    // Its life: always counted down, gone at 0 (a copy given none dies at
    // once), or only counted while it has one.
    auto lifeAlways = [&](VmObject* mm) {
        const float l = f(mm, "LifeSpan") - dt;
        set(mm, "LifeSpan", l);
        if (l > 0)
            return true;
        gone(mm);
        return false;
    };
    auto lifeIfAny = [&](VmObject* mm) {
        const float l = f(mm, "LifeSpan");
        return l > 0 ? lifeAlways(mm) : true;
    };
    // Moved by its velocity, then that changed by its acceleration (and
    // `extra`); or the other way round.
    auto moveThenAccel = [&](VmObject* mm, const Vec& extra = {}) {
        const Vec v = vec(mm, "Velocity");
        setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
        setv(mm, "Velocity", add(v, mul(add(vec(mm, "Acceleration"), extra), dt)));
    };
    auto accelThenMove = [&](VmObject* mm, const Vec& extra = {}) {
        const Vec v = add(vec(mm, "Velocity"), mul(add(vec(mm, "Acceleration"), extra), dt));
        setv(mm, "Velocity", v);
        setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
    };
    auto facing = [&](const Vec& v) {
        return vm_.rotatorValue(units(std::atan2(v[2], std::hypot(v[0], v[1]))), units(std::atan2(v[1], v[0])), 0);
    };
    auto setRot = [&](VmObject* mm, const Value& r) {
        if (Value* p = prop(mm, "Rotation"))
            *p = r;
    };
    auto setBool = [&](VmObject* o, const char* name, bool v) {
        if (Value* p = o ? prop(o, name) : nullptr)
            *p = Value::boolValue(v);
    };
    auto texAt = [&](VmObject* o, const char* name, int i) -> VmObject* {
        const Value* t = o ? prop(o, name) : nullptr;
        return t ? t[i].asObject() : nullptr;
    };
    auto setTex = [&](VmObject* mm, VmObject* t) {
        if (Value* p = prop(mm, "Texture"); p && t)
            *p = Value::objectValue(t);
    };
    // A spark's trail: a smaller copy left behind it, lasting a moment.
    auto newCopy = [&](VmObject* holder) -> VmObject* {
        VmFunction* make = vm_.findFunction(holder, vm_.name("NewMultiMesh"));
        return make ? vm_.invoke(make, holder, {}).asObject() : nullptr;
    };
    auto trail = [&](VmObject* mm, float size, float life, float glow) {
        VmObject* c = newCopy(actor);
        if (!c)
            return;
        setv(c, "Location", vec(mm, "Location"));
        set(c, "DrawScale", size);
        setv(c, "Velocity", mul(vec(mm, "Velocity"), 0.8f));
        setv(c, "Acceleration", mul(vec(mm, "Acceleration"), 0.8f));
        set(c, "ScaleGlow", glow);
        if (Value* st = prop(c, "Style"))
            *st = Value::intValue(3);
        set(c, "LifeSpan", life);
        setv(c, "ColorFilter", vec(mm, "ColorFilter"));
        if (const Value* t = prop(mm, "Texture"))
            if (Value* p = prop(c, "Texture"))
                *p = *t;
        setBool(c, "bUse", true);
    };
    // Turned about `axis` keeping its speed (a trail's or cinder's curl).
    auto curl = [&](VmObject* mm, const Vec& axis, float by) {
        const Vec v = vec(mm, "Velocity");
        const float speed = length(v);
        if (speed <= 0)
            return;
        const Vec d = normal(v);
        const Vec c{d[1] * axis[2] - d[2] * axis[1], d[2] * axis[0] - d[0] * axis[2], d[0] * axis[1] - d[1] * axis[0]};
        setv(mm, "Velocity", mul(normal(add(v, mul(c, by))), speed));
    };
    // The explosions' smoke textures (CTVE_EXP_Smoke's Tex, FadeTex,
    // TranslucentTex), picked again each tick.
    auto expTexture = [&](VmObject* mm) {
        const Value* st = prop(actor, "Style");
        const float l = f(mm, "LifeSpan");
        if (st && st->asInt() == 3)
            setTex(mm, texAt(actor, "TranslucentTex", int(frand() * 3.9f)));
        else if (l < 2)
            setTex(mm, texAt(actor, "FadeTex", std::clamp(int(14 - 7 * l), 0, 13)));
        else
            setTex(mm, texAt(actor, "Tex", int(frand() * 3.9f)));
    };
    const bool generator = owner && vm_.isA(owner, "CTVE_Generator");

    switch (physics) {
    // Beams: placed by their scripts each tick, left be.
    case kPhysBeamSustain:
        return;
    // An arc's segments (physSpark, CTVE_ChainSpark): laid on its curve
    // again when it's updated (the base copy's: from Vect2, weights
    // 3t(1-t)^2, 3t^2(1-t), t^3 on its Location, Velocity and
    // Acceleration, t on by its Phase a copy; a wave of AxisScale x
    // sin(Angle t) with bUse); then every tick each is jolted a random way
    // by its Use1 (the ends Use2), as bright as the arc, and the rest
    // turned to the one before and stretched to it; the tip is the last.
    case kPhysSpark: {
        if (!vm_.isA(actor, "CTVE_ChainSpark"))
            return;
        if (b(actor, "bUpdate")) {
            setBool(actor, "bUpdate", false);
            float t = 0;
            const Vec from = vec(base, "Vect2"), p1 = vec(base, "Location"), p2 = vec(base, "Velocity"),
                      p3 = vec(base, "Acceleration"), wave = vec(base, "AxisScale");
            const bool waves = b(base, "bUse");
            const float step = f(base, "Phase"), freq = f(base, "Angle");
            for (VmObject* mm : copies) {
                const float u = 1 - t;
                Vec at = add(from, add(add(mul(p1, 3 * t * u * u), mul(p2, 3 * t * t * u)), mul(p3, t * t * t)));
                if (waves)
                    at = add(at, mul(wave, std::sin(freq * t)));
                setv(mm, "Vect1", at);
                t += step;
            }
        }
        const int last = (prop(actor, "NumSegments") ? prop(actor, "NumSegments")->asInt() : int(copies.size())) - 1;
        const float glow = f(actor, "ScaleGlow", 1);
        VmObject* before = nullptr;
        int k = 0;
        for (VmObject* mm : copies) {
            set(mm, "ScaleGlow", glow);
            const float jolt = (k == 0 || k == last) ? f(mm, "Use2") : f(mm, "Use1");
            setv(mm, "Location", add(vec(mm, "Vect1"), mul(vrand(), jolt)));
            if (before) {
                const Vec d = sub(vec(before, "Location"), vec(mm, "Location"));
                setRot(mm, facing(d));
                Vec scale = vec(mm, "AxisScale");
                scale[0] = length(d);
                setv(mm, "AxisScale", scale);
            }
            before = mm;
            ++k;
        }
        if (before)
            setv(actor, "TipLocation", vec(before, "Location"));
        return;
    }
    // Particle (physParticle): pushed by the generator's Forces and wind
    // (on top of the Acceleration its scripts set), slowed by DeltaSpeed,
    // growing by DeltaSize; in its last second shrinking (bFadeSize) and
    // fading (bFadeGlow) to nothing. Without a generator, both fades.
    case kPhysParticle: {
        const Vec forces = generator ? add(vec(owner, "Forces"), vec(owner, "WindForce")) : Vec{};
        const float slow = generator ? f(owner, "DeltaSpeed") : 0, grow = generator ? f(owner, "DeltaSize") : 0;
        const float glow = generator ? f(owner, "MaxScaleGlow", 1) : f(actor, "ScaleGlow", 1);
        const bool fadeSize = !generator || b(owner, "bFadeSize"), fadeGlow = !generator || b(owner, "bFadeGlow");
        for (VmObject* mm : copies) {
            Vec v = vec(mm, "Velocity");
            if (const float speed = length(v); speed > 0 && slow != 0)
                v = mul(v, std::max(0.0f, speed - slow * dt) / speed);
            setv(mm, "Velocity", v);
            accelThenMove(mm, forces);
            const float l = f(mm, "LifeSpan");
            if (l > 0 && l < 1) {
                if (fadeSize)
                    set(mm, "DrawScale", f(mm, "DrawScale") * l / (l + dt));
                if (fadeGlow)
                    set(mm, "ScaleGlow", glow * l);
            }
            set(mm, "DrawScale", std::max(0.0f, f(mm, "DrawScale") + grow * dt));
            lifeIfAny(mm);
        }
        return;
    }
    // Smoke (physSmoke): pushed by the generator's Forces and wind, fading
    // in its last second (bFadeGlow; always without a generator). No growth.
    case kPhysSmoke: {
        const Vec forces = generator ? add(vec(owner, "Forces"), vec(owner, "WindForce")) : Vec{};
        const bool fade = !generator || b(owner, "bFadeGlow");
        for (VmObject* mm : copies) {
            accelThenMove(mm, forces);
            const float l = f(mm, "LifeSpan");
            if (fade && l > 0 && l < 1)
                set(mm, "ScaleGlow", l);
            lifeIfAny(mm);
        }
        return;
    }
    // A steam jet's puff (physSteamJet): out and slowing (its
    // acceleration against its velocity), growing by DeltaSize; with a
    // second of life left, blown off (30 x a random way plus the jet's
    // MMEventLocation, the way back along it) for half a second, fading.
    case kPhysSteamJet:
        if (!generator)
            return;
        for (VmObject* mm : copies) {
            moveThenAccel(mm);
            set(mm, "DrawScale", f(mm, "DrawScale") + f(owner, "DeltaSize") * dt);
            if (!b(mm, "bUse")) {
                if (!lifeIfAny(mm))
                    continue;
                if (f(mm, "LifeSpan") > 1)
                    continue;
                setv(mm, "Acceleration", mul(add(vrand(), vec(owner, "MMEventLocation")), 30));
                set(mm, "LifeSpan", 0.5f);
                setBool(mm, "bUse", true);
            }
            set(mm, "ScaleGlow", 2 * f(owner, "MaxScaleGlow", 1) * f(mm, "LifeSpan"));
            lifeIfAny(mm);
        }
        return;
    // Hot sparks (physHotSpark): facing the way they fly, kicked off
    // somewhere new now and then (3 ticks in 10), flickering, fading in
    // their last half second.
    case kPhysHotSpark:
        if (!generator)
            return;
        for (VmObject* mm : copies) {
            moveThenAccel(mm);
            setRot(mm, facing(vec(mm, "Velocity")));
            if (frand() < 0.3f) {
                Vec a = mul(vrand(), 500);
                a[2] = -500 * frand();
                setv(mm, "Acceleration", a);
                setv(mm, "Velocity", mul(vrand(), 200));
            }
            const float l = f(mm, "LifeSpan");
            set(mm, "ScaleGlow", l < 0.5f ? 2 * l : frand());
            lifeIfAny(mm);
        }
        return;
    // CTVE_Sparks' sparks (physCTVE_Sparks): flickering in size (their
    // Phase) and brightness, jittered, dragged down to 100 across, each
    // leaving a trail of short-lived embers.
    case kPhysCtveSparks: {
        if (!vm_.isA(actor, "CTVE_Sparks"))
            return;
        const float glow = f(actor, "MaxScaleGlow", 1);
        for (VmObject* mm : copies) {
            moveThenAccel(mm);
            set(mm, "DrawScale", frand() * f(mm, "Phase"));
            setv(mm, "Location", add(vec(mm, "Location"), {frand() * 0.2f - 0.1f, frand() * 0.2f - 0.1f, frand() * 0.2f - 0.1f}));
            Vec v = vec(mm, "Velocity");
            if (const float across = std::hypot(v[0], v[1]); across > 100) {
                const float k = std::max(0.0f, across - 500 * dt) / across;
                v[0] *= k;
                v[1] *= k;
                setv(mm, "Velocity", v);
            }
            const float l = f(mm, "LifeSpan");
            set(mm, "ScaleGlow", l < 0.5f ? 2 * l * glow : frand());
            set(mm, "Angle", f(mm, "Angle") - dt);
            if (!b(mm, "bUse") && f(mm, "Angle") < 0) {
                trail(mm, frand() * f(mm, "Phase") * 0.25f, frand() * 0.6f, glow);
                setv(mm, "Velocity", add(vec(mm, "Velocity"), {frand() * 30 - 15, frand() * 30 - 15, frand() * 30 - 15}));
                set(mm, "Angle", 0.01f + 0.02f * frand());
            }
            lifeAlways(mm);
        }
        return;
    }
    // Bubbles (physBubble): rising, their drift damped, shrinking and
    // dimming in their last second (half bright before). At the water's
    // surface (the generator's SeedLocation.Z) one becomes a pop (the
    // generator's SpecialTexture) for a moment and the generator's
    // bMMEvent is raised there (its scripts make a ripple); with no pop
    // texture it's just gone.
    case kPhysBubble: {
        if (!generator)
            return;
        const float surface = vec(owner, "SeedLocation")[2];
        const Value* popValue = prop(owner, "SpecialTexture");
        VmObject* pop = popValue ? popValue->asObject() : nullptr;
        for (VmObject* mm : copies) {
            const Value* t = prop(mm, "Texture");
            if (pop && t && t->asObject() == pop) {
                set(mm, "DrawScale", f(mm, "DrawScale") + 0.3f * dt);
                Vec l = vec(mm, "Location");
                l[2] += 10 * dt;
                setv(mm, "Location", l);
                lifeIfAny(mm);
                continue;
            }
            Vec v = vec(mm, "Velocity");
            setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
            const float l = f(mm, "LifeSpan");
            if (l > 0 && l < 1) {
                set(mm, "DrawScale", f(mm, "DrawScale") * l / (l + dt));
                set(mm, "ScaleGlow", 0.5f * l);
            } else {
                set(mm, "ScaleGlow", 0.5f);
            }
            v[0] -= v[0] * dt;
            v[1] -= v[1] * dt;
            v = add(v, mul(vec(mm, "Acceleration"), dt));
            setv(mm, "Velocity", v);
            setv(mm, "Acceleration", {-v[0], -v[1], 0});
            Vec at = vec(mm, "Location");
            if (at[2] > surface) {
                if (pop) {
                    at[2] = surface;
                    setv(mm, "Location", at);
                    setv(mm, "Velocity", {0, 0, 0});
                    setTex(mm, pop);
                    set(mm, "LifeSpan", 0.08f);
                    setv(owner, "MMEventLocation", at);
                    setBool(owner, "bMMEvent", true);
                } else {
                    set(mm, "LifeSpan", 0.001f);
                }
            }
            lifeIfAny(mm);
        }
        return;
    }
    // The M4 rifle's gas cloud and the beams' fading tails (physGasCloudM4,
    // physBeamFadeTail...): a life of a second or more is spent at once
    // (the engine counts it down again until it's under a second, as
    // read); in the last second size and brightness go down 0.2 a second;
    // gone when any reaches 0.
    case kPhysGasCloudM4:
    case kPhysBeamFadeTailCatchup:
    case kPhysBeamFadeTailStationary:
        for (VmObject* mm : copies) {
            float l = f(mm, "LifeSpan");
            if (l <= 0)
                continue;
            do
                l -= dt;
            while (l >= 1 && dt > 0);
            set(mm, "LifeSpan", l);
            if (l <= 0 || f(mm, "ScaleGlow") <= 0 || f(mm, "DrawScale") <= 0) {
                gone(mm);
                continue;
            }
            set(mm, "ScaleGlow", f(mm, "ScaleGlow") - 0.2f * dt);
            set(mm, "DrawScale", f(mm, "DrawScale") - 0.2f * dt);
        }
        return;
    // A fan's stream (physFanParticle; the generator's Acceleration.X the
    // side it blows, as a whole number, and Z how fast): each copy flows
    // along its line (Phase -1 to 1), slower toward the middle, flaring
    // out toward both ends (Vect1 x (1 + 4 Phase^2) + Vect2 x Phase from
    // the generator), fading in the outer halves; gone past the end.
    case kPhysFanParticle: {
        if (!generator)
            return;
        const Vec gen = vec(owner, "Acceleration");
        const float speed = gen[2];
        const int side = int(gen[0]);
        const Vec from = vec(owner, "Location");
        for (VmObject* mm : copies) {
            float phase = f(mm, "Phase");
            phase += std::max(0.2f, 1 - std::fabs(phase)) * speed * dt;
            set(mm, "Phase", phase);
            float glow = phase > 0.5f ? 2 * (1 - phase) : phase < -0.5f ? 2 * (phase + 1) : 1;
            if (std::fabs(speed) < 1)
                glow *= std::fabs(speed);
            set(mm, "ScaleGlow", glow);
            if (std::fabs(phase) > 1 || (side == 1 && phase < 0) || (side == -1 && phase > 0))
                set(mm, "LifeSpan", 0.001f);
            setv(mm, "Location", add(add(from, mul(vec(mm, "Vect1"), 1 + 4 * phase * phase)), mul(vec(mm, "Vect2"), phase)));
            lifeIfAny(mm);
        }
        return;
    }
    // Steam spiralling out (physSpiralJet; the base: Velocity its axis,
    // Vect1 and Vect2 across it, Use3 its speed, Phase how fast it
    // widens, Use1 its growth, Use2 its brightness): each copy out along
    // the axis (its Phase the distance), round a widening circle (Angle,
    // a turn a second), growing; fading in its last second.
    case kPhysSpiralJet:
        for (VmObject* mm : copies) {
            const float out = f(mm, "Phase") + f(base, "Use3") * dt;
            set(mm, "Phase", out);
            const float r = out * f(base, "Phase");
            const float angle = f(mm, "Angle");
            setv(mm, "Location", add(add(origin, mul(vec(base, "Velocity"), out)),
                                     add(mul(vec(base, "Vect1"), r * std::sin(angle)), mul(vec(base, "Vect2"), r * std::cos(angle)))));
            float next = angle + 6.28f * dt;
            if (next > 6.28f)
                next -= 6.28f;
            set(mm, "Angle", next);
            set(mm, "DrawScale", f(mm, "DrawScale") + f(base, "Use1") * dt);
            if (const float l = f(mm, "LifeSpan"); l < 1)
                set(mm, "ScaleGlow", f(base, "Use2") * l);
            lifeIfAny(mm);
        }
        return;
    // A pipe's stream (physPipePath): each copy drawn toward its
    // Acceleration (a point, not a push), away from it for half a second
    // at 1.5 and 3.5 left, damped; brightest mid-life; at its end it
    // starts again (Vect1 the place, Vect2 the velocity, Use1 the life).
    case kPhysPipePath:
        if (!generator)
            return;
        for (VmObject* mm : copies) {
            const float l = f(mm, "LifeSpan") - dt;
            set(mm, "LifeSpan", l);
            const Vec d = sub(vec(mm, "Acceleration"), vec(mm, "Location"));
            const bool away = (l >= 1.5f && l < 2) || (l >= 3.5f && l < 4);
            Vec v = add(vec(mm, "Velocity"), mul(d, away ? -dt : dt));
            v = sub(v, mul(v, dt));
            setv(mm, "Velocity", v);
            setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
            set(mm, "ScaleGlow", l > 4 ? 5 - l : l);
            if (l <= 0) {
                setv(mm, "Location", vec(mm, "Vect1"));
                setv(mm, "Velocity", vec(mm, "Vect2"));
                set(mm, "LifeSpan", f(mm, "Use1"));
            }
        }
        return;
    // A swarm (physSwarmObject; the generator's NumAnimTextures and
    // AnimTexture): each flies by its velocity and acceleration. With no
    // textures it faces the way it flies; otherwise its texture steps a
    // frame a tick (the fireflies' 8: frames 0-3, or 4-7 with bUse).
    case kPhysSwarmObject: {
        if (!generator)
            return;
        const int frames = prop(owner, "NumAnimTextures") ? prop(owner, "NumAnimTextures")->asInt() : 0;
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            if (frames == 0) {
                setRot(mm, facing(vec(mm, "Velocity")));
                continue;
            }
            if (frames < 0)
                continue;
            float phase = f(mm, "Phase") + 1;
            if (frames == 8) {
                if (b(mm, "bUse")) {
                    if (phase >= 8)
                        phase = 4;
                } else if (phase >= 4) {
                    phase = 0;
                }
            } else if (phase >= float(frames)) {
                phase = 0;
            }
            set(mm, "Phase", phase);
            setTex(mm, texAt(owner, "AnimTexture", std::clamp(int(phase), 0, 7)));
        }
        return;
    }
    // Fish in a tank (physFishInTank; the base's Vect1-Vect2 box): each
    // swims slowly (5 to 30) to a point picked every 0.3 to 1.3 s, or half
    // the time follows the fish before it; triggered (the base's bUse),
    // they go up to the top to feed.
    case kPhysFishInTank: {
        const Vec lo = vec(base, "Vect1"), hi = vec(base, "Vect2");
        const bool feeding = b(base, "bUse");
        Vec before = lo;
        bool first = true;
        for (VmObject* mm : copies) {
            const float speed = 5 + 25 * frand();
            Vec at = vec(mm, "Location");
            float phase = f(mm, "Phase") - dt;
            if (phase < 0) {
                phase = frand() + 0.3f;
                if (!first && frand() < 0.5f) {
                    setBool(mm, "bUse", true);
                } else {
                    setBool(mm, "bUse", false);
                    setv(mm, "Vect1", {lo[0] + (hi[0] - lo[0]) * frand(), lo[1] + (hi[1] - lo[1]) * frand(),
                                       lo[2] + (hi[2] - lo[2]) * frand()});
                }
                const float top = std::max(lo[2], hi[2]);
                if (feeding && at[2] < top - 5)
                    setv(mm, "Vect1", {lo[0] + (hi[0] - lo[0]) * (0.2f + 0.6f * frand()),
                                       lo[1] + (hi[1] - lo[1]) * (0.2f + 0.6f * frand()), top});
            }
            if (!feeding && b(mm, "bUse"))
                setv(mm, "Vect1", add(before, {frand() * 4 - 2, frand() * 4 - 2, frand() * 4 - 2}));
            const Vec to = sub(vec(mm, "Vect1"), at);
            if (length(to) < 1)
                phase = 0;
            set(mm, "Phase", phase);
            const Vec a = mul(normal(to), speed);
            setv(mm, "Acceleration", a);
            Vec v = add(vec(mm, "Velocity"), mul(a, dt));
            if (length(v) > 10)
                v = mul(normal(v), 10);
            at = add(at, mul(v, dt));
            for (int k = 0; k < 3; ++k)
                at[k] = std::clamp(at[k], std::min(lo[k], hi[k]), std::max(lo[k], hi[k]));
            setv(mm, "Velocity", v);
            setv(mm, "Location", at);
            if (length(v) > 0)
                setRot(mm, facing(v));
            before = at;
            first = false;
        }
        return;
    }
    // A fire's flames and smoke (physFire_Gas; the fire, the holder's
    // owner: its DrawScale S, ScaleGlow G, ColorFilter.X the chance of
    // smoke, .Z 1 for a normal fire, else plasma; its TweenRate, if set,
    // caps the brightness). Each puff goes through stages (Angle), each
    // Phase seconds long, each setting its growth (Use1), brightening
    // (Use2), push (Vect1) and colour change (Vect2): a white flash up
    // (0.2 s), yellow to orange or green (0.3 s), then smoke turning grey
    // and sinking away (0.5 s each), or the flame shrinking out (0.5 s).
    case kPhysFireGas: {
        const float S = f(owner, "DrawScale", 1), G = f(owner, "ScaleGlow", 1);
        const Vec settings = vec(owner, "ColorFilter");
        const bool normalFire = settings[2] == 1;
        const float cap = f(owner, "TweenRate");
        VmObject* smoke = texAt(actor, "Tex", 0);
        for (VmObject* mm : copies) {
            if (!lifeAlways(mm))
                continue;
            float phase = f(mm, "Phase") - dt;
            int stage = int(f(mm, "Angle"));
            if (phase < 0) {
                if (phase < -0.15f)
                    stage = 4;
                auto begin = [&](float length, float use1, float use2, const Vec& push, const Vec& colour) {
                    phase = length;
                    set(mm, "Use1", use1);
                    set(mm, "Use2", use2);
                    setv(mm, "Vect1", push);
                    setv(mm, "Vect2", colour);
                };
                switch (stage) {
                case 0:
                    setv(mm, "ColorFilter", {1, 1, 1});
                    begin(0.2f, 1.5f * S, 4, {0, 0, 300 * G}, {0, 0, -5});
                    set(mm, "DrawScale", 0);
                    set(mm, "ScaleGlow", 0);
                    setBool(mm, "bUse", true);
                    stage = 1;
                    break;
                case 1:
                    setv(mm, "ColorFilter", {1, 1, 0});
                    begin(0.3f, S, 2.0f / 3, {0, 0, 0}, normalFire ? Vec{0, -2, 0} : Vec{-1 / 0.3f, 0, 0});
                    stage = frand() <= settings[0] ? 2 : 5;
                    break;
                case 2:
                    setv(mm, "ColorFilter", normalFire ? Vec{1, 0.4f, 0} : Vec{0, 1, 0});
                    begin(0.5f, 0.8f * S, 0, {0, 0, G}, normalFire ? Vec{-1.4f, -0.2f, 0.6f} : Vec{1, -1, 1});
                    setTex(mm, smoke);
                    stage = 3;
                    break;
                case 3:
                    setv(mm, "ColorFilter", normalFire ? Vec{0.3f, 0.3f, 0.3f} : Vec{0.5f, 0.5f, 0.5f});
                    begin(0.5f, 0.2f * S, -2, {0, 0, -100 * G}, normalFire ? Vec{0.4f, 0.4f, 0.4f} : Vec{0, 0, 0});
                    stage = 4;
                    break;
                case 5:
                    setv(mm, "ColorFilter", normalFire ? Vec{1, 0.4f, 0} : Vec{0, 1, 0});
                    begin(0.5f, -2.96f * S, -2, {0, 0, 0}, {0, 0, 0});
                    setTex(mm, smoke);
                    stage = 4;
                    break;
                default: // 4: out
                    phase = 10000;
                    setBool(mm, "bUse", false);
                    set(mm, "LifeSpan", 0.001f);
                    set(mm, "ScaleGlow", 0);
                    set(mm, "DrawScale", 0.0001f);
                    set(mm, "Use2", 0);
                    stage = 3;
                    break;
                }
                set(mm, "Angle", float(stage));
            }
            set(mm, "Phase", phase);
            const float use1 = f(mm, "Use1");
            const Vec v = add(vec(mm, "Velocity"), mul(add(vec(mm, "Vect1"), mul(vec(mm, "Acceleration"), use1)), dt));
            setv(mm, "Velocity", v);
            setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
            set(mm, "DrawScale", f(mm, "DrawScale") + use1 * dt);
            setv(mm, "ColorFilter", add(vec(mm, "ColorFilter"), mul(vec(mm, "Vect2"), dt)));
            float glow = f(mm, "ScaleGlow") + f(mm, "Use2") * dt;
            if (cap != 0 && glow > cap)
                glow = cap;
            set(mm, "ScaleGlow", glow);
        }
        return;
    }
    // An explosion's trail (EXP_Trail): fire to smoke over its life (the
    // base's Vect1 to Vect2 by LifeSpan over the base's Phase), its
    // texture picked again each tick.
    case kPhysExpTrail: {
        const float span = std::max(0.01f, f(base, "Phase", 1));
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            const float t = f(mm, "LifeSpan") / span;
            setv(mm, "ColorFilter", add(vec(base, "Vect2"), mul(sub(vec(base, "Vect1"), vec(base, "Vect2")), t)));
            expTexture(mm);
            lifeAlways(mm);
        }
        return;
    }
    // Explosions' smoke (EXP_Smoke): growing by its Phase; once falling
    // faster than 40, drawn back to the explosion and up.
    case kPhysExpSmoke:
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            expTexture(mm);
            set(mm, "DrawScale", f(mm, "DrawScale") + f(mm, "Phase") * dt);
            if (vec(mm, "Velocity")[2] < -40) {
                Vec a = sub(origin, vec(mm, "Location"));
                a[2] = 50;
                setv(mm, "Acceleration", a);
            }
            lifeAlways(mm);
        }
        return;
    // An explosion's column (EXP_Column): growing by its Phase, at least 0.3.
    case kPhysExpColumn:
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            expTexture(mm);
            set(mm, "DrawScale", std::max(0.3f, f(mm, "DrawScale") + f(mm, "Phase") * dt));
            lifeAlways(mm);
        }
        return;
    // Cinders and particles (EXP_Cinder, EXP_Particle): flickering between
    // the base's colours and in brightness (dimming in the last second),
    // curling about their own axis (the base's Angle), each leaving a
    // trail of embers; cinders are jostled now and then (their Phase).
    case kPhysExpCinder:
    case kPhysExpParticle: {
        const bool cinder = physics == kPhysExpCinder;
        const float twist = f(base, "Angle");
        const Vec c1 = vec(base, "Vect1"), c2 = vec(base, "ColorFilter");
        for (VmObject* mm : copies) {
            if (cinder && frand() < 0.1f) {
                Vec a = vec(mm, "Acceleration");
                for (float& k : a)
                    k += (2 * frand() - 1) * f(mm, "Phase");
                setv(mm, "Acceleration", a);
                setv(mm, "Vect2", vrand());
            }
            accelThenMove(mm);
            setv(mm, "ColorFilter", add(c1, mul(sub(c2, c1), frand())));
            const float l = f(mm, "LifeSpan");
            float glow = frand();
            if (!cinder)
                glow *= l;
            if (l < 1)
                glow = std::min(glow, l);
            set(mm, "ScaleGlow", glow);
            if (twist != 0 && !b(mm, "bUse"))
                curl(mm, vec(mm, "Vect2"), dt * twist * 300);
            if (!lifeAlways(mm))
                continue;
            set(mm, "Angle", f(mm, "Angle") - dt);
            if (!b(mm, "bUse") && f(mm, "Angle") < 0) {
                trail(mm, 0.7f * frand() * f(mm, cinder ? "Use1" : "Phase"), 0.5f + 0.8f * frand(), 1);
                set(mm, "Angle", 0.005f + 0.01f * frand());
            }
        }
        return;
    }
    // A plasma thrower's stream (physPlasmaThrower; the generator: its
    // place and X axis, MMEventLocation where it hits, WindDir the hit's
    // normal): drawn onto the axis until 50 short of the hit, then
    // bounced off it and spread along the surface; green, growing.
    case kPhysPlasmaThrower: {
        if (!generator)
            return;
        const Vec from = vec(owner, "Location");
        const Value* rr = prop(owner, "Rotation");
        const auto ro = rr ? Vm::rotatorOf(*rr) : std::array<int32_t, 3>{};
        const float yaw = float(ro[1]) * kPi / 32768, pitch = float(ro[0]) * kPi / 32768;
        const Vec x{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        const Vec hit = vec(owner, "MMEventLocation"), n = vec(owner, "WindDir");
        const float reach = length(sub(hit, from));
        for (VmObject* mm : copies) {
            if (f(mm, "DrawScale") < 2)
                set(mm, "DrawScale", f(mm, "DrawScale") + 10 * dt);
            set(mm, "ScaleGlow", std::min(f(mm, "LifeSpan"), 0.25f));
            accelThenMove(mm);
            setv(mm, "ColorFilter", {0.6f * frand(), 1, 0.6f * frand()});
            const Vec at = vec(mm, "Location");
            const float along = dot(sub(at, from), x);
            if (along < reach - 50) {
                setv(mm, "Acceleration", mul(sub(add(from, mul(x, along)), at), 100));
            } else if (f(mm, "Use1") == 0) {
                const Vec v = vec(mm, "Velocity");
                setv(mm, "Velocity", sub(v, mul(n, 2 * dot(v, n))));
                set(mm, "Use1", 1);
            } else {
                set(mm, "DrawScale", f(mm, "DrawScale") + 10 * dt);
                setv(mm, "Acceleration", sub(add(hit, mul(n, 100)), at));
            }
            lifeIfAny(mm);
        }
        return;
    }
    // Lava blobs (physLava): falling until under the generator, then
    // still there, shrinking away.
    case kPhysLava: {
        if (!generator)
            return;
        const float floor = vec(owner, "Location")[2];
        for (VmObject* mm : copies) {
            if (!b(mm, "bUse")) {
                accelThenMove(mm);
                Vec at = vec(mm, "Location");
                if (at[2] < floor) {
                    at[2] = floor;
                    setv(mm, "Location", at);
                    setv(mm, "Velocity", {0, 0, 0});
                    setv(mm, "Acceleration", {0, 0, 0});
                    setBool(mm, "bUse", true);
                }
                continue;
            }
            const float size = f(mm, "DrawScale") - 0.2f * dt;
            set(mm, "DrawScale", size);
            if (size <= 0)
                gone(mm);
        }
        return;
    }
    // A phaser's disintegration specks: sinking at 30 a second, fading.
    case kPhysDisintegrationPhaser:
        for (VmObject* mm : copies) {
            Vec at = vec(mm, "Location");
            at[2] -= 30 * dt;
            setv(mm, "Location", at);
            set(mm, "ScaleGlow", f(mm, "ScaleGlow") - 0.5f * dt);
            lifeIfAny(mm);
        }
        return;
    // Exhaust (physExhaustJet): orange to red to pale, rising less as the
    // jet's own Velocity.Z (its speed) pulls it back, growing, fading.
    case kPhysExhaustJet: {
        const float pull = vec(actor, "Velocity")[2];
        for (VmObject* mm : copies) {
            Vec c = vec(mm, "ColorFilter");
            if (c[1] > c[2]) {
                c[1] -= 8 * dt;
            } else {
                c[2] += dt;
                c[1] = c[2];
            }
            setv(mm, "ColorFilter", c);
            Vec v = vec(mm, "Velocity");
            setv(mm, "Location", add(vec(mm, "Location"), mul(v, dt)));
            v[2] -= pull * dt;
            setv(mm, "Velocity", add(v, mul(vec(mm, "Acceleration"), dt)));
            set(mm, "DrawScale", f(mm, "DrawScale") + dt);
            set(mm, "ScaleGlow", f(mm, "ScaleGlow") - dt);
            lifeIfAny(mm);
        }
        return;
    }
    // The pulse rifle's rings and scorches: brightening to 1, dimming in
    // their last half second; the rings grow by the shot's power (the
    // actor's Acceleration.Z, at most 1) plus a half.
    case kPhysEmPulseRifle:
    case kPhysEmPulseRifleQuad: {
        const float power = std::min(1.0f, vec(actor, "Acceleration")[2]);
        for (VmObject* mm : copies) {
            const float l = f(mm, "LifeSpan");
            if (l <= 0)
                continue;
            set(mm, "ScaleGlow", l < 0.5f ? std::max(0.0f, f(mm, "ScaleGlow") - 2 * dt) : std::min(1.0f, f(mm, "ScaleGlow") + 8 * dt));
            if (physics == kPhysEmPulseRifle)
                set(mm, "DrawScale", f(mm, "DrawScale") + (power + 0.5f) * dt);
            lifeIfAny(mm);
        }
        return;
    }
    // Water rings (physWaterRing): drifting, spreading by their Angle a
    // second, fading by their Phase a second; gone when dark.
    case kPhysWaterRing:
        for (VmObject* mm : copies) {
            setv(mm, "Location", add(vec(mm, "Location"), mul(vec(mm, "Velocity"), dt)));
            const float glow = f(mm, "ScaleGlow") - f(mm, "Phase") * dt;
            set(mm, "ScaleGlow", glow);
            set(mm, "DrawScale", f(mm, "DrawScale") + f(mm, "Angle") * dt);
            if (glow < 0)
                gone(mm);
        }
        return;
    // Explosion cores and rings: by velocity, gone when their life is.
    case kPhysExpCore:
    case kPhysExpRing:
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            lifeAlways(mm);
        }
        return;
    case kPhysPlasmaJet:
        for (VmObject* mm : copies) {
            accelThenMove(mm);
            set(mm, "ScaleGlow", f(mm, "LifeSpan"));
            Vec c = vec(mm, "ColorFilter");
            c[0] = frand();
            c[2] = frand();
            setv(mm, "ColorFilter", c);
            lifeAlways(mm);
        }
        return;
    // Disintegration (physDisintigration): spinning by Vect1 a second,
    // drawn in to the generator and up until let go (bUse), fading.
    case kPhysDisintegration:
        if (!generator)
            return;
        for (VmObject* mm : copies) {
            if (Value* r = prop(mm, "Rotation")) {
                auto ro = Vm::rotatorOf(*r);
                const Vec spin = vec(mm, "Vect1");
                ro[1] += int32_t(spin[0] * dt);
                ro[0] += int32_t(spin[1] * dt);
                ro[2] += int32_t(spin[2] * dt);
                *r = vm_.rotatorValue(ro[0] & 0xffff, ro[1] & 0xffff, ro[2] & 0xffff);
            }
            if (!b(mm, "bUse")) {
                Vec a = mul(sub(vec(owner, "Location"), vec(mm, "Location")), 50);
                a[2] = 500;
                setv(mm, "Acceleration", a);
            }
            accelThenMove(mm);
            set(mm, "ScaleGlow", f(mm, "ScaleGlow") - dt);
            lifeIfAny(mm);
        }
        return;
    default:
        // Others: by velocity and acceleration, aged.
        for (VmObject* mm : copies)
            stepCopy(mm, dt, true, true, {});
        return;
    }
}

// What a copy's age was a tick ago, for a fade that scales what the
// scripts set rather than replacing it.
float ScriptLevel::copyAgeBefore(VmObject* mm, float age, float dt) {
    auto it = copyStarts_.find(mm);
    if (it == copyStarts_.end() || it->second <= 0)
        return age;
    return std::max(0.0f, age - dt / it->second);
}

// --- Actors moved by The Fallen's modes -----------------------------------------------------

// The modes that move the actor itself.
void ScriptLevel::fallenPhysics(VmObject* a, int physics, float dt) {
    Value* velocityValue = prop(a, "Velocity");
    Value* loc = prop(a, "Location");
    if (!velocityValue || !loc)
        return;
    Vec v = Vm::vectorOf(*velocityValue);
    const Vec accel = prop(a, "Acceleration") ? Vm::vectorOf(*prop(a, "Acceleration")) : Vec{};
    auto f = [&](const char* name, float otherwise = 0) {
        const Value* p = prop(a, name);
        return p ? p->asFloat() : otherwise;
    };
    auto hitWall = [&](const MoveHit& h) {
        if (!a->deleted && h.hit())
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(h.actor)});
    };
    switch (physics) {
    case kPhysTrailer: {
        // Following its owner (physTrailer), through everything. A sprite
        // goes to the owner's place plus its PrePivot (bTrailerPrePivot),
        // or its Mass behind the owner along its facing
        // (bTrailerSameRotation), or the place itself. Anything else goes
        // to the place and takes the owner's turn (bTrailerSameRotation),
        // else faces away from where the owner's going (straight up when
        // it's still).
        const Value* o = prop(a, "Owner");
        VmObject* owner = o ? o->asObject() : nullptr;
        if (!owner || owner->deleted)
            return;
        const Value* at = prop(owner, "Location");
        if (!at)
            return;
        Vec to = Vm::vectorOf(*at);
        auto flag = [&](const char* name) {
            const Value* p = prop(a, name);
            return p && p->asBool();
        };
        const Value* drawType = prop(a, "DrawType");
        const Value* ownerRot = prop(owner, "Rotation");
        if (drawType && drawType->asInt() == 1) {
            if (flag("bTrailerPrePivot")) {
                to = add(to, prop(a, "PrePivot") ? Vm::vectorOf(*prop(a, "PrePivot")) : Vec{});
            } else if (flag("bTrailerSameRotation") && ownerRot) {
                const auto r = Vm::rotatorOf(*ownerRot);
                const float yaw = float(r[1]) * kPi / 32768, pitch = float(r[0]) * kPi / 32768;
                const Vec x{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
                to = sub(to, mul(x, f("Mass")));
            }
            vm_.setVector(*loc, to[0], to[1], to[2]);
            updateRegion(a);
            return;
        }
        vm_.setVector(*loc, to[0], to[1], to[2]);
        updateRegion(a);
        Value* r = prop(a, "Rotation");
        if (!r)
            return;
        if (flag("bTrailerSameRotation")) {
            if (ownerRot)
                *r = *ownerRot;
            return;
        }
        const Value* ov = prop(owner, "Velocity");
        const Vec w = ov ? Vm::vectorOf(*ov) : Vec{};
        if (std::fabs(w[0]) < 1e-4f && std::fabs(w[1]) < 1e-4f && std::fabs(w[2]) < 1e-4f)
            *r = vm_.rotatorValue(16384, 0, 0);
        else
            *r = vm_.rotatorValue(units(std::atan2(-w[2], std::hypot(w[0], w[1]))) & 0xffff,
                                  units(std::atan2(-w[1], -w[0])) & 0xffff, 0);
        return;
    }
    case kPhysRotatingYaw:
    case kPhysRotatingPitch: {
        // Turning about one axis only (physRotatingYaw, physRotatingPitch),
        // and only with bFixedRotationDir (round and round) or
        // bRotateToDesired (to DesiredRotation, then EndedRotation), by
        // RotationRate a second.
        Value* r = prop(a, "Rotation");
        const Value* rate = prop(a, "RotationRate");
        if (!r || !rate)
            return;
        auto flag = [&](const char* name) {
            const Value* p = prop(a, name);
            return p && p->asBool();
        };
        const bool toDesired = flag("bRotateToDesired"), fixed = flag("bFixedRotationDir");
        if (!toDesired && !fixed)
            return;
        const int k = physics == kPhysRotatingYaw ? 1 : 0;
        auto now = Vm::rotatorOf(*r);
        const auto want = prop(a, "DesiredRotation") ? Vm::rotatorOf(*prop(a, "DesiredRotation")) : now;
        const int32_t cur = now[size_t(k)] & 0xffff, goal = want[size_t(k)] & 0xffff;
        if (toDesired && cur == goal)
            return;
        const int32_t step = int32_t(float(Vm::rotatorOf(*rate)[size_t(k)]) * dt);
        int32_t next = cur;
        if (fixed && !toDesired) {
            next = (cur + step) & 0xffff;
        } else if (step != 0) {
            // The shorter way round to the goal, at most |step|.
            int32_t d = (goal - cur) & 0xffff;
            if (d > 32768)
                d -= 65536;
            const int32_t most = std::abs(step);
            next = (cur + std::clamp(d, -most, most)) & 0xffff;
        }
        now[size_t(k)] = next;
        *r = vm_.rotatorValue(now[0], now[1], now[2]);
        if (toDesired && next == goal && vm_.findFunction(a, vm_.name("EndedRotation")))
            vm_.callEvent(a, "EndedRotation");
        return;
    }
    case kPhysParticle: {
        // Without copies (physParticle): drifting by its velocity and
        // acceleration, through everything (no collision, no events).
        if (const Value* mm = prop(a, "MultiMesh"); mm && mm->asObject())
            return;
        v = add(v, mul(accel, dt));
        vm_.setVector(*velocityValue, v[0], v[1], v[2]);
        const Vec l = add(Vm::vectorOf(*loc), mul(v, dt));
        vm_.setVector(*loc, l[0], l[1], l[2]);
        updateRegion(a);
        return;
    }
    case kPhysNewtonian: {
        // Free flight by momentum (physNewtonian). An actor (a thrown drum)
        // keeps its velocity, which only its scripts change; it slides
        // along what it hits. A pawn (the Vulture, the Wraith, the
        // Arachnid) flies as a flyer does with no friction: its
        // acceleration (at most AccelRate) changes its velocity, up to
        // AirSpeed (times DesiredSpeed for those not players); its
        // velocity is then what it moved.
        if (vm_.isA(a, "Pawn")) {
            Vec push = accel;
            const float rate = f("AccelRate", 500);
            if (length(push) > rate)
                push = mul(normal(push), rate);
            const bool player = prop(a, "bIsPlayer") && prop(a, "bIsPlayer")->asBool();
            const float most = f("AirSpeed", 320) * (player ? 1 : f("DesiredSpeed", 1));
            v = add(v, mul(push, dt));
            if (length(v) > most)
                v = mul(normal(v), most);
            vm_.setVector(*velocityValue, v[0], v[1], v[2]);
            const Vec start = Vm::vectorOf(*loc);
            if (length(v) > 1e-3f)
                hitWall(moveSmooth(a, mul(v, dt)));
            if (!a->deleted && dt > 0)
                if (Value* vv = prop(a, "Velocity")) {
                    const Vec moved = mul(sub(Vm::vectorOf(*prop(a, "Location")), start), 1 / dt);
                    vm_.setVector(*vv, moved[0], moved[1], moved[2]);
                }
            return;
        }
        if (length(v) > 1e-3f)
            hitWall(moveSmooth(a, mul(v, dt)));
        return;
    }
    case kPhysFish: {
        // A fish (physFish): swimming as a swimmer does (the zone's
        // friction, buoyancy, current), up to WaterSpeed, facing the way
        // it swims (no roll). (The engine also works out a pull toward its
        // Destination, but keeps it in LastSeenPos, not its velocity.)
        swimOrFly(a, dt, true);
        if (a->deleted)
            return;
        Value* vv = prop(a, "Velocity");
        if (!vv)
            return;
        v = Vm::vectorOf(*vv);
        const float most = std::max(1.0f, f("WaterSpeed", 200));
        if (length(v) > most)
            v = mul(normal(v), most);
        vm_.setVector(*vv, v[0], v[1], v[2]);
        if (Value* r = prop(a, "Rotation"); r && length(v) > 1e-3f)
            *r = vm_.rotatorValue(units(std::atan2(v[2], std::hypot(v[0], v[1]))) & 0xffff,
                                  units(std::atan2(v[1], v[0])) & 0xffff, 0);
        return;
    }
    case kPhysExpTrail:
        // An explosion's trail flies by its velocity and acceleration,
        // through everything.
        v = add(v, mul(accel, dt));
        vm_.setVector(*velocityValue, v[0], v[1], v[2]);
        {
            const Vec l = add(Vm::vectorOf(*loc), mul(v, dt));
            vm_.setVector(*loc, l[0], l[1], l[2]);
            updateRegion(a);
        }
        return;
    case kPhysInterpolating:
    case kPhysCtveInterpolating:
        interpolate(a, dt);
        return;
    default:
        return; // the rest move only their copies
    }
}

// Along a path of interpolation points (physPathing, CTVE_physPathing):
// PhysAlpha on from Target to its Next at PhysRate a second, times the
// two points' RateModifiers blended by the way along (backward with a
// negative rate). With a point before and after, the place is a spline
// through the four (weights x^4/16 - x^2/2 + 1 at t+1, t, t-1, t-2,
// made to sum to 1), as is the turn (mode 8: the rotators' numbers as
// they are; mode 9: slerped between the two); else straight between,
// the turn's numbers too. Past a point: the point's InterpolateEnd, then
// the actor's, then on to the next (or back to the one before),
// skipping those with bSkipNextPath, with the time left over. A pawn's
// ViewRotation follows; the velocity is what it moved.
void ScriptLevel::interpolate(VmObject* a, float dt) {
    const Value* on = prop(a, "bInterpolating");
    Value* target = prop(a, "Target");
    Value* alpha = prop(a, "PhysAlpha");
    const Value* rateValue = prop(a, "PhysRate");
    const float physRate = rateValue ? rateValue->asFloat() : 0;
    if (!on || !on->asBool() || !target || !target->asObject() || !alpha || physRate == 0 || dt <= 0)
        return;
    const bool ctve = (prop(a, "Physics") ? prop(a, "Physics")->asInt() : 0) == kPhysCtveInterpolating;
    auto obj = [&](VmObject* o, const char* name) -> VmObject* {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? v->asObject() : nullptr;
    };
    auto num = [&](VmObject* o, const char* name, float otherwise) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? v->asFloat() : otherwise;
    };
    auto place = [&](VmObject* o) {
        const Value* v = o ? prop(o, "Location") : nullptr;
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto turn = [&](VmObject* o) {
        const Value* v = o ? prop(o, "Rotation") : nullptr;
        return v ? Vm::rotatorOf(*v) : std::array<int32_t, 3>{};
    };
    const Vec start = place(a);
    float left = dt;
    for (int guard = 0; guard < 16 && left > 0; ++guard) {
        VmObject* from = target->asObject();
        if (!from)
            break;
        VmObject* to = obj(from, "Next");
        if (!to) {
            if (physRate > 0) // the path's end: held there
                break;
            to = from;
        }
        const float t0 = alpha->asFloat();
        const float rate = num(from, "RateModifier", 1) + (num(to, "RateModifier", 1) - num(from, "RateModifier", 1)) * t0;
        const float step = physRate * rate * left;
        float t = t0 + step;
        const bool past = t > 1 || t < 0;
        float spent = left;
        if (past && step != 0) {
            const float over = t > 1 ? t - 1 : -t;
            spent = left * (1 - over / std::fabs(step));
            t = t > 1 ? 1.0f : 0.0f;
        }
        t = std::clamp(t, 0.0f, 1.0f);
        // Where it is now.
        VmObject* before = obj(from, "Prev");
        VmObject* after = obj(to, "Next");
        Vec at;
        std::array<int32_t, 3> rot{};
        const auto r1 = turn(from), r2 = turn(to);
        if (before && after) {
            auto w = [](float x) { return x * x * x * x / 16 - x * x / 2 + 1; };
            float k[4] = {w(t + 1), w(t), w(t - 1), w(t - 2)};
            const float sum = k[0] + k[1] + k[2] + k[3];
            for (float& q : k)
                q /= sum;
            VmObject* pts[4] = {before, from, to, after};
            at = {};
            for (int i = 0; i < 4; ++i)
                at = add(at, mul(place(pts[i]), k[i]));
            if (ctve) {
                const Rotator r = slerpRotators({r1[0], r1[1], r1[2]}, {r2[0], r2[1], r2[2]}, t);
                rot = {r.pitch, r.yaw, r.roll};
            } else {
                for (size_t c = 0; c < 3; ++c) {
                    float sumR = 0;
                    for (int i = 0; i < 4; ++i)
                        sumR += float(turn(pts[i])[c]) * k[i];
                    rot[c] = int32_t(sumR);
                }
            }
        } else {
            at = add(place(from), mul(sub(place(to), place(from)), t));
            for (size_t c = 0; c < 3; ++c)
                rot[c] = r1[c] + int32_t(float(r2[c] - r1[c]) * t);
        }
        setLocation(a, at);
        if (a->deleted)
            return;
        for (const char* name : {"Rotation", "ViewRotation"}) {
            if (std::string(name) == "ViewRotation" && !vm_.isA(a, "Pawn"))
                continue;
            if (Value* p = prop(a, name))
                *p = vm_.rotatorValue(rot[0] & 0xffff, rot[1] & 0xffff, rot[2] & 0xffff);
        }
        alpha->f = t;
        // The points' game speed (the level's TimeDilation), and for a
        // player its view's field (its class's FovAngle times the points'
        // FovModifier) and screen flash, blended by the way along.
        auto lerp = [&](const char* name, float otherwise) {
            const float a0 = num(from, name, otherwise);
            return a0 + (num(to, name, otherwise) - a0) * t;
        };
        if (VmObject* li = levelInfo_)
            if (Value* d = prop(li, "TimeDilation"))
                *d = Value::floatValue(lerp("GameSpeedModifier", 1));
        if (vm_.isA(a, "PlayerPawn")) {
            const float flash = lerp("ScreenFlashScale", 1);
            const Value* f0 = prop(from, "ScreenFlashFog");
            const Value* f1 = prop(to, "ScreenFlashFog");
            const Vec fog0 = f0 ? Vm::vectorOf(*f0) : Vec{}, fog1 = f1 ? Vm::vectorOf(*f1) : Vec{};
            const Vec fog = add(fog0, mul(sub(fog1, fog0), t));
            for (const char* name : {"FlashScale"})
                if (Value* p = prop(a, name))
                    vm_.setVector(*p, flash, flash, flash);
            if (Value* p = prop(a, "DesiredFlashScale"))
                *p = Value::floatValue(flash);
            for (const char* name : {"FlashFog", "DesiredFlashFog"})
                if (Value* p = prop(a, name))
                    vm_.setVector(*p, fog[0], fog[1], fog[2]);
            if (const VmProperty* fp = a->cls->findProperty(vm_.name("FovAngle"));
                fp && size_t(fp->slot) < a->cls->defaults.size())
                if (Value* p = prop(a, "FovAngle"))
                    *p = Value::floatValue(a->cls->defaults[size_t(fp->slot)].asFloat() * lerp("FovModifier", 1));
        }
        left -= spent;
        if (!past)
            break;
        // There: the point and the actor told, then on.
        const bool forward = physRate * rate >= 0;
        alpha->f = forward ? 0.0f : 1.0f;
        vm_.callEvent(from, "InterpolateEnd", {Value::objectValue(a)});
        if (a->deleted)
            return;
        if (!ctve || target->asObject())
            vm_.callEvent(a, "InterpolateEnd", {Value::objectValue(from)});
        if (a->deleted || !prop(a, "bInterpolating")->asBool())
            break;
        VmObject* next = forward ? obj(from, "Next") : obj(from, "Prev");
        for (int skip = 0; next && skip < 64; ++skip) {
            const Value* sk = prop(next, "bSkipNextPath");
            if (!sk || !sk->asBool())
                break;
            next = forward ? obj(next, "Next") : obj(next, "Prev");
        }
        if (!next)
            break;
        *target = Value::objectValue(next);
    }
    if (Value* vv = prop(a, "Velocity")) {
        const Vec moved = mul(sub(place(a), start), 1 / dt);
        vm_.setVector(*vv, moved[0], moved[1], moved[2]);
    }
}

} // namespace reluva::unreal
