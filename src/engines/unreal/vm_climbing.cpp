// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's player climbing, for a running level (ScriptLevel): ledges
// and ladders found as the player walks or jumps into them, and its three
// physics modes for them: hanging from a ledge (PHYS_Hanging), climbing a
// ladder (PHYS_Climbing) and pulling up onto a ledge (PHYS_Clambering).
// The engine finds them and asks the player's scripts (ChangePhysics); the
// scripts' states play the animations and turn the input into
// Acceleration. As Engine.dll's ACTVE_PlayerPawn (TestForLedge,
// CheckClimbing, TestStepUp, TestForHangLedge, TestForHandHold and the
// three physics), read from its code and written anew; where Reluva's
// collision has no counterpart (BSP node items, a ladder's edges) it does
// without. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;
constexpr int kPhysWalking = 1, kPhysFalling = 2, kPhysClambering = 13, kPhysClimbing = 14, kPhysHanging = 15;
constexpr int kLadderTag = 1; // Texture.TexTag's TT_Ladder

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec& a) { return std::sqrt(dot(a, a)); }
Vec flat(const Vec& a) {
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1]);
    return l > 1e-4f ? Vec{a[0] / l, a[1] / l, 0} : Vec{0, 0, 0};
}
Vec unit(const Vec& a) {
    const float l = length(a);
    return l > 1e-4f ? mul(a, 1 / l) : Vec{0, 0, 0};
}
// A rotator (65536 a turn) facing along a direction, no roll.
std::array<int32_t, 3> facing(const Vec& d) {
    const float pitch = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
    const float yaw = std::atan2(d[1], d[0]);
    return {int32_t(std::lround(pitch * 32768 / kPi)) & 0xffff, int32_t(std::lround(yaw * 32768 / kPi)) & 0xffff, 0};
}

} // namespace


float ScriptLevel::climbFloat(VmObject* a, const char* name, float otherwise) {
    const Value* v = prop(a, name);
    return v ? v->asFloat() : otherwise;
}

// A line or box swept from `from` to `to` against the world and its movers.
ScriptLevel::Sweep ScriptLevel::sweep(VmObject* a, const Vec& from, const Vec& to, const Vec& extent) {
    Sweep s;
    Vec at, n;
    s.actor = trace(a, from, to, extent, false, at, n);
    if (!s.actor) {
        s.at = to;
        return s;
    }
    const float l = length(sub(to, from));
    s.time = l > 1e-4f ? std::clamp(length(sub(at, from)) / l, 0.0f, 1.0f) : 0;
    s.at = at;
    s.normal = n;
    return s;
}

// The player's view axis X (Rotation, with its pitch).
std::array<float, 3> ScriptLevel::viewX(VmObject* a) {
    const Value* r = prop(a, "Rotation");
    if (!r)
        return {1, 0, 0};
    const auto rot = Vm::rotatorOf(*r);
    const auto x = rotationAxes({rot[0], rot[1], rot[2]})[0];
    return {x[0], x[1], x[2]};
}

// The CTVE_PlayerPawn class's own default CollisionRadius (Engine.dll
// reads the class's defaults, not the subclass's).
float ScriptLevel::defaultRadius(VmObject* a) {
    for (const VmClass* c = a->cls; c; c = c->parent())
        if (c->name.size() == 15 && std::equal(c->name.begin(), c->name.end(), "ctve_playerpawn",
                                               [](char p, char q) { return std::tolower((unsigned char)p) == q; }))
            if (VmProperty* p = c->findProperty(vm_.name("CollisionRadius")); p && size_t(p->slot) < c->defaults.size())
                return c->defaults[size_t(p->slot)].asFloat();
    return climbFloat(a, "CollisionRadius", 17);
}

// TestStepUp: up `stepHeight` + 1 from `start` (a box `extent`), on by
// `forward`, then down onto a floor (0.65 or flatter). On one, the ledge
// is told: LedgeHeight (the feet on it), LedgeNormal (its floor's normal),
// LedgeItem, and with `height` the room over it as a half-height
// (HeadRoom). How far on it got (0-1), 0 if nowhere.
float ScriptLevel::testStepUp(VmObject* a, float stepHeight, float height, const Vec& start, const Vec& forward,
                              const Vec& extent) {
    auto set = [&](const char* name, const Value& v) {
        if (Value* p = prop(a, name))
            *p = v;
    };
    set("LedgeHeight", Value::floatValue(start[2] - extent[2]));
    const Sweep up = sweep(a, start, add(start, {0, 0, stepHeight + 1}), extent);
    const float climbed = (stepHeight + 1) * up.time;
    const Vec top = add(start, {0, 0, climbed});
    const Sweep on = sweep(a, top, add(top, forward), extent);
    const float fwd = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    if (!(on.time * fwd >= 3 || on.time >= 0.99f))
        return 0;
    const Sweep down = sweep(a, on.at, add(on.at, {0, 0, -(climbed + 1)}), extent);
    if (!down.hit() || down.normal[2] <= 0.65f)
        return 0;
    set("LedgeItem", Value::intValue(down.actor == levelInfo_ ? 0 : -1));
    set("LedgeHeight", Value::floatValue(down.at[2] - extent[2]));
    set("LedgeNormal", vm_.vectorValue(down.normal[0], down.normal[1], down.normal[2]));
    if (height != 0) {
        const Sweep room = sweep(a, down.at, add(down.at, {0, 0, 2 * (height - extent[2])}), extent);
        set("HeadRoom", Value::floatValue(extent[2] + (height - extent[2]) * room.time));
    }
    return on.time;
}

// TestForStrongLedgeFooting: floor (0.9 or flatter) 2 units under the
// ledge's top just past the wall: 10, 20 or 24 units in from the player's
// side.
bool ScriptLevel::strongFooting(VmObject* a, const Vec& normal, const Vec& at) {
    const float radius = climbFloat(a, "CollisionRadius", 17), ledge = climbFloat(a, "LedgeHeight", 0);
    for (const float in : {10.0f, 20.0f, 24.0f}) {
        const Vec p{at[0] - normal[0] * (radius + in), at[1] - normal[1] * (radius + in), ledge + 1};
        const Sweep s = sweep(a, p, add(p, {0, 0, -2}), {});
        if (s.hit() && std::fabs(s.normal[2]) > 0.9f)
            return true;
    }
    return false;
}

// TestForHandHold: the wall facing the player 2 under the ledge's top,
// and floor under the hands 8 units to either side along it; the player's
// Base becomes what the hands are on.
bool ScriptLevel::handHold(VmObject* a, const Vec& normal, const Vec& at) {
    const float radius = climbFloat(a, "CollisionRadius", 17), ledge = climbFloat(a, "LedgeHeight", 0);
    const Vec from{at[0], at[1], ledge - 2};
    const Sweep wall = sweep(a, from, sub(from, mul(normal, 1.6f * radius)), {});
    if (!wall.hit() || dot(normal, wall.normal) <= 0.99f)
        return false;
    const Vec hands = add(sub(wall.at, mul(normal, 0.9f)), {0, 0, 3});
    const Vec side{-normal[1], normal[0], 0};
    VmObject* on = nullptr;
    for (const float k : {8.0f, -8.0f}) {
        const Vec p = add(hands, mul(side, k));
        const Sweep s = sweep(a, p, add(p, {0, 0, -2}), {});
        if (!s.hit() || std::fabs(s.normal[2]) <= 0.9f)
            return false;
        on = s.actor;
    }
    const Value* base = prop(a, "Base");
    if (on && (!base || base->asObject() != on))
        setBase(a, on);
    return true;
}

// TestForHangLedge: from the hands (`at` raised by StandCollisionHeight +
// ArmCollisionHeight) up clear by twice the hands' height, 4 units into
// the wall clear, and down onto the ledge; `at` ends there.
bool ScriptLevel::hangLedge(VmObject* a, const Vec& normal, Vec& at) {
    const float radius = climbFloat(a, "CollisionRadius", 17), hand = climbFloat(a, "HandCollisionHeight", 4);
    at[2] += climbFloat(a, "StandCollisionHeight", 49.5f) + climbFloat(a, "ArmCollisionHeight", 7.5f);
    const Vec e{radius, radius, hand};
    const Vec up = add(at, {0, 0, 2 * hand});
    if (sweep(a, at, up, e).hit())
        return false;
    const Vec in = sub(up, mul(normal, 4));
    if (sweep(a, up, in, e).hit())
        return false;
    const Sweep down = sweep(a, in, add(in, {0, 0, -2 * hand}), e);
    if (!down.hit())
        return false;
    at = down.at;
    return true;
}

// TestForLedge, for a walk blocked by `hit` with `dir` of it left: a
// ledge the player can pull up onto. Only head on to an upright wall
// (the move and the view both within -0.85 of it), not crouching, and not
// in a level without action unless climbing: then up to twice
// ArmCollisionHeight + StandCollisionHeight, a box of the ducked height
// from the feet if standing. A ledge above a step: deep enough (1.6 of
// the class's radius over 45 high, else 0.5), flat (0.9), with footing
// and a hand hold. Smaller steps are left to the walk's own step up.
bool ScriptLevel::testForLedge(VmObject* a, const Vec& hitNormal, const Vec& dir) {
    if (length(dir) < 1e-4f)
        return false;
    const Value* loc = prop(a, "Location");
    if (!loc)
        return false;
    const Vec at = Vm::vectorOf(*loc);
    const Vec f = flat(dir), x = viewX(a);
    const float dR = defaultRadius(a), s = 2 * dR;
    const float radius = climbFloat(a, "CollisionRadius", 17), height = climbFloat(a, "CollisionHeight", 49.5f);
    const float duck = climbFloat(a, "DuckCollisionHeight", 32.5f), step = climbFloat(a, "MaxStepHeight", 25);
    const float arm = climbFloat(a, "ArmCollisionHeight", 7.5f), stand = climbFloat(a, "StandCollisionHeight", 49.5f);
    const int physics = prop(a, "Physics") ? prop(a, "Physics")->asInt() : 0;
    const bool crouching = prop(a, "bIsCrouching") && prop(a, "bIsCrouching")->asBool();
    bool noAction = false;
    if (VmObject* li = levelInfo_)
        if (const Value* v = prop(li, "bNoActionLevel"))
            noAction = v->asBool();
    if (!(std::fabs(hitNormal[2]) < 0.1f && dot(f, hitNormal) < -0.85f && dot(x, hitNormal) < -0.85f && !crouching &&
          (!noAction || physics == kPhysClimbing)))
        return false;
    Vec start = at;
    Vec e{radius, radius, height};
    const Value* duckStage = prop(a, "DuckStage");
    if (height > duck + 1 && (!duckStage || duckStage->asInt() == 0) && physics == kPhysWalking) {
        start[2] = at[2] - height + duck + 1;
        e[2] = duck + 1;
    }
    const float r = testStepUp(a, 2 * (arm + stand), stand, start, mul(f, s), e);
    const float depth = r * s;
    if (Value* p = prop(a, "LedgeDepth"))
        *p = Value::floatValue(depth);
    const float rise = climbFloat(a, "LedgeHeight", 0) - (at[2] - height);
    if (rise <= step + 1 || r <= 0)
        return false;
    const Value* ln = prop(a, "LedgeNormal");
    const Value* item = prop(a, "LedgeItem");
    return depth > dR * (rise > 45 ? 1.6f : 0.5f) && ln && Vm::vectorOf(*ln)[2] > 0.9f && (!item || item->asInt() >= 0) &&
           strongFooting(a, hitNormal, at) && handHold(a, hitNormal, at);
}

// CheckClimbing: a ladder (its texture's TexTag TT_Ladder) 1.6 radii
// ahead at the feet (4 over them), the chest and/or the head (4 under
// it), as asked. With `snap`, ahead is into the wall met (upright, met
// head on), and the player is put against the ladder, 2 units off it.
bool ScriptLevel::checkClimbing(VmObject* a, const Vec& wallNormal, const Vec& dir, bool snap, bool feet, bool chest,
                                bool head) {
    if (const Value* c = prop(a, "bIsCrouching"); c && c->asBool())
        return false;
    const Value* loc = prop(a, "Location");
    if (!loc)
        return false;
    const float radius = climbFloat(a, "CollisionRadius", 17), height = climbFloat(a, "CollisionHeight", 49.5f);
    const Vec x = viewX(a);
    Vec ahead;
    if (snap) {
        if (std::fabs(wallNormal[2]) > 0.1f || dot(wallNormal, unit(dir)) > 0.85f || dot(wallNormal, x) > 0.85f)
            return false;
        ahead = mul(wallNormal, -1.6f * radius);
    } else {
        ahead = mul(x, 1.6f * radius);
    }
    const Vec at = Vm::vectorOf(*loc);
    auto ladderFrom = [&](const Vec& from) {
        VmObject* t = surfaceTexture(from, add(from, ahead));
        const Value* tag = t ? prop(t, "TexTag") : nullptr;
        return tag && tag->asInt() == kLadderTag;
    };
    if (feet && !ladderFrom({at[0], at[1], at[2] - height + 4}))
        return false;
    if (chest && !ladderFrom(at))
        return false;
    if (head && !ladderFrom({at[0], at[1], at[2] + height - 4}))
        return false;
    if (snap) {
        moveActor(a, ahead);
        if (a->deleted)
            return false;
        setLocation(a, add(Vm::vectorOf(*prop(a, "Location")), mul(wallNormal, 2)));
    }
    return true;
}

// Facing into the wall, then the player's scripts asked to change to
// `physics`; their Rotation put back if they say no.
bool ScriptLevel::askClimb(VmObject* a, const Vec& wallNormal, int physics) {
    Value* r = prop(a, "Rotation");
    const Value before = r ? *r : Value{};
    if (r) {
        const auto f = facing(mul(wallNormal, -1));
        *r = vm_.rotatorValue(f[0], f[1], f[2]);
    }
    const bool yes = vm_.callEvent(a, "ChangePhysics", {Value::byteValue(physics)}).asBool();
    if (!yes && r && !a->deleted)
        *r = before;
    return yes;
}

// A walking move of the player blocked by a wall (`hit`, with `left` of
// the move to go): a ledge to pull up onto (Clambering), else a ladder
// (Climbing). True if its scripts took it.
bool ScriptLevel::climbBlocked(VmObject* a, VmObject* by, const Vec& hitNormal, const Vec& left) {
    if (by != levelInfo_ || !vm_.isA(a, "CTVE_PlayerPawn"))
        return false;
    if (testForLedge(a, hitNormal, left))
        return askClimb(a, hitNormal, kPhysClambering) || true;
    if (checkClimbing(a, hitNormal, left, true, false, true, true))
        return askClimb(a, hitNormal, kPhysClimbing);
    return false;
}

// physFalling's catch, after a falling move of the player in its
// reaching jump (CollisionHeight = StandCollisionHeight +
// ArmCollisionHeight) moving toward what's 6 units ahead: a ladder there
// (Climbing), else a ledge its hands can hang from (Hanging), the player
// put there with the box's top at the ledge.
void ScriptLevel::climbCheck(VmObject* a, bool falling, float dt) {
    (void)dt;
    if (!falling || !vm_.isA(a, "CTVE_PlayerPawn"))
        return;
    const Value* loc = prop(a, "Location");
    if (!loc)
        return;
    const float radius = climbFloat(a, "CollisionRadius", 17), height = climbFloat(a, "CollisionHeight", 49.5f);
    const float arm = climbFloat(a, "ArmCollisionHeight", 7.5f), stand = climbFloat(a, "StandCollisionHeight", 49.5f);
    const float hand = climbFloat(a, "HandCollisionHeight", 4);
    if (std::fabs(height - (stand + arm)) > 0.01f)
        return;
    const Vec x = viewX(a), f6 = mul(x, 6);
    const Value* vel = prop(a, "Velocity");
    if (vel && dot(Vm::vectorOf(*vel), f6) <= -1)
        return;
    const Vec at = Vm::vectorOf(*loc);
    const Vec s = add(at, {0, 0, stand});
    const Sweep wall = sweep(a, s, add(s, f6), {radius, radius, arm + hand});
    if (!wall.hit() || std::fabs(wall.normal[2]) >= 0.1f || dot(x, wall.normal) >= 0.85f)
        return;
    const Vec n = wall.normal;
    if (checkClimbing(a, n, f6, true, false, true, true) && askClimb(a, n, kPhysClimbing))
        return;
    if (a->deleted)
        return;
    Vec p = add(add(s, mul(f6, wall.time)), n);
    p[2] -= arm;
    const float got = testStepUp(a, 2 * (arm + hand), stand, p, mul(x, radius), {radius, radius, hand});
    const float depth = got * radius;
    if (Value* d = prop(a, "LedgeDepth"))
        *d = Value::floatValue(depth);
    const Value* ln = prop(a, "LedgeNormal");
    if (got <= 0 || depth < 4 || !ln || Vm::vectorOf(*ln)[2] <= 0.9f)
        return;
    Vec hang{p[0], p[1], climbFloat(a, "LedgeHeight", 0) - stand - arm};
    const Vec e{radius, radius, height};
    if (sweep(a, at, hang, e).hit()) {
        const Vec nearer = add(at, mul(sub(hang, at), 0.98f));
        const float dx = nearer[0] - hang[0], dy = nearer[1] - hang[1];
        if (std::sqrt(dx * dx + dy * dy) > 4 || sweep(a, at, nearer, e).hit())
            return;
        hang = nearer;
    }
    Vec ledge = hang;
    if (!hangLedge(a, n, ledge) || !handHold(a, n, hang))
        return;
    if (askClimb(a, n, kPhysHanging) && !a->deleted)
        setLocation(a, hang);
}

// Moved straight there (the climbing moves are the animations'), with the
// camera's smoothing told so it eases after, as the scripts do for their
// own jumps of the player (LocationOffset).
void ScriptLevel::setLocation(VmObject* a, const Vec& to) {
    Value* loc = prop(a, "Location");
    if (!loc)
        return;
    const Vec from = Vm::vectorOf(*loc);
    vm_.setVector(*loc, to[0], to[1], to[2]);
    updateRegion(a);
    if (Value* off = prop(a, "LocationOffset")) {
        const Vec o = sub(Vm::vectorOf(*off), sub(to, from));
        vm_.setVector(*off, o[0], o[1], o[2]);
    }
}

void ScriptLevel::climbPhysics(VmObject* a, int physics, float dt) {
    Value* velocity = prop(a, "Velocity");
    Value* loc = prop(a, "Location");
    if (!velocity || !loc || dt <= 0)
        return;
    const Vec accel = prop(a, "Acceleration") ? Vm::vectorOf(*prop(a, "Acceleration")) : Vec{};
    const float height = climbFloat(a, "CollisionHeight", 49.5f);
    const Vec x = viewX(a);
    const Vec old = Vm::vectorOf(*loc);
    auto moved = [&] {
        if (a->deleted)
            return;
        const Vec v = mul(sub(Vm::vectorOf(*loc), old), 1 / dt);
        vm_.setVector(*velocity, v[0], v[1], v[2]);
    };

    if (physics == kPhysClimbing) {
        // physClimbing: by its acceleration while there's ladder at the
        // feet going down, at the head going up; onto a floor, landed. At
        // the top, 4 units on and a ledge there pulled up onto; at the
        // bottom with no ladder, fallen.
        const Vec d = mul(accel, dt);
        if (checkClimbing(a, {}, d, false, d[2] < 0, false, d[2] > 0)) {
            const MoveHit h = moveActor(a, d);
            if (!a->deleted && h.hit() && h.normal[2] > 0) {
                Value* v = prop(a, "Velocity");
                if (v) {
                    Vec now = Vm::vectorOf(*v);
                    vm_.setVector(*v, now[0], now[1], 0);
                }
                vm_.callEvent(a, "Landed", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2])});
                return;
            }
        } else if (d[2] > 0) {
            const MoveHit h = moveActor(a, mul(x, 4));
            if (!a->deleted && h.hit() && testForLedge(a, h.normal, mul(x, 4)) &&
                vm_.callEvent(a, "ChangePhysics", {Value::byteValue(kPhysClambering)}).asBool())
                return;
        } else if (vm_.callEvent(a, "ChangePhysics", {Value::byteValue(kPhysFalling)}).asBool()) {
            return;
        }
        moved();
        return;
    }

    if (physics == kPhysHanging) {
        // physHanging: along the ledge by its acceleration, a hand's box
        // swept over the ledge, as far as there's a hand hold; no ledge
        // under the hands, it falls (the scripts' Falling).
        Vec d = mul(accel, dt);
        const Vec n = flat(mul(x, -1));
        Vec p = Vm::vectorOf(*loc);
        if (!hangLedge(a, n, p)) {
            vm_.callEvent(a, "Falling");
        } else {
            const float radius = climbFloat(a, "CollisionRadius", 17), hand = climbFloat(a, "HandCollisionHeight", 4);
            d = mul(d, sweep(a, p, add(p, d), {radius, radius, hand}).time);
            if (handHold(a, n, add(Vm::vectorOf(*loc), d)))
                moveActor(a, d);
        }
        moved();
        return;
    }

    // Clambering, as ACTVE_PlayerPawn::physClambering: up at
    // DodgeClickTimer units a second (its scripts' PlayClambering sets
    // 100 or 130) until its feet are over the ledge (LedgeHeight), feeling
    // 4 units forward; once clear (or 4 over), Landed with what it felt
    // (its scripts set the rise to 0); then forward at 120 a second until
    // something stops it, when it walks. Blocked going up: it falls.
    vm_.setVector(*velocity, 0, 0, 0);
    if (Value* ac = prop(a, "Acceleration"))
        vm_.setVector(*ac, 0, 0, 0);
    const float rise = climbFloat(a, "DodgeClickTimer", 0);
    if (rise > 0) {
        const MoveHit up = moveActor(a, {0, 0, rise * dt});
        if (a->deleted)
            return;
        if (up.hit()) {
            vm_.callEvent(a, "ChangePhysics", {Value::byteValue(kPhysFalling)});
            return;
        }
    }
    const float over = Vm::vectorOf(*loc)[2] - height - climbFloat(a, "LedgeHeight", 0);
    if (over < 0)
        return;
    if (rise > 0) {
        const MoveHit ahead = moveActor(a, mul(x, 4));
        if (!a->deleted && (!ahead.hit() || over > 4))
            vm_.callEvent(a, "Landed", {vm_.vectorValue(ahead.normal[0], ahead.normal[1], ahead.normal[2])});
    } else {
        const MoveHit on = moveActor(a, mul(x, 120 * dt));
        if (!a->deleted && on.hit())
            vm_.callEvent(a, "ChangePhysics", {Value::byteValue(kPhysWalking)});
    }
}

// Where a pull up onto a ledge ends: standing on it, past the wall.
std::array<float, 3> ScriptLevel::Clamber::to(float height) const {
    return {from[0] + forward[0] * reach, from[1] + forward[1] * reach, ledge + height + 1};
}

// A pull up ended (its scripts walk again as its animation ends): on the
// ledge, wherever the animation had got to.
void ScriptLevel::endClamber(VmObject* a) {
    auto it = clambers_.find(a);
    if (it == clambers_.end())
        return;
    const float height = prop(a, "CollisionHeight") ? prop(a, "CollisionHeight")->asFloat() : 49.5f;
    const Vec to = it->second.to(height);
    clambers_.erase(it);
    if (clear(to, extentOf(a), a))
        setLocation(a, to);
}

} // namespace reluva::unreal
