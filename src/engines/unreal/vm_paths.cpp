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

// How far from a pawn (or a goal) the points it can get straight to are
// looked for, and how many of the nearest are tried.
constexpr float kEntryRadius = 1200;
constexpr size_t kEntryPoints = 8;

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
        raw.clear(); // no paths: the pawns go straight or not at all
    reachSpecs_.clear();
    reachSpecs_.reserve(raw.size());
    for (const RawReachSpec& s : raw)
        reachSpecs_.push_back({s.distance, s.start ? vm_.objectAt(map_, s.start) : nullptr,
                               s.end ? vm_.objectAt(map_, s.end) : nullptr, s.radius, s.height, s.flags, s.pruned});
    navigationPoints_.clear();
    for (VmObject* a : actors_)
        if (vm_.isA(a, "NavigationPoint"))
            navigationPoints_.push_back(a);
}

const std::vector<VmObject*>& ScriptLevel::navigationPoints() {
    navigationPoints_.erase(std::remove_if(navigationPoints_.begin(), navigationPoints_.end(),
                                           [](VmObject* a) { return a->deleted; }),
                            navigationPoints_.end());
    return navigationPoints_;
}

bool ScriptLevel::lineOfSight(VmObject* self, const Vec& from, const Vec& to) {
    std::array<float, 3> where{}, normal{};
    return !trace(self, from, to, {}, false, where, normal);
}

// Whether a pawn could go straight from `from` to `to`: its box (a little
// smaller) meets nothing in the world on the way, nor is the climb more
// than a step's.
bool ScriptLevel::walkable(VmObject* pawn, const Vec& from, const Vec& to) {
    const float r = prop(pawn, "CollisionRadius") ? prop(pawn, "CollisionRadius")->asFloat() : 0;
    const float h = prop(pawn, "CollisionHeight") ? prop(pawn, "CollisionHeight")->asFloat() : 0;
    std::array<float, 3> where{}, normal{};
    return !trace(pawn, from, to, {r * 0.5f, r * 0.5f, h * 0.5f}, false, where, normal);
}

// The pawn's ways of getting along a reach (Pawn's bCanWalk, bCanFly...):
// what it lacks, it can't take. A pawn without the property can.
int ScriptLevel::reachAbilities(VmObject* pawn) {
    auto can = [&](const char* name, bool otherwise) {
        const Value* v = prop(pawn, name);
        return v ? v->asBool() : otherwise;
    };
    int flags = kReachSpecial;
    flags |= can("bCanWalk", true) ? kReachWalk : 0;
    flags |= can("bCanFly", false) ? kReachFly : 0;
    flags |= can("bCanSwim", true) ? kReachSwim : 0;
    flags |= can("bCanJump", true) ? kReachJump : 0;
    flags |= can("bCanOpenDoors", true) ? kReachDoor : 0;
    flags |= can("bIsPlayer", false) ? kReachPlayerOnly : 0;
    return flags;
}

// The way there: the navigation points the pawn can get straight to
// (the nearest few in sight), the ones that can get straight to the goal
// (or the goal itself, if it's one), and the shortest way between them
// along the reaches the pawn fits and can take, each costing its distance
// and its end's ExtraCost. The route goes in the pawn's RouteCache; the
// first point on it it isn't already at is the answer. The goal itself if
// the pawn can get straight there; none if there's no way.
VmObject* ScriptLevel::findPath(VmObject* pawn, VmObject* goal, const Vec* point) {
    if (!pawn || (!goal && !point))
        return nullptr;
    const Value* loc = prop(pawn, "Location");
    if (!loc)
        return nullptr;
    const Vec from = Vm::vectorOf(*loc);
    const Vec to = point ? *point : Vm::vectorOf(*goal->find(vm_, "Location"));
    Value* cache = prop(pawn, "RouteCache");
    const VmProperty* cacheProp = pawn->cls ? pawn->cls->findProperty(vm_.name("RouteCache")) : nullptr;
    auto setRoute = [&](const std::vector<VmObject*>& route) {
        if (!cache || !cacheProp)
            return;
        for (int k = 0; k < 16 && size_t(cacheProp->slot + k) < pawn->values.size(); ++k)
            pawn->values[size_t(cacheProp->slot + k)] =
                Value::objectValue(size_t(k) < route.size() ? route[size_t(k)] : nullptr);
    };
    if (walkable(pawn, from, to)) {
        ++paths.straight;
        setRoute({});
        return goal; // straight there (none for a place: no path point needed)
    }
    const std::vector<VmObject*>& nav = navigationPoints();
    if (nav.empty() || reachSpecs_.empty()) {
        ++paths.failed;
        return nullptr;
    }
    std::map<VmObject*, size_t> indexOf;
    for (size_t i = 0; i < nav.size(); ++i)
        indexOf[nav[i]] = i;
    auto locationOf = [&](VmObject* a) { return Vm::vectorOf(*a->find(vm_, "Location")); };
    // The nearest few within reach of a place that can see it.
    auto entries = [&](const Vec& at, bool walk) {
        std::vector<std::pair<float, size_t>> near;
        for (size_t i = 0; i < nav.size(); ++i) {
            const float d = length(sub(locationOf(nav[i]), at));
            if (d <= kEntryRadius)
                near.push_back({d, i});
        }
        std::sort(near.begin(), near.end());
        std::vector<std::pair<size_t, float>> out;
        for (const auto& [d, i] : near) {
            if (out.size() >= kEntryPoints)
                break;
            const Vec p = locationOf(nav[i]);
            if (walk ? walkable(pawn, at, p) : lineOfSight(pawn, p, at))
                out.push_back({i, d});
        }
        return out;
    };
    std::vector<std::pair<size_t, float>> starts = entries(from, true);
    std::vector<std::pair<size_t, float>> ends;
    if (goal && indexOf.count(goal))
        ends.push_back({indexOf[goal], 0});
    else
        ends = entries(to, false);
    if (starts.empty() || ends.empty()) {
        ++paths.failed;
        ++(starts.empty() ? paths.noStart : paths.noEnd);
        return nullptr;
    }
    const float radius = prop(pawn, "CollisionRadius") ? prop(pawn, "CollisionRadius")->asFloat() : 0;
    const float height = prop(pawn, "CollisionHeight") ? prop(pawn, "CollisionHeight")->asFloat() : 0;
    const int abilities = reachAbilities(pawn);
    // Shortest way (Dijkstra) from the starts, each costing the way to it.
    std::vector<float> cost(nav.size(), 1e30f);
    std::vector<int> previous(nav.size(), -1);
    using Item = std::pair<float, size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (const auto& [i, d] : starts)
        if (d < cost[i]) {
            cost[i] = d;
            open.push({d, i});
        }
    std::vector<float> endCost(nav.size(), -1);
    for (const auto& [i, d] : ends)
        endCost[i] = d;
    int best = -1;
    float bestCost = 1e30f;
    while (!open.empty()) {
        const auto [c, i] = open.top();
        open.pop();
        if (c > cost[i] || c >= bestCost)
            continue;
        if (endCost[i] >= 0 && c + endCost[i] < bestCost) {
            bestCost = c + endCost[i];
            best = int(i);
        }
        const VmProperty* pathsProp = nav[i]->cls->findProperty(vm_.name("Paths"));
        for (int k = 0; pathsProp && k < 16; ++k) {
            const int spec = nav[i]->values[size_t(pathsProp->slot + k)].asInt();
            if (spec < 0 || size_t(spec) >= reachSpecs_.size())
                continue;
            const ReachSpec& s = reachSpecs_[size_t(spec)];
            if (!s.end || s.end->deleted || float(s.radius) < radius || float(s.height) < height ||
                (s.flags & ~abilities) != 0)
                continue;
            auto next = indexOf.find(s.end);
            if (next == indexOf.end())
                continue;
            const Value* extra = s.end->find(vm_, "ExtraCost");
            const float step = float(std::max(1, s.distance)) + (extra ? float(std::max(0, extra->asInt())) : 0);
            if (c + step < cost[next->second]) {
                cost[next->second] = c + step;
                previous[next->second] = int(i);
                open.push({c + step, next->second});
            }
        }
    }
    if (best < 0) {
        ++paths.failed;
        ++paths.noRoute;
        return nullptr;
    }
    std::vector<VmObject*> route;
    for (int i = best; i >= 0; i = previous[size_t(i)])
        route.push_back(nav[size_t(i)]);
    std::reverse(route.begin(), route.end());
    // Already at the first: on to the next.
    if (route.size() > 1 && length(sub(locationOf(route[0]), from)) <= std::max(24.0f, radius * 2))
        route.erase(route.begin());
    setRoute(route);
    ++paths.found;
    return route.front();
}

void registerPathNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // The iterators: the level's navigation points of a class (BaseClass;
    // None for all), each in turn.
    auto pointsOf = [](NativeCall& c, const Value& classArg) {
        std::vector<VmObject*> out;
        if (!c.vm.level)
            return out;
        VmObject* o = classArg.asObject();
        const VmClass* cls = o && o->asClass ? o->asClass : nullptr;
        for (VmObject* a : c.vm.level->navigationPoints())
            if (!cls || (a->cls && a->cls->isA(cls)))
                out.push_back(a);
        return out;
    };
    auto locationOf = [](NativeCall& c, VmObject* a) {
        const Value* l = a ? a->find(c.vm, "Location") : nullptr;
        return l ? Vm::vectorOf(*l) : Vec{};
    };
    // AllPathNodes(class<Actor> BaseClass, out Actor Actor, optional name MatchTag)
    reg("Actor.AllPathNodes", [=](NativeCall& c) {
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        for (VmObject* a : pointsOf(c, c.args[0])) {
            const Value* tag = byTag ? a->find(c.vm, "Tag") : nullptr;
            if (!byTag || (tag && tag->i == c.i(2)))
                c.items.push_back(Value::objectValue(a));
        }
    });
    // RadiusPathNodes(class<Actor> BaseClass, out Actor Actor, float Radius, optional Vector Loc):
    // those within Radius of Loc (else of this actor); RadiusVisiblePathNodes those of them
    // that can see Loc, nothing in the world in the way.
    for (const bool visible : {false, true})
        reg(visible ? "Actor.RadiusVisiblePathNodes" : "Actor.RadiusPathNodes", [=](NativeCall& c) {
            const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : locationOf(c, c.self);
            for (VmObject* a : pointsOf(c, c.args[0])) {
                const Vec p = locationOf(c, a);
                if (length(sub(p, at)) <= c.f(2) && (!visible || c.vm.level->lineOfSight(c.self, p, at)))
                    c.items.push_back(Value::objectValue(a));
            }
        });
    // FindPathToward(Actor anActor, optional bool bSinglePath, optional bool bClearPaths) and
    // FindPathTo(Vector aPoint, ...): the next point on the way (ScriptLevel::findPath).
    reg("Pawn.FindPathToward", [](NativeCall& c) {
        c.result = Value::objectValue(c.vm.level && c.self && c.o(0) ? c.vm.level->findPath(c.self, c.o(0), nullptr)
                                                                       : nullptr);
    });
    reg("Pawn.FindPathTo", [](NativeCall& c) {
        const Vec to = Vm::vectorOf(c.args[0]);
        c.result = Value::objectValue(c.vm.level && c.self ? c.vm.level->findPath(c.self, nullptr, &to) : nullptr);
    });
    // LineOfSightTo(Actor Other): from its eyes (EyeHeight above it) to the
    // other's middle, or failing that its top, nothing in the world in the way.
    reg("Pawn.LineOfSightTo", [=](NativeCall& c) {
        VmObject* other = c.o(0);
        c.result = Value::boolValue(false);
        if (!c.vm.level || !c.self || !other)
            return;
        Vec eye = locationOf(c, c.self);
        if (const Value* e = c.self->find(c.vm, "EyeHeight"))
            eye[2] += e->asFloat();
        Vec there = locationOf(c, other);
        if (c.vm.level->lineOfSight(c.self, eye, there)) {
            c.result = Value::boolValue(true);
            return;
        }
        if (const Value* h = other->find(c.vm, "CollisionHeight"))
            there[2] += h->asFloat() * 0.8f;
        c.result = Value::boolValue(c.vm.level->lineOfSight(c.self, eye, there));
    });
}

} // namespace reluva::unreal
