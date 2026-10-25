// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' movement: from the area a bot is in, the reachability that
// leads quickest to its goal, and for each kind of travel (walking,
// jumping, dropping off ledges, teleporters, jump pads, elevators...) the
// elementary actions that carry it out, frame by frame. Behaviour read
// from the GPL ioquake3 bot library (be_ai_move.c); new code.

#include "engines/idtech3/botlib/bot_library.h"

#include <cmath>

namespace reluva::idtech3::botlib {

namespace {

// Quake III's movement settings, as the bot library takes them.
constexpr float kMaxStep = 18, kMaxBarrier = 32, kGravity = 800;
constexpr float kAvoidReachTime = 6;
constexpr int kAvoidReachTries = 4;
constexpr int kEntityWorld = 1022, kEntityNone = 1023;
constexpr int kLiquid = contents::Water | contents::Slime | contents::Lava;

Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3f add_(const Vec3f& a, const Vec3f& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3f scale(const Vec3f& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec3f& v) { return std::sqrt(dot(v, v)); }
float normalize(Vec3f& v) {
    const float l = length(v);
    if (l > 0)
        v = scale(v, 1 / l);
    return l;
}
// The way from `from` to `to` along the ground, and how far.
float horizontal(const Vec3f& from, const Vec3f& to, Vec3f& dir) {
    dir = {to[0] - from[0], to[1] - from[1], 0};
    return normalize(dir);
}
// Pitch and yaw looking along a direction, in degrees (up is negative pitch).
Vec3f anglesOf(const Vec3f& dir) {
    const float forward = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    float yaw = std::atan2(dir[1], dir[0]) * 180 / 3.14159265f;
    if (yaw < 0)
        yaw += 360;
    float pitch = std::atan2(dir[2], forward) * 180 / 3.14159265f;
    return {-pitch, yaw, 0};
}

float reachabilityTime(int travelType) {
    switch (travelType & travel::TypeMask) {
    case travel::Ladder: return 6;
    case travel::Elevator:
    case travel::JumpPad:
    case travel::FuncBob: return 10;
    default: return 5;
    }
}

} // namespace

int BotLibrary::allocMoveState() { return alloc(moveStates_); }

void BotLibrary::freeMoveState(int handle) {
    if (state(moveStates_, handle))
        moveStates_[size_t(handle) - 1].reset();
}

void BotLibrary::resetMoveState(int handle) {
    if (MoveState* ms = state(moveStates_, handle))
        *ms = MoveState{};
}

void BotLibrary::initMoveState(int handle, const BotInitMove& init) {
    MoveState* ms = state(moveStates_, handle);
    if (!ms)
        return;
    ms->origin = init.origin;
    ms->velocity = init.velocity;
    ms->viewOffset = init.viewOffset;
    ms->entity = init.entity;
    ms->client = init.client;
    ms->thinkTime = init.thinkTime;
    ms->presence = init.presence;
    ms->viewAngles = init.viewAngles;
    // The flags the game knows (on the ground, teleported, water jumping,
    // walking) replaced by its; the rest kept.
    constexpr int fromGame = moveflag::OnGround | moveflag::Teleported | moveflag::WaterJump | moveflag::Walk;
    ms->moveFlags = (ms->moveFlags & ~fromGame) | (init.orMoveFlags & fromGame);
}

void BotLibrary::resetAvoidReach(int handle) {
    if (MoveState* ms = state(moveStates_, handle))
        ms->avoidReach.clear();
}

void BotLibrary::resetLastAvoidReach(int handle) {
    // The latest reachability added to the avoided ones taken off.
    MoveState* ms = state(moveStates_, handle);
    if (!ms || ms->avoidReach.empty())
        return;
    auto latest = ms->avoidReach.begin();
    for (auto it = ms->avoidReach.begin(); it != ms->avoidReach.end(); ++it)
        if (it->second.first > latest->second.first)
            latest = it;
    if (--latest->second.second <= 0)
        ms->avoidReach.erase(latest);
}

bool BotLibrary::onGround(const MoveState& ms) const {
    if (ms.moveFlags & moveflag::OnGround)
        return true;
    if (!trace)
        return false;
    Vec3f mins{}, maxs{};
    AasWorld::presenceBox(ms.presence ? ms.presence : presence::Normal, mins, maxs);
    const Trace t = trace(ms.origin, mins, maxs, {ms.origin[0], ms.origin[1], ms.origin[2] - 10}, ms.entity,
                          contents::Solid | contents::PlayerClip);
    return !t.startSolid && t.fraction < 1 && ms.velocity[2] <= 10;
}

bool BotLibrary::moverDown(const AasReach& reach) const {
    Vec3f mins{}, maxs{}, origin{};
    const int model = reach.face & 0xffff;
    if (!modelBounds || !moverOrigin || !modelBounds(model, mins, maxs) || !moverOrigin(model, origin))
        return false;
    return origin[2] + maxs[2] < reach.start[2];
}

Vec3f BotLibrary::moverBottomCenter(const AasReach& reach) const {
    Vec3f mins{}, maxs{}, origin{};
    const int model = reach.face & 0xffff;
    if (modelBounds)
        modelBounds(model, mins, maxs);
    if (moverOrigin)
        moverOrigin(model, origin);
    return {origin[0] + (mins[0] + maxs[0]) / 2, origin[1] + (mins[1] + maxs[1]) / 2, reach.start[2]};
}

bool BotLibrary::onMover(const Vec3f& origin, int entity, const AasReach& reach) const {
    // Within its box (and 16 units) across, and a short box dropped from
    // above the bot lands on it.
    Vec3f mins{}, maxs{}, at{};
    const int model = reach.face & 0xffff;
    if (!trace || !modelBounds || !moverOrigin || !modelBounds(model, mins, maxs) || !moverOrigin(model, at))
        return false;
    for (size_t i = 0; i < 2; ++i)
        if (origin[i] > at[i] + maxs[i] + 16 || origin[i] < at[i] + mins[i] - 16)
            return false;
    const Trace t = trace({origin[0], origin[1], origin[2] + 24}, {-16, -16, -8}, {16, 16, 8},
                          {origin[0], origin[1], origin[2] - 48}, entity, contents::Solid | contents::PlayerClip);
    return !t.startSolid && t.entity != kEntityNone && t.entity != kEntityWorld &&
           entityInfo(t.entity).modelIndex == model;
}

int BotLibrary::entityOnTopOf(const MoveState& ms) const {
    if (!trace)
        return -1;
    Vec3f mins{}, maxs{};
    AasWorld::presenceBox(ms.presence ? ms.presence : presence::Normal, mins, maxs);
    const Trace t = trace(ms.origin, mins, maxs, {ms.origin[0], ms.origin[1], ms.origin[2] - 10}, ms.entity,
                          contents::Solid | contents::PlayerClip);
    if (!t.startSolid && t.entity != kEntityWorld && t.entity != kEntityNone)
        return t.entity;
    return -1;
}

void BotLibrary::checkBlocked(const MoveState& ms, const Vec3f& dir, bool checkBottom, BotMoveResult& result) const {
    // Something just ahead that isn't the world: an entity in the way (a
    // door, another player), for the game to deal with.
    if (!trace)
        return;
    Vec3f mins{}, maxs{};
    AasWorld::presenceBox(ms.presence ? ms.presence : presence::Normal, mins, maxs);
    if (std::fabs(dir[2]) < 0.7f) {
        mins[2] += kMaxStep;
        maxs[2] -= 10;
    }
    const Trace t = trace(ms.origin, mins, maxs, add_(ms.origin, scale(dir, 3)), ms.entity,
                          contents::Solid | contents::PlayerClip | contents::Body);
    if (!t.startSolid && t.entity != kEntityWorld && t.entity != kEntityNone) {
        result.blocked = true;
        result.blockEntity = t.entity;
    } else if (checkBottom && !(aas_.validArea(ms.area) && aas_.settings(ms.area).numReach)) {
        AasWorld::presenceBox(ms.presence ? ms.presence : presence::Normal, mins, maxs);
        const Trace b = trace(ms.origin, mins, maxs, {ms.origin[0], ms.origin[1], ms.origin[2] - 3}, ms.entity,
                              contents::Solid | contents::PlayerClip);
        if (!b.startSolid && b.entity != kEntityWorld && b.entity != kEntityNone) {
            result.blocked = true;
            result.blockEntity = b.entity;
            result.flags |= moveresult::OnTopOfObstacle;
        }
    }
}

float BotLibrary::gapDistance(const MoveState& ms, const Vec3f& hordir) const {
    // How far ahead the floor drops away (0: it doesn't within 100 units),
    // to slow down before it.
    if (!trace)
        return 0;
    Vec3f mins{}, maxs{};
    AasWorld::presenceBox(presence::Crouch, mins, maxs);
    Trace t = trace(ms.origin, mins, maxs, {ms.origin[0], ms.origin[1], ms.origin[2] - 60}, ms.entity,
                    contents::Solid | contents::PlayerClip);
    if (t.fraction >= 1)
        return 1;
    float startZ = t.end[2] + 1;
    for (float dist = 8; dist <= 100; dist += 8) {
        Vec3f start = add_(ms.origin, scale(hordir, dist));
        start[2] = startZ + 24;
        const Vec3f end{start[0], start[1], start[2] - 48 - kMaxBarrier};
        t = trace(start, mins, maxs, end, ms.entity, contents::Solid | contents::PlayerClip);
        if (t.startSolid)
            continue;
        if (t.end[2] < startZ - kMaxStep - 8) {
            if (pointContents && (pointContents({t.end[0], t.end[1], t.end[2] - 20}) & contents::Water))
                break;
            return dist;
        }
        startZ = t.end[2];
    }
    return 0;
}

int BotLibrary::reachabilityToGoal(const Vec3f& origin, int area, int lastGoalArea, int lastArea, MoveState* ms,
                                   const BotGoal& goal, int travelFlags) {
    // The area's reachability with the quickest way on to the goal, not
    // straight back where the bot came from, nor one tried too often lately.
    (void)origin;
    if (!area || !aas_.validArea(area))
        return 0;
    if ((aas_.settings(area).contents | (aas_.validArea(goal.area) ? aas_.settings(goal.area).contents : 0)) &
        areacontents::DoNotEnter)
        travelFlags |= tfl::DoNotEnter;
    int best = 0, bestTime = 0;
    for (int r = aas_.nextReachability(area, 0); r; r = aas_.nextReachability(area, r)) {
        if (ms) {
            const auto it = ms->avoidReach.find(r);
            if (it != ms->avoidReach.end() && it->second.first >= time_ && it->second.second > kAvoidReachTries)
                continue;
        }
        const AasReach& reach = aas_.reachabilities()[size_t(r)];
        if (lastGoalArea == goal.area && reach.area == lastArea)
            continue;
        if ((AasWorld::travelFlagForType(reach.travelType) | aas_.contentsTravelFlags(reach.area)) & ~travelFlags)
            continue;
        int t = aas_.travelTime(reach.area, reach.end, goal.area, travelFlags);
        if (!t)
            continue;
        t += reach.time;
        if (!bestTime || t < bestTime) {
            bestTime = t;
            best = r;
        }
    }
    return best;
}

BotMoveResult BotLibrary::moveToGoal(int handle, const BotGoal& goal, int travelFlags) {
    BotMoveResult result;
    MoveState* ms = state(moveStates_, handle);
    if (!ms || !aas_.loaded()) {
        result.failure = true;
        return result;
    }
    ms->moveFlags &= ~(moveflag::Swimming | moveflag::AgainstLadder);
    if (onGround(*ms))
        ms->moveFlags |= moveflag::OnGround;
    // Standing on something that moves: an elevator or a func_bobbing to
    // ride, or else something in the way.
    if (ms->moveFlags & moveflag::OnGround) {
        if (const int ent = entityOnTopOf(*ms); ent >= 0) {
            const int model = entityInfo(ent).modelIndex;
            const auto type = modelTypes_.find(model);
            const std::string kind = type == modelTypes_.end() ? std::string() : type->second;
            if (kind == "func_plat" || kind == "func_bobbing") {
                const int travelType = kind == "func_plat" ? travel::Elevator : travel::FuncBob;
                const AasReach* last = ms->lastReach ? &aas_.reachabilities()[size_t(ms->lastReach)] : nullptr;
                if (!last || (last->travelType & travel::TypeMask) != travelType || (last->face & 0xffff) != model) {
                    int found = 0;
                    for (size_t r = 1; r < aas_.reachabilities().size() && !found; ++r) {
                        const AasReach& reach = aas_.reachabilities()[r];
                        if ((reach.travelType & travel::TypeMask) == travelType && (reach.face & 0xffff) == model)
                            found = int(r);
                    }
                    if (!found) {
                        result.blocked = true;
                        result.blockEntity = ent;
                        result.flags |= moveresult::OnTopOfObstacle;
                        return result;
                    }
                    ms->lastReach = found;
                    ms->reachTime = time_ + reachabilityTime(travelType);
                }
                result.flags |= kind == "func_plat" ? moveresult::OnTopOfElevator : moveresult::OnTopOfFuncBob;
            } else if (kind == "func_door" || kind == "func_static") {
                ms->area = reachabilityArea(ms->origin);
                if (!aas_.validArea(ms->area) || !aas_.settings(ms->area).numReach) {
                    result.blocked = true;
                    result.blockEntity = ent;
                    result.flags |= moveresult::OnTopOfObstacle;
                    return result;
                }
            } else {
                result.blocked = true;
                result.blockEntity = ent;
                result.flags |= moveresult::OnTopOfObstacle;
                return result;
            }
        }
    }
    if (pointContents && (pointContents({ms->origin[0], ms->origin[1], ms->origin[2] - 2}) & kLiquid))
        ms->moveFlags |= moveflag::Swimming;

    if (ms->moveFlags & (moveflag::OnGround | moveflag::Swimming)) {
        ms->area = reachabilityArea(ms->origin);
        if (!ms->area) {
            result.failure = result.blocked = true;
            result.type = moveresult::TypeInSolidArea;
            return result;
        }
        if (ms->area == goal.area) {
            // In the goal's area: straight to it, slowing near it.
            Vec3f dir = sub(goal.origin, ms->origin);
            if (!(ms->moveFlags & moveflag::Swimming))
                dir[2] = 0;
            const float dist = std::min(normalize(dir), 100.0f);
            float speed = 4 * dist;
            if (speed < 10)
                speed = 0;
            checkBlocked(*ms, dir, true, result);
            move(ms->client, dir, speed);
            result.moveDir = dir;
            result.travelType = ms->moveFlags & moveflag::Swimming ? travel::Swim : travel::Walk;
            if (ms->moveFlags & moveflag::Swimming) {
                result.idealViewAngles = anglesOf(dir);
                result.flags |= moveresult::SwimView;
            }
            ms->lastReach = ms->lastArea = 0;
            ms->lastGoalArea = goal.area;
            ms->lastOrigin = ms->origin;
            return result;
        }
        // Keep last frame's reachability unless the goal or the area
        // changed or it took too long; elevators until reached.
        int reach = ms->lastReach;
        if (reach) {
            const AasReach& r = aas_.reachabilities()[size_t(reach)];
            const int type = r.travelType & travel::TypeMask;
            if (!(AasWorld::travelFlagForType(r.travelType) & travelFlags)) {
                reach = 0;
            } else if (type == travel::Elevator || type == travel::FuncBob) {
                if (result.flags & (moveresult::OnTopOfElevator | moveresult::OnTopOfFuncBob))
                    ms->reachTime = time_ + 5;
                if (ms->area == r.area || ms->reachTime < time_)
                    reach = 0;
            } else if (ms->lastGoalArea != goal.area || ms->reachTime < time_ || ms->lastArea != ms->area) {
                reach = 0;
            }
        }
        if (!reach) {
            reach = reachabilityToGoal(ms->origin, ms->area, ms->lastGoalArea, ms->lastArea, ms, goal, travelFlags);
            ms->reachArea = ms->area;
            ms->jumpReach = 0;
            if (reach) {
                ms->reachTime = time_ + reachabilityTime(aas_.reachabilities()[size_t(reach)].travelType);
                // Tried once more; given up on for a while after a few tries.
                auto& avoid = ms->avoidReach[reach];
                avoid.second = avoid.first >= time_ ? avoid.second + 1 : 1;
                avoid.first = time_ + kAvoidReachTime;
            }
        }
        ms->lastReach = reach;
        ms->lastGoalArea = goal.area;
        ms->lastArea = ms->area;
        if (reach) {
            const AasReach& r = aas_.reachabilities()[size_t(reach)];
            const int flags = result.flags;
            result = travel(*ms, r, false);
            result.flags |= flags;
            result.travelType = r.travelType;
        } else {
            result.failure = true;
        }
    } else if (ms->lastReach) {
        // In the air: finish what the last reachability started.
        const AasReach& r = aas_.reachabilities()[size_t(ms->lastReach)];
        result = travel(*ms, r, true);
        result.travelType = r.travelType;
    }
    if (result.blocked)
        ms->reachTime -= 10 * ms->thinkTime;
    ms->lastOrigin = ms->origin;
    return result;
}

BotMoveResult BotLibrary::travel(MoveState& ms, const AasReach& reach, bool finishing) {
    BotMoveResult result;
    Vec3f dir{};
    const int client = ms.client;
    auto airControl = [&](const Vec3f& goal, Vec3f& out, float& speed) {
        // Where the bot comes down, falling as it goes, and the way from
        // there to the goal: to steer while in the air.
        Vec3f org = ms.origin, vel = scale(ms.velocity, 0.1f);
        for (int i = 0; i < 50; ++i) {
            vel[2] -= kGravity * 0.01f;
            if (vel[2] < 0 && org[2] + vel[2] < goal[2]) {
                org = add_(org, scale(vel, (goal[2] - org[2]) / vel[2]));
                out = sub(goal, org);
                speed = 13 * std::min(normalize(out), 32.0f);
                return true;
            }
            org = add_(org, vel);
        }
        out = {};
        speed = 400;
        return false;
    };
    switch (reach.travelType & travel::TypeMask) {
    case travel::Walk: {
        float dist = horizontal(ms.origin, reach.start, dir);
        checkBlocked(ms, dir, true, result);
        if (dist < 10)
            dist = horizontal(ms.origin, reach.end, dir);
        if (!(aas_.settings(reach.area).presence & presence::Normal) && dist < 20)
            input(client).actionFlags |= action::Crouch;
        const float gap = gapDistance(ms, dir);
        move(client, dir, gap > 0 ? 40 + 2 * gap : 400);
        result.moveDir = dir;
        break;
    }
    case travel::Crouch:
        if (finishing)
            break;
        horizontal(ms.origin, reach.end, dir);
        checkBlocked(ms, dir, true, result);
        input(client).actionFlags |= action::Crouch;
        move(client, dir, 400);
        result.moveDir = dir;
        break;
    case travel::BarrierJump: {
        if (finishing) {
            // Over the top: on to the end.
            if (ms.velocity[2] < 250) {
                horizontal(ms.origin, reach.end, dir);
                checkBlocked(ms, dir, true, result);
                move(client, dir, 400);
                result.moveDir = dir;
            }
            break;
        }
        const float dist = horizontal(ms.origin, reach.start, dir);
        checkBlocked(ms, dir, true, result);
        if (dist < 9)
            jump(client);
        else
            move(client, dir, 6 * std::min(dist, 60.0f));
        result.moveDir = dir;
        break;
    }
    case travel::WalkOffLedge: {
        if (finishing) {
            Vec3f v{};
            const float dist = horizontal(ms.origin, reach.end, v);
            const Vec3f end = dist > 16 ? add_(reach.end, scale(v, 16)) : reach.end;
            float speed = 400;
            if (!airControl(end, dir, speed)) {
                dir = sub(reach.end, ms.origin);
                dir[2] = 0;
                speed = 400;
            }
            move(client, dir, speed);
            result.moveDir = dir;
            break;
        }
        Vec3f d{};
        const float reachDist = horizontal(reach.start, reach.end, d);
        float dist = horizontal(ms.origin, reach.start, dir);
        float speed = 400;
        if (dist < 48) {
            horizontal(ms.origin, reach.end, dir);
            speed = reachDist < 20 ? 100 : 400;
        } else if (reachDist < 20) {
            speed = 144 + 4 * std::min(dist, 64.0f);
        }
        checkBlocked(ms, dir, true, result);
        move(client, dir, speed);
        result.moveDir = dir;
        break;
    }
    case travel::Jump: {
        if (finishing) {
            if (!ms.jumpReach)
                break;
            Vec3f along{};
            const float dist = horizontal(ms.origin, reach.end, dir);
            horizontal(reach.start, reach.end, along);
            if (dot(dir, along) < -0.5f && dist < 24)
                break;
            move(client, dir, 400);
            result.moveDir = dir;
            break;
        }
        // A run up from behind the start (as far as the area goes, up to
        // 80 units), then jump at the start towards the end.
        Vec3f back{};
        horizontal(reach.end, reach.start, back);
        Vec3f runStart = add_(reach.start, scale(back, 80));
        for (float gap = 0; gap < 80; gap += 10) {
            Vec3f p = add_(reach.start, scale(back, gap + 10));
            p[2] += 2;
            if (aas_.pointArea(p) != ms.reachArea) {
                runStart = add_(reach.start, scale(back, gap));
                break;
            }
        }
        Vec3f toStart{}, toRun{};
        const float dist1 = horizontal(reach.start, ms.origin, toStart);
        const float dist2 = horizontal(runStart, ms.origin, toRun);
        if (dot(toStart, toRun) < -0.8f || dist2 < 5) {
            horizontal(ms.origin, reach.end, dir);
            if (dist1 < 24)
                jump(client);
            else if (dist1 < 32)
                delayedJump(client);
            move(client, dir, 400);
            ms.jumpReach = ms.lastReach;
        } else {
            horizontal(ms.origin, runStart, dir);
            move(client, dir, 5 * std::min(dist2, 80.0f));
        }
        result.moveDir = dir;
        break;
    }
    case travel::Ladder: {
        dir = sub(reach.end, ms.origin);
        normalize(dir);
        result.idealViewAngles = anglesOf({dir[0], dir[1], 3 * dir[2]});
        move(client, {}, 0);
        input(client).actionFlags |= action::MoveForward;
        result.flags |= moveresult::MovementView;
        result.moveDir = dir;
        break;
    }
    case travel::Swim:
        dir = sub(reach.start, ms.origin);
        normalize(dir);
        checkBlocked(ms, dir, true, result);
        move(client, dir, 400);
        result.moveDir = dir;
        result.idealViewAngles = anglesOf(dir);
        result.flags |= moveresult::SwimView;
        break;
    case travel::WaterJump: {
        if (finishing) {
            if (ms.moveFlags & moveflag::WaterJump)
                break;
            if (!pointContents || !(pointContents({ms.origin[0], ms.origin[1], ms.origin[2] - 32}) & kLiquid))
                break;
            dir = sub(reach.end, ms.origin);
            dir[2] += 70;
            move(client, dir, 400);
            result.idealViewAngles = anglesOf(dir);
            result.flags |= moveresult::MovementView;
            result.moveDir = dir;
            break;
        }
        dir = sub(reach.end, ms.origin);
        Vec3f h{};
        const float dist = horizontal(ms.origin, reach.end, h);
        dir[2] += 15;
        normalize(dir);
        input(client).actionFlags |= action::MoveForward;
        if (dist < 40)
            input(client).actionFlags |= action::MoveUp;
        result.idealViewAngles = anglesOf(dir);
        result.flags |= moveresult::MovementView;
        result.moveDir = dir;
        break;
    }
    case travel::Teleport: {
        if (finishing || (ms.moveFlags & moveflag::Teleported))
            break;
        dir = sub(reach.start, ms.origin);
        if (!(ms.moveFlags & moveflag::Swimming))
            dir[2] = 0;
        const float dist = normalize(dir);
        checkBlocked(ms, dir, true, result);
        move(client, dir, dist < 30 ? 200 : 400);
        result.moveDir = dir;
        break;
    }
    case travel::JumpPad: {
        if (finishing) {
            float speed = 400;
            if (!airControl(reach.end, dir, speed)) {
                horizontal(ms.origin, reach.end, dir);
                speed = 400;
            }
            checkBlocked(ms, dir, true, result);
            move(client, dir, speed);
            result.moveDir = dir;
            break;
        }
        horizontal(ms.origin, reach.start, dir);
        checkBlocked(ms, dir, true, result);
        move(client, dir, 400);
        result.moveDir = dir;
        break;
    }
    case travel::FuncBob: {
        // Onto the platform at the start; ride it until level with the
        // end, then step off.
        const int ent = entityOnTopOf(ms);
        const bool riding = ent >= 0 && entityInfo(ent).modelIndex == (reach.face & 0xffff);
        if (riding) {
            if (std::fabs(ms.origin[2] - reach.end[2]) < kMaxBarrier) {
                horizontal(ms.origin, reach.end, dir);
                move(client, dir, 400);
                result.moveDir = dir;
            } else {
                result.flags |= moveresult::Waiting;
            }
        } else if (finishing) {
            dir = sub(reach.end, ms.origin);
            normalize(dir);
            move(client, dir, 300);
            result.moveDir = dir;
        } else {
            const float dist = horizontal(ms.origin, reach.start, dir);
            checkBlocked(ms, dir, false, result);
            if (dist > 5)
                move(client, dir, 6 * std::min(dist, 60.0f));
            result.moveDir = dir;
        }
        break;
    }
    case travel::Elevator: {
        // As the original's: on the lift, step off once level with the end,
        // else stand in its middle and ride; off it, wait beside it while
        // it's up, and step on (towards its middle) when it's down.
        if (onMover(ms.origin, ms.entity, reach)) {
            if (std::fabs(ms.origin[2] - reach.end[2]) < kMaxBarrier) {
                horizontal(ms.origin, reach.end, dir);
                move(client, dir, 400);
            } else {
                const float dist = horizontal(ms.origin, moverBottomCenter(reach), dir);
                if (dist > 10)
                    move(client, dir, 4 * std::min(dist, 100.0f));
                result.flags |= moveresult::Waiting;
            }
            result.moveDir = dir;
            break;
        }
        Vec3f toEnd = sub(reach.end, ms.origin);
        if (float dist = normalize(toEnd); dist < 64) {
            // Nearly there: off the lift at the top.
            if (6 * std::min(dist, 60.0f) > 5)
                move(client, toEnd, 6 * std::min(dist, 60.0f));
            result.moveDir = toEnd;
            ms.reachTime = 0;
            break;
        }
        Vec3f toStart{};
        const float distStart = horizontal(ms.origin, reach.start, toStart);
        if (!moverDown(reach)) {
            // Up: wait at the start till it comes down.
            checkBlocked(ms, toStart, false, result);
            if (6 * std::min(distStart, 60.0f) > 5)
                move(client, toStart, 6 * std::min(distStart, 60.0f));
            result.moveDir = toStart;
            result.type = moveresult::TypeElevatorUp;
            result.flags |= moveresult::Waiting;
            break;
        }
        Vec3f toCenter{};
        const float distCenter = horizontal(ms.origin, moverBottomCenter(reach), toCenter);
        const bool towardsCenter = distStart < 20 || distCenter < distStart ||
                                   toStart[0] * toCenter[0] + toStart[1] * toCenter[1] < 0;
        dir = towardsCenter ? toCenter : toStart;
        const float dist = towardsCenter ? distCenter : distStart;
        checkBlocked(ms, dir, false, result);
        move(client, dir, 6 * std::min(dist, 60.0f));
        result.moveDir = dir;
        break;
    }
    default: result.failure = true; break;
    }
    return result;
}

bool BotLibrary::moveInDirection(int handle, const Vec3f& dirIn, float speed, int type) {
    MoveState* ms = state(moveStates_, handle);
    if (!ms)
        return false;
    Vec3f dir = dirIn;
    if (pointContents && (pointContents({ms->origin[0], ms->origin[1], ms->origin[2] - 2}) & kLiquid)) {
        // Swimming: any way will do.
        move(ms->client, dir, speed);
        return true;
    }
    dir[2] = 0;
    if (normalize(dir) <= 0)
        return false;
    // Not off a ledge nor into slime or lava: where the floor is a little
    // way ahead must be somewhere the bot can go on from.
    if (trace) {
        Vec3f mins{}, maxs{};
        AasWorld::presenceBox(presence::Crouch, mins, maxs);
        const Vec3f ahead = add_(ms->origin, scale(dir, 32));
        const Trace t = trace({ahead[0], ahead[1], ahead[2] + kMaxStep}, mins, maxs,
                              {ahead[0], ahead[1], ahead[2] - kMaxStep - 40}, ms->entity,
                              contents::Solid | contents::PlayerClip);
        if (!t.startSolid) {
            if (t.fraction >= 1)
                return false;
            const int area = reachabilityArea({t.end[0], t.end[1], t.end[2] + 16});
            if (!area || (aas_.settings(area).contents & (areacontents::Lava | areacontents::Slime)))
                return false;
        }
    }
    move(ms->client, dir, speed);
    if (type & movetype::Jump)
        jump(ms->client);
    if (type & movetype::Crouch)
        input(ms->client).actionFlags |= action::Crouch;
    return true;
}

namespace {
// Along a path towards a point `lookahead` units ahead: false while short of it.
bool addToTarget(const Vec3f& start, const Vec3f& end, float maxDist, float& dist, Vec3f& target) {
    Vec3f d = sub(end, start);
    const float len = normalize(d);
    if (dist + len >= maxDist) {
        target = add_(start, scale(d, maxDist - dist));
        return true;
    }
    dist += len;
    target = end;
    return false;
}
} // namespace

bool BotLibrary::movementViewTarget(int handle, const BotGoal& goal, int travelFlags, float lookahead, Vec3f& target) {
    // A point some way along the route, to look at while moving.
    MoveState* ms = state(moveStates_, handle);
    if (!ms || !ms->lastReach)
        return false;
    int reach = ms->lastReach, lastArea = ms->lastArea;
    Vec3f end = ms->origin;
    float dist = 0;
    for (int steps = 0; reach && dist < lookahead && steps < 32; ++steps) {
        const AasReach& r = aas_.reachabilities()[size_t(reach)];
        if (addToTarget(end, r.start, lookahead, dist, target))
            return true;
        const int type = r.travelType & travel::TypeMask;
        if (type == travel::Teleport || type == travel::RocketJump || type == travel::BfgJump)
            return true;
        if (type != travel::JumpPad && type != travel::Elevator && type != travel::FuncBob &&
            addToTarget(r.start, r.end, lookahead, dist, target))
            return true;
        const int next = reachabilityToGoal(r.end, r.area, ms->lastGoalArea, lastArea, nullptr, goal, travelFlags);
        end = r.end;
        lastArea = r.area;
        if (lastArea == goal.area) {
            addToTarget(r.end, goal.origin, lookahead, dist, target);
            return true;
        }
        reach = next;
    }
    return false;
}

bool BotLibrary::predictVisiblePosition(const Vec3f& origin, int area, const BotGoal& goal, int travelFlags,
                                        Vec3f& target) {
    // Following the route from `origin` to the goal, the first point the
    // goal can be seen from.
    if (!area || !goal.area)
        return false;
    auto visible = [&](const Vec3f& from) {
        if (!trace)
            return false;
        const Trace t = trace(goal.origin, {}, {}, from, goal.entity, contents::Solid);
        return t.fraction >= 1 || t.entity == goal.entity;
    };
    int lastArea = area;
    Vec3f end = origin;
    for (int i = 0; i < 20 && area != goal.area; ++i) {
        const int reach = reachabilityToGoal(end, area, goal.area, lastArea, nullptr, goal, travelFlags);
        if (!reach)
            return false;
        const AasReach& r = aas_.reachabilities()[size_t(reach)];
        if (visible(r.start)) {
            target = r.start;
            return true;
        }
        if (visible(r.end) || r.area == goal.area) {
            target = r.end;
            return true;
        }
        lastArea = area;
        area = r.area;
        end = r.end;
    }
    return false;
}

bool BotLibrary::predictClientMovement(BotClientMove& move, int entity, const Vec3f& origin, int presenceType,
                                       bool onGround, const Vec3f& velocityIn, const Vec3f& cmdMove, int cmdFrames,
                                       int maxFrames, float frameTime, int stopEvents, int stopArea) {
    move = BotClientMove{};
    if (!trace)
        return false;
    if (frameTime <= 0)
        frameTime = 0.1f;
    // The movement settings, as the bot library's (its settings or Quake
    // III's defaults).
    auto setting = [&](const char* name, float fallback) {
        const std::string v = libVar(name);
        return v.empty() ? fallback : float(std::atof(v.c_str()));
    };
    const float friction = setting("phys_friction", 6), stopSpeed = setting("phys_stopspeed", 100);
    const float gravity = setting("phys_gravity", 800), waterFriction = setting("phys_waterfriction", 1);
    const float waterGravity = setting("phys_watergravity", 400);
    const float maxWalk = setting("phys_maxwalkvelocity", 320), maxCrouch = setting("phys_maxcrouchvelocity", 100);
    const float maxSwim = setting("phys_maxswimvelocity", 150);
    const float walkAccel = setting("phys_walkaccelerate", 10), airAccel = setting("phys_airaccelerate", 1);
    const float swimAccel = setting("phys_swimaccelerate", 4);
    const float maxStep = setting("phys_maxstep", 19), maxSteepness = setting("phys_maxsteepness", 0.7f);
    const float jumpVel = setting("phys_jumpvel", 270), maxBarrier = setting("phys_maxbarrier", 33);
    constexpr int solid = contents::Solid | contents::PlayerClip | contents::Body;

    auto box = [&](int presenceType, Vec3f& mins, Vec3f& maxs) {
        AasWorld::presenceBox(presenceType == presence::Crouch ? presence::Crouch : presence::Normal, mins, maxs);
    };
    auto traceBox = [&](const Vec3f& from, const Vec3f& to, int presenceType, int pass) {
        Vec3f mins{}, maxs{};
        box(presenceType, mins, maxs);
        return trace(from, mins, maxs, to, pass, solid);
    };
    auto swimming = [&](const Vec3f& p) {
        return pointContents && (pointContents({p[0], p[1], p[2] - 2}) & kLiquid);
    };
    auto groundBelow = [&](const Vec3f& p, int presenceType) {
        const Trace t = traceBox(p, {p[0], p[1], p[2] - 10}, presenceType, entity);
        return !t.startSolid && t.fraction < 1 && p[2] - t.end[2] <= 10 && t.normal[2] >= maxSteepness;
    };
    // Speeding up towards `wishSpeed` along `dir`, as the game's movement does.
    auto accelerate = [&](Vec3f& vel, const Vec3f& dir, float wishSpeed, float accel) {
        const float add = wishSpeed - dot(vel, dir);
        if (add <= 0)
            return;
        const float a = std::min(accel * frameTime * wishSpeed, add);
        vel = add_(vel, scale(dir, a));
    };
    auto finish = [&](const Vec3f& at, const Vec3f& vel, const Trace& t, int event, int n, int area) {
        move.endPos = at;
        move.endArea = area;
        move.velocity = vel;
        move.traceStartSolid = t.startSolid;
        move.traceFraction = t.fraction;
        move.traceEnd = t.end;
        move.traceEntity = t.entity;
        move.presence = presenceType;
        move.stopEvent = event;
        move.time = float(n) * frameTime;
        move.frames = n;
        return true;
    };

    Vec3f org = origin, vel = velocityIn;
    org[2] += 0.25f;
    int jumpFrame = -1;
    Trace t;
    for (int n = 0; n < maxFrames; ++n) {
        const bool swim = swimming(org);
        const float g = swim ? waterGravity : gravity;
        vel[2] -= g * frameTime;
        if (onGround || swim) {
            // Friction on the ground or in water.
            const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
            if (speed > 0) {
                const float control = std::max(speed, stopSpeed);
                const float f = std::max(0.0f, speed - frameTime * control * (swim ? waterFriction : friction)) / speed;
                vel[0] *= f;
                vel[1] *= f;
            }
        }
        bool crouch = false;
        if (n < cmdFrames) {
            // The command: walking, crouching (down a lot), jumping (up)
            // or swimming, accelerating on the ground faster than in the air.
            float maxVel = maxWalk, accel = airAccel;
            Vec3f dir = cmdMove;
            if (onGround) {
                if (cmdMove[2] < -300) {
                    crouch = true;
                    maxVel = maxCrouch;
                }
                if (!swim && cmdMove[2] > 1) {
                    vel[2] = jumpVel;
                    jumpFrame = n;
                } else {
                    accel = walkAccel;
                }
            }
            if (swim) {
                maxVel = maxSwim;
                accel = swimAccel;
            } else {
                dir[2] = 0;
            }
            const float wish = std::min(normalize(dir), maxVel);
            if (wish > 0)
                accelerate(vel, dir, wish, accel);
        }
        if (crouch)
            presenceType = presence::Crouch;
        else if (presenceType == presence::Crouch) {
            const int a = aas_.pointArea(org);
            if (a && (aas_.settings(a).presence & presence::Normal))
                presenceType = presence::Normal;
        }
        // This frame's move, sliding along what it hits and stepping up
        // onto what's low enough.
        const Vec3f lastOrg = org;
        Vec3f left = scale(vel, frameTime);
        int tries = 0;
        do {
            const Vec3f end = add_(org, left);
            t = traceBox(org, end, presenceType, entity);
            if (stopEvents & (stopevent::EnterArea | stopevent::TouchJumpPad | stopevent::TouchTeleporter |
                              stopevent::TouchClusterPortal)) {
                std::vector<int> areas;
                std::vector<Vec3f> points;
                aas_.traceAreas(org, t.end, areas, &points, 20);
                for (size_t i = 0; i < areas.size(); ++i) {
                    const int c = aas_.settings(areas[i]).contents;
                    int event = 0;
                    if ((stopEvents & stopevent::EnterArea) && areas[i] == stopArea)
                        event = stopevent::EnterArea;
                    else if ((stopEvents & stopevent::TouchJumpPad) && n && (c & areacontents::JumpPad))
                        event = stopevent::TouchJumpPad;
                    else if ((stopEvents & stopevent::TouchTeleporter) && (c & areacontents::Teleporter))
                        event = stopevent::TouchTeleporter;
                    else if ((stopEvents & stopevent::TouchClusterPortal) && (c & areacontents::ClusterPortal))
                        event = stopevent::TouchClusterPortal;
                    if (event)
                        return finish(points[i], vel, t, event, n, areas[i]);
                }
            }
            org = t.end;
            if (t.fraction >= 1)
                break;
            const Vec3f normal = t.normal;
            if ((stopEvents & stopevent::HitGroundArea) && normal[2] > maxSteepness) {
                const Vec3f p{org[0], org[1], org[2] + 0.5f};
                if (aas_.pointArea(p) == stopArea)
                    return finish(p, vel, t, stopevent::HitGroundArea, n, stopArea);
            }
            // A wall: a step up onto it if it's low enough, unless just jumped.
            bool stepped = false;
            if (std::fabs(normal[2]) < 0.001f && (jumpFrame < 0 || n - jumpFrame > 2)) {
                const Vec3f start{org[0] - normal[0] * 0.25f, org[1] - normal[1] * 0.25f, org[2] + maxStep};
                const Vec3f up{start[0], start[1], start[2]};
                const Trace rise = traceBox(org, up, presenceType, entity);
                const Vec3f top = rise.end;
                const Trace over = traceBox(top, add_(top, {left[0] * (1 - t.fraction), left[1] * (1 - t.fraction), 0}),
                                            presenceType, entity);
                const Trace down = traceBox(over.end, {over.end[0], over.end[1], org[2]}, presenceType, entity);
                if (!rise.startSolid && !down.startSolid && over.fraction > 0 && down.normal[2] > maxSteepness &&
                    down.end[2] > org[2]) {
                    org = down.end;
                    left = {0, 0, 0};
                    vel[2] = 0;
                    stepped = true;
                }
            }
            if (!stepped) {
                // Sliding along it: what's left of the move, and the
                // velocity, without the part into it.
                const Vec3f before = vel;
                const float rest = 1 - t.fraction;
                left = scale(left, rest);
                left = sub(left, scale(normal, dot(left, normal)));
                vel = sub(vel, scale(normal, dot(vel, normal)));
                if (normal[2] > maxSteepness)
                    onGround = true;
                if (stopEvents & stopevent::HitGroundDamage) {
                    // Landing hard: falling damage.
                    float delta = 0;
                    if (before[2] < 0 && vel[2] > before[2] && !onGround)
                        delta = before[2];
                    else if (onGround)
                        delta = vel[2] - before[2];
                    delta = delta * frameTime * 10;
                    delta = delta * delta * 0.0001f;
                    if (!swim && delta > 40)
                        return finish(org, vel, t, stopevent::HitGroundDamage, n, aas_.pointArea(org));
                }
            }
        } while (++tries < 20 && length(left) > 0.01f);
        if (vel[2] * frameTime <= 10) { // not rising more than 10 units a frame
            // Into slime, lava or water at the feet.
            const Vec3f feet{org[0], org[1], org[2] - 22};
            const int pc = pointContents ? pointContents(feet) : 0;
            const int area = aas_.pointArea(org);
            const int ac = area ? aas_.settings(area).contents : 0;
            int event = 0;
            if ((pc & contents::Lava) || (ac & areacontents::Lava))
                event |= stopevent::EnterLava;
            if ((pc & contents::Slime) || (ac & areacontents::Slime))
                event |= stopevent::EnterSlime;
            if ((pc & contents::Water) || (ac & areacontents::Water))
                event |= stopevent::EnterWater;
            if (event & stopEvents) {
                finish(org, vel, t, event & stopEvents, n, area);
                move.endContents = pc;
                return true;
            }
        }
        onGround = groundBelow(org, presenceType);
        if (onGround) {
            if (stopEvents & stopevent::HitGround)
                return finish(org, vel, t, stopevent::HitGround, n, aas_.pointArea(org));
        } else if (stopEvents & stopevent::LeaveGround) {
            return finish(org, vel, t, stopevent::LeaveGround, n, aas_.pointArea(org));
        } else if (stopEvents & stopevent::Gap) {
            // Off the edge of a drop deeper than a step (not into water).
            const Vec3f end{org[0], org[1], org[2] - 48 - maxBarrier};
            const Trace gap = traceBox(org, end, presence::Crouch, -1);
            if (!gap.startSolid && gap.end[2] < org[2] - maxStep - 1 &&
                !(pointContents && (pointContents(end) & contents::Water)))
                return finish(lastOrg, vel, t, stopevent::Gap, n, aas_.pointArea(lastOrg));
        }
    }
    return finish(org, vel, t, stopevent::None, maxFrames, aas_.pointArea(org));
}

} // namespace reluva::idtech3::botlib
