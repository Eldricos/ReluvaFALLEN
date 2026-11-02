// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AI's paths, for a running level (ScriptLevel): the level's
// navigation points (path nodes, patrol and cover spots...) linked by the
// reach list the editor stored in the Level object, the iterators over
// them (AllPathNodes, RadiusPathNodes, RadiusVisiblePathNodes), the way
// from a pawn to an actor or a place across them (FindPathToward,
// FindPathTo), and line of sight (LineOfSightTo). See
// docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/properties.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <set>

namespace reluva::unreal {

namespace {

using Vec = ScriptLevel::Vec;

Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float length(const Vec& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

// A reach's ways of getting along it (its ReachFlags), as far as the
// scripts' pawns' abilities go.
enum ReachFlag : int {
    kReachWalk = 1,
    kReachFly = 2,
    kReachSwim = 4,
    kReachJump = 8,
    kReachDoor = 16,
    kReachSpecial = 32,
    kReachPlayerOnly = 64,
};


} // namespace

// The Level object, after its actor list (readLevelActors): its URL
// (protocol, host, map and portal strings, the options, the port and
// whether it's valid), its model, then the reach list: for each, its
// distance, start and end actors, the biggest collision radius and height
// that fit along it, its flags and whether it was pruned.
Error readReachSpecs(const Package& map, std::vector<RawReachSpec>& out) {
    out.clear();
    const std::vector<int> levels = map.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(map.name() + ": no Level object");
    const int index = levels.front();
    std::vector<uint8_t> data;
    if (Error err = map.readExport(index, data))
        return err;
    Reader r(data, size_t(map.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(map, index, r, props))
        return err;
    const int32_t actors = r.i32();
    r.i32();
    if (!r.ok() || actors < 0 || size_t(actors) > r.remaining())
        return Error::fail(map.name() + ": the actor list doesn't fit");
    for (int32_t i = 0; i < actors && r.ok(); ++i)
        r.index();
    const int version = map.version();
    for (int s = 0; s < 4; ++s) // protocol, host, map, portal
        r.string(version);
    const int32_t options = r.index();
    for (int32_t i = 0; i < options && r.ok(); ++i)
        r.string(version);
    r.i32(); // port
    r.i32(); // valid
    r.index(); // the model
    const int32_t count = r.index();
    if (!r.ok() || count < 0 || size_t(count) * 15 > r.remaining())
        return Error::fail(map.name() + ": the reach list doesn't fit");
    out.reserve(size_t(count));
    for (int32_t i = 0; i < count && r.ok(); ++i) {
        RawReachSpec s;
        s.distance = r.i32();
        s.start = r.index();
        s.end = r.index();
        s.radius = r.i32();
        s.height = r.i32();
        s.flags = r.i32();
        s.pruned = r.u8() != 0;
        out.push_back(s);
    }
    return r.ok() ? Error::success() : Error::fail(map.name() + ": the reach list runs past the end");
}

void ScriptLevel::loadPaths() {
    std::vector<RawReachSpec> raw;
    if (readReachSpecs(map_, raw))
        raw.clear(); // no paths: the pawns go nowhere by them
    reachSpecs_.clear();
    reachSpecs_.reserve(raw.size());
    for (const RawReachSpec& s : raw)
        reachSpecs_.push_back({s.distance, s.start ? vm_.objectAt(map_, s.start) : nullptr,
                               s.end ? vm_.objectAt(map_, s.end) : nullptr, s.radius, s.height, s.flags, s.pruned});
    // In the level's own order: LevelInfo's NavigationPointList, each to
    // its nextNavigationPoint (saved with the map); any others after.
    navigationPoints_.clear();
    std::set<VmObject*> seen;
    const Value* head = prop(levelInfo_, "NavigationPointList");
    for (VmObject* n = head ? head->asObject() : nullptr; n && !seen.count(n) && seen.size() < 100000;) {
        seen.insert(n);
        navigationPoints_.push_back(n);
        const Value* next = prop(n, "nextNavigationPoint");
        n = next ? next->asObject() : nullptr;
    }
    for (VmObject* a : actors_)
        if (vm_.isA(a, "NavigationPoint") && !seen.count(a))
            navigationPoints_.push_back(a);
    navState_.assign(navigationPoints_.size(), {});
    navIndex_.clear();
    for (size_t i = 0; i < navigationPoints_.size(); ++i)
        navIndex_[navigationPoints_[i]] = i;
}

const std::vector<VmObject*>& ScriptLevel::navigationPoints() { return navigationPoints_; }

bool ScriptLevel::lineOfSight(VmObject* self, const Vec& from, const Vec& to) {
    std::array<float, 3> where{}, normal{};
    return !trace(self, from, to, {}, false, where, normal);
}

namespace {
Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float dist2(const Vec& a, const Vec& b) { return dot(sub(a, b), sub(a, b)); }
Vec vecOf(const Value* v) { return v ? Vm::vectorOf(*v) : Vec{}; }
float flat2(const Vec& a, const Vec& b) {
    const float x = a[0] - b[0], y = a[1] - b[1];
    return x * x + y * y;
}
constexpr float kNear2 = 640000; // 800 units, squared
} // namespace

float ScriptLevel::pf(VmObject* a, const char* name, float otherwise) {
    const Value* v = a ? prop(a, name) : nullptr;
    return v ? v->asFloat() : otherwise;
}
bool ScriptLevel::pb(VmObject* a, const char* name) {
    const Value* v = a ? prop(a, name) : nullptr;
    return v && v->asBool();
}
Vec ScriptLevel::pv(VmObject* a, const char* name) {
    const Value* v = a ? prop(a, name) : nullptr;
    return v ? Vm::vectorOf(*v) : Vec{};
}

// The pawn's eyes: BaseEyeHeight above it.
Vec ScriptLevel::eyeOf(VmObject* pawn) {
    Vec e = pv(pawn, "Location");
    e[2] += pf(pawn, "BaseEyeHeight");
    return e;
}

// A test move (the engine's MoveActor with bTest): through the world and
// movers, 2 units further and stopped 2 back; no events, pawns don't stop it.
ScriptLevel::MoveHit ScriptLevel::testMove(VmObject* a, const Vec& delta) {
    MoveHit out;
    Value* loc = prop(a, "Location");
    const Vec start = vecOf(loc);
    const float asked = length(delta);
    if (asked <= 0)
        return out;
    const Vec dir = mul(delta, 1 / asked);
    const Vec reach = add(delta, mul(dir, 2));
    Vec at{}, n{};
    VmObject* by = trace(a, start, add(start, reach), extentOf(a), false, at, n);
    Vec end = add(start, delta);
    if (by) {
        const float t = dot(sub(at, start), reach) / dot(reach, reach);
        const float along = (asked + 2) * t;
        if (along <= 2) {
            out.time = 0;
            end = start;
        } else {
            end = sub(mul(reach, t), mul(dir, 2));
            end = add(start, end);
            out.time = (along - 2) / asked;
        }
        out.normal = n;
        out.actor = by;
    }
    vm_.setVector(*loc, end[0], end[1], end[2]);
    return out;
}

bool ScriptLevel::waterAt(const Vec& p) {
    const Value region = regionAt(p);
    VmObject* zone = !region.fields.empty() ? region.fields[0].asObject() : nullptr;
    return zone && pb(zone, "bWaterZone");
}

// walkMove: a test step along the floor; up a step if blocked, then down
// onto a floor (0.7 or flatter) within MaxStepHeight + 2. 1 moved, 0
// blocked, -1 fell (left at the ledge).
int ScriptLevel::walkMove(VmObject* pawn, Vec delta, float threshold, float& floorZ) {
    Value* loc = prop(pawn, "Location");
    const Vec start = vecOf(loc);
    delta[2] = 0;
    const float stepHeight = pf(pawn, "MaxStepHeight", 25);
    const MoveHit h = testMove(pawn, delta);
    if (h.hit()) {
        testMove(pawn, {0, 0, stepHeight});
        testMove(pawn, mul(delta, 1 - h.time));
        const MoveHit down = testMove(pawn, {0, 0, -stepHeight});
        if (down.hit() && down.normal[2] < 0.7f) {
            vm_.setVector(*loc, start[0], start[1], start[2]);
            return 0;
        }
    }
    const MoveHit floor = testMove(pawn, {0, 0, -(stepHeight + 2)});
    if (!floor.hit())
        return -1;
    floorZ = floor.normal[2];
    if (floor.normal[2] < 0.7f) {
        vm_.setVector(*loc, start[0], start[1], start[2]);
        return -1;
    }
    if (length(sub(vecOf(loc), start)) < threshold)
        return 0;
    return 1;
}

// walkReachable: steps of max(R, 128) towards the place, each a walkMove,
// until within `threshold` (2D) and near enough its height; too steep a
// climb, a fall (unless the step can shrink to a stair) or 101 steps fail.
bool ScriptLevel::walkReachable(VmObject* pawn, Vec dest, float threshold, VmObject* goal) {
    const float r = pf(pawn, "CollisionRadius"), h = pf(pawn, "CollisionHeight");
    float step = std::max(r, 128.0f);
    Vec at{}, n{};
    if (trace(pawn, dest, add(dest, {0, 0, -53}), {}, false, at, n)) {
        const float t = (dest[2] - at[2]) / 53;
        dest[2] = dest[2] - 50 * t + h;
    }
    const float maxH = std::max(h, goal ? pf(goal, "CollisionHeight") : 0.0f);
    const float goalR = goal ? pf(goal, "CollisionRadius") : 46;
    float floorZ = 1;
    for (int it = 0; it < 101; ++it) {
        const Vec here = pv(pawn, "Location");
        Vec dir = sub(dest, here);
        const float zdiff = dir[2];
        dir[2] = 0;
        const float d2 = dot(dir, dir);
        if (zdiff > maxH && 0.8f * (zdiff - maxH) * (zdiff - maxH) > d2)
            return false;
        if (d2 <= threshold * threshold) {
            if (std::fabs(zdiff) < maxH)
                return true;
            if (floorZ > 0.7f && floorZ < 0.95f) {
                const float tan = std::sqrt(1 / (floorZ * floorZ) - 1);
                if (zdiff < 0 && -zdiff < r * tan + h)
                    return true;
                if (r < goalR && zdiff < (goalR + 15 - r) * tan + maxH)
                    return true;
            }
            return false;
        }
        const int moved = d2 < step * step ? walkMove(pawn, dir, 8, floorZ)
                                           : walkMove(pawn, mul(dir, step / std::sqrt(d2)), 4.1f, floorZ);
        if (moved == 1) {
            if (waterAt(pv(pawn, "Location")))
                return pb(pawn, "bCanSwim");
            continue;
        }
        if (moved == -1 && step > pf(pawn, "MaxStepHeight", 25) && !pb(pawn, "bCanJump")) {
            step = pf(pawn, "MaxStepHeight", 25);
            continue;
        }
        return false; // (the engine's jumping and flying here aren't Reluva's yet)
    }
    return false;
}

// Reachable: by the pawn's way of moving; a test, so the pawn is put back.
bool ScriptLevel::reachable(VmObject* pawn, const Vec& dest, float threshold, VmObject* goal) {
    Value* loc = prop(pawn, "Location");
    Value* vel = prop(pawn, "Velocity");
    if (!loc)
        return false;
    const Value savedLoc = *loc;
    const Value savedVel = vel ? *vel : Value();
    const int physics = prop(pawn, "Physics") ? prop(pawn, "Physics")->asInt() : 0;
    bool ok = false;
    if (waterAt(vecOf(loc)) || physics == 4 /*flying*/) {
        // Swimming, flying: straight there (Reluva's; the engine steps as walking does).
        Vec at{}, n{};
        const float r = pf(pawn, "CollisionRadius"), h = pf(pawn, "CollisionHeight");
        ok = !trace(pawn, vecOf(loc), dest, {r, r, h}, false, at, n);
    } else if (physics == 1 || physics == 3) {
        ok = walkReachable(pawn, dest, threshold, goal);
    }
    *loc = savedLoc;
    if (vel)
        *vel = savedVel;
    return ok;
}

// pointReachable: within 800 across, not water it can't swim, seen from
// the eyes (unless known), the place nudged to where the pawn fits.
bool ScriptLevel::pointReachable(VmObject* pawn, Vec p, bool knowVisible) {
    const Vec here = pv(pawn, "Location");
    if (flat2(here, p) > kNear2)
        return false;
    if (waterAt(p) && !waterAt(here) && !pb(pawn, "bCanSwim"))
        return false;
    if (!knowVisible && !lineOfSight(pawn, eyeOf(pawn), p))
        return false;
    Vec fit = p;
    if (findSpot(extentOf(pawn), fit))
        p = fit;
    return reachable(pawn, p, 15, nullptr);
}

// actorReachable (the engine's own): within 800 (not a pawn), seen from
// the eyes unless known, near enough by its size.
bool ScriptLevel::actorReachableRaw(VmObject* pawn, VmObject* a, bool knowVisible) {
    if (!a || a->deleted)
        return false;
    const Vec here = pv(pawn, "Location"), there = pv(a, "Location");
    const bool other = vm_.isA(a, "Pawn");
    const float d2 = dist2(here, there);
    if (!other && d2 > kNear2)
        return false;
    if (waterAt(there) && !pb(pawn, "bCanSwim"))
        return false;
    if (!knowVisible) {
        Vec at{}, n{};
        VmObject* by = trace(pawn, there, eyeOf(pawn), {}, false, at, n);
        if (by && by != a)
            return false;
    }
    const float r = pf(pawn, "CollisionRadius");
    float g = 15;
    VmObject* goal = nullptr;
    if (other) {
        const float t = std::min(1.5f * r, pf(pawn, "MeleeRange"));
        g = pf(a, "CollisionRadius") + r + t;
        if (d2 <= g * g)
            return true;
        if (d2 > kNear2)
            g = std::max(g, std::sqrt(d2) - 800);
        goal = a;
    } else {
        if (vm_.isA(a, "Inventory") || vm_.isA(a, "Trigger"))
            g = pf(a, "CollisionRadius") + r - 2;
        if (pb(a, "bBlockActors") || vm_.isA(a, "WarpZoneMarker"))
            goal = a;
    }
    Vec dest = there, fit = there;
    if (findSpot(extentOf(pawn), fit))
        dest = fit;
    return reachable(pawn, dest, g, goal);
}

// The reach specs a pawn fits along (calcMoveFlags): sizes as whole
// units, its ways of moving (walk 1, fly 2, swim 4, jump 8, doors 16,
// special 32, player 64) covering the spec's.
bool ScriptLevel::specFits(VmObject* pawn, const ReachSpec& s) {
    int flags = 0;
    flags |= pb(pawn, "bCanWalk") ? 1 : 0;
    flags |= pb(pawn, "bCanFly") ? 2 : 0;
    flags |= pb(pawn, "bCanSwim") ? 4 : 0;
    flags |= pb(pawn, "bCanJump") ? 8 : 0;
    flags |= pb(pawn, "bCanOpenDoors") ? 16 : 0;
    flags |= pb(pawn, "bCanDoSpecial") ? 32 : 0;
    flags |= pb(pawn, "bIsPlayer") ? 64 : 0;
    return s.radius >= int(pf(pawn, "CollisionRadius")) && s.height >= int(pf(pawn, "CollisionHeight")) &&
           (s.flags & flags) == s.flags;
}

const ScriptLevel::ReachSpec* ScriptLevel::specAt(VmObject* node, const char* list, int k) {
    const VmProperty* p = node->cls ? node->cls->findProperty(vm_.name(list)) : nullptr;
    if (!p || size_t(p->slot + k) >= node->values.size())
        return nullptr;
    const int i = node->values[size_t(p->slot + k)].asInt();
    return i >= 0 && size_t(i) < reachSpecs_.size() ? &reachSpecs_[size_t(i)] : nullptr;
}

// A spec's way blocked by a mover the pawn can't open (expandAnchor,
// CanMoveTo): a line along it meeting a mover, and the pawn without
// bCanOpenDoors, or the mover for players only.
bool ScriptLevel::moverAllows(VmObject* pawn, const ReachSpec& s) {
    if (!s.start || !s.end)
        return false;
    Vec at{}, n{};
    VmObject* by = trace(pawn, pv(s.end, "Location"), pv(s.start, "Location"), {}, false, at, n);
    if (!by || by == levelInfo_ || !vm_.isA(by, "Mover"))
        return true;
    return pb(pawn, "bCanOpenDoors") && (pb(pawn, "bIsPlayer") || !pb(by, "bPlayerOnly"));
}

bool ScriptLevel::canMoveTo(VmObject* pawn, VmObject* from, VmObject* to) {
    for (const char* list : {"Paths", "PrunedPaths"})
        for (int k = 0; k < 16; ++k) {
            const ReachSpec* s = specAt(from, list, k);
            if (s && s->end == to && specFits(pawn, *s) && moverAllows(pawn, *s))
                return true;
        }
    return false;
}

void ScriptLevel::clearPaths(VmObject* pawn) {
    for (size_t i = 0; i < navigationPoints_.size(); ++i) {
        NavState& st = navState_[i];
        VmObject* n = navigationPoints_[i];
        st.visited = 10000000;
        st.endPoint = false;
        st.previous = -1;
        st.bestPathWeight = 0;
        st.cost = pb(n, "bSpecialCost") ? vm_.callEvent(n, "SpecialCost", {Value::objectValue(pawn)}).asInt()
                                        : int(pf(n, "ExtraCost"));
    }
}

// The way there (APawn::findPathToward / findPathTo): one start node (the
// nearest seen and reachable, or the one it stands on), one end node
// (the nearest from which the goal is reachable), and the cheapest way
// between, searched back from the end over the specs into each node.
VmObject* ScriptLevel::findPath(VmObject* pawn, VmObject* goal, const Vec* point, bool singlePath, bool clear) {
    if (!pawn || (!goal && !point))
        return nullptr;
    const std::vector<VmObject*>& nav = navigationPoints_;
    if (nav.empty() || reachSpecs_.empty()) {
        ++paths.failed;
        return nullptr;
    }
    // A pawn falling: where it will land.
    Vec to = point ? *point : pv(goal, "Location");
    const Vec here = pv(pawn, "Location");
    const float r = pf(pawn, "CollisionRadius");
    struct Cand {
        size_t node;
        int key;
    };
    auto insert = [](std::vector<Cand>& list, Cand c) {
        auto it = std::find_if(list.begin(), list.end(), [&](const Cand& o) { return o.key >= c.key; });
        list.insert(it, c);
        if (list.size() > 32)
            list.pop_back();
    };
    std::vector<Cand> starts, dests;
    bool startAnchor = false, endAnchor = false;
    if (goal && navIndex_.count(goal)) {
        dests.push_back({navIndex_[goal], 0});
        endAnchor = true;
    }
    if (VmObject* mt = prop(pawn, "MoveTarget") ? prop(pawn, "MoveTarget")->asObject() : nullptr;
        mt && navIndex_.count(mt)) {
        const Vec m = pv(mt, "Location");
        const float lim = vm_.isA(mt, "InventorySpot") ? 2 * r * r : r * r;
        if (std::fabs(m[2] - here[2]) < 50 && flat2(m, here) < lim) {
            starts.push_back({navIndex_[mt], 0});
            startAnchor = true;
        }
    }
    if (clear)
        clearPaths(pawn);
    for (size_t i = 0; i < nav.size(); ++i) {
        if (nav[i]->deleted)
            continue;
        const Vec p = pv(nav[i], "Location");
        if (!startAnchor) {
            const int k = int(dist2(here, p));
            if (k < kNear2)
                insert(starts, {i, k});
        }
        if (!endAnchor) {
            const int k = int(dist2(to, p));
            if (k < kNear2)
                insert(dests, {i, k});
        }
    }
    if (starts.empty() || dests.empty()) {
        ++paths.failed;
        ++(starts.empty() ? paths.noStart : paths.noEnd);
        return nullptr;
    }
    const Value savedLoc = *prop(pawn, "Location");
    auto restore = [&] { *prop(pawn, "Location") = savedLoc; };
    // The start node: the nearest seen and reachable.
    int startDist = 0;
    if (!startAnchor) {
        while (!starts.empty()) {
            VmObject* n = nav[starts.front().node];
            const Vec p = pv(n, "Location");
            if (lineOfSight(pawn, eyeOf(pawn), p) && pointReachable(pawn, p, true))
                break;
            starts.erase(starts.begin());
        }
        if (starts.empty()) {
            ++paths.failed;
            ++paths.noStart;
            return nullptr;
        }
        startDist = int(std::sqrt(float(starts.front().key)));
        const Vec p = pv(nav[starts.front().node], "Location");
        if (startDist < std::max(int(r), 48) && std::fabs(p[2] - here[2]) < 50) {
            startAnchor = true;
        } else {
            navState_[starts.front().node].endPoint = true;
            navState_[starts.front().node].bestPathWeight = startDist;
        }
    }
    const size_t first = starts.front().node;
    if (startAnchor) {
        VmObject* anchor = nav[first];
        bool direct = goal && navIndex_.count(goal) && canMoveTo(pawn, anchor, goal);
        if (!direct && dist2(to, pv(anchor, "Location")) < kNear2 && lineOfSight(pawn, pv(anchor, "Location"), to)) {
            Vec fit = pv(anchor, "Location");
            if (findSpot(extentOf(pawn), fit)) {
                vm_.setVector(*prop(pawn, "Location"), fit[0], fit[1], fit[2]);
                direct = pointReachable(pawn, to, false);
                restore();
            }
        }
        if (direct) {
            ++paths.straight;
            return goal && navIndex_.count(goal) ? goal : anchor;
        }
        navState_[first].cost = 1000000;
        for (int k = 0; k < 16; ++k) {
            const ReachSpec* s = specAt(anchor, "Paths", k);
            if (!s || !s->end || !navIndex_.count(s->end) || !specFits(pawn, *s) || !moverAllows(pawn, *s))
                continue;
            NavState& e = navState_[navIndex_[s->end]];
            e.endPoint = true;
            e.bestPathWeight = s->distance;
        }
    }
    // The end node: the nearest from which the goal is reachable.
    if (!endAnchor) {
        size_t chosen = dests.size();
        for (size_t k = 0; k < dests.size(); ++k) {
            const Vec p = pv(nav[dests[k].node], "Location");
            if (!lineOfSight(pawn, p, to))
                continue;
            Vec fit = p;
            if (!findSpot(extentOf(pawn), fit))
                continue;
            vm_.setVector(*prop(pawn, "Location"), fit[0], fit[1], fit[2]);
            const bool ok = point ? pointReachable(pawn, to, true) : actorReachableRaw(pawn, goal, true);
            restore();
            if (ok) {
                chosen = k;
                break;
            }
        }
        if (chosen == dests.size()) {
            if (!goal || !pb(pawn, "bHunting")) {
                ++paths.failed;
                ++paths.noEnd;
                return nullptr;
            }
            chosen = 0;
        } else {
            dests[chosen].key = int(std::sqrt(float(dests[chosen].key)));
        }
        std::swap(dests[0], dests[chosen]);
    }
    // Back from the end (breadthPathFrom): cheapest first, along the specs
    // into each node, each costing its distance and the node's cost (and
    // a start's own distance), to the first start reached.
    const size_t end = dests[0].node;
    navState_[end].visited = dests[0].key;
    std::vector<size_t> open{end};
    const bool player = pb(pawn, "bIsPlayer");
    int popped = 0;
    int best = -1;
    while (!open.empty()) {
        const size_t i = open.front();
        open.erase(open.begin());
        if (++popped > 1000 || (singlePath && popped > 4))
            break;
        NavState& st = navState_[i];
        if (st.endPoint) {
            best = int(i);
            break;
        }
        VmObject* node = nav[i];
        if (pb(node, "bPlayerOnly") && !player && i != first)
            continue;
        for (int k = 0; k < 16; ++k) {
            const ReachSpec* s = specAt(node, "upstreamPaths", k);
            if (!s || !s->start || !navIndex_.count(s->start) || !specFits(pawn, *s))
                continue;
            const size_t j = navIndex_[s->start];
            NavState& o = navState_[j];
            const int w = st.visited + s->distance + o.cost + (o.endPoint ? o.bestPathWeight : 0);
            if (o.visited > w) {
                open.erase(std::remove(open.begin(), open.end(), j), open.end());
                o.previous = int(i);
                o.visited = w;
                auto it = std::find_if(open.begin(), open.end(), [&](size_t q) { return navState_[q].visited >= w; });
                open.insert(it, j);
            }
        }
    }
    restore();
    if (best < 0) {
        ++paths.failed;
        ++paths.noRoute;
        return nullptr;
    }
    VmObject* bestPath = nav[size_t(best)];
    // A better start than the nearest: closer to the goal overall, or
    // behind the pawn against the way it was going.
    if (!startAnchor && !singlePath) {
        const int total = navState_[first].visited + startDist;
        std::vector<std::pair<int, size_t>> better;
        for (size_t k = 1; k < starts.size(); ++k) {
            const size_t i = starts[k].node;
            const int w = navState_[i].visited + int(std::sqrt(float(starts[k].key)));
            const Vec p = pv(nav[i], "Location");
            if (w < total && std::fabs(p[2] - here[2]) < 120 &&
                (dot(sub(pv(bestPath, "Location"), here), sub(p, here)) < 0 ||
                 w < std::max(int(0.85f * float(total)), total - 150)))
                better.push_back({w, i});
        }
        std::stable_sort(better.begin(), better.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [w, i] : better) {
            const Vec p = pv(nav[i], "Location");
            if (lineOfSight(pawn, eyeOf(pawn), p) && pointReachable(pawn, p, true)) {
                bestPath = nav[i];
                best = int(i);
                break;
            }
        }
        restore();
    }
    // RouteCache: the way from there to the end.
    const VmProperty* cacheProp = pawn->cls ? pawn->cls->findProperty(vm_.name("RouteCache")) : nullptr;
    int at = best;
    for (int k = 0; cacheProp && k < 16 && size_t(cacheProp->slot + k) < pawn->values.size(); ++k) {
        pawn->values[size_t(cacheProp->slot + k)] = Value::objectValue(at >= 0 ? nav[size_t(at)] : nullptr);
        if (at >= 0)
            at = navState_[size_t(at)].previous;
    }
    ++paths.found;
    return bestPath;
}

// HandleSpecial: a node with its own way (a lift, a door) names what to
// go for instead (SpecialHandling); the pawn's SpecialGoal.
VmObject* ScriptLevel::handleSpecial(VmObject* pawn, VmObject* result) {
    VmFunction* f = result ? vm_.findFunction(result, vm_.name("SpecialHandling")) : nullptr;
    if (!f || f->qualified == "Actor.SpecialHandling")
        return result;
    VmObject* g = vm_.callEvent(result, "SpecialHandling", {Value::objectValue(pawn)}).asObject();
    if (!g || g == result)
        return g;
    if (!pb(pawn, "bCanDoSpecial"))
        return nullptr;
    if (Value* sg = prop(pawn, "SpecialGoal"))
        *sg = Value::objectValue(g);
    if (actorReachableRaw(pawn, g, false))
        return g;
    VmObject* way = findPath(pawn, g, nullptr, false, true);
    return way != result ? way : nullptr;
}

VmObject* ScriptLevel::findPathNative(VmObject* pawn, VmObject* goal, const Vec* point, bool singlePath, bool clear) {
    VmObject* result = findPath(pawn, goal, point, singlePath, clear);
    if (Value* v = prop(pawn, "bShootSpecial"))
        *v = Value::boolValue(false);
    if (Value* v = prop(pawn, "SpecialPause"))
        *v = Value::floatValue(0);
    if (result)
        result = handleSpecial(pawn, result);
    if (Value* sg = prop(pawn, "SpecialGoal"); sg && result && sg->asObject() == result)
        *sg = Value::objectValue(nullptr);
    return result;
}

// actorReachable as The Fallen's native: a navigation point (or an item's
// marker) by the path network alone when the pawn's at one and fits the
// level's biggest; else the engine's test.
bool ScriptLevel::actorReachable(VmObject* pawn, VmObject* a) {
    if (!a)
        return false;
    if (vm_.isA(a, "Inventory"))
        if (VmObject* m = prop(a, "myMarker") ? prop(a, "myMarker")->asObject() : nullptr)
            a = m;
    if (navIndex_.count(a) && !reachSpecs_.empty() && pf(pawn, "CollisionRadius") <= pf(levelInfo_, "LargestRadius", 40) &&
        pf(pawn, "CollisionHeight") <= pf(levelInfo_, "LargestHeight", 80)) {
        const Vec here = pv(pawn, "Location");
        const float zLim = std::max(pf(pawn, "CollisionHeight"), 50.0f), rLim = std::max(pf(pawn, "CollisionRadius"), 48.0f);
        auto near = [&](VmObject* n) {
            const Vec p = pv(n, "Location");
            return std::fabs(p[2] - here[2]) < zLim && flat2(p, here) < rLim * rLim;
        };
        bool any = false;
        VmObject* mt = prop(pawn, "MoveTarget") ? prop(pawn, "MoveTarget")->asObject() : nullptr;
        if (mt && navIndex_.count(mt) && near(mt)) {
            any = true;
            if (mt == a || canMoveTo(pawn, mt, a))
                return true;
        }
        for (VmObject* n : navigationPoints_)
            if (!n->deleted && near(n)) {
                any = true;
                if (n == a || canMoveTo(pawn, n, a))
                    return true;
            }
        if (any && (!prop(pawn, "Physics") || prop(pawn, "Physics")->asInt() != 4))
            return false;
    }
    return actorReachableRaw(pawn, a, false);
}

// GetClosestPathNodeList: the nearest navigation points within a radius,
// cached for all (kept while asked about within 250 of where first; its
// place moved only when asked 50 off; widened, never narrowed).
const std::vector<std::pair<float, VmObject*>>& ScriptLevel::closestNodes(Vec at, float radius) {
    ClosestCache& c = closest_;
    auto distOf = [&](VmObject* n) { return length(sub(pv(n, "Location"), at)); };
    if (c.filled && dist2(c.center, at) < 62500) {
        bool stale = false;
        if (dist2(c.last, at) > 2500) {
            c.last = at;
            stale = true;
        } else {
            at = c.last;
        }
        if (radius > c.radius) {
            for (VmObject* n : navigationPoints_)
                if (!n->deleted && distOf(n) <= radius &&
                    std::none_of(c.nodes.begin(), c.nodes.end(), [&](const auto& e) { return e.second == n; }))
                    c.nodes.push_back({distOf(n), n});
            c.radius = radius;
            stale = true;
        }
        if (stale) {
            for (auto& e : c.nodes)
                e.first = distOf(e.second);
            std::sort(c.nodes.begin(), c.nodes.end(), [](const auto& a, const auto& b) { return int(a.first - b.first) < 0; });
        }
        return c.nodes;
    }
    c = ClosestCache{};
    c.filled = true;
    c.center = c.last = at;
    c.radius = radius;
    for (VmObject* n : navigationPoints_)
        if (!n->deleted && distOf(n) <= radius)
            c.nodes.push_back({distOf(n), n});
    std::sort(c.nodes.begin(), c.nodes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return c.nodes;
}

void registerPathNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto classOf = [](const Value& classArg) {
        VmObject* o = classArg.asObject();
        return o && o->asClass ? o->asClass : nullptr;
    };
    auto locationOf = [](NativeCall& c, VmObject* a) {
        const Value* l = a ? a->find(c.vm, "Location") : nullptr;
        return l ? Vm::vectorOf(*l) : Vec{};
    };
    // AllPathNodes(class<Actor> BaseClass, out Actor Actor, optional name MatchTag):
    // in the level's own order.
    reg("Actor.AllPathNodes", [=](NativeCall& c) {
        if (!c.vm.level)
            return;
        const VmClass* cls = classOf(c.args[0]);
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        for (VmObject* a : c.vm.level->navigationPoints()) {
            if (a->deleted || (cls && !(a->cls && a->cls->isA(cls))))
                continue;
            const Value* tag = byTag ? a->find(c.vm, "Tag") : nullptr;
            if (!byTag || (tag && tag->i == c.i(2)))
                c.items.push_back(Value::objectValue(a));
        }
    });
    // RadiusPathNodes(class<Actor> BaseClass, out Actor Actor, float Radius, optional Vector Loc):
    // of the 8 nearest navigation points within Radius of Loc (else of
    // this actor), nearest first, those of the class; RadiusVisiblePathNodes
    // those of them that can see Loc too.
    for (const bool visible : {false, true})
        reg(visible ? "Actor.RadiusVisiblePathNodes" : "Actor.RadiusPathNodes", [=](NativeCall& c) {
            if (!c.vm.level)
                return;
            const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : locationOf(c, c.self);
            const VmClass* cls = classOf(c.args[0]);
            const auto& list = c.vm.level->closestNodes(at, c.f(2));
            for (size_t k = 0; k < std::min<size_t>(list.size(), 8); ++k) {
                VmObject* a = list[k].second;
                if (a->deleted || list[k].first > c.f(2) || (cls && !(a->cls && a->cls->isA(cls))))
                    continue;
                if (visible && !c.vm.level->lineOfSight(c.self, locationOf(c, a), at))
                    continue;
                c.items.push_back(Value::objectValue(a));
            }
        });
    // FindPathToward(Actor anActor, optional bool bSinglePath, optional bool bClearPaths) and
    // FindPathTo(Vector aPoint, ...): the next navigation point on the way.
    auto opt = [](NativeCall& c, size_t k, bool otherwise) { return c.given.size() > k && c.given[k] ? c.b(k) : otherwise; };
    reg("Pawn.FindPathToward", [=](NativeCall& c) {
        c.result = Value::objectValue(c.vm.level && c.self && c.o(0)
                                          ? c.vm.level->findPathNative(c.self, c.o(0), nullptr, opt(c, 1, false), opt(c, 2, true))
                                          : nullptr);
    });
    reg("Pawn.FindPathTo", [=](NativeCall& c) {
        const Vec to = Vm::vectorOf(c.args[0]);
        c.result = Value::objectValue(c.vm.level && c.self
                                          ? c.vm.level->findPathNative(c.self, nullptr, &to, opt(c, 1, false), opt(c, 2, true))
                                          : nullptr);
    });
    reg("Pawn.ClearPaths", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->clearPaths(c.self);
    });
    reg("Pawn.pointReachable", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->pointReachable(c.self, Vm::vectorOf(c.args[0]), false));
    });
    reg("Pawn.actorReachable", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->actorReachable(c.self, c.o(0)));
    });
    // LineOfSightTo(Actor Other): from its eyes (BaseEyeHeight) to the
    // other: its enemy's middle; else, within 1000, its chest (0.8 of its
    // height up) and, a pawn within 500, its sides; far off, a pawn's middle
    // (the AI half the time, and not past half its sight range).
    reg("Pawn.LineOfSightTo", [=](NativeCall& c) {
        VmObject* other = c.o(0);
        c.result = Value::boolValue(false);
        ScriptLevel* level = c.vm.level;
        if (!level || !c.self || !other)
            return;
        const Vec eye = level->eyeOf(c.self);
        const Vec there = locationOf(c, other);
        const float d2 = dist2(eye, there);
        const bool pawn = c.vm.isA(other, "Pawn"), player = c.vm.isA(c.self, "PlayerPawn");
        float maxD2 = 9e6f;
        if (pawn) {
            const float vis = std::min((level->pf(other, "Visibility", 128) + 16) * 0.015f, 1.0f) * (player ? 5000 : 4000);
            maxD2 = std::min(vis * vis, player ? 2.5e7f : 1.6e7f);
        }
        if (d2 > maxD2)
            return;
        const Value* enemy = c.self->find(c.vm, "Enemy");
        if (enemy && enemy->asObject() == other && level->lineOfSight(c.self, eye, there)) {
            c.result = Value::boolValue(true);
            return;
        }
        if (d2 > 1e6f) {
            if (!pawn)
                return;
            if (d2 > 0.5f * maxD2)
                return;
            if (!player && level->coinFlip())
                return;
            c.result = Value::boolValue(level->lineOfSight(c.self, eye, there));
            return;
        }
        const float h = level->pf(other, "CollisionHeight");
        if (level->lineOfSight(c.self, eye, {there[0], there[1], there[2] + 0.8f * h})) {
            c.result = Value::boolValue(true);
            return;
        }
        if (d2 <= 250000 && pawn) {
            const float rr = level->pf(other, "CollisionRadius");
            for (const Vec& off : {Vec{rr, rr, 0}, Vec{-rr, -rr, 0}})
                if (level->lineOfSight(c.self, eye, add(there, off))) {
                    c.result = Value::boolValue(true);
                    return;
                }
        }
    });
}

} // namespace reluva::unreal
