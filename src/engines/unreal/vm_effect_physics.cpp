// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's own physics modes (past PHYS_MovingBrush), for a running
// level (ScriptLevel): how each kind of effect moves its copies (its
// CTVE_MultiMesh list) and, for a few, itself; and the flight and
// interpolation modes some actors use. Each is Reluva's reading of what
// the effect's scripts set and leave undone; the original code isn't
// known. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include <algorithm>
#include <cmath>
#include <random>

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
    kPhysSwarmObject = 33,
    kPhysWaterRing = 34,
    kPhysFish = 35,
    kPhysSmoke = 36,
    kPhysLava = 37,
    kPhysGasCloudM4 = 38,
    kPhysBeamSustain = 39,
    kPhysExpTrail = 43,
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
        *p = vm_.vectorValue(v[0], v[1], v[2]);
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
            *vel = vm_.vectorValue(v[0], v[1], v[2]);
        }
        if (move && (v[0] != 0 || v[1] != 0 || v[2] != 0)) {
            const Vec l = add(Vm::vectorOf(*loc), mul(v, dt));
            *loc = vm_.vectorValue(l[0], l[1], l[2]);
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

    switch (physics) {
    // Placed by their scripts or natives each tick (arcs, beams, the
    // gas cloud's spheres): only aged.
    case kPhysSpark:
    case kPhysBeamSustain:
        return;
    case kPhysGasCloudM4:
    case kPhysEmPulseRifleQuad:
        for (VmObject* mm : copies) {
            const float age = copyAge(mm);
            if (physics == kPhysGasCloudM4) // fading as they go
                setCopyFloat(mm, "ScaleGlow", f(mm, "ScaleGlow") * (1 - age) / std::max(0.05f, 1 - copyAgeBefore(mm, age, dt)));
            else // a scorch on the wall: flaring up, then fading
                setCopyFloat(mm, "ScaleGlow", std::sin(age * kPi));
            ageCopy(mm, dt);
        }
        return;
    // The pulse rifle's trail rings: still, growing and fading (its
    // Acceleration.Z is the shot's power, not an acceleration).
    case kPhysEmPulseRifle:
        for (VmObject* mm : copies) {
            const float power = vec(mm, "Acceleration")[2];
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + (0.5f + power) * dt);
            setCopyFloat(mm, "ScaleGlow", 1 - copyAge(mm));
            ageCopy(mm, dt);
        }
        return;
    // Fish in a tank (the base's Vect1-Vect2 box): each wanders to a
    // place in it and on to another, darting while it's been startled
    // (the base's bUse); facing the way it swims.
    case kPhysFishInTank: {
        const Vec lo = vec(base, "Vect1"), hi = vec(base, "Vect2");
        const bool startled = b(base, "bUse");
        const float speed = startled ? 120 : 30, accel = startled ? 400 : 60;
        auto inside = [&] {
            return Vec{lo[0] + (hi[0] - lo[0]) * frand(), lo[1] + (hi[1] - lo[1]) * frand(),
                       lo[2] + (hi[2] - lo[2]) * frand()};
        };
        for (VmObject* mm : copies) {
            Vec target = vec(mm, "Vect1");
            Vec at = vec(mm, "Location");
            if ((target[0] == 0 && target[1] == 0 && target[2] == 0) || length(sub(target, at)) < 8 ||
                (startled && frand() < dt * 2))
                setCopyVec(mm, "Vect1", target = inside());
            Vec v = add(vec(mm, "Velocity"), mul(normal(sub(target, at)), accel * dt));
            if (length(v) > speed)
                v = mul(normal(v), speed);
            at = add(at, mul(v, dt));
            for (int k = 0; k < 3; ++k)
                at[k] = std::clamp(at[k], std::min(lo[k], hi[k]), std::max(lo[k], hi[k]));
            setCopyVec(mm, "Velocity", v);
            setCopyVec(mm, "Location", at);
            if (Value* r = prop(mm, "Rotation"); r && length(v) > 1)
                *r = vm_.rotatorValue(units(std::atan2(v[2], std::hypot(v[0], v[1]))), units(std::atan2(v[1], v[0])), 0);
        }
        return;
    }
    // A fan's stream (its generator's X and Y the disc, Z the axis times
    // its reach; Acceleration.X which side it blows, Z how fast): each
    // copy flows from its place on the disc along the axis (Phase, -1 to
    // 1 of the reach), fading toward the end, gone past it.
    case kPhysFanParticle: {
        const Vec axis = vec(owner, "Acceleration");
        const float reach = std::max(1.0f, length(vec(copies.front(), "Vect2")));
        for (VmObject* mm : copies) {
            float phase = f(mm, "Phase");
            const float side = axis[0] < 0 ? -1 : axis[0] > 0 ? 1 : (phase < 0 ? -1 : 1);
            phase += side * axis[2] / reach * dt;
            if (std::fabs(phase) > 1 || (axis[0] > 0 && phase < 0) || (axis[0] < 0 && phase > 0)) {
                copyStarts_.erase(mm);
                vm_.callEvent(mm, "Delete");
                continue;
            }
            setCopyFloat(mm, "Phase", phase);
            const Vec at = add(add(vec(owner ? owner : actor, "Location"), vec(mm, "Vect1")), mul(vec(mm, "Vect2"), phase));
            setCopyVec(mm, "Location", at);
            setCopyFloat(mm, "ScaleGlow", f(owner, "MaxScaleGlow", 1) * (1 - std::fabs(phase)));
            ageCopy(mm, dt);
        }
        return;
    }
    // Steam spiralling out (the base: Velocity, Vect1 and Vect2 its X, Y
    // and Z axes, Use3 its speed, Phase how fast it widens, Use1 how fast
    // it grows, Use2 its brightness): each copy out along X, round and
    // round (Angle) on a widening circle, growing and fading.
    case kPhysSpiralJet:
        for (VmObject* mm : copies) {
            const float age = f(mm, "Phase") + dt;
            setCopyFloat(mm, "Phase", age);
            const float angle = f(mm, "Angle") + 2 * kPi * dt;
            setCopyFloat(mm, "Angle", angle);
            const float out = f(base, "Use3") * age;
            const Vec circle = add(mul(vec(base, "Vect1"), std::cos(angle)), mul(vec(base, "Vect2"), std::sin(angle)));
            setCopyVec(mm, "Location", add(add(origin, mul(vec(base, "Velocity"), out)), mul(circle, f(base, "Phase") * out)));
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + f(base, "Use1") * dt);
            setCopyFloat(mm, "ScaleGlow", f(base, "Use2") * (1 - copyAge(mm)));
            ageCopy(mm, dt);
        }
        return;
    // Fire's smoke puffs (the fire, their holder's owner: its DrawScale
    // their size; the holder's ScaleGlow how fast they rise): each grows
    // from nothing as it rises, fading, gone after its life (or 1.7
    // seconds, for those given none).
    case kPhysFireGas: {
        const float size = f(owner, "DrawScale", 1), rise = 40 * f(actor, "ScaleGlow", 1);
        for (VmObject* mm : copies) {
            const float age = f(mm, "Phase") + dt;
            setCopyFloat(mm, "Phase", age);
            float life = f(mm, "LifeSpan");
            const float span = life > 0 ? std::max(life + age, 0.01f) : 1.7f;
            const float t = std::clamp(age / span, 0.0f, 1.0f);
            setCopyFloat(mm, "DrawScale", size * (0.3f + 0.7f * t));
            setCopyFloat(mm, "ScaleGlow", 0.5f * (1 - t)); // many overlap: half as bright
            setCopyFloat(mm, "Angle", f(mm, "Angle") + dt);
            if (life <= 0 && age >= span) {
                copyStarts_.erase(mm);
                vm_.callEvent(mm, "Delete");
                continue;
            }
            stepCopy(mm, dt, true, true, {0, 0, rise});
        }
        return;
    }
    default:
        break;
    }

    // The rest fly by their velocity and acceleration, and age; besides:
    // A generator's (the holder's owner): growing by its DeltaSize a
    // second; fading with bFadeGlow; shrinking away with bFadeSize when
    // it doesn't grow; blown by its wind.
    const bool generated = owner && vm_.isA(owner, "CTVE_Generator") &&
                           (physics == kPhysParticle || physics == kPhysSmoke || physics == kPhysSteamJet);
    const float deltaSize = generated ? f(owner, "DeltaSize") : 0;
    const Vec wind = generated && b(owner, "bIsWindy") ? vec(owner, "WindForce") : Vec{};
    // Bubbles: up to the water's surface (their generator's SeedLocation),
    // where they pop.
    const float surface = physics == kPhysBubble && owner ? vec(owner, "SeedLocation")[2] : 0;
    // Explosions' particles and cinders: falling by their forces, cinders
    // also jostled (their Phase: the explosion's chaos).
    Vec forces{};
    if (physics == kPhysExpParticle)
        forces = vec(actor, "ParticleForces");
    else if (physics == kPhysExpCinder)
        forces = vec(actor, "wind");
    for (VmObject* mm : copies) {
        const float age = copyAge(mm);
        Vec extra = wind;
        switch (physics) {
        case kPhysParticle:
        case kPhysSmoke:
        case kPhysSteamJet:
            if (generated) {
                if (deltaSize != 0)
                    setCopyFloat(mm, "DrawScale", std::max(0.0f, f(mm, "DrawScale") + deltaSize * dt));
                else if (b(owner, "bFadeSize") && age > 0)
                    setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") * (1 - age) / std::max(0.05f, 1 - copyAgeBefore(mm, age, dt)));
                if (b(owner, "bFadeGlow"))
                    setCopyFloat(mm, "ScaleGlow", f(owner, "MaxScaleGlow", 1) * (1 - age));
            }
            if (physics == kPhysSteamJet) {
                // A puff slows to a stop and hangs there.
                const Vec v = vec(mm, "Velocity"), a = vec(mm, "Acceleration");
                if (dot(v, a) > 0) {
                    setCopyVec(mm, "Velocity", {0, 0, 0});
                    setCopyVec(mm, "Acceleration", {0, 0, 0});
                    if (Value* u = prop(mm, "bUse"))
                        *u = Value::boolValue(true);
                }
                if (!generated)
                    setCopyFloat(mm, "ScaleGlow", 0.25f * (1 - age));
            }
            break;
        case kPhysHotSpark:
        case kPhysCtveSparks:
        case kPhysDisintegrationPhaser:
        case kPhysLava:
            if (physics == kPhysCtveSparks)
                setCopyFloat(mm, "ScaleGlow", f(actor, "MaxScaleGlow", 1) * (1 - age));
            else if (physics != kPhysLava)
                setCopyFloat(mm, "ScaleGlow", (1 - age));
            if (physics == kPhysHotSpark)
                if (Value* r = prop(mm, "Rotation")) {
                    const Vec v = vec(mm, "Velocity");
                    *r = vm_.rotatorValue(units(std::atan2(v[2], std::hypot(v[0], v[1]))), units(std::atan2(v[1], v[0])), 0);
                }
            break;
        case kPhysExhaustJet:
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + f(base, "Use1") * dt);
            setCopyFloat(mm, "ScaleGlow", f(base, "Use2", 1) * (1 - age));
            break;
        case kPhysPlasmaThrower: {
            // Growing to the thrower's size as it flies, fading.
            const float full = f(actor, "DrawScale", 1);
            setCopyFloat(mm, "DrawScale", 0.1f + (full - 0.1f) * age);
            setCopyFloat(mm, "ScaleGlow", 1 - age * age);
            break;
        }
        case kPhysWaterRing:
            // Spreading by its Angle a second, fading (Phase: how bright).
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + f(mm, "Angle") * dt);
            setCopyFloat(mm, "ScaleGlow", std::max(0.0f, f(mm, "Phase", 1)) * 0.6f * (1 - age));
            break;
        case kPhysBubble:
            if (owner && vec(mm, "Location")[2] >= surface && surface != 0) {
                if (Value* e = prop(owner, "bMMEvent"))
                    *e = Value::boolValue(true);
                if (Value* e = prop(owner, "MMEventLocation")) {
                    const Vec l = vec(mm, "Location");
                    *e = vm_.vectorValue(l[0], l[1], surface);
                }
                copyStarts_.erase(mm);
                vm_.callEvent(mm, "Delete");
                continue;
            }
            // Sideways drift dies down; a little wobble.
            setCopyVec(mm, "Velocity", [&] {
                Vec v = vec(mm, "Velocity");
                const float k = std::max(0.0f, 1 - 2 * dt);
                v[0] = v[0] * k + (frand() - 0.5f) * 40 * dt;
                v[1] = v[1] * k + (frand() - 0.5f) * 40 * dt;
                return v;
            }());
            break;
        case kPhysExpTrail: {
            // Fire to smoke (the base's Vect1 to Vect2), fading over its
            // life (the base's Phase).
            const float span = std::max(0.01f, f(base, "Phase", 1));
            const float t = std::clamp(1 - f(mm, "LifeSpan") / span, 0.0f, 1.0f);
            const Vec c = add(mul(vec(base, "Vect1"), 1 - t), mul(vec(base, "Vect2"), t));
            setCopyVec(mm, "ColorFilter", c);
            setCopyFloat(mm, "ScaleGlow", f(base, "ScaleGlow", 1) * (1 - t));
            break;
        }
        case kPhysExpSmoke:
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + f(mm, "Phase") * dt);
            setCopyFloat(mm, "ScaleGlow", 1 - age);
            break;
        case kPhysExpColumn:
            setCopyFloat(mm, "DrawScale", f(mm, "DrawScale") + f(mm, "Angle") * dt);
            setCopyFloat(mm, "ScaleGlow", std::clamp(f(mm, "LifeSpan") / std::max(0.01f, f(mm, "Phase", 1)), 0.0f, 1.0f));
            break;
        case kPhysExpParticle:
        case kPhysExpCinder:
            extra = add(extra, forces);
            if (physics == kPhysExpCinder)
                extra = add(extra, mul(vrand(), f(mm, "Phase") * 0.5f));
            setCopyFloat(mm, "ScaleGlow", 1 - age * age);
            // The base's bUse: some sparkle, the flicker colour (the
            // base's ColorFilter) against their own (Vect1).
            if (b(base, "bUse")) {
                const bool flick = frand() < 0.1f;
                setCopyVec(mm, "ColorFilter", flick ? vec(base, "ColorFilter") : vec(base, "Vect1"));
            }
            break;
        default:
            break;
        }
        stepCopy(mm, dt, true, true, extra);
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
        // Following its owner, as UE1's trailers do.
        const Value* o = prop(a, "Owner");
        VmObject* owner = o ? o->asObject() : nullptr;
        if (!owner || owner->deleted)
            return;
        const Value* at = prop(owner, "Location");
        if (!at)
            return;
        Vec to = Vm::vectorOf(*at);
        const Value* prePivot = prop(a, "bTrailerPrePivot");
        if (prePivot && prePivot->asBool())
            to = add(to, prop(a, "PrePivot") ? Vm::vectorOf(*prop(a, "PrePivot")) : Vec{});
        *loc = vm_.vectorValue(to[0], to[1], to[2]);
        updateRegion(a);
        const Value* same = prop(a, "bTrailerSameRotation");
        if (same && same->asBool())
            if (Value* r = prop(a, "Rotation"); r && prop(owner, "Rotation"))
                *r = *prop(owner, "Rotation");
        return;
    }
    case kPhysRotatingYaw:
    case kPhysRotatingPitch: {
        // Turning about one axis only, by its RotationRate.
        Value* r = prop(a, "Rotation");
        const Value* rate = prop(a, "RotationRate");
        if (!r || !rate)
            return;
        auto now = Vm::rotatorOf(*r);
        const auto by = Vm::rotatorOf(*rate);
        const int k = physics == kPhysRotatingYaw ? 1 : 0;
        now[size_t(k)] = (now[size_t(k)] + int32_t(std::lround(float(by[size_t(k)]) * dt))) & 0xffff;
        *r = vm_.rotatorValue(now[0], now[1], now[2]);
        return;
    }
    case kPhysParticle: {
        // Drifting by its velocity and acceleration, no gravity (a ship
        // lifting off, debris settled); against the world, landing on
        // floors. One that makes copies stays where its scripts put it.
        if (const Value* mm = prop(a, "MultiMesh"); mm && mm->asObject())
            return;
        v = add(v, mul(accel, dt));
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        if (length(v) < 1e-3f)
            return;
        const MoveHit h = moveActor(a, mul(v, dt));
        if (!a->deleted && h.hit()) {
            if (h.normal[2] >= 0.7f)
                vm_.callEvent(a, "Landed", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2])});
            else
                hitWall(h);
        }
        return;
    }
    case kPhysNewtonian: {
        // Free flight by momentum: the acceleration (a goal's, or the
        // scripts') changes the velocity, up to AirSpeed; no gravity, no
        // friction (the scripts brake and fall themselves).
        v = add(v, mul(accel, dt));
        const float most = f("AirSpeed", 0);
        if (most > 0 && length(v) > most)
            v = mul(normal(v), most);
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        if (length(v) > 1e-3f)
            hitWall(moveSmooth(a, mul(v, dt)));
        return;
    }
    case kPhysFish: {
        // Swimming toward where it's going (its acceleration, at its
        // AccelRate) up to WaterSpeed, against the water's friction;
        // no gravity; facing the way it swims.
        const float rate = f("AccelRate", 500), most = std::max(1.0f, f("WaterSpeed", 200));
        const Vec push = length(accel) > 1e-3f ? mul(normal(accel), rate) : Vec{};
        v = add(mul(v, std::max(0.0f, 1 - dt)), mul(push, dt));
        if (length(v) > most)
            v = mul(normal(v), most);
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        if (length(v) < 1e-3f)
            return;
        hitWall(moveSmooth(a, mul(v, dt)));
        if (!a->deleted)
            if (Value* r = prop(a, "Rotation")) {
                auto now = Vm::rotatorOf(*r);
                now[0] = units(std::atan2(v[2], std::hypot(v[0], v[1])));
                now[1] = units(std::atan2(v[1], v[0]));
                *r = vm_.rotatorValue(now[0], now[1], now[2]);
            }
        return;
    }
    case kPhysExpTrail:
        // An explosion's trail flies by its velocity and acceleration,
        // through everything.
        v = add(v, mul(accel, dt));
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        {
            const Vec l = add(Vm::vectorOf(*loc), mul(v, dt));
            *loc = vm_.vectorValue(l[0], l[1], l[2]);
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

// Along a path of interpolation points (Target, then each one's Next):
// PhysAlpha on from one point to the next at PhysRate a second (times the
// point's RateModifier), placed and turned between them; at each,
// InterpolateEnd (its scripts end the path at the last, or turn it off).
void ScriptLevel::interpolate(VmObject* a, float dt) {
    const Value* on = prop(a, "bInterpolating");
    Value* target = prop(a, "Target");
    Value* alpha = prop(a, "PhysAlpha");
    if (!on || !on->asBool() || !target || !target->asObject() || !alpha)
        return;
    VmObject* from = target->asObject();
    const Value* nextValue = prop(from, "Next");
    VmObject* to = nextValue ? nextValue->asObject() : nullptr;
    const Value* rateMod = prop(from, "RateModifier");
    const float rate = (prop(a, "PhysRate") ? prop(a, "PhysRate")->asFloat() : 1) * (rateMod ? rateMod->asFloat() : 1);
    alpha->f += dt * rate;
    if (alpha->f >= 1 || !to) {
        alpha->f = to ? alpha->f - 1 : 0;
        if (!to)
            alpha->f = 0;
        // There: the point and the actor told, then on to the next.
        vm_.callEvent(from, "InterpolateEnd", {Value::objectValue(a)});
        if (a->deleted)
            return;
        vm_.callEvent(a, "InterpolateEnd", {Value::objectValue(from)});
        if (a->deleted || !prop(a, "bInterpolating")->asBool() || !target->asObject())
            return;
        if (!to) {
            *target = Value::objectValue(nullptr);
            return;
        }
        *target = Value::objectValue(to);
        if (const Value* skip = prop(to, "bSkipNextPath"); skip && skip->asBool())
            if (const Value* n = prop(to, "Next"); n && n->asObject()) {
                *target = Value::objectValue(n->asObject());
                alpha->f = 0;
            }
        from = target->asObject();
        nextValue = prop(from, "Next");
        to = nextValue ? nextValue->asObject() : nullptr;
    }
    const Value* fromLoc = prop(from, "Location");
    if (!fromLoc)
        return;
    const float t = std::clamp(alpha->f, 0.0f, 1.0f);
    Vec at = Vm::vectorOf(*fromLoc);
    const Value* toLoc = to ? prop(to, "Location") : nullptr;
    if (toLoc)
        at = add(at, mul(sub(Vm::vectorOf(*toLoc), at), t));
    if (Value* l = prop(a, "Location")) {
        *l = vm_.vectorValue(at[0], at[1], at[2]);
        updateRegion(a);
    }
    const Value* fromRot = prop(from, "Rotation");
    const Value* toRot = to ? prop(to, "Rotation") : nullptr;
    if (fromRot) {
        auto r = Vm::rotatorOf(*fromRot);
        if (toRot) {
            const auto r2 = Vm::rotatorOf(*toRot);
            for (size_t k = 0; k < 3; ++k) {
                int32_t d = (r2[k] - r[k]) & 0xffff;
                if (d > 32767)
                    d -= 65536;
                r[k] = r[k] + int32_t(std::lround(float(d) * t));
            }
        }
        for (const char* name : {"Rotation", "ViewRotation"})
            if (Value* p = prop(a, name))
                *p = vm_.rotatorValue(r[0], r[1], r[2]);
    }
}

} // namespace reluva::unreal
