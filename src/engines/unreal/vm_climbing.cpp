// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's player climbing, for a running level (ScriptLevel): ledges
// and ladders found as the player walks or jumps into them, and its three
// physics modes for them: hanging from a ledge (PHYS_Hanging), climbing a
// ladder (PHYS_Climbing) and pulling up onto a ledge (PHYS_Clambering).
// The engine finds them and asks the player's scripts (ChangePhysics); the
// scripts' states play the animations and turn the input into
// Acceleration. Reluva's reading of what the scripts expect; the original
// code isn't known. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;
constexpr float kWalkable = 0.7f;
constexpr int kPhysWalking = 1, kPhysFalling = 2, kPhysClambering = 13, kPhysClimbing = 14, kPhysHanging = 15;
constexpr int kLadderTag = 1;          // Texture.TexTag's TT_Ladder
constexpr float kClamberHighest = 100; // above the feet: the highest ledge the player pulls up onto at once
constexpr float kReach = 30;           // above the head: how high hands catch a ledge in a jump
constexpr float kPushTime = 0.15f;     // walking into a ledge this long climbs it

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec& a) { return std::sqrt(dot(a, a)); }
Vec flat(const Vec& a) {
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1]);
    return l > 1e-4f ? Vec{a[0] / l, a[1] / l, 0} : Vec{0, 0, 0};
}
float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

// What's ahead of the player along `dir` (flat): a wall within reach, a
// ladder on it, or a ledge on top of it no higher than `highest` above its
// feet, how deep the ledge is and how much room is over it (as a
// collision half-height: the scripts compare it with theirs).
bool ScriptLevel::findLedge(VmObject* a, const Vec& dir, float highest, Ledge& out) {
    const Value* loc = prop(a, "Location");
    if (!loc)
        return false;
    const Vec at = Vm::vectorOf(*loc);
    auto f = [&](const char* name, float otherwise) {
        const Value* v = prop(a, name);
        return v ? v->asFloat() : otherwise;
    };
    const float radius = f("CollisionRadius", 17), height = f("CollisionHeight", 49.5f);
    const float step = f("MaxStepHeight", 25);
    const float feet = at[2] - height;
    out = {};
    // A wall just above a step's height, facing the player.
    auto wallAt = [&](float z, Vec& hit, Vec& normal) {
        const Vec from{at[0], at[1], z};
        const Vec to = add(from, mul(dir, radius + 12));
        return trace(a, from, to, {}, false, hit, normal) == levelInfo_ && std::fabs(normal[2]) < 0.3f &&
               dot(flat(normal), dir) < -0.6f;
    };
    Vec hit, normal;
    if (!wallAt(feet + step + 2, hit, normal) && !wallAt(at[2] + height * 0.5f, hit, normal))
        return false;
    out.normal = flat(normal);
    out.wall = hit;
    // A ladder: the wall's texture says so, at the chest.
    {
        const Vec from{at[0], at[1], at[2] + height * 0.3f};
        const Vec to = add(from, mul(dir, radius + 12));
        if (VmObject* t = surfaceTexture(from, to))
            if (const Value* tag = prop(t, "TexTag"); tag && tag->asInt() == kLadderTag)
                out.ladder = true;
    }
    // The top: down onto it from above, just past the wall's face, with
    // nothing in the way from the player's side up there.
    const float top = feet + highest;
    const Vec over = sub(hit, mul(out.normal, 6));
    Vec h2, n2;
    const Vec aboveNear{hit[0] + out.normal[0], hit[1] + out.normal[1], top};
    if (trace(a, aboveNear, {over[0], over[1], top}, {}, false, h2, n2))
        return out.ladder; // the wall goes on up
    if (!trace(a, {over[0], over[1], top}, {over[0], over[1], feet + step}, {}, false, h2, n2) ||
        n2[2] < kWalkable || h2[2] >= top - 0.5f)
        return out.ladder;
    out.height = h2[2];
    // How deep: on along the top until something stops it.
    const float most = radius * 3;
    const Vec deepFrom{aboveNear[0], aboveNear[1], out.height + 4};
    Vec h3, n3;
    out.depth = trace(a, deepFrom, add(deepFrom, mul(dir, most)), {}, false, h3, n3) ? length(sub(h3, deepFrom)) : most;
    // How much room over it.
    const float room = 2 * f("StandCollisionHeight", height) + 4;
    const Vec upFrom{over[0], over[1], out.height + 1};
    out.headRoom =
        (trace(a, upFrom, add(upFrom, Vec{0, 0, room}), {}, false, h3, n3) ? h3[2] - upFrom[2] : room) / 2;
    out.ledge = true;
    return true;
}

// Told to the player's scripts before they're asked to climb: the
// ledge's height, depth, the room over it and the wall's normal.
void ScriptLevel::tellLedge(VmObject* a, const Ledge& l) {
    auto set = [&](const char* name, const Value& v) {
        if (Value* p = prop(a, name))
            *p = v;
    };
    set("LedgeHeight", Value::floatValue(l.height));
    set("LedgeDepth", Value::floatValue(l.depth));
    set("HeadRoom", Value::floatValue(l.headRoom));
    set("LedgeNormal", vm_.vectorValue(l.normal[0], l.normal[1], l.normal[2]));
}

// After a walking or falling move: the player pushing into a ladder
// climbs it; into a ledge it can pull up onto (no more than
// kClamberHighest above its feet, deep enough, with room over it), pulls
// up onto it after a moment; jumping at a higher one within its hands'
// reach, catches hold and hangs.
void ScriptLevel::climbCheck(VmObject* a, bool falling, float dt) {
    if (!vm_.isA(a, "CTVE_PlayerPawn"))
        return;
    const Value* accel = prop(a, "Acceleration");
    const Value* rot = prop(a, "Rotation");
    if (!accel || !rot)
        return;
    const auto r = Vm::rotatorOf(*rot);
    const float yaw = float(r[1]) * 2 * kPi / 65536;
    const Vec dir{std::cos(yaw), std::sin(yaw), 0};
    if (dot(flat(Vm::vectorOf(*accel)), dir) < 0.5f) {
        ledgePush_.erase(a);
        return;
    }
    const float height = prop(a, "CollisionHeight") ? prop(a, "CollisionHeight")->asFloat() : 49.5f;
    const float arm = prop(a, "ArmCollisionHeight") ? prop(a, "ArmCollisionHeight")->asFloat() : 7.5f;
    const float highest = falling ? 2 * height + arm + kReach : kClamberHighest;
    Ledge l;
    if (!findLedge(a, dir, highest, l)) {
        ledgePush_.erase(a);
        return;
    }
    const Vec at = Vm::vectorOf(*prop(a, "Location"));
    const float feet = at[2] - height;
    auto ask = [&](int physics) {
        tellLedge(a, l);
        ledgePush_.erase(a);
        return vm_.callEvent(a, "ChangePhysics", {Value::byteValue(physics)}).asBool();
    };
    if (l.ladder) {
        ask(kPhysClimbing);
        return;
    }
    const float duck = prop(a, "DuckCollisionHeight") ? prop(a, "DuckCollisionHeight")->asFloat() : 32.5f;
    const float radius = prop(a, "CollisionRadius") ? prop(a, "CollisionRadius")->asFloat() : 17;
    const bool roomy = l.depth > radius * 0.7f && l.headRoom >= duck + 2;
    const float rise = l.height - feet;
    if (rise <= kClamberHighest) {
        if (!roomy)
            return;
        float& pushed = ledgePush_[a];
        pushed += dt;
        if (pushed >= kPushTime || falling)
            ask(kPhysClambering);
        return;
    }
    // Hands catch it: hung from it, facing the wall, just off it.
    const float hands = at[2] + height + arm;
    if (!falling || l.height < hands - 10 || l.height > hands + kReach)
        return;
    const float hangZ = l.height - height - arm;
    const Vec offWall = add(l.wall, mul(l.normal, radius + 1));
    const Vec hangAt{offWall[0], offWall[1], hangZ};
    if (!clear(hangAt, extentOf(a), a))
        return;
    setLocation(a, hangAt);
    if (Value* v = prop(a, "Velocity"))
        vm_.setVector(*v, 0, 0, 0);
    if (Value* p = prop(a, "Rotation")) {
        auto now = Vm::rotatorOf(*p);
        const Vec in = mul(l.normal, -1);
        now[1] = int32_t(std::lround(std::atan2(in[1], in[0]) * 32768 / kPi)) & 0xffff;
        *p = vm_.rotatorValue(now[0], now[1], now[2]);
    }
    if (!ask(kPhysHanging))
        if (Value* p = prop(a, "Physics"); p && p->asInt() != kPhysHanging)
            p->i = kPhysFalling;
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
    Value* physicsValue = prop(a, "Physics");
    Value* velocity = prop(a, "Velocity");
    Value* loc = prop(a, "Location");
    if (!physicsValue || !velocity || !loc)
        return;
    const Vec accel = prop(a, "Acceleration") ? Vm::vectorOf(*prop(a, "Acceleration")) : Vec{};
    const Vec normal = prop(a, "LedgeNormal") ? flat(Vm::vectorOf(*prop(a, "LedgeNormal"))) : Vec{};
    const Vec in = mul(normal, -1);
    const float height = prop(a, "CollisionHeight") ? prop(a, "CollisionHeight")->asFloat() : 49.5f;
    const float radius = prop(a, "CollisionRadius") ? prop(a, "CollisionRadius")->asFloat() : 17;

    if (physics == kPhysClimbing) {
        // Up and down the ladder as the input asks (the scripts' climbing
        // speed is its Acceleration).
        const Vec v{0, 0, accel[2]};
        vm_.setVector(*velocity, v[0], v[1], v[2]);
        if (std::fabs(v[2]) < 0.01f || length(in) < 0.5f)
            return;
        const Vec at = Vm::vectorOf(*loc);
        if (v[2] > 0) {
            // The ladder ends at the chest: over the top onto what's there.
            Ledge l;
            const bool ladder = findLedge(a, in, kClamberHighest + height, l) && l.ladder;
            if (!ladder) {
                if (l.ledge) {
                    tellLedge(a, l);
                    vm_.callEvent(a, "ChangePhysics", {Value::byteValue(kPhysClambering)});
                } else {
                    vm_.setVector(*velocity, 0, 0, 0); // nothing to climb onto
                }
                return;
            }
        }
        const MoveHit h = moveActor(a, mul(v, dt));
        if (a->deleted || !h.hit() || v[2] > 0 || h.normal[2] < kWalkable)
            return;
        // Down to the floor: off the ladder.
        vm_.callEvent(a, "Landed", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2])});
        if (!a->deleted && physicsValue->asInt() == kPhysClimbing)
            physicsValue->i = kPhysWalking;
        (void)at;
        return;
    }

    if (physics == kPhysHanging) {
        // Along the ledge as the input asks, while there's ledge to hold.
        const Vec side{-in[1], in[0], 0};
        const Vec v = mul(side, dot(accel, side));
        vm_.setVector(*velocity, v[0], v[1], v[2]);
        if (length(v) < 0.01f)
            return;
        const Vec before = Vm::vectorOf(*loc);
        moveActor(a, mul(v, dt));
        if (a->deleted)
            return;
        const float arm = prop(a, "ArmCollisionHeight") ? prop(a, "ArmCollisionHeight")->asFloat() : 7.5f;
        Ledge l;
        const Vec now = Vm::vectorOf(*loc);
        const float hands = now[2] + height + arm;
        if (!findLedge(a, in, 2 * height + arm + kReach, l) || !l.ledge || std::fabs(l.height - hands) > 8) {
            vm_.setVector(*loc, before[0], before[1], before[2]);
            updateRegion(a);
            vm_.setVector(*velocity, 0, 0, 0);
        }
        return;
    }

    // Clambering: up as its animation climbs, then forward onto the
    // ledge. From where it started to standing on the ledge's top, past
    // the wall; the rise over the animation's first two thirds, the step
    // forward over the rest.
    vm_.setVector(*velocity, 0, 0, 0);
    auto it = clambers_.find(a);
    if (it == clambers_.end()) {
        Clamber c;
        c.from = Vm::vectorOf(*loc);
        const float ledge = prop(a, "LedgeHeight") ? prop(a, "LedgeHeight")->asFloat() : c.from[2] - height;
        c.ledge = ledge;
        c.forward = length(in) > 0.5f ? in : Vec{0, 0, 0};
        c.reach = radius * 2 + 3;
        it = clambers_.emplace(a, c).first;
    }
    Clamber& c = it->second;
    c.seconds += dt;
    const Value* frame = prop(a, "AnimFrame");
    const Value* rate = prop(a, "AnimRate");
    const Value* last = prop(a, "AnimLast");
    float t = 0;
    if (frame && rate && frame->asFloat() >= 0 && rate->asFloat() > 0) {
        if (c.startFrame < 0)
            c.startFrame = frame->asFloat();
        else if (frame->asFloat() < c.lastFrame - 0.05f)
            c.done = 1; // played through and started again (its scripts' AnimEnd) before this saw its end
        c.lastFrame = frame->asFloat();
        const float end = last && last->asFloat() > c.startFrame ? last->asFloat() : 1;
        t = std::clamp((frame->asFloat() - c.startFrame) / std::max(0.01f, end - c.startFrame), 0.0f, 1.0f);
    }
    t = std::max(t, std::clamp(c.seconds - 2.0f, 0.0f, 1.0f)); // no animation to follow: in time anyway
    c.done = std::max(c.done, t);
    const Vec to = c.to(height);
    const float up = smooth(c.done / 0.65f), on = smooth((c.done - 0.6f) / 0.4f);
    const Vec want{c.from[0] + (to[0] - c.from[0]) * on, c.from[1] + (to[1] - c.from[1]) * on,
                   c.from[2] + (to[2] - c.from[2]) * up};
    setLocation(a, want);
    // Up: falling the last bit onto the ledge, and landing there (its
    // scripts' clambering state walks again once it has landed; their
    // animation would only start again).
    if (c.done >= 1) {
        clambers_.erase(it);
        physicsValue->i = kPhysFalling;
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
