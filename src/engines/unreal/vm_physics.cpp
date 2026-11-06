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
              kPhysRotating = 5, kPhysProjectile = 6, kPhysMovingBrush = 10, kPhysClambering = 13,
              kPhysClimbing = 14, kPhysHanging = 15, kPhysSimpleWalking = 32;

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const Vec& a) { return std::sqrt(dot(a, a)); }
Vec normalOf(const Vec& a) {
    const float l = length(a);
    return l > 0 ? mul(a, 1 / l) : Vec{0, 0, 0};
}
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
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

const std::vector<std::pair<VmObject*, const ScriptLevel::CollideSlots*>>& ScriptLevel::collidingActors() {
    const auto& all = colliders();
    if (collidingDirty_ || collidingTick_ != ticks_ + 1 || collidingCount_ != all.size()) {
        colliding_.clear();
        for (const auto& c : all) {
            const int slot = c.second->collideActors;
            if (slot >= 0 && size_t(slot) < c.first->values.size() && c.first->values[size_t(slot)].asBool())
                colliding_.push_back(c);
        }
        collidingTick_ = ticks_ + 1;
        collidingCount_ = all.size();
        collidingDirty_ = false;
    }
    return colliding_;
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
            (boolOf(prop(m, "bBlockActors")) || boolOf(prop(m, "bBlockPlayers"))) && moverOverlaps(m, point, extent)) {
            return false;
        }
    return true;
}

bool ScriptLevel::findSpot(const Vec& e, Vec& at) {
    // SinglePointCheck without actors: the level's geometry alone.
    auto fits = [&](const Vec& p, const Vec& ext) {
        if (!collision_)
            return true;
        return ext[0] > 0 || ext[2] > 0 ? !collision_->solid(Vec3f{p[0], p[1], p[2]}, Vec3f{ext[0], ext[1], ext[2]})
                                        : !collision_->solid(Vec3f{p[0], p[1], p[2]});
    };
    // The spot itself in solid: nowhere.
    if (!fits(at, {0, 0, 0}))
        return false;
    if ((e[0] == 0 && e[1] == 0 && e[2] == 0) || fits(at, e))
        return true;
    // AdjustSpot: a line from the spot `offset` out; if the world (or a
    // mover) is in the way, the spot is pushed back off it along its
    // face's normal by (1.05 - how far along) times `push`, and tried.
    Vec spot = at;
    auto adjust = [&](const Vec& offset, float push) {
        Vec hitAt{}, normal{};
        const Vec end = add(spot, offset);
        if (!trace(nullptr, spot, end, {0, 0, 0}, false, hitAt, normal))
            return false;
        const Vec d = sub(end, spot);
        const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const Vec run = sub(hitAt, spot);
        const float t = dd > 0 ? (run[0] * d[0] + run[1] * d[1] + run[2] * d[2]) / dd : 0;
        spot = add(spot, mul(normal, (1.05f - t) * push));
        return fits(spot, e);
    };
    auto done = [&] {
        at = spot;
        return true;
    };
    // Down and up, back and forth, either side, a little past its extent;
    // then towards each corner.
    for (int i = -1; i <= 1; i += 2) {
        const float s = float(i);
        if (adjust({0, 0, s * (e[2] + 2)}, e[2]) || adjust({s * (e[0] + 2), 0, 0}, e[0]) ||
            adjust({0, s * (e[1] + 2), 0}, e[1]))
            return done();
    }
    const float corner = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) + 2;
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2)
            for (int k = -1; k <= 1; k += 2)
                if (adjust({float(i) * e[0], float(j) * e[1], float(k) * e[2]}, corner))
                    return done();
    // Where all that pushed it, if that's not far and it fits there.
    const Vec moved = sub(spot, at);
    if ((moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]) <
            1.5f * (e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) &&
        fits(spot, e))
        return done();
    // Else up from the spot, 20 units at a time, three times.
    spot = at;
    for (int k = 0; k < 3; ++k) {
        spot[2] += 20;
        if (fits(spot, e))
            return done();
    }
    return false;
}

bool ScriptLevel::place(VmObject* a, const Vec& point, bool spawning) {
    Value* loc = prop(a, "Location");
    if (!loc)
        return false;
    Vec at = point;
    if (boolOf(prop(a, "bCollideWorld")) || (spawning && boolOf(prop(a, "bCollideWhenPlacing")))) {
        if (!findSpot(extentOf(a), at)) {
            ++movement.refused;
            return false;
        }
    }
    vm_.setVector(*loc, at[0], at[1], at[2]);
    updateRegion(a);
    return true;
}

bool ScriptLevel::resize(VmObject* a, float r, float h, bool alignToFloor, bool adjustToFit, bool alignToTop) {
    Value* rv = prop(a, "CollisionRadius");
    Value* hv = prop(a, "CollisionHeight");
    Value* loc = prop(a, "Location");
    if (!rv || !hv || !loc)
        return false;
    const float r0 = rv->asFloat(), h0 = hv->asFloat();
    const Value was = *loc;
    Vec at = vecOf(loc);
    if (alignToFloor)
        at[2] += h - h0;
    else if (alignToTop)
        at[2] -= h - h0;
    *rv = Value::floatValue(r);
    *hv = Value::floatValue(h);
    collidersTick_ = -1;
    if (!boolOf(prop(a, "bCollideWorld")) || clear(at, {r, r, h}, a)) {
        vm_.setVector(*loc, at[0], at[1], at[2]);
        updateRegion(a);
        return true;
    }
    if (adjustToFit && place(a, at))
        return true;
    *rv = Value::floatValue(r0);
    *hv = Value::floatValue(h0);
    *loc = was;
    return false;
}

bool ScriptLevel::pickWallAdjust(VmObject* pawn) {
    Value* dest = prop(pawn, "Destination");
    if (!dest)
        return false;
    const Vec from = vecOf(prop(pawn, "Location"));
    const Vec to = vecOf(dest);
    Vec dir{to[0] - from[0], to[1] - from[1], 0};
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    if (len < 1)
        return false;
    dir = mul(dir, 1 / len);
    const Vec e = extentOf(pawn);
    const Vec side{-dir[1], dir[0], 0};
    const float step = std::max(e[0] * 3, 48.0f);
    for (const float s : {1.0f, -1.0f}) {
        // Aside, then on: both clear of the world.
        const Vec aside = add(from, mul(side, s * step));
        const Vec beyond = add(aside, mul(dir, std::min(len, step * 2)));
        Vec where{}, n{};
        if (trace(pawn, from, aside, e, false, where, n) || trace(pawn, aside, beyond, e, false, where, n))
            continue;
        vm_.setVector(*dest, aside[0], aside[1], aside[2]);
        return true;
    }
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

// Whether a box swept from `start` by `delta` comes within reach of a
// mover's brush at all (its bounding sphere): most traces pass far from
// most movers, and placing them in its space is the costly part. It also
// keeps a flat brush's BSP (a force field: one plane, solid all the way
// behind it) from blocking things across the level: CM04_Lab's refused
// 301 placings 2000 units from one.
bool ScriptLevel::moverNear(VmObject* m, const Vec& start, const Vec& delta, const Vec& e) {
    auto it = m->package == &map_ ? brushes_.find(m->exportIndex) : brushes_.end();
    if (it == brushes_.end() || it->second.radius <= 0)
        return true;
    const Vec c = vecOf(prop(m, "Location"));
    const float len2 = dot(delta, delta);
    float t = len2 > 0 ? std::clamp(dot(sub(c, start), delta) / len2, 0.0f, 1.0f) : 0;
    const Vec nearest = add(start, mul(delta, t));
    const float reach = it->second.radius + length(e) + 1;
    return dot(sub(c, nearest), sub(c, nearest)) <= reach * reach;
}

bool ScriptLevel::moverHit(VmObject* m, const Vec& start, const Vec& delta, const Vec& e, float& time, Vec& normal) {
    if (!moverNear(m, start, delta, e))
        return false;
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
    if (!moverNear(m, centre, {0, 0, 0}, e))
        return false;
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
    // Within its brush's box first: a flat brush (a force field's sheet)
    // makes a BSP of one plane, solid all the way behind it.
    for (size_t k = 0; k < 3; ++k)
        if (c[k] + le[k] < sp.b->min[k] - 1 || c[k] - le[k] > sp.b->max[k] + 1)
            return false;
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
    // As ULevel::MoveActor: swept 2 units further than asked, and stopped
    // 2 units back along its way from what it hits.
    const float asked = length(delta);
    const Vec dir = asked > 0 ? mul(delta, 1 / asked) : Vec{0, 0, 0};
    const Vec reach = asked > 0 ? add(delta, mul(dir, 2)) : delta;
    if (collideWorld && collision_ && asked > 0) {
        auto sweep = [&] {
            return collision_->trace(Vec3f{start[0], start[1], start[2]},
                                     Vec3f{start[0] + reach[0], start[1] + reach[1], start[2] + reach[2]}, Vec3f{e[0], e[1], e[2]});
        };
        LevelCollision::Hit h = sweep();
        if (h.startSolid && !clear(add(start, reach), e, a)) {
            // Starting a little in solid (a sliver at a corner the box
            // sweep doesn't round off) and not moving out of it: nudged
            // to the nearest open spot first, else it stays put.
            if (place(a, start) && vecOf(locValue) != start) {
                start = vecOf(locValue);
                h = sweep();
            }
            if (h.startSolid) {
                time = 0;
                n = mul(dir, -1);
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
    if (collideActors && asked > 0) {
        const bool player = boolOf(prop(a, "bIsPlayer"));
        const bool blocks = boolOf(prop(a, "bBlockActors")), blocksPlayers = boolOf(prop(a, "bBlockPlayers"));
        Value* ownerValue = prop(a, "Owner");
        VmObject* owner = ownerValue ? ownerValue->asObject() : nullptr;
        for (const auto& [o, slots] : collidingActors()) {
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
                                 ? moverHit(o, start, reach, e, t, on)
                                 : cylinderHit(start, reach, e[0], e[2], vecOf(prop(o, "Location")),
                                               floatOf(prop(o, "CollisionRadius")), floatOf(prop(o, "CollisionHeight")), t, on);
            if (hit && t < time) {
                time = t;
                n = on;
                by = o;
            }
        }
    }
    Vec end = add(start, delta);
    if (time < 1) {
        const float along = (asked + 2) * time;
        if (along <= 2) {
            time = 0;
            end = start;
        } else {
            end = add(start, sub(mul(reach, time), mul(dir, 2)));
            time = (along - 2) / asked;
        }
        if (by == levelInfo_)
            ++movement.blockedByWorld;
        else
            ++movement.blockedByActors;
    }
    vm_.setVector(*locValue, end[0], end[1], end[2]);
    out.time = time;
    out.normal = n;
    out.actor = by;
    // Then, as the engine: what it bumped told (Bump2 then Bump, the other
    // first), what it touches, and last its zone.
    if (by && by != levelInfo_) {
        const Vec hitAt = end;
        vm_.callEvent(by, "Bump2", {Value::objectValue(a), vm_.vectorValue(hitAt[0], hitAt[1], hitAt[2]),
                                    vm_.vectorValue(-n[0], -n[1], -n[2])});
        if (!a->deleted)
            vm_.callEvent(a, "Bump2", {Value::objectValue(by), vm_.vectorValue(hitAt[0], hitAt[1], hitAt[2]),
                                       vm_.vectorValue(n[0], n[1], n[2])});
        if (!by->deleted && !a->deleted)
            vm_.callEvent(by, "Bump", {Value::objectValue(a)});
        if (!a->deleted && !by->deleted)
            vm_.callEvent(a, "Bump", {Value::objectValue(by)});
    }
    if (collideActors && !a->deleted)
        updateTouching(a);
    if (a->deleted)
        return out;
    Value* region = prop(a, "Region");
    VmObject* oldZone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    const Vec now = vecOf(locValue);
    if (region && hasModel_)
        vm_.assign(*region, regionAt(now));
    VmObject* newZone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    if (newZone != oldZone && newZone) {
        ++movement.zoneChanges;
        if (oldZone)
            vm_.callEvent(oldZone, "ActorLeaving", {Value::objectValue(a)});
        vm_.callEvent(a, "ZoneChange", {Value::objectValue(newZone)});
        vm_.callEvent(newZone, "ActorEntered", {Value::objectValue(a)});
    }
    if (!a->deleted)
        updateBodyRegions(a, true);
    return out;
}

// HitWall for a move something stopped, as the engine's processHitWall
// passes it on: not for a pawn that ran into another pawn; for a pawn
// only while it's pushing (some acceleration) and the wall faces back
// along its way to its Destination (dot under MinHitWall; level, when
// walking); and if its script has no HitWall of its own, the pawn just
// gives up its move (MoveTimer -1, bFromWall) unless it's falling.
void ScriptLevel::hitWall(VmObject* a, const MoveHit& h) {
    if (a->deleted || !h.hit() || !h.actor)
        return;
    if (h.actor != levelInfo_ && vm_.isA(h.actor, "Pawn"))
        return;
    Vec n = h.normal;
    if (vm_.isA(a, "Pawn")) {
        const Vec accel = vecOf(prop(a, "Acceleration"));
        if (length(accel) == 0)
            return;
        // Which way it's going: toward its Destination (a CTVE pawn: its
        // acceleration), made one long; walking, flattened (the normal
        // too, and the event gets that).
        const bool walking = prop(a, "Physics") && prop(a, "Physics")->asInt() == kPhysWalking;
        Vec toward = normalOf(vm_.isA(a, "CTVE_Pawn") ? accel : sub(vecOf(prop(a, "Destination")), vecOf(prop(a, "Location"))));
        if (walking)
            toward[2] = 0, n[2] = 0;
        if (dot(toward, n) > floatOf(prop(a, "MinHitWall")))
            return;
        VmFunction* f = vm_.findFunction(a, vm_.name("HitWall"));
        const bool own = f && f->qualified != "Actor.HitWall";
        if (!own && prop(a, "Physics") && prop(a, "Physics")->asInt() != kPhysFalling) {
            if (Value* t = prop(a, "MoveTimer"))
                *t = Value::floatValue(-1);
            if (Value* w = prop(a, "bFromWall"))
                *w = Value::boolValue(true);
            return;
        }
    }
    vm_.callEvent(a, "HitWall", {vm_.vectorValue(n[0], n[1], n[2]), Value::objectValue(h.actor)});
}

// Two walls: the move's rest along the second, or along the crease where
// they meet, as the engine's TwoWallAdjust; nothing if that goes back.
Vec ScriptLevel::twoWallAdjust(const Vec& desired, Vec delta, const Vec& n, const Vec& oldN, float time) {
    if (dot(oldN, n) > 0) {
        delta = mul(sub(delta, mul(n, dot(delta, n))), 1 - time);
        if (dot(delta, desired) <= 0)
            delta = {0, 0, 0};
    } else {
        const Vec crease = normalOf(cross(n, oldN));
        delta = mul(crease, dot(crease, delta) * (1 - time));
        if (dot(delta, desired) < 0)
            delta = mul(delta, -1);
    }
    return delta;
}

// As the engine's moveSmooth: the move, then along what stopped it, then
// (told HitWall only now) along both: at most three moves, no steps.
ScriptLevel::MoveHit ScriptLevel::moveSmooth(VmObject* a, const Vec& delta) {
    MoveHit h = moveActor(a, delta);
    if (!h.hit() || a->deleted)
        return h;
    Vec adjust = mul(sub(delta, mul(h.normal, dot(delta, h.normal))), 1 - h.time);
    if (dot(adjust, delta) < 0)
        return h;
    const Vec first = h.normal;
    h = moveActor(a, adjust);
    if (!h.hit() || a->deleted)
        return h;
    // The second wall: HitWall as it is (the engine's moveSmooth).
    vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(h.actor)});
    if (a->deleted)
        return h;
    adjust = twoWallAdjust(normalOf(delta), adjust, h.normal, first, h.time);
    return moveActor(a, adjust);
}

void ScriptLevel::updateTouching(VmObject* a) {
    const Vec p = vecOf(prop(a, "Location"));
    const float r = floatOf(prop(a, "CollisionRadius")), h = floatOf(prop(a, "CollisionHeight"));
    std::vector<VmObject*>& now = touching_[a];
    std::vector<VmObject*> touching;
    for (const auto& [o, slots] : collidingActors()) {
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

// The surface a line meets: the node whose plane stopped it, or another
// in the same plane (UnrealEd's coplanar nodes, linked by planeNode) whose
// polygon holds the point.
int ScriptLevel::surfaceAt(const Vec& start, const Vec& end) {
    Vec at{};
    const int node = nodeAt(start, end, at);
    if (node < 0)
        return -1;
    const int32_t surf = model_.nodes[size_t(node)].surf;
    return surf >= 0 && size_t(surf) < model_.surfs.size() ? surf : -1;
}

// The BSP node whose polygon a line from start to end hits (of the
// coplanar nodes sharing the plane it stopped at, the one holding the hit
// point), and where; -1 if none.
int ScriptLevel::nodeAt(const Vec& start, const Vec& end, Vec& where) {
    if (!collision_)
        return -1;
    const LevelCollision::Hit hit = collision_->trace({start[0], start[1], start[2]}, {end[0], end[1], end[2]});
    if (!hit.hit() || hit.node < 0)
        return -1;
    where = {hit.location[0], hit.location[1], hit.location[2]};
    const Vec3f& p = hit.location;
    auto holds = [&](const BspNode& n) {
        if (n.vertexCount < 3 || size_t(n.vertPool) + n.vertexCount > model_.verts.size())
            return false;
        int sign = 0;
        for (int k = 0; k < n.vertexCount; ++k) {
            const int32_t ia = model_.verts[size_t(n.vertPool + k)].point;
            const int32_t ib = model_.verts[size_t(n.vertPool + (k + 1) % n.vertexCount)].point;
            if (ia < 0 || ib < 0 || size_t(ia) >= model_.points.size() || size_t(ib) >= model_.points.size())
                return false;
            const Vec3f& a = model_.points[size_t(ia)];
            const Vec3f& b = model_.points[size_t(ib)];
            const float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
            const float px = p[0] - a[0], py = p[1] - a[1], pz = p[2] - a[2];
            const float side = (ey * pz - ez * py) * n.plane[0] + (ez * px - ex * pz) * n.plane[1] +
                               (ex * py - ey * px) * n.plane[2];
            if (std::fabs(side) < 0.01f)
                continue;
            const int s = side > 0 ? 1 : -1;
            if (sign && s != sign)
                return false;
            sign = s;
        }
        return true;
    };
    int32_t found = hit.node;
    for (int32_t at = hit.node, guard = 0; at >= 0 && size_t(at) < model_.nodes.size() && guard < 1000; ++guard) {
        const BspNode& n = model_.nodes[size_t(at)];
        if (holds(n)) {
            found = at;
            break;
        }
        if (n.planeNode <= 0 || n.planeNode == at)
            break;
        at = n.planeNode;
    }
    return found;
}

// GetNodeXOffsets: how far the polygon hit runs to either side of the hit
// point along `side` (lo the nearer edge one way, as a negative distance;
// hi the other way). False if nothing was hit.
bool ScriptLevel::nodeSideExtent(const Vec& start, const Vec& end, const Vec& side, float& lo, float& hi) {
    Vec at{};
    const int node = nodeAt(start, end, at);
    if (node < 0)
        return false;
    const BspNode& n = model_.nodes[size_t(node)];
    lo = 1e30f, hi = -1e30f;
    for (int k = 0; k < n.vertexCount; ++k) {
        const size_t v = size_t(n.vertPool + k);
        if (v >= model_.verts.size())
            return false;
        const int32_t pi = model_.verts[v].point;
        if (pi < 0 || size_t(pi) >= model_.points.size())
            return false;
        const Vec3f& p = model_.points[size_t(pi)];
        const float d = (p[0] - at[0]) * side[0] + (p[1] - at[1]) * side[1] + (p[2] - at[2]) * side[2];
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return n.vertexCount >= 3;
}

// A surface's texture as it is now: swapped by the scripts, or its own.
VmObject* ScriptLevel::surfaceTexture(const Vec& start, const Vec& end) {
    const int surf = surfaceAt(start, end);
    if (surf < 0)
        return nullptr;
    ObjectRef tex = vm_.packages().resolve(map_, model_.surfs[size_t(surf)].texture);
    if (auto it = surfaceSwaps_.surfaces.find(surf); it != surfaceSwaps_.surfaces.end())
        tex = it->second;
    else if (auto w = surfaceSwaps_.world.find({tex.package, tex.exportIndex}); tex && w != surfaceSwaps_.world.end())
        tex = w->second;
    return tex ? vm_.objectAt(*tex.package, tex.exportIndex + 1) : nullptr;
}

void ScriptLevel::swapSurfaceTexture(int surf, VmObject* texture, bool wholeWorld) {
    if (surf < 0 || size_t(surf) >= model_.surfs.size() || !texture || !texture->package || texture->exportIndex < 0)
        return;
    const ObjectRef to{texture->package, texture->exportIndex};
    if (wholeWorld) {
        // Every surface with the texture this one has now.
        ObjectRef from = vm_.packages().resolve(map_, model_.surfs[size_t(surf)].texture);
        if (auto it = surfaceSwaps_.surfaces.find(surf); it != surfaceSwaps_.surfaces.end())
            from = it->second;
        if (from)
            surfaceSwaps_.world[{from.package, from.exportIndex}] = to;
    } else {
        surfaceSwaps_.surfaces[surf] = to;
    }
    ++surfaceSwaps_.serial;
}

bool ScriptLevel::inSolid(const Vec& p) { return collision_ && collision_->solid(Vec3f{p[0], p[1], p[2]}); }

bool ScriptLevel::worldBetween(const Vec& start, const Vec& end, float slack) {
    // An end inside solid (an ambient sound placed in a wall or ceiling)
    // sounds from the wall's face: nothing between.
    if (!collision_ || collision_->solid(Vec3f{end[0], end[1], end[2]}))
        return false;
    const LevelCollision::Hit h =
        collision_->trace(Vec3f{start[0], start[1], start[2]}, Vec3f{end[0], end[1], end[2]}, Vec3f{0, 0, 0});
    const Vec d = sub(end, start);
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return h.hit() && h.time * length < length - slack;
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
        auto check = [&](VmObject* o, const CollideSlots& cs) {
            if (o == self || o->deleted || !boolOf(slotOf(o, cs.collideActors)))
                return;
            if (!boolOf(slotOf(o, cs.blockActors)) && !boolOf(prop(o, "bProjTarget")) &&
                !boolOf(slotOf(o, cs.blockPlayers)))
                return;
            if (!actors && !cs.mover)
                return;
            float t = 1;
            Vec on{};
            const bool hit = cs.mover ? moverHit(o, start, d, extent, t, on)
                                      : cylinderHit(start, d, extent[0], extent[2], vecOf(slotOf(o, cs.location)),
                                                    floatOf(slotOf(o, cs.radius)), floatOf(slotOf(o, cs.height)), t, on);
            if (hit && t < time) {
                time = t;
                n = on;
                by = o;
            }
        };
        if (actors)
            for (const auto& [o, cs] : collidingActors())
                check(o, *cs);
        else
            for (VmObject* o : movers_)
                check(o, collideSlots(o));
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
            vm_.setVector(*acc, 0, 0, 0);
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
            vm_.setVector(*acc, d[0], d[1], d[2]);
        }
        if (Value* d = prop(a, "DesiredRotation")) {
            auto want = Vm::rotatorOf(*d);
            Vec faces = to;
            if (g.facing) {
                const Vec focus = g.focusActor && !g.focusActor->deleted ? vecOf(prop(g.focusActor, "Location")) : g.focus;
                faces = sub(focus, here);
                if (Value* f = prop(a, "Focus"))
                    vm_.setVector(*f, focus[0], focus[1], focus[2]);
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
    if (physics != kPhysClambering && clambers_.count(a))
        endClamber(a);
    steer(a, dt);
    // Turning, as the engine's performPhysics, after the move: an actor
    // (not a pawn) only in PHYS_Rotating (The Fallen's 23 and 24 turn one
    // axis: fallenPhysics); a pawn as physicsRotation has it (not
    // climbing walls; Newtonian ones newtonianRotation); a player's pawn
    // as its input says (UpdateRotation). Then a pawn's MoveTimer.
    const bool pawn = vm_.isA(a, "Pawn");
    const Vec velocityBefore = vecOf(prop(a, "Velocity"));
    move(a, physics, dt);
    if (a->deleted)
        return;
    const Value* player = prop(a, "Player");
    if (!pawn) {
        if (physics == kPhysRotating)
            rotate(a, dt);
    } else if ((!player || !player->asObject()) && physics != 11) {
        if (physics == 17)
            rotate(a, dt);
        else
            pawnRotation(a, dt, velocityBefore);
    }
    if (pawn)
        if (Value* t = prop(a, "MoveTimer"))
            *t = Value::floatValue(t->asFloat() - dt);
}

// Out of the world (its Region's zone 0), as the engine's physics checks
// before moving: a pawn walking, or anything falling, is told
// FellOutOfWorld (its scripts destroy it; a pawn's die, 'Fell'); a pawn
// flying or Newtonian that collides with the world is gone ("flew out of
// the world") but a player's, which only doesn't move; any other actor
// Newtonian that collides with the world is gone, a projectile gone
// whatever. Each logged as the engine does (falling: an inventory item,
// decoration or pawn). True if it didn't move.
bool ScriptLevel::outOfWorld(VmObject* a, int physics) {
    constexpr int kPhysNewtonian = 17;
    if (!hasModel_ || (physics != kPhysWalking && physics != kPhysFalling && physics != kPhysFlying &&
                       physics != kPhysProjectile && physics != kPhysNewtonian))
        return false;
    const Value* region = prop(a, "Region");
    if (!region || region->fields.size() < 3 || region->fields[2].asInt() != 0)
        return false;
    const bool pawn = vm_.isA(a, "Pawn");
    auto log = [&](const char* what) {
        if (vm_.logger)
            vm_.logger(vm_.objectName(a) + what);
    };
    if (physics == kPhysWalking || physics == kPhysFalling) {
        if (physics == kPhysWalking && !pawn)
            return false;
        if (pawn || vm_.isA(a, "Inventory") || vm_.isA(a, "Decoration"))
            log(" fell out of the world!");
        vm_.callEvent(a, "FellOutOfWorld");
        return true;
    }
    if (physics == kPhysProjectile) {
        destroy(a);
        return true;
    }
    if ((physics == kPhysFlying && !pawn) || !pb(a, "bCollideWorld"))
        return false;
    log(" flew out of the world!");
    if (!pawn || !pb(a, "bIsPlayer"))
        destroy(a);
    return true;
}

void ScriptLevel::move(VmObject* a, int physics, float dt) {
    Value* physicsValue = prop(a, "Physics");
    if (physics == kPhysNone)
        return;
    if (outOfWorld(a, physics))
        return;
    if (physics == kPhysMovingBrush) {
        moveBrush(a, dt);
        return;
    }
    Value* velocityValue = prop(a, "Velocity");
    if (!velocityValue)
        return;
    Vec v = vecOf(velocityValue);
    Vec accel = vecOf(prop(a, "Acceleration"));
    switch (physics) {
    case kPhysSimpleWalking:
        // Only a plain pawn's (APawn::physSimpleWalking: the leeches);
        // The Fallen's own pawns don't move in it.
        if (!vm_.isA(a, "CTVE_Pawn") && vm_.isA(a, "Pawn"))
            simpleWalk(a, dt);
        break;
    case kPhysWalking:
        if (vm_.isA(a, "CTVE_PlayerPawn"))
            walkPlayer(a, dt);
        else if (vm_.isA(a, "CTVE_Pawn")) {
            // A step's rise or drop eased out of how it's drawn (PrePivot),
            // a tenth of a second or so.
            const float z0 = vecOf(prop(a, "Location"))[2];
            walkPawn(a, dt, 4);
            Value* pivot = prop(a, "PivotOffset");
            if (pivot && !a->deleted) {
                Vec po = vecOf(pivot);
                if (physicsOf(a) == kPhysWalking)
                    po[2] += z0 - vecOf(prop(a, "Location"))[2];
                if (po[2] != 0) {
                    po[2] -= po[2] * std::min(10 * dt, 1.0f);
                    vm_.setVector(*pivot, po[0], po[1], po[2]);
                    const Vec orig = vecOf(prop(a, "OriginalPrePivot"));
                    if (Value* pp = prop(a, "PrePivot"))
                        vm_.setVector(*pp, orig[0] + po[0], orig[1] + po[1], orig[2] + po[2]);
                }
            }
        } else if (vm_.isA(a, "Pawn"))
            walkPawn(a, dt, 8);
        break;
    case kPhysFalling: {
        // Where a fall began: The Fallen's falling damage is by the height
        // fallen from FallHeight, which its scripts set only while rising
        // in a jump (nor does the engine), so Reluva sets it as a fall
        // starts.
        if (fallers_.insert(a).second)
            if (Value* from = prop(a, "FallHeight"))
                *from = Value::floatValue(vecOf(prop(a, "Location"))[2]);
        if (vm_.isA(a, "CTVE_PlayerPawn"))
            fallPlayer(a, dt);
        else
            fall(a, dt);
        if (!a->deleted && physicsOf(a) == kPhysFalling)
            climbCheck(a, true, dt);
        break;
    }
    case kPhysSwimming:
    case kPhysFlying: {
        if (!vm_.isA(a, "Pawn"))
            break; // a pawn's physics only
        const bool swim = physics == kPhysSwimming;
        if (swim && !vm_.isA(a, "PlayerPawn")) {
            swimPawn(a, dt, false);
            break;
        }
        swimOrFly(a, dt, swim);
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
        vm_.setVector(*velocityValue, v[0], v[1], v[2]);
        const MoveHit h = moveActor(a, mul(v, dt));
        if (h.hit() && !a->deleted && h.actor == levelInfo_)
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(levelInfo_)});
        break;
    }
    case kPhysClambering:
    case kPhysClimbing:
    case kPhysHanging:
        climbPhysics(a, physics, dt);
        break;
    default:
        fallenPhysics(a, physics, dt); // PHYS_Rotating's done above
        break;
    }
}

// --- Walking, falling, swimming, flying: as Engine.dll's ------------------------------------
// (read for what they do; see docs/engines/unreal_vm.md, Movement.)

ScriptLevel::ZoneFeel ScriptLevel::zoneFeel(VmObject* a) {
    ZoneFeel z;
    Value* region = prop(a, "Region");
    VmObject* zone = region && !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    if (zone) {
        if (Value* g = prop(zone, "ZoneGravity"))
            z.gravity = vecOf(g);
        z.groundFriction = floatOf(prop(zone, "ZoneGroundFriction"), 8);
        z.fluidFriction = floatOf(prop(zone, "ZoneFluidFriction"), 1.2f);
        z.terminal = floatOf(prop(zone, "ZoneTerminalVelocity"), 2500);
        if (Value* zv = prop(zone, "ZoneVelocity"))
            z.velocity = vecOf(zv);
        z.water = boolOf(prop(zone, "bWaterZone"));
    }
    return z;
}

// Velocity towards the acceleration (calcVelocity): the acceleration only
// capped at AccelRate (30% of it walking or crouching); turned towards
// it by friction; with none, braking (the player's in one go and stopped
// under 2.2 a second, the AI's in steps of 0.03 s and stopped under 10);
// then up to the speed, walking easing down to 30% of it.
// A crawler's walk (APawn::physSimpleWalking, SR3_Leech): forward the
// way it faces at GroundSpeed (its acceleration aside, its AccelRate held
// at 300), along the floor found 10 over and under where that takes it,
// 2 above it; blocked, it tries the plain move (going down) or 60 a second
// up and then across (level or up), no sliding. No floor ahead, or none
// within 10 under it after: Falling. Standing on a slope steeper than 0.6:
// HitWall. Its Floor and Base are left as they were.
void ScriptLevel::simpleWalk(VmObject* a, float dt) {
    Value* vel = prop(a, "Velocity");
    Value* acc = prop(a, "Acceleration");
    if (!vel || dt <= 0)
        return;
    Vec v = vecOf(vel);
    v[2] = 0;
    if (acc) {
        Vec ac = vecOf(acc);
        ac[2] = 0;
        vm_.setVector(*acc, ac[0], ac[1], ac[2]);
    }
    if (Value* r = prop(a, "AccelRate"))
        *r = Value::floatValue(300);
    const float speed = floatOf(prop(a, "GroundSpeed"), 320);
    const Value* rot = prop(a, "Rotation");
    const auto r = rot ? Vm::rotatorOf(*rot) : std::array<int32_t, 3>{};
    const float yaw = float(r[1]) * 3.14159265f / 32768, pitch = float(r[0]) * 3.14159265f / 32768;
    v = mul(Vec{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)}, speed);
    if (Value* old = prop(a, "OldLocation")) {
        const Vec l = vecOf(prop(a, "Location"));
        vm_.setVector(*old, l[0], l[1], l[2]);
    }
    vm_.setVector(*vel, v[0], v[1], v[2]);
    const Vec extent{floatOf(prop(a, "CollisionRadius"), 17), floatOf(prop(a, "CollisionRadius"), 17),
                     floatOf(prop(a, "CollisionHeight"), 17)};
    auto fall = [&] {
        if (Value* ph = prop(a, "Physics"))
            ph->i = kPhysFalling;
        vm_.callEvent(a, "Falling");
    };
    const Vec here = vecOf(prop(a, "Location"));
    const Vec dest = add(here, mul(v, dt));
    Vec at, n;
    const Vec probeTop{dest[0], dest[1], dest[2] + 10};
    if (!trace(a, probeTop, {dest[0], dest[1], dest[2] - 10}, extent, false, at, n)) {
        fall();
    } else {
        const float t = length(sub(at, probeTop)) / 20;
        const Vec floorPt{dest[0], dest[1], dest[2] + 10 - 20 * t + 2};
        v = mul(normalOf(sub(floorPt, here)), speed);
        vm_.setVector(*vel, v[0], v[1], v[2]);
        const MoveHit h = moveActor(a, mul(v, dt));
        if (!a->deleted && h.hit()) {
            if (floorPt[2] < dest[2]) {
                moveActor(a, sub(dest, vecOf(prop(a, "Location"))));
            } else {
                moveActor(a, {0, 0, 60 * dt});
                if (!a->deleted)
                    moveActor(a, {v[0] * dt, v[1] * dt, 0});
            }
        }
    }
    if (a->deleted)
        return;
    const Vec now = vecOf(prop(a, "Location"));
    VmObject* by = trace(a, now, {now[0], now[1], now[2] - 10}, extent, false, at, n);
    if (!by) {
        if (physicsOf(a) == kPhysSimpleWalking)
            fall();
        return;
    }
    if (n[2] < 0.6f)
        vm_.callEvent(a, "HitWall", {vm_.vectorValue(n[0], n[1], n[2]), Value::objectValue(by)});
}

// An AI pawn's turn after its move (APawn::physicsRotation): set to turn
// to DesiredRotation (bRotateToDesired on, bFixedRotationDir off); its
// yaw toward the desired at RotationRate.Yaw a second, its pitch the
// desired's, kept within RotationRate.Pitch of level; with a
// RotationRate.Roll, a lean into its turns at speed (the sideways part
// of how its velocity changed, MaxRoll 4096 walking or 28000 else over
// AccelRate, at most RotationRate.Roll, eased in at 5 a second), easing
// back level at 8 a second when it doesn't, or walking under 200.
void ScriptLevel::pawnRotation(VmObject* a, float dt, const Vec& velocityBefore) {
    Value* rot = prop(a, "Rotation");
    if (!rot || dt <= 0)
        return;
    const auto rate = prop(a, "RotationRate") ? Vm::rotatorOf(*prop(a, "RotationRate")) : std::array<int32_t, 3>{};
    auto r = Vm::rotatorOf(*rot);
    const auto want = prop(a, "DesiredRotation") ? Vm::rotatorOf(*prop(a, "DesiredRotation")) : r;
    if (r == want && rate[2] <= 0)
        return;
    if (Value* p = prop(a, "bRotateToDesired"))
        *p = Value::boolValue(true);
    if (Value* p = prop(a, "bFixedRotationDir"))
        *p = Value::boolValue(false);
    r[1] = turnTowards(r[1], want[1], std::fabs(float(rate[1])) * dt) & 0xffff;
    int32_t pitch = int16_t(want[0] & 0xffff);
    const int32_t most = std::abs(rate[0]);
    pitch = std::clamp(pitch, -most, most);
    r[0] = pitch & 0xffff;
    if (rate[2] > 0) {
        float roll = float(int16_t(r[2] & 0xffff));
        const Vec v = vecOf(prop(a, "Velocity"));
        const bool walking = physicsOf(a) == kPhysWalking;
        const Vec change = mul(sub(v, velocityBefore), 1 / dt);
        if ((walking && length(v) < 200) || length(change) <= 100) {
            roll -= roll * std::min(8 * dt, 1.0f);
        } else {
            const float yaw = float(r[1]) * 3.14159265f / 32768, pt = float(int16_t(r[0])) * 3.14159265f / 32768;
            // The side axis of its new facing (roll level).
            const Vec side{-std::sin(yaw), std::cos(yaw), 0};
            (void)pt;
            const float maxRoll = walking ? 4096 : 28000;
            float target = dot(change, side) * maxRoll / std::max(1.0f, floatOf(prop(a, "AccelRate"), 500));
            target = std::clamp(target, -float(rate[2]), float(rate[2]));
            roll += (target - roll) * std::min(5 * dt, 1.0f);
        }
        r[2] = int32_t(roll) & 0xffff;
    } else {
        r[2] = 0;
    }
    *rot = vm_.rotatorValue(r[0], r[1], r[2]);
}

// The water line between a point in the water and one out of it, found by
// halving (APawn::findWaterLine): the last point found in the water.
Vec ScriptLevel::findWaterLine(const Vec& in, const Vec& out) {
    Vec a = in, b = out;
    for (int k = 0; k < 40 && dot(sub(b, a), sub(b, a)) > 0.01f; ++k) {
        const Vec m = mul(add(a, b), 0.5f);
        if (waterAt({m[0], m[1], m[2]}))
            a = m;
        else
            b = m;
    }
    return a;
}

// An AI pawn swimming (APawn::physSwimming) or a fish (physFish):
// calcVelocity in the water (WaterSpeed, the zone's fluid friction,
// buoyant, no braking), then the move with the zone's current in up to
// three goes (APawn::Swim): out of the water, back to the water line (or
// back where it was); head on into a wall (MinHitWall), HitWall and
// stop, else along it. A swimmer's velocity is then what it moved; a
// fish is pulled on toward its Destination when that's over 20 away
// (kept in LastSeenPos too), held to WaterSpeed and faces the way it
// swims. Neither leaves the water: their scripts do that.
void ScriptLevel::swimPawn(VmObject* a, float dt, bool fish) {
    Value* vel = prop(a, "Velocity");
    if (!vel || dt <= 0)
        return;
    const ZoneFeel z = zoneFeel(a);
    const Vec start = vecOf(prop(a, "Location"));
    if (Value* old = prop(a, "OldLocation"))
        vm_.setVector(*old, start[0], start[1], start[2]);
    if (Value* t = prop(a, "bJustTeleported"))
        *t = Value::boolValue(false);
    Vec accel = vecOf(prop(a, "Acceleration"));
    Vec v = vecOf(vel);
    const float most = floatOf(prop(a, "WaterSpeed"), 200);
    calcVelocity(a, v, accel, dt, most, z.fluidFriction, true, false, true, z.gravity);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    Vec delta = mul(add(v, z.velocity), dt);
    const Vec desired = normalOf(delta);
    const float minHitWall = floatOf(prop(a, "MinHitWall"));
    for (int k = 0; k < 3 && dot(delta, delta) > 0.0001f; ++k) {
        const Vec from = vecOf(prop(a, "Location"));
        MoveHit h = moveActor(a, delta);
        if (a->deleted)
            return;
        delta = mul(delta, 1 - h.time);
        const Vec now = vecOf(prop(a, "Location"));
        if (!waterAt({now[0], now[1], now[2]})) {
            const Vec line = findWaterLine(from, now);
            const MoveHit back = moveActor(a, sub(line, now));
            if (a->deleted)
                return;
            if (back.hit()) {
                setLocation(a, from);
                h.normal = {0, 0, 0};
                h.time = 0;
            }
            delta[2] = 0;
        }
        if (!h.hit())
            break;
        if (dot(desired, h.normal) < minHitWall) {
            if (vm_.findFunction(a, vm_.name("HitWall")))
                vm_.callEvent(a, "HitWall", {vm_.vectorValue(h.normal[0], h.normal[1], h.normal[2]), Value::objectValue(h.actor)});
            break;
        }
        delta = sub(delta, mul(h.normal, dot(delta, h.normal)));
    }
    if (a->deleted)
        return;
    const Vec here = vecOf(prop(a, "Location"));
    if (!fish) {
        const Vec moved = mul(sub(here, start), 1 / dt);
        vm_.setVector(*vel, moved[0], moved[1], moved[2]);
        return;
    }
    v = vecOf(vel);
    Vec pull{};
    const Vec to = sub(vecOf(prop(a, "Destination")), here);
    const float d = length(to);
    if (d > 20)
        pull = mul(to, floatOf(prop(a, "AccelRate"), 500) / d);
    if (Value* p = prop(a, "LastSeenPos"))
        vm_.setVector(*p, pull[0], pull[1], pull[2]);
    v = add(v, mul(pull, dt));
    if (length(v) > most)
        v = mul(normalOf(v), most);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    if (Value* r = prop(a, "Rotation"); r && length(v) > 1e-3f)
        *r = vm_.rotatorValue(int32_t(std::lround(std::atan2(v[2], std::hypot(v[0], v[1])) * 32768 / 3.14159265f)) & 0xffff,
                              int32_t(std::lround(std::atan2(v[1], v[0]) * 32768 / 3.14159265f)) & 0xffff, 0);
}

void ScriptLevel::calcVelocity(VmObject* a, Vec& v, Vec& accel, float dt, float maxSpeed, float friction, bool fluid,
                               bool brake, bool buoyant, const Vec& gravity) {
    const bool player = vm_.isA(a, "CTVE_PlayerPawn");
    const float f = std::max(fluid ? 1.0f : 0.0f, friction);
    const float rate = floatOf(prop(a, "AccelRate"), 500);
    bool slow = vm_.isA(a, "PlayerPawn") && boolOf(prop(a, "bIsWalking"));
    if (player) {
        slow = boolOf(prop(a, "bIsWalking")) || boolOf(prop(a, "bIsCrouching"));
        const bool fwd = boolOf(prop(a, "bWasForward")), back = boolOf(prop(a, "bWasBack"));
        if ((boolOf(prop(a, "bWasLeft")) || boolOf(prop(a, "bWasRight"))) && !fwd && !back)
            maxSpeed *= slow ? 0.5f : 0.8f;
        else if (back && !fwd)
            maxSpeed *= 0.8f;
        if (boolOf(prop(a, "bIsCrawling")))
            maxSpeed *= 0.75f;
    }
    const Vec dir = normalOf(accel);
    if (brake && accel[0] == 0 && accel[1] == 0 && accel[2] == 0) {
        const Vec old = v;
        if (player) {
            v = sub(v, mul(v, 2 * dt * f));
        } else {
            Vec sum{};
            float left = dt;
            while (left > 0) {
                const float step = left > 0.03f ? 0.03f : left;
                left -= step;
                v = sub(v, mul(v, 2 * step * f));
                if (dot(v, old) > 0)
                    sum = add(sum, mul(v, step / dt));
            }
            v = sum;
        }
        if (dot(old, v) < 0 || dot(v, v) < (player ? 5.0f : 100.0f))
            v = {0, 0, 0};
    } else {
        const float cap = slow ? 0.3f * rate : rate;
        if (dot(accel, accel) > cap * cap)
            accel = mul(dir, cap);
        v = sub(v, mul(sub(v, mul(dir, length(v))), dt * f));
    }
    v = add(mul(v, 1 - (fluid ? dt * friction : 0.0f)), mul(accel, dt));
    if (!vm_.isA(a, "PlayerPawn") && prop(a, "DesiredSpeed"))
        maxSpeed *= floatOf(prop(a, "DesiredSpeed"), 1);
    if (buoyant && !player)
        v = add(v, mul(gravity, dt * (1 - floatOf(prop(a, "Buoyancy")) / std::max(1e-3f, floatOf(prop(a, "Mass"), 100)))));
    const float speed = length(v);
    if (slow && speed > 0.3f * maxSpeed)
        v = mul(v, std::max(0.3f * maxSpeed, (1 - 2 * f * dt) * speed) / speed);
    else if (speed > maxSpeed)
        v = mul(v, maxSpeed / speed);
}

// stepUp: up MaxStepHeight, across, down again; a wall across tried as a
// step again if square to it, else along it; landing on a slope too
// steep, back off it.
void ScriptLevel::stepUp(VmObject* a, const Vec& gravDir, const Vec& desiredDir, Vec delta, const MoveHit& hit) {
    const float stepHeight = floatOf(prop(a, "MaxStepHeight"), 25);
    Value* loc = prop(a, "Location");
    const Value before = *loc;
    moveActor(a, mul(gravDir, -stepHeight));
    MoveHit h = moveActor(a, delta);
    if (a->deleted)
        return;
    if (h.hit()) {
        if (std::fabs(h.normal[2]) < 0.2f && dot(delta, delta) * h.time > 144) {
            stepUp(a, gravDir, desiredDir, mul(delta, 1 - h.time), h);
            if (a->deleted || physicsOf(a) == kPhysFalling)
                return;
        } else {
            hitWall(a, h);
            if (a->deleted)
                return;
            const Vec n0 = h.normal;
            delta = mul(sub(delta, mul(n0, dot(delta, n0))), 1 - h.time);
            if (dot(delta, desiredDir) > 0) {
                h = moveActor(a, delta);
                if (h.hit() && !a->deleted) {
                    hitWall(a, h);
                    if (a->deleted || physicsOf(a) == kPhysFalling)
                        return;
                    moveActor(a, twoWallAdjust(desiredDir, delta, h.normal, n0, h.time));
                }
            }
        }
    }
    if (a->deleted)
        return;
    const MoveHit down = moveActor(a, mul(gravDir, stepHeight));
    if (down.hit() && down.normal[2] < kWalkable && !a->deleted) {
        // Onto a slope too steep: not a step (Reluva's reading of the
        // undo: back where it was, along the wall first met).
        *loc = before;
        updateRegion(a);
        hitWall(a, down);
        const Vec along = sub(delta, mul(hit.normal, dot(delta, hit.normal)));
        if (dot(along, desiredDir) > 0)
            moveActor(a, along);
        return;
    }
    ++movement.steps;
}

int ScriptLevel::physicsOf(VmObject* a) {
    const Value* p = prop(a, "Physics");
    return p ? p->asInt() : 0;
}

void ScriptLevel::setBase(VmObject* a, VmObject* base) {
    if (Value* b = prop(a, "Base"); b && b->asObject() != base) {
        b->o = base;
        vm_.callEvent(a, "BaseChange");
    }
}

// The player's walking (ACTVE_PlayerPawn::physWalking): one move along the
// floor's slope with up to three slides, told HitWall at each wall; then
// down onto the floor (sliding down one too steep), else falling; its
// velocity what it really moved.
void ScriptLevel::walkPlayer(VmObject* a, float dt) {
    Value* vel = prop(a, "Velocity");
    Value* accelValue = prop(a, "Acceleration");
    const Vec start = vecOf(prop(a, "Location"));
    const ZoneFeel z = zoneFeel(a);
    Vec accel = vecOf(accelValue);
    if (Value* b = prop(a, "Base"); b && b->asObject() && vm_.isA(b->o, "Pawn")) {
        Vec away = sub(start, vecOf(prop(b->o, "Location")));
        away[2] = 0;
        accel = add(accel, mul(normalOf(away), 100));
    }
    accel[2] = 0;
    Vec v = vecOf(vel);
    calcVelocity(a, v, accel, dt, floatOf(prop(a, "GroundSpeed"), 320), z.groundFriction, false, true, false, z.gravity);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    Vec moving = add(v, z.velocity);
    if (length(moving) > z.terminal)
        moving = mul(normalOf(moving), z.terminal);
    Vec delta = mul(moving, dt);
    Value* floorValue = prop(a, "FloorNormal");
    const Vec floorN = floorValue ? vecOf(floorValue) : Vec{0, 0, 1};
    delta = sub(delta, mul(floorN, dot(delta, floorN)));
    const float stepHeight = floatOf(prop(a, "MaxStepHeight"), 25);
    bool stepped = false;
    float remaining = 1;
    for (int it = 0; it < 3 && remaining > 0.01f && !a->deleted; ++it) {
        if (std::fabs(delta[0]) < 1e-4f && std::fabs(delta[1]) < 1e-4f && std::fabs(delta[2]) < 1e-4f)
            break;
        const Vec dir = normalOf(delta);
        const Vec was = vecOf(prop(a, "Location"));
        const MoveHit h = moveActor(a, delta);
        if (a->deleted || !h.hit())
            break;
        remaining -= h.time * remaining;
        delta = mul(delta, 1 - h.time);
        // A ledge or a ladder there (Engine.dll's TestForLedge, then
        // CheckClimbing, on the move left): the walk ends.
        if (h.normal[2] < 0.65f && climbBlocked(a, h.actor, h.normal, delta))
            return;
        // A step up (the engine's ledge test does it for small steps): up, across, down.
        if (h.normal[2] < 0.65f && remaining > 0.01f) {
            Value* loc = prop(a, "Location");
            const Value here = *loc;
            const MoveHit up = moveActor(a, {0, 0, stepHeight});
            const MoveHit across = moveActor(a, delta);
            const MoveHit down = moveActor(a, {0, 0, -(stepHeight * up.time + 2)});
            if (across.time > 0 && down.hit() && down.normal[2] >= 0.65f && !a->deleted) {
                ++movement.steps;
                stepped = true;
                remaining -= across.time * remaining;
                delta = mul(delta, 1 - across.time);
                continue;
            }
            *loc = here;
            updateRegion(a);
        }
        // Along the wall, the player told how head-on and how fast.
        const float into = dot(dir, h.normal);
        delta = sub(delta, mul(h.normal, into * length(delta)));
        const Vec vNow = vecOf(vel);
        const float flatSpeed = std::sqrt(vNow[0] * vNow[0] + vNow[1] * vNow[1]);
        const float slid = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1]) * remaining / dt;
        if (h.normal[2] < 0.65f)
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(into, flatSpeed, slid), Value::objectValue(h.actor)});
        (void)was;
        if (a->deleted || physicsOf(a) != kPhysWalking || into < -0.8f)
            break;
    }
    if (a->deleted || physicsOf(a) != kPhysWalking)
        return;
    // Pushing on: the auto-duck's timer (Engine.dll's physWalking adds
    // each frame's time; a fall or a real move sets it back to 0).
    if (Value* t = prop(a, "PushToAutoDuckTimer"))
        *t = Value::floatValue(t->asFloat() + dt);
    // Down onto the floor, for real.
    const Vec down{0, 0, -(stepHeight + 2)};
    MoveHit floor = moveActor(a, down);
    if (a->deleted)
        return;
    if (floor.hit() && floor.normal[2] < 0.65f && !stepped) {
        const MoveHit slope = moveActor(a, sub(down, mul(floor.normal, dot(down, floor.normal))));
        if (slope.hit() && slope.normal[2] < 0.65f)
            stepped = true;
    }
    if (floor.hit()) {
        setBase(a, floor.actor == levelInfo_ ? levelInfo_ : floor.actor);
        if (floorValue)
            vm_.setVector(*floorValue, floor.normal[0], floor.normal[1], floor.normal[2]);
        const Vec here = vecOf(prop(a, "Location"));
        if (Value* t = prop(a, "FloorTexture"))
            *t = Value::objectValue(surfaceTexture(here, add(here, {0, 0, -(floatOf(prop(a, "CollisionHeight")) + 4)})));
    }
    if ((!floor.hit() || floor.normal[2] < 0.65f) && !stepped) {
        if (floorValue)
            vm_.setVector(*floorValue, 0, 0, 0);
        moveActor(a, mul(down, -floor.time));
        if (Value* b = prop(a, "Base"))
            b->o = nullptr;
        vm_.callEvent(a, "Falling");
        if (!a->deleted && physicsOf(a) == kPhysWalking)
            prop(a, "Physics")->i = kPhysFalling;
        if (Value* t = prop(a, "PushToAutoDuckTimer"))
            *t = Value::floatValue(0);
        return;
    }
    if (!stepped) {
        const Vec moved = mul(sub(vecOf(prop(a, "Location")), start), 1 / dt);
        vm_.setVector(*vel, moved[0], moved[1], moved[2]);
        if (length(moved) > 10)
            if (Value* t = prop(a, "PushToAutoDuckTimer"))
                *t = Value::floatValue(0);
    }
}

// Other pawns' walking (APawn / ACTVE_Pawn::physWalking): in steps of at
// most 0.05 s when going far, up to 4 (8 for non-CTVE pawns); stepping up
// what stops them; kept 1.9-2.4 above the floor; off a ledge only if they
// can jump, else back and their move given up; velocity what they moved.
void ScriptLevel::walkPawn(VmObject* a, float dt, int maxIterations) {
    Value* vel = prop(a, "Velocity");
    Value* accelValue = prop(a, "Acceleration");
    const ZoneFeel z = zoneFeel(a);
    Vec accel = vecOf(accelValue);
    const Vec start = vecOf(prop(a, "Location"));
    if (Value* b = prop(a, "Base"); b && b->asObject() && vm_.isA(b->o, "Pawn")) {
        Vec away = sub(start, vecOf(prop(b->o, "Location")));
        away[2] = 0;
        accel = add(accel, mul(normalOf(away), 100));
    }
    Vec v = vecOf(vel);
    v[2] = 0, accel[2] = 0;
    vm_.setVector(*accelValue, accel[0], accel[1], 0);
    calcVelocity(a, v, accel, dt, floatOf(prop(a, "GroundSpeed"), 320), z.groundFriction, false, true, false, z.gravity);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    Vec desired = v;
    if (vm_.isA(a, "PlayerPawn") || dot(z.velocity, z.velocity) > 90000)
        desired = add(desired, mul(z.velocity, 25 * dt));
    desired[2] = 0;
    const Vec gravDir{0, 0, z.gravity[2] > 0 ? 1.0f : -1.0f};
    const float stepHeight = floatOf(prop(a, "MaxStepHeight"), 25);
    const Vec down = mul(gravDir, stepHeight + 2);
    const Vec e = extentOf(a);
    float remaining = dt;
    for (int it = 0; it < maxIterations && remaining > 0 && !a->deleted; ++it) {
        const Vec old = vecOf(prop(a, "Location"));
        float tick = remaining;
        if (remaining > 0.05f && (vm_.isA(a, "PlayerPawn") || dot(desired, desired) * remaining * remaining > 400))
            tick = std::min(0.05f, remaining * 0.5f);
        remaining -= tick;
        const Vec delta = mul(desired, tick);
        if (length(delta) >= 1e-4f) {
            const MoveHit h = moveActor(a, delta);
            if (a->deleted)
                return;
            if (h.hit()) {
                stepUp(a, gravDir, normalOf(delta), mul(delta, 1 - h.time), h);
                if (a->deleted)
                    return;
                if (physicsOf(a) == kPhysFalling) {
                    Vec moved = sub(vecOf(prop(a, "Location")), old);
                    moved[2] = 0;
                    remaining += tick * (1 - std::min(1.0f, length(moved) / length(delta)));
                    vm_.callEvent(a, "Falling");
                    if (remaining > 0.01f && !a->deleted)
                        fall(a, remaining);
                    return;
                }
            }
        } else {
            remaining = 0;
        }
        // The floor: kept between 1.9 and 2.4 above it.
        const Vec here = vecOf(prop(a, "Location"));
        Vec hitAt{}, n{};
        VmObject* floor = trace(a, here, add(here, down), e, false, hitAt, n);
        bool onFloor = false;
        if (floor) {
            const float floorDist = length(sub(hitAt, here));
            VmObject* base = prop(a, "Base") ? prop(a, "Base")->asObject() : nullptr;
            if (floor != base || floorDist > 2.4f) {
                const MoveHit settle = moveActor(a, down);
                if (settle.hit())
                    n = settle.normal;
                setBase(a, floor);
            } else if (floorDist < 1.9f) {
                moveActor(a, mul(gravDir, -(2.1f - floorDist)));
            }
            onFloor = n[2] >= kWalkable;
        }
        if (onFloor)
            continue;
        // No floor (or too steep): a pawn that can't jump, or is walking,
        // stays where it was and gives its move up; else it falls.
        if (!boolOf(prop(a, "bCanJump")) || boolOf(prop(a, "bIsWalking"))) {
            vm_.setVector(*vel, 0, 0, 0);
            vm_.setVector(*accelValue, 0, 0, 0);
            vm_.setVector(*prop(a, "Location"), old[0], old[1], old[2]);
            updateRegion(a);
            if (Value* t = prop(a, "MoveTimer"))
                *t = Value::floatValue(-1);
            return;
        }
        Vec moved = sub(vecOf(prop(a, "Location")), old);
        moved[2] = 0;
        remaining += tick * (1 - std::min(1.0f, length(delta) > 0 ? length(moved) / length(delta) : 1.0f));
        if (Value* b = prop(a, "Base"))
            b->o = nullptr;
        vm_.callEvent(a, "Falling");
        if (a->deleted)
            return;
        if (physicsOf(a) == kPhysWalking)
            prop(a, "Physics")->i = kPhysFalling;
        Vec soFar = mul(sub(vecOf(prop(a, "Location")), start), 1 / std::max(1e-4f, dt - remaining));
        soFar[2] = 0;
        vm_.setVector(*vel, soFar[0], soFar[1], 0);
        if (remaining > 0.01f)
            fall(a, remaining);
        return;
    }
    if (a->deleted)
        return;
    Vec moved = mul(sub(vecOf(prop(a, "Location")), start), 1 / dt);
    vm_.setVector(*vel, moved[0], moved[1], 0);
}

// Landing (processLanded): Landed, then walking (a pawn) or still, and
// the rest of the tick walked.
void ScriptLevel::landed(VmObject* a, const Vec& n, VmObject* on, float remaining) {
    ++movement.landings;
    const bool player = vm_.isA(a, "CTVE_PlayerPawn");
    if (player)
        if (Value* f = prop(a, "FloorNormal"))
            vm_.setVector(*f, n[0], n[1], n[2]);
    vm_.callEvent(a, "Landed", {vm_.vectorValue(n[0], n[1], n[2])});
    if (a->deleted)
        return;
    const bool pawn = vm_.isA(a, "Pawn");
    if (player || physicsOf(a) == kPhysFalling) {
        prop(a, "Physics")->i = pawn ? kPhysWalking : kPhysNone;
        setBase(a, on);
        if (!pawn)
            vm_.setVector(*prop(a, "Velocity"), 0, 0, 0);
    }
    if (pawn && physicsOf(a) == kPhysWalking && !a->deleted) {
        Value* accel = prop(a, "Acceleration");
        const Vec dir = normalOf(vecOf(accel));
        vm_.setVector(*accel, dir[0], dir[1], dir[2]);
        if (remaining > 0.01f) {
            if (player)
                walkPlayer(a, remaining);
            else
                walkPawn(a, remaining, 8);
        }
    }
}

// The player falling (ACTVE_PlayerPawn::physFalling): gravity 1.4 times
// the zone's, eased by a factor 1 - 2.2 dt (0.5-1); one move with up to
// four slides, HitWall at the first; landing on 0.65.
void ScriptLevel::fallPlayer(VmObject* a, float dt) {
    Value* vel = prop(a, "Velocity");
    const ZoneFeel z = zoneFeel(a);
    const Vec start = vecOf(prop(a, "Location"));
    Vec accel = vecOf(prop(a, "Acceleration"));
    Vec v = vecOf(vel);
    float maxAccel = floatOf(prop(a, "AirControl"), 0.05f) * floatOf(prop(a, "AccelRate"), 500);
    const float flatSpeed = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    if (flatSpeed < 10)
        maxAccel += (10 - flatSpeed) / dt;
    if (length(accel) > maxAccel)
        accel = mul(normalOf(accel), maxAccel);
    const Vec g = mul(z.gravity, 1.4f);
    if (z.water) {
        v = mul(v, 1 - dt * z.fluidFriction);
        v = add(v, mul(add(g, accel), dt));
    } else {
        v = add(v, mul(add(g, accel), dt * std::clamp(1 - 2.2f * dt, 0.5f, 1.0f)));
    }
    if (length(v) > z.terminal)
        v = mul(normalOf(v), z.terminal);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    Vec delta = mul(add(v, z.velocity), dt);
    float remaining = 1;
    Vec lastN{};
    VmObject* lastActor = nullptr;
    for (int it = 1; it <= 4 && remaining > 0.01f && !a->deleted; ++it) {
        const Vec dir = normalOf(delta);
        const MoveHit h = moveActor(a, delta);
        if (a->deleted || !h.hit())
            break;
        remaining -= remaining * h.time;
        if (h.normal[2] > 0.65f || (it == 4 && v[2] <= 1 && h.actor == lastActor && remaining <= 0.99f)) {
            landed(a, h.normal, h.actor, remaining * dt);
            return;
        }
        if (it == 1) {
            vm_.callEvent(a, "HitWall", {vm_.vectorValue(dot(h.normal, dir), flatSpeed, 0), Value::objectValue(h.actor)});
            if (a->deleted || physicsOf(a) != kPhysFalling)
                return;
            delta = mul(sub(delta, mul(h.normal, dot(delta, h.normal))), 1 - h.time);
        } else {
            delta = twoWallAdjust(dir, delta, h.normal, lastN, h.time);
        }
        lastN = h.normal;
        lastActor = h.actor;
    }
    if (a->deleted || physicsOf(a) != kPhysFalling)
        return;
    const Vec moved = mul(sub(vecOf(prop(a, "Location")), start), 1 / dt);
    vm_.setVector(*vel, moved[0], moved[1], moved[2]);
}

// Everything else falling (AActor::physFalling): steps of at most 0.1 s,
// each moving at the average of its start and end velocity; split at the
// top of an arc; a little air control (less with something just ahead);
// landing on more than 0.7, sliding along anything steeper.
void ScriptLevel::fall(VmObject* a, float dt) {
    Value* vel = prop(a, "Velocity");
    Value* accelValue = prop(a, "Acceleration");
    const Vec savedAccel = vecOf(accelValue);
    const ZoneFeel z = zoneFeel(a);
    const bool pawn = vm_.isA(a, "Pawn");
    Vec accel = savedAccel;
    float boundSpeed = 0;
    if (pawn) {
        Vec v0 = vecOf(vel);
        float air = floatOf(prop(a, "AirControl"), 0.05f);
        const float rate = floatOf(prop(a, "AccelRate"), 500);
        if (air > 0.15f) {
            Vec ahead = mul(add(mul(normalOf(accel), air * rate), v0), dt);
            ahead[2] = 0;
            Vec hitAt{}, n{};
            const Vec here = vecOf(prop(a, "Location"));
            if (length(ahead) > 0 && trace(a, here, add(here, ahead), extentOf(a), false, hitAt, n))
                air = 0.05f;
        }
        float maxAccel = air * rate;
        accel[2] = 0;
        const float s = std::sqrt(v0[0] * v0[0] + v0[1] * v0[1]);
        if (s < 10)
            maxAccel += (10 - s) / dt;
        else if (s >= floatOf(prop(a, "GroundSpeed"), 320)) {
            if (air <= 0.05f)
                maxAccel = 1;
            else
                boundSpeed = s;
        }
        if (length(accel) > maxAccel)
            accel = mul(normalOf(accel), maxAccel);
    }
    const bool bounce = boolOf(prop(a, "bBounce"));
    const bool zoneMoves = !pawn || vm_.isA(a, "PlayerPawn") || dot(z.velocity, z.velocity) > 40000;
    float remaining = dt;
    int bounces = 0;
    for (int it = 0; it < 8 && remaining > 0 && !a->deleted; ++it) {
        float tick = remaining > 0.1f ? std::min(0.1f, remaining * 0.5f) : remaining;
        const Vec old = vecOf(prop(a, "Location"));
        const Vec oldV = vecOf(vel);
        auto half = [&](float t) {
            if (z.water) {
                const float buoy = 1 - floatOf(prop(a, "Buoyancy")) / std::max(1.0f, floatOf(prop(a, "Mass"), 100));
                return add(mul(oldV, 1 - 2 * z.fluidFriction * t), mul(add(mul(z.gravity, buoy), accel), 0.5f * t));
            }
            return add(oldV, mul(add(z.gravity, accel), 0.5f * t));
        };
        Vec v = half(tick);
        if (std::fabs(oldV[2]) >= 5 && std::fabs(v[2]) >= 5 && (oldV[2] > 0) != (v[2] > 0)) {
            const float part = std::fabs(oldV[2]) / (std::fabs(oldV[2]) + std::fabs(v[2]));
            if (part * tick >= 0.015f && (1 - part) * tick >= 0.015f) {
                tick *= part;
                v = half(tick);
            }
        }
        remaining -= tick;
        if (boundSpeed > 0) {
            const float s = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            if (s > boundSpeed)
                v[0] *= boundSpeed / s, v[1] *= boundSpeed / s;
        }
        vm_.setVector(*vel, v[0], v[1], v[2]);
        const Vec adjusted = mul(zoneMoves ? add(v, z.velocity) : v, tick);
        MoveHit h = moveActor(a, adjusted);
        if (a->deleted || physicsOf(a) != kPhysFalling)
            return;
        if (h.hit()) {
            if (bounce) {
                hitWall(a, h);
                if (a->deleted || physicsOf(a) == kPhysNone)
                    return;
                if (bounces++ < 2)
                    remaining += tick * (1 - h.time);
                continue;
            }
            if (h.normal[2] > kWalkable) {
                remaining += tick * (1 - h.time);
                if (h.time > 0.1f && h.time * tick > 0.003f) {
                    const Vec landedV = mul(sub(vecOf(prop(a, "Location")), old), 1 / (tick * h.time));
                    vm_.setVector(*vel, landedV[0], landedV[1], landedV[2]);
                }
                landed(a, h.normal, h.actor, remaining);
                break;
            }
            hitWall(a, h);
            if (a->deleted || physicsOf(a) != kPhysFalling)
                return;
            Vec delta = mul(sub(adjusted, mul(h.normal, dot(adjusted, h.normal))), 1 - h.time);
            if (dot(delta, adjusted) >= 0) {
                const MoveHit h2 = moveActor(a, delta);
                if (a->deleted)
                    return;
                if (h2.hit()) {
                    if (h2.normal[2] > kWalkable) {
                        landed(a, h2.normal, h2.actor, remaining);
                        break;
                    }
                    hitWall(a, h2);
                    if (a->deleted || physicsOf(a) != kPhysFalling)
                        return;
                    const bool ditch = h.normal[2] > 0 && h2.normal[2] > 0 && dot(h.normal, h2.normal) < 0;
                    delta = twoWallAdjust(normalOf(adjusted), delta, h2.normal, h.normal, h2.time);
                    if (ditch)
                        delta[2] = 0;
                    const MoveHit h3 = moveActor(a, delta);
                    if (a->deleted)
                        return;
                    if (ditch || (h3.hit() && h3.normal[2] > kWalkable)) {
                        landed(a, h3.hit() ? h3.normal : Vec{0, 0, 1}, h3.actor ? h3.actor : h2.actor, remaining);
                        break;
                    }
                }
            }
        }
        if (!a->deleted && physicsOf(a) == kPhysFalling) {
            Vec nv = sub(mul(sub(vecOf(prop(a, "Location")), old), 1 / tick), zoneMoves ? z.velocity : Vec{0, 0, 0});
            if (nv[2] < oldV[2] || oldV[2] >= 0)
                nv = sub(mul(nv, 2), oldV);
            if (length(nv) > z.terminal)
                nv = mul(normalOf(nv), z.terminal);
            vm_.setVector(*vel, nv[0], nv[1], nv[2]);
        }
    }
    if (!a->deleted)
        vm_.setVector(*accelValue, savedAccel[0], savedAccel[1], savedAccel[2]);
}

// Swimming and flying (APawn::physSwimming, physFlying): calcVelocity in
// the fluid, one move (flying: steps up a low wall, else along it).
void ScriptLevel::swimOrFly(VmObject* a, float dt, bool swim) {
    Value* vel = prop(a, "Velocity");
    const ZoneFeel z = zoneFeel(a);
    const Vec start = vecOf(prop(a, "Location"));
    Vec accel = vecOf(prop(a, "Acceleration"));
    Vec v = vecOf(vel);
    calcVelocity(a, v, accel, dt, floatOf(prop(a, swim ? "WaterSpeed" : "AirSpeed"), swim ? 200 : 320), z.fluidFriction,
                 true, swim, swim, z.gravity);
    vm_.setVector(*vel, v[0], v[1], v[2]);
    const bool zoneMoves = swim || vm_.isA(a, "PlayerPawn") || dot(z.velocity, z.velocity) > 90000;
    const Vec delta = mul(zoneMoves ? add(v, z.velocity) : v, dt);
    MoveHit h = moveActor(a, delta);
    if (a->deleted)
        return;
    // The player swimming into a wall: a ledge or ladder there, as
    // walking (Engine.dll's physSwimming asks the same tests).
    if (swim && h.hit() && climbBlocked(a, h.actor, h.normal, mul(delta, 1 - h.time)))
        return;
    if (h.hit()) {
        const Vec gravDir{0, 0, z.gravity[2] > 0 ? 1.0f : -1.0f};
        const float up = dot(gravDir, normalOf(delta));
        if (!swim && std::fabs(h.normal[2]) < 0.2f && up > -0.2f && up < 0.5f) {
            stepUp(a, gravDir, normalOf(delta), mul(delta, 1 - h.time), h);
        } else {
            hitWall(a, h);
            if (a->deleted)
                return;
            Vec along = mul(sub(delta, mul(h.normal, dot(delta, h.normal))), 1 - h.time);
            const MoveHit h2 = moveActor(a, along);
            if (h2.hit() && !a->deleted)
                moveActor(a, twoWallAdjust(normalOf(delta), along, h2.normal, h.normal, h2.time));
        }
    }
    if (a->deleted)
        return;
    const Vec moved = mul(sub(vecOf(prop(a, "Location")), start), 1 / dt);
    vm_.setVector(*vel, moved[0], moved[1], moved[2]);
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
    // GetSurfaceTexture(Vector Start, Vector End): the texture of the
    // level's surface between them (the footsteps' sound goes by its
    // TexMaterial).
    // ResizeActor(float NewRadius, float NewHeight, optional bool
    // bAlignToFloor, optional bool bAdjustToFit, optional bool bAdjustToTop):
    // ScriptLevel::resize; the player crouches and crawls by it.
    reg("Actor.ResizeActor", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self &&
                                    c.vm.level->resize(c.self, c.f(0), c.f(1), c.b(2), c.b(3), c.b(4)));
    });
    // SetSurfaceTexture(Vector Start, Vector End, Texture NewTexture,
    // optional bool bReplaceAllWorld, optional int XORPolyFlags): the
    // surface between them shows NewTexture (switches, card readers); with
    // bReplaceAllWorld, every surface showing what it shows. Its flags
    // (XORPolyFlags, a texture changer's own setting) aren't applied yet.
    reg("Actor.SetSurfaceTexture", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->swapSurfaceTexture(c.vm.level->surfaceAt(Vm::vectorOf(c.args[0]), Vm::vectorOf(c.args[1])),
                                           c.o(2), c.b(3));
    });
    // PickWallAdjust(): ScriptLevel::pickWallAdjust.
    reg("Pawn.PickWallAdjust", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->pickWallAdjust(c.self));
    });
    reg("Actor.GetSurfaceTexture", [](NativeCall& c) {
        c.result = Value::objectValue(
            c.vm.level ? c.vm.level->surfaceTexture(Vm::vectorOf(c.args[0]), Vm::vectorOf(c.args[1])) : nullptr);
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
    // how much of Target is seen from Pos looking along Rot, as Engine.dll's
    // (read for what it does). Its middle is its Location raised by
    // LocationZAdjust; H its CollisionHeight + HeightAdjust.
    // - 0 unless the middle is closer than Radius and within the cone
    //   (the cosine of the angle off Rot's forward axis over CosFOV).
    // - A point is seen if a line to it (through the world, movers and
    //   actors, not this one) first meets Target itself.
    // - Without bCalcVisible, or with H 10 or less and no bCheckExtents,
    //   the middle first: seen, 1. Else, without bCalcVisible only if H >
    //   10 or bCheckExtents, the grid below, 1 at the first point seen.
    // - The grid: rows at +0.9 H and -0.9 H (if H > 10, else the middle's
    //   height); with bCheckExtents two points a row, 0.9 CollisionRadius
    //   either side across Rot's forward axis (level, not normalised; the
    //   lower row a step further over, as the original's loop leaves it).
    //   With bCalcVisible, the share of its points seen.
    // - BestHitLocation the first point seen; untouched if none.
    reg("Actor.CheckFOV", [](NativeCall& c) {
        c.result = Value::floatValue(0);
        VmObject* target = c.o(2);
        if (!c.vm.level || !c.self || !target || target->deleted)
            return;
        using V = std::array<float, 3>;
        const V from = Vm::vectorOf(c.args[0]);
        const auto r = Vm::rotatorOf(c.args[1]);
        const V fwd = rotationAxes({r[0], r[1], r[2]})[0];
        const float radius = c.f(3), cosFov = c.f(4);
        const bool extents = c.b(6), share = c.b(7);
        const Value* loc = target->find(c.vm, "Location");
        if (!loc)
            return;
        V test = Vm::vectorOf(*loc);
        test[2] += c.f(8);
        const Value* hv = target->find(c.vm, "CollisionHeight");
        const float h = c.f(9) + (hv ? hv->asFloat() : 0);
        const V to{test[0] - from[0], test[1] - from[1], test[2] - from[2]};
        const float distance = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (!(distance < radius) || distance <= 0)
            return;
        if (!((to[0] * fwd[0] + to[1] * fwd[1] + to[2] * fwd[2]) / distance > cosFov))
            return;
        auto seen = [&](const V& p) {
            V where{}, n{};
            return c.vm.level->trace(c.self, from, p, {}, true, where, n) == target;
        };
        if (!share || (h <= 10 && !extents)) {
            if (seen(test)) {
                c.args[5] = c.vm.vectorValue(test[0], test[1], test[2]);
                c.result = Value::floatValue(1);
                return;
            }
            if (!(h > 10 || extents))
                return;
        }
        int rows = 1, across = 1;
        float zStep = 0;
        V step{};
        if (h > 10) {
            const float s = h * 0.9f;
            rows = 2;
            test[2] += s;
            zStep = 2 * s;
        }
        if (extents) {
            const Value* rv = target->find(c.vm, "CollisionRadius");
            const float rad = rv ? rv->asFloat() : 0;
            const V side{fwd[1] * rad * 0.9f, -fwd[0] * rad * 0.9f, 0};
            across = 2;
            test = {test[0] + side[0], test[1] + side[1], test[2]};
            step = {side[0] * 2, side[1] * 2, 0};
        }
        float result = 0;
        bool first = true;
        for (int row = 0; row < rows; ++row) {
            for (int k = 0; k < across; ++k) {
                if (seen(test)) {
                    if (first)
                        c.args[5] = c.vm.vectorValue(test[0], test[1], test[2]);
                    first = false;
                    if (!share) {
                        c.result = Value::floatValue(1);
                        return;
                    }
                    result += 1.0f / float(rows * across);
                }
                test = {test[0] - step[0], test[1] - step[1], test[2]};
            }
            test = {test[0] + step[0], test[1] + step[1], test[2] - zStep};
        }
        c.result = Value::floatValue(result);
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
    (void)lower;
}

} // namespace reluva::unreal
