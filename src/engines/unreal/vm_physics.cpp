// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Movement against the level for a running level (ScriptLevel): actors
// swept through the world (collision.h), each other's cylinders and
// movers' boxes; stepping and sliding; what touches what; zones entered;
// the physics modes scripts set (walking, falling, swimming, flying,
// projectiles, rotating, movers gliding between keyframes); and pawns'
// latent movement. Reluva's own model of UE1's movement, from what the
// scripts set and expect: see docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/actors.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;
constexpr float kWalkable = 0.7f; // floors face up at least this much (about 45 degrees)
constexpr int kPhysNone = 0, kPhysWalking = 1, kPhysFalling = 2, kPhysSwimming = 3, kPhysFlying = 4,
              kPhysRotating = 5, kPhysProjectile = 6, kPhysMovingBrush = 10, kPhysSimpleWalking = 32;

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec& a) { return std::sqrt(dot(a, a)); }
Vec normalOf(const Vec& a) {
    const float l = length(a);
    return l > 0 ? mul(a, 1 / l) : Vec{0, 0, 0};
}

// A rotator's axes (forward, right, up), as Core's GetAxes.
std::array<Vec, 3> axes(const std::array<int32_t, 3>& r) {
    const float p = float(r[0]) * 2 * kPi / 65536, y = float(r[1]) * 2 * kPi / 65536,
                rl = float(r[2]) * 2 * kPi / 65536;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(rl),
                cr = std::cos(rl);
    return {Vec{cp * cy, cp * sy, sp}, Vec{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
            Vec{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
}

// The yaw facing along a direction, in rotator units.
int32_t yawOf(const Vec& d) { return int32_t(std::lround(std::atan2(d[1], d[0]) * 32768 / kPi)) & 0xffff; }

// The difference between two angles, the short way round (-32768-32767).
int32_t angleDiff(int32_t from, int32_t to) {
    int32_t diff = (to - from) & 0xffff;
    return diff > 32767 ? diff - 65536 : diff;
}

// Turns an angle towards another by at most `step` (rotator units).
int32_t turnTowards(int32_t from, int32_t to, float step) {
    const int32_t diff = angleDiff(from, to), s = int32_t(step);
    if (std::abs(diff) <= s)
        return to & 0xffff;
    return (from + (diff > 0 ? s : -s)) & 0xffff;
}

// When a moving cylinder (centre p, moving d, radius r, half height h)
// first touches a still one: the time (0-1) and the normal pushing it
// back; false if it doesn't, or already overlaps (left to separate).
bool cylinderHit(const Vec& p, const Vec& d, float r, float h, const Vec& q, float rq, float hq, float& time,
                 Vec& normal) {
    const float R = r + rq, H = h + hq;
    const float px = p[0] - q[0], py = p[1] - q[1], pz = p[2] - q[2];
    if (px * px + py * py < R * R && std::fabs(pz) < H)
        return false;
    float h0 = -1e9f, h1 = 1e9f; // horizontally within R: |(px, py) + t (dx, dy)| < R
    const float a = d[0] * d[0] + d[1] * d[1], b = 2 * (px * d[0] + py * d[1]), c = px * px + py * py - R * R;
    if (a < 1e-9f) {
        if (c >= 0)
            return false;
    } else {
        const float disc = b * b - 4 * a * c;
        if (disc < 0)
            return false;
        const float sq = std::sqrt(disc);
        h0 = (-b - sq) / (2 * a);
        h1 = (-b + sq) / (2 * a);
    }
    float v0 = -1e9f, v1 = 1e9f; // vertically within H: |pz + t dz| < H
    if (std::fabs(d[2]) < 1e-9f) {
        if (std::fabs(pz) >= H)
            return false;
    } else {
        v0 = (-H - pz) / d[2];
        v1 = (H - pz) / d[2];
        if (v0 > v1)
            std::swap(v0, v1);
    }
    const float t0 = std::max(h0, v0), t1 = std::min(h1, v1);
    if (t0 > t1 || t0 < 0 || t0 > 1)
        return false;
    time = t0;
    if (h0 >= v0)
        normal = normalOf(Vec{px + d[0] * t0, py + d[1] * t0, 0});
    else
        normal = {0, 0, pz > 0 ? 1.0f : -1.0f};
    return true;
}

// When a moving box (centre p, moving d, half sizes e) first touches a
// still box: the time and the face's normal; false if not, or already in.
bool boxHit(const Vec& p, const Vec& d, const Vec& e, const Vec& min, const Vec& max, float& time, Vec& normal) {
    float t0 = -1e9f, t1 = 1e9f;
    int axis = -1;
    bool inside = true;
    for (int k = 0; k < 3; ++k) {
        const float lo = min[size_t(k)] - e[size_t(k)], hi = max[size_t(k)] + e[size_t(k)];
        if (p[size_t(k)] <= lo || p[size_t(k)] >= hi)
            inside = false;
        if (std::fabs(d[size_t(k)]) < 1e-9f) {
            if (p[size_t(k)] <= lo || p[size_t(k)] >= hi)
                return false;
            continue;
        }
        float a = (lo - p[size_t(k)]) / d[size_t(k)], b = (hi - p[size_t(k)]) / d[size_t(k)];
        if (a > b)
            std::swap(a, b);
        if (a > t0) {
            t0 = a;
            axis = k;
        }
        t1 = std::min(t1, b);
    }
    if (inside || t0 > t1 || t0 < 0 || t0 > 1 || axis < 0)
        return false;
    time = t0;
    normal = {0, 0, 0};
    normal[size_t(axis)] = d[size_t(axis)] > 0 ? -1.0f : 1.0f;
    return true;
}

std::string lower(const std::string& s) { return fs::toLower(s); }

} // namespace

// --- Properties ----------------------------------------------------------------------------

// By the class and the name's text where it is: the engine side asks for
// properties by string literals, many times an actor a tick, and interning
// the name each time took a third of a level's run.
Value* ScriptLevel::prop(VmObject* a, const char* name) {
    if (!a || !a->cls)
        return nullptr;
    const SlotKey key{a->cls, name};
    auto it = slots_.find(key);
    if (it == slots_.end()) {
        VmProperty* p = a->cls->findProperty(vm_.name(name));
        it = slots_.emplace(key, p ? p->slot : -1).first;
    }
    return it->second >= 0 && size_t(it->second) < a->values.size() ? &a->values[size_t(it->second)] : nullptr;
}

// Where a class's collision properties are in its values, found once: the
// actor loops below read them for every other actor on every move.
const ScriptLevel::CollideSlots& ScriptLevel::collideSlots(VmObject* a) {
    auto it = collideSlots_.find(a->cls);
    if (it == collideSlots_.end()) {
        CollideSlots c;
        auto slot = [&](const char* name) {
            const VmProperty* p = a->cls->findProperty(vm_.name(name));
            return p ? p->slot : -1;
        };
        c.location = slot("Location");
        c.radius = slot("CollisionRadius");
        c.height = slot("CollisionHeight");
        c.collideActors = slot("bCollideActors");
        c.blockActors = slot("bBlockActors");
        c.blockPlayers = slot("bBlockPlayers");
        c.isPlayer = slot("bIsPlayer");
        c.owner = slot("Owner");
        c.base = slot("Base");
        c.mover = vm_.isA(a, "Mover");
        it = collideSlots_.emplace(a->cls, c).first;
    }
    return it->second;
}

// Every actor with its class's collision slots (those the loops below go
// through), made again each tick and added to as actors are spawned.
const std::vector<std::pair<VmObject*, const ScriptLevel::CollideSlots*>>& ScriptLevel::colliders() {
    if (collidersTick_ != ticks_ + 1 || collidersCount_ != actors_.size()) {
        if (collidersTick_ != ticks_ + 1 || collidersCount_ > actors_.size())
            colliders_.clear();
        for (size_t i = colliders_.empty() ? 0 : collidersCount_; i < actors_.size(); ++i)
            colliders_.push_back({actors_[i], &collideSlots(actors_[i])});
        collidersTick_ = ticks_ + 1;
        collidersCount_ = actors_.size();
    }
    return colliders_;
}

namespace {
// A value by its slot (none for -1).
Value* slotOf(VmObject* a, int slot) {
    return slot >= 0 && size_t(slot) < a->values.size() ? &a->values[size_t(slot)] : nullptr;
}
Vec vecOf(Value* v) { return v ? Vm::vectorOf(*v) : Vec{0, 0, 0}; }
float floatOf(Value* v, float fallback = 0) { return v ? v->asFloat() : fallback; }
bool boolOf(Value* v) { return v && v->asBool(); }
} // namespace

Vec ScriptLevel::extentOf(VmObject* a) {
    const float r = floatOf(prop(a, "CollisionRadius")), h = floatOf(prop(a, "CollisionHeight"));
    return {r, r, h};
}

// --- The world -----------------------------------------------------------------------------

bool ScriptLevel::clear(const Vec& point, const Vec& extent, VmObject* self) {
    if (collision_ && (extent[0] > 0 || extent[2] > 0 ? collision_->solid(Vec3f{point[0], point[1], point[2]}, Vec3f{extent[0], extent[1], extent[2]})
                                                      : collision_->solid(Vec3f{point[0], point[1], point[2]})))
        return false;
    // Movers that block it count too.
    for (VmObject* m : movers_)
        if (m != self && !m->deleted && boolOf(prop(m, "bCollideActors")) &&
            (boolOf(prop(m, "bBlockActors")) || boolOf(prop(m, "bBlockPlayers"))) && moverOverlaps(m, point, extent))
            return false;
    return true;
}

bool ScriptLevel::place(VmObject* a, const Vec& point) {
    Value* loc = prop(a, "Location");
    if (!loc)
        return false;
    const Vec e = extentOf(a);
    if (!boolOf(prop(a, "bCollideWorld")) || clear(point, e, a)) {
        *loc = vm_.vectorValue(point[0], point[1], point[2]);
        updateRegion(a);
        return true;
    }
    // Nudged: up first (most such starts are a little into the floor),
    // then sideways, then both.
    for (int step = 1; step <= 16; ++step) {
        const float d = float(step) * 2;
        const Vec tries[] = {{0, 0, d}, {d, 0, 0}, {-d, 0, 0}, {0, d, 0}, {0, -d, 0}, {d, d, d}, {-d, -d, d},
                             {d, -d, d}, {-d, d, d}, {0, 0, -d}};
        for (const Vec& t : tries) {
            const Vec at = add(point, t);
            if (clear(at, e, a)) {
                *loc = vm_.vectorValue(at[0], at[1], at[2]);
                updateRegion(a);
                return true;
            }
        }
    }
    ++movement.refused;
    return false;
}

// A mover's box where it is now: its brush's box through its placement.
bool ScriptLevel::brushBox(VmObject* m, Vec& min, Vec& max) {
    auto it = m->package == &map_ ? brushes_.find(m->exportIndex) : brushes_.end();
    if (it == brushes_.end())
        return false;
    const BrushBox& b = it->second;
    const Vec loc = vecOf(prop(m, "Location"));
    const auto rot = prop(m, "Rotation") ? Vm::rotatorOf(*prop(m, "Rotation")) : std::array<int32_t, 3>{0, 0, 0};
    const auto ax = axes(rot);
    min = {1e30f, 1e30f, 1e30f};
    max = {-1e30f, -1e30f, -1e30f};
    for (int c = 0; c < 8; ++c) {
        Vec v{(c & 1) ? b.max[0] : b.min[0], (c & 2) ? b.max[1] : b.min[1], (c & 4) ? b.max[2] : b.min[2]};
        for (size_t k = 0; k < 3; ++k)
            v[k] = (v[k] - b.prePivot[k]) * b.mainScale[k];
        Vec w = add(add(mul(ax[0], v[0]), mul(ax[1], v[1])), mul(ax[2], v[2]));
        for (size_t k = 0; k < 3; ++k) {
            w[k] = w[k] * b.postScale[k] + loc[k];
            min[k] = std::min(min[k], w[k]);
            max[k] = std::max(max[k], w[k]);
        }
    }
    return true;
}

// A mover's brush's own space: a brush point v is at
// location + postScale * (rotation * (mainScale * (v - prePivot))).
struct ScriptLevel::MoverSpace {
    const BrushBox* b = nullptr;
    Vec loc{};
    std::array<Vec, 3> ax{};
    static float nz(float v) { return v != 0 ? v : 1; }
    // A world point (or direction) in the brush's space.
    Vec local(const Vec& w, bool point) const {
        Vec u{};
        for (size_t k = 0; k < 3; ++k)
            u[k] = (w[k] - (point ? loc[k] : 0)) / nz(b->postScale[k]);
        Vec v{dot(u, ax[0]), dot(u, ax[1]), dot(u, ax[2])};
        for (size_t k = 0; k < 3; ++k)
            v[k] = v[k] / nz(b->mainScale[k]) + (point ? b->prePivot[k] : 0);
        return v;
    }
    // A box's half sizes there (bounding it, if the mover is turned).
    Vec extent(const Vec& e) const {
        Vec le{};
        for (size_t k = 0; k < 3; ++k) {
            for (size_t j = 0; j < 3; ++j)
                le[k] += std::fabs(ax[k][j]) * e[j] / std::fabs(nz(b->postScale[j]));
            le[k] /= std::fabs(nz(b->mainScale[k]));
        }
        return le;
    }
    // A normal there back in the world.
    Vec normal(const Vec3f& n) const {
        Vec w{};
        for (size_t k = 0; k < 3; ++k)
            w = add(w, mul(ax[k], n[k] / nz(b->mainScale[k])));
        for (size_t k = 0; k < 3; ++k)
            w[k] /= nz(b->postScale[k]);
        return normalOf(w);
    }
};

bool ScriptLevel::moverSpace(VmObject* m, MoverSpace& space) {
    auto it = m->package == &map_ ? brushes_.find(m->exportIndex) : brushes_.end();
    if (it == brushes_.end() || !it->second.collision)
        return false;
    space.b = &it->second;
    space.loc = vecOf(prop(m, "Location"));
    space.ax = axes(prop(m, "Rotation") ? Vm::rotatorOf(*prop(m, "Rotation")) : std::array<int32_t, 3>{0, 0, 0});
    return true;
}

bool ScriptLevel::moverHit(VmObject* m, const Vec& start, const Vec& delta, const Vec& e, float& time, Vec& normal) {
    MoverSpace sp;
    if (!moverSpace(m, sp)) {
        Vec bmin, bmax;
        return brushBox(m, bmin, bmax) && boxHit(start, delta, e, bmin, bmax, time, normal);
    }
    const Vec s0 = sp.local(start, true), s1 = add(s0, sp.local(delta, false)), le = sp.extent(e);
    const LevelCollision::Hit h = sp.b->collision->trace(Vec3f{s0[0], s0[1], s0[2]}, Vec3f{s1[0], s1[1], s1[2]},
                                                         Vec3f{le[0], le[1], le[2]});
    if (!h.hit() || h.startSolid) // starting in it (it moved into the actor): let it get out
        return false;
    time = h.time;
    normal = sp.normal(h.normal);
    return true;
}

bool ScriptLevel::moverOverlaps(VmObject* m, const Vec& centre, const Vec& e) {
    MoverSpace sp;
    if (!moverSpace(m, sp)) {
        Vec bmin, bmax;
        if (!brushBox(m, bmin, bmax))
            return false;
        for (size_t k = 0; k < 3; ++k)
            if (centre[k] + e[k] <= bmin[k] || centre[k] - e[k] >= bmax[k])
                return false;
        return true;
    }
    const Vec c = sp.local(centre, true), le = sp.extent(e);
    return sp.b->collision->solid(Vec3f{c[0], c[1], c[2]}, Vec3f{le[0], le[1], le[2]});
}

// --- Moving ---------------------------------------------------------------------------------

ScriptLevel::MoveHit ScriptLevel::moveActor(VmObject* a, const Vec& delta) {
    MoveHit out;
    Value* locValue = prop(a, "Location");
    if (!locValue || a->deleted)
        return out;
    ++movement.moves;
    Vec start = vecOf(locValue);
    const Vec e = extentOf(a);
    const bool collideWorld = boolOf(prop(a, "bCollideWorld"));
    const bool collideActors = boolOf(prop(a, "bCollideActors"));
    float time = 1;
    Vec n{};
    VmObject* by = nullptr;
    if (collideWorld && collision_ && length(delta) > 0) {
        auto sweep = [&] {
            return collision_->trace(Vec3f{start[0], start[1], start[2]},
                                     Vec3f{start[0] + delta[0], start[1] + delta[1], start[2] + delta[2]}, Vec3f{e[0], e[1], e[2]});
        };
        LevelCollision::Hit h = sweep();
        if (h.startSolid && !clear(add(start, delta), e, a)) {
            // Starting a little in solid (a sliver at a corner the box
            // sweep doesn't round off) and not moving out of it: nudged
            // to the nearest open spot first, else it stays put.
            if (place(a, start) && vecOf(locValue) != start) {
                start = vecOf(locValue);
                h = sweep();
            }
            if (h.startSolid) {
                time = 0;
                n = mul(normalOf(delta), -1);
                by = levelInfo_;
            }
        }
        if (!h.startSolid && h.time < time) {
            time = h.time;
            n = {h.normal[0], h.normal[1], h.normal[2]};
            by = levelInfo_;
        }
    }
    // Actors and movers that block it.
    if (collideActors && length(delta) > 0) {
        const bool player = boolOf(prop(a, "bIsPlayer"));
        const bool blocks = boolOf(prop(a, "bBlockActors")), blocksPlayers = boolOf(prop(a, "bBlockPlayers"));
        Value* ownerValue = prop(a, "Owner");
        VmObject* owner = ownerValue ? ownerValue->asObject() : nullptr;
        for (const auto& [o, slots] : colliders()) {
            if (o == a || o->deleted || o == owner)
                continue;
            const CollideSlots& cs = *slots;
            if (!boolOf(slotOf(o, cs.collideActors)))
                continue;
            if (Value* oo = slotOf(o, cs.owner); oo && oo->asObject() == a)
                continue;
            const bool blocked = (boolOf(slotOf(o, cs.blockActors)) && blocks) ||
                                 (boolOf(slotOf(o, cs.blockPlayers)) && player) ||
                                 (blocksPlayers && boolOf(slotOf(o, cs.isPlayer)));
            if (!blocked)
                continue;
            float t = 1;
            Vec on{};
            const bool hit = cs.mover
                                 ? moverHit(o, start, delta, e, t, on)
                                 : cylinderHit(start, delta, e[0], e[2], vecOf(prop(o, "Location")),
                                               floatOf(prop(o, "CollisionRadius")), floatOf(prop(o, "CollisionHeight")), t, on);
            if (hit && t < time) {
                time = t;
                n = on;
                by = o;
            }
        }
    }
    if (time < 1) {
        // Stopped a little short.
        const float len = length(delta);
        time = len > 0 ? std::max(0.0f, time - 0.1f / len) : 0;
        if (by == levelInfo_)
            ++movement.blockedByWorld;
        else
            ++movement.blockedByActors;
    }
    const Vec end = add(start, mul(delta, time));
    *locValue = vm_.vectorValue(end[0], end[1], end[2]);
    out.time = time;
    out.normal = n;
    out.actor = by;
    // Where it is now: its zone, what it touches.
    Value* region = prop(a, "Region");
    VmObject* oldZone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    updateRegion(a);
    VmObject* newZone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    if (newZone != oldZone && newZone) {
        ++movement.zoneChanges;
        if (oldZone)
            vm_.callEvent(oldZone, "ActorLeaving", {Value::objectValue(a)});
        vm_.callEvent(a, "ZoneChange", {Value::objectValue(newZone)});
        vm_.callEvent(newZone, "ActorEntered", {Value::objectValue(a)});
    }
    if (collideActors)
        updateTouching(a);
    if (by && by != levelInfo_) { // bumped into an actor: both told
        vm_.callEvent(a, "Bump", {Value::objectValue(by)});
        vm_.callEvent(by, "Bump", {Value::objectValue(a)});
    }
    return out;
}

ScriptLevel::MoveHit ScriptLevel::moveSmooth(VmObject* a, const Vec& delta) {
    MoveHit h = moveActor(a, delta);
    if (!h.hit() || a->deleted)
        return h;
    Vec remaining = mul(delta, 1 - h.time);
    const bool walking = prop(a, "Physics") && (prop(a, "Physics")->asInt() == kPhysWalking ||
                                                prop(a, "Physics")->asInt() == kPhysSimpleWalking);
    // A wall: up a step if it's low enough (walking), else along it.
    if (walking && h.normal[2] < kWalkable) {
        const float stepHeight = prop(a, "MaxStepHeight") ? floatOf(prop(a, "MaxStepHeight")) : 25;
        Value* loc = prop(a, "Location");
        const Value saved = *loc;
        const MoveHit up = moveActor(a, {0, 0, stepHeight});
        const MoveHit across = moveActor(a, remaining);
        const MoveHit down = moveActor(a, {0, 0, -(stepHeight * (up.time > 0 ? up.time : 0) + 2)});
        if (across.time > 0.05f && down.hit() && down.normal[2] >= kWalkable) {
            ++movement.steps;
            return across;
        }
        *loc = saved; // no step there: back, and slide
        updateRegion(a);
    }
    if (h.actor == levelInfo_ && h.normal[2] < kWalkable)
        vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(levelInfo_)});
    for (int slide = 0; slide < 2 && !a->deleted; ++slide) {
        const Vec n = h.normal;
        Vec along = sub(remaining, mul(n, dot(remaining, n)));
        if (walking && n[2] < kWalkable)
            along[2] = std::min(along[2], 0.0f); // walkers don't climb walls by sliding
        if (length(along) < 0.01f)
            break;
        h = moveActor(a, along);
        if (!h.hit())
            break;
        remaining = mul(along, 1 - h.time);
    }
    return h;
}

void ScriptLevel::updateTouching(VmObject* a) {
    const Vec p = vecOf(prop(a, "Location"));
    const float r = floatOf(prop(a, "CollisionRadius")), h = floatOf(prop(a, "CollisionHeight"));
    std::vector<VmObject*>& now = touching_[a];
    std::vector<VmObject*> touching;
    for (const auto& [o, slots] : colliders()) {
        if (o == a || o->deleted)
            continue;
        const CollideSlots& cs = *slots;
        if (cs.mover || !boolOf(slotOf(o, cs.collideActors)))
            continue;
        const Vec q = vecOf(slotOf(o, cs.location));
        const float R = r + floatOf(slotOf(o, cs.radius)), H = h + floatOf(slotOf(o, cs.height));
        const float dx = p[0] - q[0], dy = p[1] - q[1];
        if (dx * dx + dy * dy < R * R && std::fabs(p[2] - q[2]) < H)
            touching.push_back(o);
    }
    // Those it stopped touching, then those it started to.
    const std::vector<VmObject*> was = now;
    for (VmObject* o : was)
        if (std::find(touching.begin(), touching.end(), o) == touching.end()) {
            auto& theirs = touching_[o];
            theirs.erase(std::remove(theirs.begin(), theirs.end(), a), theirs.end());
            now.erase(std::remove(now.begin(), now.end(), o), now.end());
            vm_.callEvent(a, "UnTouch", {Value::objectValue(o)});
            vm_.callEvent(o, "UnTouch", {Value::objectValue(a)});
        }
    for (VmObject* o : touching)
        if (std::find(now.begin(), now.end(), o) == now.end() && !a->deleted && !o->deleted) {
            now.push_back(o);
            touching_[o].push_back(a);
            ++movement.touches;
            vm_.callEvent(o, "Touch", {Value::objectValue(a)});
            vm_.callEvent(a, "Touch", {Value::objectValue(o)});
        }
    // The first four, where scripts see them.
    for (VmObject* x : {a}) {
        Value* t = prop(x, "Touching");
        if (!t)
            continue;
        const auto& list = touching_[x];
        for (size_t k = 0; k < 4; ++k)
            t[k] = Value::objectValue(k < list.size() ? list[k] : nullptr);
    }
}

VmObject* ScriptLevel::trace(VmObject* self, const Vec& start, const Vec& end, const Vec& extent, bool actors,
                             Vec& hitLocation, Vec& hitNormal) {
    float time = 1;
    VmObject* by = nullptr;
    Vec n{};
    if (collision_) {
        const LevelCollision::Hit h = collision_->trace(Vec3f{start[0], start[1], start[2]}, Vec3f{end[0], end[1], end[2]},
                                                        Vec3f{extent[0], extent[1], extent[2]});
        if (h.hit()) {
            time = h.time;
            n = {h.normal[0], h.normal[1], h.normal[2]};
            by = levelInfo_;
        }
    }
    // Movers count as the world; other actors only if asked.
    {
        const Vec d = sub(end, start);
        for (VmObject* o : actors ? actors_ : movers_) {
            if (o == self || o->deleted || !boolOf(prop(o, "bCollideActors")))
                continue;
            if (!boolOf(prop(o, "bBlockActors")) && !boolOf(prop(o, "bProjTarget")) && !boolOf(prop(o, "bBlockPlayers")))
                continue;
            const bool mover = vm_.isA(o, "Mover");
            if (!actors && !mover)
                continue;
            float t = 1;
            Vec on{};
            const bool hit = mover ? moverHit(o, start, d, extent, t, on)
                                   : cylinderHit(start, d, extent[0], extent[2], vecOf(prop(o, "Location")),
                                               floatOf(prop(o, "CollisionRadius")), floatOf(prop(o, "CollisionHeight")), t, on);
            if (hit && t < time) {
                time = t;
                n = on;
                by = o;
            }
        }
    }
    hitLocation = add(start, mul(sub(end, start), time));
    hitNormal = n;
    return by;
}

// --- Physics ---------------------------------------------------------------------------------

void ScriptLevel::rotate(VmObject* a, float dt) {
    Value* rot = prop(a, "Rotation");
    if (!rot)
        return;
    const auto rate = prop(a, "RotationRate") ? Vm::rotatorOf(*prop(a, "RotationRate")) : std::array<int32_t, 3>{};
    auto r = Vm::rotatorOf(*rot);
    if (boolOf(prop(a, "bFixedRotationDir")) || (prop(a, "Physics") && prop(a, "Physics")->asInt() == kPhysRotating &&
                                                 !boolOf(prop(a, "bRotateToDesired")))) {
        for (size_t k = 0; k < 3; ++k)
            r[k] = (r[k] + int32_t(float(rate[k]) * dt)) & 0xffff;
    } else if (boolOf(prop(a, "bRotateToDesired"))) {
        const auto want = prop(a, "DesiredRotation") ? Vm::rotatorOf(*prop(a, "DesiredRotation")) : r;
        for (size_t k = 0; k < 3; ++k)
            r[k] = turnTowards(r[k], want[k], std::fabs(float(rate[k])) * dt);
    } else {
        return;
    }
    *rot = vm_.rotatorValue(r[0], r[1], r[2]);
}

void ScriptLevel::moveBrush(VmObject* m, float dt) {
    // Only while its script has it going somewhere (InterpolateTo):
    // otherwise it stays where it is.
    Value* alpha = prop(m, "PhysAlpha");
    const Value* going = prop(m, "bInterpolating");
    if (!alpha || !going || !going->asBool())
        return;
    const float a = std::min(1.0f, alpha->asFloat() + floatOf(prop(m, "PhysRate")) * dt);
    alpha->f = a;
    const int key = prop(m, "KeyNum") ? std::clamp(prop(m, "KeyNum")->asInt(), 0, 7) : 0;
    Value* keyPos = prop(m, "KeyPos");
    Value* keyRot = prop(m, "KeyRot");
    const Vec target = add(vecOf(prop(m, "BasePos")), keyPos ? Vm::vectorOf(keyPos[key]) : Vec{});
    const auto baseRot = prop(m, "BaseRot") ? Vm::rotatorOf(*prop(m, "BaseRot")) : std::array<int32_t, 3>{};
    const auto keyR = keyRot ? Vm::rotatorOf(keyRot[key]) : std::array<int32_t, 3>{};
    const Vec from = vecOf(prop(m, "OldPos"));
    const auto fromRot = prop(m, "OldRot") ? Vm::rotatorOf(*prop(m, "OldRot")) : std::array<int32_t, 3>{};
    // MoverGlideType 1 (glide by time): eased in and out.
    const bool glide = prop(m, "MoverGlideType") && prop(m, "MoverGlideType")->asInt() == 1;
    const float f = glide ? a * a * (3 - 2 * a) : a;
    const Vec now = add(from, mul(sub(target, from), f));
    const Vec before = vecOf(prop(m, "Location"));
    *prop(m, "Location") = vm_.vectorValue(now[0], now[1], now[2]);
    if (Value* r = prop(m, "Rotation")) {
        std::array<int32_t, 3> rr{};
        for (size_t k = 0; k < 3; ++k)
            rr[k] = fromRot[k] + int32_t(float((baseRot[k] + keyR[k]) - fromRot[k]) * f);
        *r = vm_.rotatorValue(rr[0], rr[1], rr[2]);
    }
    // What stands on it goes with it.
    const Vec moved = sub(now, before);
    if (length(moved) > 0)
        for (const auto& [o, slots] : colliders())
            if (Value* base = o->deleted ? nullptr : slotOf(o, slots->base); base && base->asObject() == m)
                moveActor(o, moved);
    if (a >= 1) {
        if (Value* v = prop(m, "bInterpolating"))
            v->i = 0;
        vm_.callEvent(m, "InterpolateEnd", {Value::objectValue(nullptr)});
    }
}

// Steering towards a pawn's goal (MoveTo, TurnTo...): its acceleration
// and desired rotation, and whether it's there (its latent call then ends).
void ScriptLevel::steer(VmObject* a, float dt) {
    auto it = goals_.find(a);
    if (it == goals_.end())
        return;
    Goal& g = it->second;
    auto finish = [&] {
        if (Value* acc = prop(a, "Acceleration"))
            *acc = vm_.vectorValue(0, 0, 0);
        if (a->latent && a->stateSerial == g.stateSerial)
            a->sleeping = 0; // the latent call ends: its code carries on next tick
        goals_.erase(it);
    };
    if (a->deleted || a->stateSerial != g.stateSerial) { // its state changed: the call was abandoned
        goals_.erase(it);
        return;
    }
    g.timeLeft -= dt;
    const Vec here = vecOf(prop(a, "Location"));
    Vec dest = g.target ? vecOf(prop(g.target, "Location")) : g.point;
    if (g.target && g.target->deleted)
        return finish();
    switch (g.kind) {
    case Goal::Kind::Land:
        if (!prop(a, "Physics") || prop(a, "Physics")->asInt() != kPhysFalling || g.timeLeft <= 0)
            finish();
        return;
    case Goal::Kind::Rotation: {
        const auto r = Vm::rotatorOf(*prop(a, "Rotation"));
        const auto want = prop(a, "DesiredRotation") ? Vm::rotatorOf(*prop(a, "DesiredRotation")) : r;
        if ((std::abs(angleDiff(r[1], want[1])) < 2000 && std::abs(angleDiff(r[0], want[0])) < 2000) || g.timeLeft <= 0)
            finish();
        return;
    }
    case Goal::Kind::Turn: {
        const int32_t yaw = yawOf(sub(dest, here));
        if (Value* d = prop(a, "DesiredRotation")) {
            auto want = Vm::rotatorOf(*d);
            *d = vm_.rotatorValue(want[0], yaw, want[2]);
        }
        const auto r = Vm::rotatorOf(*prop(a, "Rotation"));
        if (std::abs(angleDiff(r[1], yaw)) < 2000 || g.timeLeft <= 0)
            finish();
        return;
    }
    case Goal::Kind::Move: {
        Vec to = sub(dest, here);
        const int physics = prop(a, "Physics") ? prop(a, "Physics")->asInt() : 0;
        if (physics == kPhysWalking || physics == kPhysSimpleWalking)
            to[2] = 0;
        const float reach = std::max(8.0f, floatOf(prop(a, "CollisionRadius")) * 0.5f) +
                            (g.target ? floatOf(prop(g.target, "CollisionRadius")) : 0);
        if (length(to) <= reach || g.timeLeft <= 0) {
            if (g.timeLeft <= 0)
                ++movement.gaveUp;
            finish();
            return;
        }
        const float rate = floatOf(prop(a, "AccelRate"), 500);
        if (Value* acc = prop(a, "Acceleration")) {
            const Vec d = mul(normalOf(to), rate);
            *acc = vm_.vectorValue(d[0], d[1], d[2]);
        }
        if (Value* d = prop(a, "DesiredRotation")) {
            auto want = Vm::rotatorOf(*d);
            Vec faces = to;
            if (g.facing) {
                const Vec focus = g.focusActor && !g.focusActor->deleted ? vecOf(prop(g.focusActor, "Location")) : g.focus;
                faces = sub(focus, here);
                if (Value* f = prop(a, "Focus"))
                    *f = vm_.vectorValue(focus[0], focus[1], focus[2]);
            }
            *d = vm_.rotatorValue(want[0], yawOf(faces), want[2]);
        }
        if (Value* s = prop(a, "DesiredSpeed"))
            s->f = g.speed;
        return;
    }
    }
}

void ScriptLevel::setGoal(VmObject* pawn, const Goal& goal) {
    Goal g = goal;
    g.stateSerial = pawn->stateSerial;
    goals_[pawn] = g;
}

void ScriptLevel::physics(VmObject* a, float dt) {
    if (a->deleted || dt <= 0)
        return;
    Value* physicsValue = prop(a, "Physics");
    const int physics = physicsValue ? physicsValue->asInt() : 0;
    if (physics != kPhysFalling)
        fallers_.erase(a);
    steer(a, dt);
    if (physics == kPhysNone && !boolOf(prop(a, "bRotateToDesired")))
        return;
    // A player's pawn turns as its input says (UpdateRotation), not
    // toward its DesiredRotation.
    const Value* player = prop(a, "Player");
    if (!player || !player->asObject())
        rotate(a, dt);
    if (physics == kPhysMovingBrush) {
        moveBrush(a, dt);
        return;
    }
    Value* velocityValue = prop(a, "Velocity");
    if (!velocityValue)
        return;
    Vec v = vecOf(velocityValue);
    Vec accel = vecOf(prop(a, "Acceleration"));
    // The zone it's in: gravity, friction.
    Value* region = prop(a, "Region");
    VmObject* zone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    const Vec gravity = zone && prop(zone, "ZoneGravity") ? vecOf(prop(zone, "ZoneGravity")) : Vec{0, 0, -950};
    const float groundFriction = zone ? floatOf(prop(zone, "ZoneGroundFriction"), 8) : 8;
    const float fluidFriction = zone ? floatOf(prop(zone, "ZoneFluidFriction"), 1.2f) : 1.2f;
    const float terminal = zone ? floatOf(prop(zone, "ZoneTerminalVelocity"), 2500) : 2500;
    const float accelRate = floatOf(prop(a, "AccelRate"), 500);
    // Velocity towards the acceleration, slowed by friction, up to a speed.
    auto steerVelocity = [&](float friction, float maxSpeed) {
        if (length(accel) > accelRate)
            accel = mul(normalOf(accel), accelRate);
        if (length(accel) < 1e-3f) {
            v = mul(v, std::max(0.0f, 1 - 2 * friction * dt));
        } else {
            const float speed = length(v);
            v = sub(v, mul(sub(v, mul(normalOf(accel), speed)), std::min(1.0f, friction * dt)));
            v = add(v, mul(accel, dt));
        }
        const float desired = prop(a, "DesiredSpeed") ? std::clamp(floatOf(prop(a, "DesiredSpeed"), 1), 0.0f, 1.0f) : 1.0f;
        const float cap = maxSpeed * (desired > 0 ? desired : 1);
        if (length(v) > cap)
            v = mul(normalOf(v), cap);
    };
    switch (physics) {
    case kPhysWalking:
    case kPhysSimpleWalking: {
        // Walkers accelerate at their rate whichever way they're pushed.
        accel[2] = 0;
        if (length(accel) > 1e-3f)
            accel = mul(normalOf(accel), accelRate);
        v[2] = 0;
        steerVelocity(groundFriction, floatOf(prop(a, "GroundSpeed"), 320));
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        if (length(v) > 0.01f)
            moveSmooth(a, mul(v, dt));
        if (a->deleted)
            return;
        // Still on the floor? Down to it, else falling.
        const float stepHeight = prop(a, "MaxStepHeight") ? floatOf(prop(a, "MaxStepHeight")) : 25;
        const Vec here = vecOf(prop(a, "Location"));
        const Vec e = extentOf(a);
        Vec hitLocation, hitNormal;
        VmObject* floor = trace(a, here, {here[0], here[1], here[2] - stepHeight - 4}, e, false, hitLocation, hitNormal);
        if (floor && hitNormal[2] >= kWalkable) {
            if (hitLocation[2] < here[2] - 0.5f)
                *prop(a, "Location") = vm_.vectorValue(here[0], here[1], hitLocation[2] + 0.1f);
            if (Value* base = prop(a, "Base"); base && base->asObject() != floor) {
                base->o = floor;
                vm_.callEvent(a, "BaseChange");
            }
        } else {
            physicsValue->i = kPhysFalling;
            if (Value* base = prop(a, "Base"))
                base->o = nullptr;
            vm_.callEvent(a, "Falling");
        }
        break;
    }
    case kPhysFalling: {
        // Where a fall began: The Fallen's falling damage is by the height
        // fallen from FallHeight, which its scripts set only while rising
        // in a jump, so the engine must set it as a fall starts.
        if (fallers_.insert(a).second)
            if (Value* from = prop(a, "FallHeight"))
                *from = Value::floatValue(vecOf(prop(a, "Location"))[2]);
        // Gravity, a little steering in the air, a top speed.
        accel[2] = 0;
        const float airControl = floatOf(prop(a, "AirControl"), 0.05f);
        v = add(v, mul(add(gravity, mul(accel, airControl)), dt));
        if (length(v) > terminal)
            v = mul(normalOf(v), terminal);
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        // Down until something stops it; off a wall or slope too steep to
        // stand on, the rest of the move slides along it (and may land).
        Vec delta = mul(v, dt);
        MoveHit h = moveActor(a, delta);
        if (!a->deleted && h.hit() && h.normal[2] < kWalkable) {
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(h.actor)});
            if (a->deleted || physicsValue->asInt() != kPhysFalling)
                break;
            const Vec along = sub(v, mul(h.normal, dot(v, h.normal)));
            *velocityValue = vm_.vectorValue(along[0], along[1], along[2]);
            delta = mul(sub(delta, mul(h.normal, dot(delta, h.normal))), 1 - h.time);
            h = length(delta) > 1e-3f ? moveActor(a, delta) : MoveHit{};
        }
        if (a->deleted || !h.hit())
            break;
        if (h.normal[2] >= kWalkable) {
            ++movement.landings;
            vm_.callEvent(a, "Landed", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2])});
            if (!a->deleted && physicsValue->asInt() == kPhysFalling) {
                physicsValue->i = vm_.isA(a, "Pawn") ? kPhysWalking : kPhysNone;
                Vec landed = vecOf(velocityValue);
                landed[2] = 0;
                if (!vm_.isA(a, "Pawn"))
                    landed = {0, 0, 0};
                *velocityValue = vm_.vectorValue(landed[0], landed[1], landed[2]);
            }
            if (Value* base = prop(a, "Base"))
                base->o = h.actor;
        } else if (physicsValue->asInt() == kPhysFalling) {
            // Wedged between two walls: only the fall along both is left.
            Vec now = vecOf(velocityValue);
            now = sub(now, mul(h.normal, std::min(0.0f, dot(now, h.normal))));
            *velocityValue = vm_.vectorValue(now[0], now[1], now[2]);
        }
        break;
    }
    case kPhysSwimming:
    case kPhysFlying: {
        const bool swim = physics == kPhysSwimming;
        steerVelocity(fluidFriction, floatOf(prop(a, swim ? "WaterSpeed" : "AirSpeed"), swim ? 200 : 320));
        if (swim) { // sinks or floats by its buoyancy against its mass
            const float mass = std::max(1.0f, floatOf(prop(a, "Mass"), 100));
            v = add(v, mul(gravity, dt * (1 - floatOf(prop(a, "Buoyancy")) / mass)));
        }
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        moveSmooth(a, mul(v, dt));
        if (swim && !a->deleted) {
            Value* r = prop(a, "Region");
            VmObject* z = r && !r->fields.empty() ? r->fields[0].asObject() : nullptr;
            if (!z || !boolOf(prop(z, "bWaterZone"))) {
                physicsValue->i = kPhysFalling;
                vm_.callEvent(a, "Falling");
            }
        }
        break;
    }
    case kPhysProjectile: {
        v = add(v, mul(accel, dt));
        *velocityValue = vm_.vectorValue(v[0], v[1], v[2]);
        const MoveHit h = moveActor(a, mul(v, dt));
        if (h.hit() && !a->deleted && h.actor == levelInfo_)
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(levelInfo_)});
        break;
    }
    default:
        break; // PHYS_Rotating (above), and The Fallen's own effect physics, not done yet
    }
}

// --- Natives ---------------------------------------------------------------------------------

void registerMovementNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto B = [](bool v) { return Value::boolValue(v); };
    reg("Actor.Move", [=](NativeCall& c) {
        if (!c.vm.level || !c.self) {
            c.result = B(false);
            return;
        }
        c.result = B(!c.vm.level->moveActor(c.self, Vm::vectorOf(c.args[0])).hit());
    });
    reg("Actor.MoveSmooth", [=](NativeCall& c) {
        if (!c.vm.level || !c.self) {
            c.result = B(false);
            return;
        }
        c.result = B(!c.vm.level->moveSmooth(c.self, Vm::vectorOf(c.args[0])).hit());
    });
    reg("Actor.SetLocation", [=](NativeCall& c) {
        c.result = B(c.vm.level && c.self && c.vm.level->place(c.self, Vm::vectorOf(c.args[0])));
    });
    reg("Actor.Trace", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        Value* loc = c.self->find(c.vm, "Location");
        const std::array<float, 3> start = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3])
                                                                             : (loc ? Vm::vectorOf(*loc) : std::array<float, 3>{});
        const std::array<float, 3> extent = c.given.size() > 5 && c.given[5] ? Vm::vectorOf(c.args[5]) : std::array<float, 3>{};
        std::array<float, 3> where{}, n{};
        VmObject* hit = c.vm.level->trace(c.self, start, Vm::vectorOf(c.args[2]), extent, c.b(4), where, n);
        c.args[0] = c.vm.vectorValue(where[0], where[1], where[2]);
        c.args[1] = c.vm.vectorValue(n[0], n[1], n[2]);
        c.result = Value::objectValue(hit);
    });
    reg("Actor.FastTrace", [](NativeCall& c) { // true if nothing in the world is in the way
        if (!c.vm.level || !c.self)
            return;
        Value* loc = c.self->find(c.vm, "Location");
        const std::array<float, 3> start = c.given.size() > 1 && c.given[1] ? Vm::vectorOf(c.args[1])
                                                                             : (loc ? Vm::vectorOf(*loc) : std::array<float, 3>{});
        std::array<float, 3> where{}, n{};
        c.result = Value::boolValue(!c.vm.level->trace(c.self, start, Vm::vectorOf(c.args[0]), {}, false, where, n));
    });
    // CheckFOV(Vector Pos, Rotator Rot, Actor Target, float Radius, float CosFOV,
    // out Vector BestHitLocation, optional bool bCheckExtents, optional bool
    // bCalcVisible, optional float LocationZAdjust, optional float HeightAdjust):
    // how much of Target is seen from Pos looking along Rot. Reluva's reading
    // of how the scripts use it (thresholds of 0, 0.1, 0.5 and 0.99; a head's
    // weight for whom to look at multiplied by it):
    // - 0 if its middle (Location raised by LocationZAdjust) is beyond Radius
    //   or outside the cone CosFOV is the cosine of;
    // - the points looked for are its middle and, with bCheckExtents or
    //   bCalcVisible, its top, bottom and sides (its collision cylinder, its
    //   height by HeightAdjust more), each seen if nothing in the world is in
    //   the way;
    // - with bCalcVisible, the share of them seen; else 1 if any is;
    // - BestHitLocation the first seen (the middle first), where a turret aims.
    reg("Actor.CheckFOV", [](NativeCall& c) {
        c.result = Value::floatValue(0);
        VmObject* target = c.o(2);
        if (!c.vm.level || !c.self || !target || target->deleted)
            return;
        using V = std::array<float, 3>;
        const V from = Vm::vectorOf(c.args[0]);
        const auto r = Vm::rotatorOf(c.args[1]);
        const auto axes = rotationAxes({r[0], r[1], r[2]});
        const float radius = c.f(3), cosFov = c.f(4);
        const bool extents = c.b(6), share = c.b(7);
        const float zAdjust = c.f(8), heightAdjust = c.f(9);
        const Value* loc = target->find(c.vm, "Location");
        if (!loc)
            return;
        V middle = Vm::vectorOf(*loc);
        middle[2] += zAdjust;
        c.args[5] = c.vm.vectorValue(middle[0], middle[1], middle[2]);
        const V to{middle[0] - from[0], middle[1] - from[1], middle[2] - from[2]};
        const float distance = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (distance > radius)
            return;
        if (distance > 1e-3f &&
            (to[0] * axes[0][0] + to[1] * axes[0][1] + to[2] * axes[0][2]) / distance < cosFov)
            return;
        std::vector<V> points{middle};
        if (extents || share) {
            const Value* h = target->find(c.vm, "CollisionHeight");
            const Value* rad = target->find(c.vm, "CollisionRadius");
            const float up = 0.9f * ((h ? h->asFloat() : 0) + heightAdjust), side = 0.9f * (rad ? rad->asFloat() : 0);
            // Across the line of sight, level.
            V across{-to[1], to[0], 0};
            const float l = std::sqrt(across[0] * across[0] + across[1] * across[1]);
            across = l > 1e-3f ? V{across[0] / l, across[1] / l, 0} : V{0, 1, 0};
            points.push_back({middle[0], middle[1], middle[2] + up});
            points.push_back({middle[0], middle[1], middle[2] - up});
            points.push_back({middle[0] + across[0] * side, middle[1] + across[1] * side, middle[2]});
            points.push_back({middle[0] - across[0] * side, middle[1] - across[1] * side, middle[2]});
        }
        int seen = 0;
        for (const V& p : points) {
            V where{}, n{};
            if (c.vm.level->trace(c.self, from, p, {}, false, where, n))
                continue;
            if (seen++ == 0)
                c.args[5] = c.vm.vectorValue(p[0], p[1], p[2]);
            if (!share)
                break;
        }
        c.result = Value::floatValue(share ? float(seen) / float(points.size()) : seen > 0 ? 1.0f : 0.0f);
    });
    reg("Actor.PointCheck", [](NativeCall& c) { // true if there's room there
        const std::array<float, 3> extent = c.given.size() > 1 && c.given[1] ? Vm::vectorOf(c.args[1]) : std::array<float, 3>{};
        c.result = Value::boolValue(!c.vm.level || c.vm.level->clear(Vm::vectorOf(c.args[0]), extent));
    });
    for (const char* run : {"Actor.AutonomousPhysics", "Actor.PerformPhysics"})
        reg(run, [](NativeCall& c) {
            if (c.vm.level && c.self)
                c.vm.level->physics(c.self, c.f(0));
        });
    reg("Actor.SetPhysics", [](NativeCall& c) {
        Value* v = c.self ? c.self->find(c.vm, "Physics") : nullptr;
        if (!v)
            return;
        const int was = v->i;
        v->i = c.i(0) & 0xff;
        if (was == 1 && v->i == 2) // walking off: falling
            c.vm.callEvent(c.self, "Falling");
    });
    // Pawns' latent movement: the goal set, the call waiting until the
    // level's physics says it's there.
    auto goal = [](NativeCall& c, ScriptLevel::Goal g) {
        if (!c.vm.level || !c.self)
            return;
        c.vm.level->setGoal(c.self, g);
        c.latentSeconds = 1e9f; // until the goal ends it
    };
    reg("Pawn.MoveTo", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.point = Vm::vectorOf(c.args[0]);
        g.speed = c.given.size() > 1 && c.given[1] ? c.f(1) : 1;
        if (Value* d = c.self ? c.self->find(c.vm, "Destination") : nullptr)
            *d = c.args[0];
        goal(c, g);
    });
    // MoveTo2(Vector NewDestination, optional float Speed): The Fallen's,
    // taken as MoveTo. StrafeTo(Vector NewDestination, Vector NewFocus) and
    // StrafeFacing(Vector NewDestination, Actor NewTarget): there, facing
    // the focus all the way.
    reg("Pawn.MoveTo2", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.point = Vm::vectorOf(c.args[0]);
        g.speed = c.given.size() > 1 && c.given[1] ? c.f(1) : 1;
        if (Value* d = c.self ? c.self->find(c.vm, "Destination") : nullptr)
            *d = c.args[0];
        goal(c, g);
    });
    for (const bool actor : {false, true})
        reg(actor ? "Pawn.StrafeFacing" : "Pawn.StrafeTo", [=](NativeCall& c) {
            ScriptLevel::Goal g;
            g.point = Vm::vectorOf(c.args[0]);
            g.facing = true;
            if (actor)
                g.focusActor = c.o(1);
            else
                g.focus = Vm::vectorOf(c.args[1]);
            if (actor && !g.focusActor)
                g.facing = false;
            if (Value* d = c.self ? c.self->find(c.vm, "Destination") : nullptr)
                *d = c.args[0];
            goal(c, g);
        });
    reg("Pawn.MoveToward", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.target = c.o(0);
        g.speed = c.given.size() > 1 && c.given[1] ? c.f(1) : 1;
        if (Value* t = c.self ? c.self->find(c.vm, "MoveTarget") : nullptr)
            t->o = g.target;
        if (!g.target)
            return;
        goal(c, g);
    });
    reg("Pawn.TurnTo", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.kind = ScriptLevel::Goal::Kind::Turn;
        g.point = Vm::vectorOf(c.args[0]);
        g.timeLeft = 3;
        goal(c, g);
    });
    reg("Pawn.TurnToward", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.kind = ScriptLevel::Goal::Kind::Turn;
        g.target = c.o(0);
        g.timeLeft = 3;
        if (!g.target)
            return;
        goal(c, g);
    });
    reg("Pawn.WaitForLanding", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.kind = ScriptLevel::Goal::Kind::Land;
        goal(c, g);
    });
    reg("Pawn.FinishRotation", [=](NativeCall& c) {
        ScriptLevel::Goal g;
        g.kind = ScriptLevel::Goal::Kind::Rotation;
        g.timeLeft = 3;
        goal(c, g);
    });
    // Whether a pawn could walk straight there: nothing in the world in
    // the way of its box (paths round corners aren't looked for yet).
    auto reachable = [](NativeCall& c, const std::array<float, 3>& to) {
        if (!c.vm.level || !c.self)
            return false;
        Value* loc = c.self->find(c.vm, "Location");
        Value* r = c.self->find(c.vm, "CollisionRadius");
        Value* h = c.self->find(c.vm, "CollisionHeight");
        if (!loc)
            return false;
        const float rr = r ? r->asFloat() : 0, hh = h ? h->asFloat() : 0;
        std::array<float, 3> where{}, n{};
        return !c.vm.level->trace(c.self, Vm::vectorOf(*loc), to, {rr * 0.9f, rr * 0.9f, hh * 0.5f}, false, where, n);
    };
    reg("Pawn.pointReachable", [=](NativeCall& c) { c.result = B(reachable(c, Vm::vectorOf(c.args[0]))); });
    reg("Pawn.actorReachable", [=](NativeCall& c) {
        VmObject* o = c.o(0);
        Value* loc = o ? o->find(c.vm, "Location") : nullptr;
        c.result = B(loc && reachable(c, Vm::vectorOf(*loc)));
    });
    (void)lower;
}

} // namespace reluva::unreal
