// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's effects' engine side, for a running level (ScriptLevel):
// the copies of an actor its scripts make (CTVE_MultiMesh: particles,
// sparks, beam sections) moved on each tick and let go when their life
// runs out; the electric arcs (CTVE_ChainSpark); the beams' sections
// (CTVE_BeamFX); bursts of sparks (CTVE_Sparks); rain and snow
// (CTVE_Weather); and decals stuck to the world (Decal). How each moves is
// Reluva's reading of what the scripts set and expect; the original code
// isn't known. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/actors.h"

#include <cmath>
#include <random>

namespace reluva::unreal {

namespace {

using Vec = std::array<float, 3>;

std::mt19937& random() {
    static std::mt19937 r(54321); // repeatable runs
    return r;
}
float frand() { return std::uniform_real_distribution<float>(0, 1)(random()); }

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float length(const Vec& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec normal(const Vec& a) {
    const float l = length(a);
    return l > 1e-6f ? mul(a, 1 / l) : Vec{1, 0, 0};
}
// A direction as a rotator (Unreal units), as rotator(Vector) gives it.
std::array<int32_t, 3> rotatorOf(const Vec& d) {
    const float yaw = std::atan2(d[1], d[0]);
    const float pitch = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
    auto units = [](float r) { return int32_t(std::lround(r * 32768.0f / 3.14159265f)); };
    return {units(pitch), units(yaw), 0};
}

} // namespace

// An actor's copies (its MultiMesh list): the ones in use, in order.
std::vector<VmObject*> ScriptLevel::multiMeshes(VmObject* actor) {
    std::vector<VmObject*> out;
    const Value* head = actor ? prop(actor, "MultiMesh") : nullptr;
    VmObject* mm = head ? head->asObject() : nullptr;
    for (int guard = 0; mm && guard < 4096; ++guard) {
        const Value* next = prop(mm, "Next");
        mm = next ? next->asObject() : nullptr;
        if (!mm)
            break;
        if (const Value* deleted = prop(mm, "bDeleted"); deleted && deleted->asBool())
            break; // the ones let go are kept at the end
        out.push_back(mm);
    }
    return out;
}

// An arc of sparks (CTVE_ChainSpark): a curve from StartLoc to FinishLoc,
// leaving along startPath and arriving along FinishPath (a cubic Bezier
// through StartLoc + startPath and FinishLoc + FinishPath, kept in P1-P3),
// cut into its segments, each a copy of its mesh (DS9_Spark: a unit long
// along X) from one point to the next, Thickness wide. The points between
// are jolted aside by up to SpikeMag (EndSpikeMag at the ends) each tick it
// updates; with bSinVariation they wave Amplitude to each side WaveLengths
// times along it. With bGrows it grows from the start at GrowthRate a
// second. It fades with bFade (FadeSpeed a second, gone at nothing), or
// glows up and down between MinGlow and MaxGlow (bCycleFade).
void ScriptLevel::chainSparkTick(VmObject* spark, float seconds) {
    auto f = [&](const char* name) {
        const Value* v = prop(spark, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto b = [&](const char* name) {
        const Value* v = prop(spark, name);
        return v && v->asBool();
    };
    auto vec = [&](const char* name) {
        const Value* v = prop(spark, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto setVec = [&](VmObject* o, const char* name, const Vec& v) {
        if (Value* p = prop(o, name))
            *p = vm_.vectorValue(v[0], v[1], v[2]);
    };
    auto setFloat = [&](VmObject* o, const char* name, float v) {
        if (Value* p = prop(o, name))
            *p = Value::floatValue(v);
    };
    // How bright: fading out, or glowing up and down.
    float glow = f("ScaleGlow");
    if (b("bFade")) {
        glow -= f("FadeSpeed") * seconds;
        if (glow <= 0) {
            destroy(spark);
            return;
        }
    } else if (b("bCycleFade")) {
        const float lo = f("MinGlow"), hi = std::max(lo, f("MaxGlow"));
        const bool up = b("bFadePlus");
        glow += (up ? 1 : -1) * f("FadeSpeed") * seconds;
        if (glow >= hi || glow <= lo)
            if (Value* p = prop(spark, "bFadePlus"))
                *p = Value::boolValue(glow <= lo);
        glow = std::clamp(glow, lo, hi);
    }
    setFloat(spark, "ScaleGlow", glow);
    float growth = 1;
    if (b("bGrows")) {
        growth = std::min(1.0f, f("Growth") + f("GrowthRate") * seconds);
        setFloat(spark, "Growth", growth);
    }

    const std::vector<VmObject*> segments = multiMeshes(spark);
    if (segments.empty())
        return;
    const Vec p0 = vec("StartLoc"), p3 = vec("FinishLoc");
    const Vec p1 = add(p0, vec("startPath")), p2 = add(p3, vec("FinishPath"));
    setVec(spark, "P1", p1);
    setVec(spark, "P2", p2);
    setVec(spark, "P3", p3);
    const bool update = b("bUpdate") || f("TimeStep") == 0;
    setFloat(spark, "TimeStep", f("TimeStep") + seconds);
    const size_t n = segments.size();
    const Vec along = normal(sub(p3, p0));
    Vec side = cross(along, Vec{0, 0, 1});
    if (length(side) < 1e-3f)
        side = Vec{1, 0, 0};
    side = normal(side);
    const Vec up = cross(side, along);
    std::vector<Vec> points(n + 1);
    for (size_t k = 0; k <= n; ++k) {
        const float t = growth * float(k) / float(n), u = 1 - t;
        Vec p = add(add(mul(p0, u * u * u), mul(p1, 3 * u * u * t)), add(mul(p2, 3 * u * t * t), mul(p3, t * t * t)));
        if (b("bSinVariation")) {
            const Value* waves = prop(spark, "WaveLengths");
            const float n = float(std::max(1, waves ? waves->asInt() : 1));
            p = add(p, mul(side, f("Amplitude") * std::sin(t * 6.2831853f * n)));
        }
        points[k] = p;
    }
    const float thickness = std::max(0.1f, f("Thickness"));
    const Value* style = prop(spark, "Style");
    const Value* texture = prop(spark, "Texture");
    for (size_t k = 0; k < n; ++k) {
        VmObject* seg = segments[k];
        Vec from = points[k], to = points[k + 1];
        if (update) {
            // Jolted aside, the ends less: each tick the arc flickers.
            auto jolt = [&](size_t i) {
                if (i == 0 || i == n)
                    return Vec{};
                const float mag = (i == 1 || i == n - 1) ? f("EndSpikeMag") : f("SpikeMag");
                return add(mul(side, (frand() * 2 - 1) * mag), mul(up, (frand() * 2 - 1) * mag));
            };
            from = add(from, jolt(k));
            to = add(to, jolt(k + 1));
        } else if (const Value* loc = prop(seg, "Location")) {
            from = Vm::vectorOf(*loc);
        }
        const Vec d = sub(to, from);
        setVec(seg, "Location", from);
        if (Value* r = prop(seg, "Rotation")) {
            const auto rot = rotatorOf(d);
            *r = vm_.rotatorValue(rot[0], rot[1], rot[2]);
        }
        setVec(seg, "AxisScale", {length(d), thickness, thickness});
        setFloat(seg, "ScaleGlow", glow);
        if (Value* s = prop(seg, "Style"); s && style)
            *s = Value::intValue(style->asInt());
        if (Value* t = prop(seg, "Texture"); t && texture)
            *t = *texture;
    }
    // Where the arc ends, for the scripts.
    setVec(spark, "TipLocation", points[n]);
}

// A beam's sections (CTVE_BeamFX: copies of its mesh, made and cut by its
// scripts) laid along it from BeamOrigin, where the weapon's tip is now,
// each SectionLength on along BeamAxesX and turned as the beam. Each is
// SectionAxisScale thick, its thickness varying at random by up to
// SectionAxisYZRandomFactor each tick; the last as long as the head has
// flown into it (the scripts cut it after, where the beam meets
// something). With a WaveAmplitude the beam
// ripples: WaveLength waves a unit along it, travelling WaveTravelVelocity
// a second.
void ScriptLevel::updateBeamSections(VmObject* beam, float seconds) {
    const VmProperty* list = beam->cls->findProperty(vm_.name("BeamSectionList"));
    if (!list)
        return;
    auto f = [&](const char* name) {
        const Value* v = prop(beam, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto i = [&](const char* name) {
        const Value* v = prop(beam, name);
        return v ? v->asInt() : 0;
    };
    auto vec = [&](const char* name) {
        const Value* v = prop(beam, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    const int first = std::max(0, i("BeamSectionIndexFirst"));
    const int last = std::min(i("BeamSectionIndexLast"), list->arrayDim);
    const Vec origin = vec("BeamOrigin"), step = vec("BeamSectionAxesX"), axis = normal(vec("BeamAxesX"));
    const Value* rotation = prop(beam, "BeamRotation");
    const Vec scale = vec("SectionAxisScale");
    const float jitter = f("SectionAxisYZRandomFactor"), amplitude = f("WaveAmplitude"), waveLength = f("WaveLength");
    const float travel = f("WaveTravelVelocity") * f("AliveTime"), sectionLength = f("SectionLength");
    Vec side = cross(axis, Vec{0, 0, 1});
    side = length(side) < 1e-3f ? Vec{0, 1, 0} : normal(side);
    for (int k = first; k < last; ++k) {
        VmObject* section = beam->values[size_t(list->slot + k)].asObject();
        if (!section || section->deleted)
            continue;
        Vec at = add(origin, mul(step, float(k)));
        if (amplitude != 0 && waveLength != 0) {
            const float phase = (float(k) * sectionLength - travel) * waveLength * 6.2831853f;
            at = add(at, mul(side, amplitude * std::sin(phase)));
        }
        if (Value* l = prop(section, "Location"))
            *l = vm_.vectorValue(at[0], at[1], at[2]);
        if (Value* r = prop(section, "Rotation"); r && rotation)
            *r = *rotation;
        if (Value* s = prop(section, "AxisScale")) {
            const Vec now = Vm::vectorOf(*s);
            auto thick = [&](float base) { return std::max(0.1f, base * (1 + jitter * (frand() - 0.5f))); };
            // The last grows as the head flies out (HeadTime x HeadVelocity
            // along); the scripts cut it after, where the beam hits.
            float x = scale[0];
            if (k == last - 1 && sectionLength > 0) {
                const float reach = f("HeadTime") * f("HeadVelocity") / sectionLength - float(k);
                x = std::clamp(reach, 0.0f, 1.0f) * scale[0];
            }
            (void)now;
            *s = vm_.vectorValue(x, thick(scale[1]), thick(scale[2]));
        }
    }
    if (Value* count = prop(beam, "FrameCount"))
        ++count->i;
    (void)seconds;
}

namespace {

// A new copy of an actor (its NewMultiMesh: like it to begin with).
VmObject* newCopy(Vm& vm, VmObject* actor) {
    VmFunction* make = vm.findFunction(actor, vm.name("NewMultiMesh"));
    return make ? vm.invoke(make, actor, {}).asObject() : nullptr;
}

} // namespace

// Sparks (CTVE_Sparks): every so often (BurstDelay, counted down by its
// scripts, set again here: about AverageBurstTime, else up to MaxWaitTime)
// a burst of MinSparksPerBurst to MaxSparksPerBurst of them flies out along
// its X axis at Speed, spread RangeWidth to the sides and RangeHeight up
// and down, and falls; each Size (give or take SizeVariance) and lasting
// SparkLife (give or take SparkLifeVariance), fading as it goes. Its light
// (the one it had, kept in saveLightType) flashes with each burst.
void ScriptLevel::sparkTick(VmObject* sparks, float seconds) {
    auto f = [&](const char* name) {
        const Value* v = prop(sparks, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto vec = [&](const char* name) {
        const Value* v = prop(sparks, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto set = [&](VmObject* o, const char* name, const Value& v) {
        if (Value* p = prop(o, name))
            *p = v;
    };
    if (auto it = sparkFlash_.find(sparks); it != sparkFlash_.end() && (it->second -= seconds) <= 0) {
        set(sparks, "LightType", Value::byteValue(0));
        sparkFlash_.erase(it);
    }
    Value* delay = prop(sparks, "BurstDelay");
    if (!delay || delay->asFloat() > 0)
        return;

    const Vec x = vec("XAxis"), y = vec("YAxis"), z = vec("ZAxis");
    const Value* loc = prop(sparks, "Location");
    const Vec from = loc ? Vm::vectorOf(*loc) : vec("SeedLocation");
    Vec gravity{0, 0, -950};
    if (const Value* r = prop(sparks, "Region"); r && !r->fields.empty())
        if (VmObject* zone = r->fields[0].asObject())
            if (const Value* g = prop(zone, "ZoneGravity"))
                gravity = Vm::vectorOf(*g);
    const Value* minCount = prop(sparks, "MinSparksPerBurst");
    const Value* maxCount = prop(sparks, "MaxSparksPerBurst");
    const int least = minCount ? std::max(0, minCount->asInt()) : 0;
    const int most = std::min(64, std::max(least, maxCount ? maxCount->asInt() : 0));
    const int count = least + int(frand() * float(most - least + 1) * 0.999f);
    auto vary = [](float base, float by) { return base + (frand() * 2 - 1) * by; };
    for (int k = 0; k < count; ++k) {
        VmObject* mm = newCopy(vm_, sparks);
        if (!mm)
            break;
        const Vec v = add(add(mul(x, f("Speed")), mul(y, (frand() * 2 - 1) * f("RangeWidth"))),
                          mul(z, (frand() * 2 - 1) * f("RangeHeight")));
        const float life = std::max(0.05f, vary(f("SparkLife"), f("SparkLifeVariance")));
        set(mm, "Location", vm_.vectorValue(from[0], from[1], from[2]));
        set(mm, "Velocity", vm_.vectorValue(v[0], v[1], v[2]));
        set(mm, "Acceleration", vm_.vectorValue(gravity[0], gravity[1], gravity[2]));
        const auto rot = rotatorOf(v);
        set(mm, "Rotation", vm_.rotatorValue(rot[0], rot[1], rot[2]));
        set(mm, "DrawScale", Value::floatValue(std::max(0.01f, vary(f("Size"), f("SizeVariance")))));
        set(mm, "LifeSpan", Value::floatValue(life));
        set(mm, "ScaleGlow", Value::floatValue(f("MaxScaleGlow")));
    }
    const float average = f("AverageBurstTime");
    delay->f = average > 0 ? average * (0.5f + frand()) : std::max(0.05f, frand() * f("MaxWaitTime"));
    if (const Value* light = prop(sparks, "bLightFX"); light && light->asBool() && count > 0)
        if (const Value* saved = prop(sparks, "saveLightType"); saved && saved->asInt() != 0) {
            set(sparks, "LightType", Value::byteValue(saved->asInt()));
            sparkFlash_[sparks] = 0.1f;
        }
}

// Rain and snow (CTVE_Weather): drops (copies of it: its mesh, or its
// sprite) falling about SeedLocation (where its scripts keep it, ahead of
// the player), up to SpawnRange around and from DropHeight above, as many
// as its Intensity asks (which comes up to what it was set to, or down to
// nothing when it's turned off, at DeltaIntensityRate a second). They fall
// at FallRate (give or take FallRateVariance), blown by DropVelocity's
// wind, which drifts with WindActivity to up to MaxLateralMove each way;
// each lasts until it meets the world (traced as it starts) or one of the
// weather blockers its scripts gathered (Block: a cylinder or a box,
// sheltering what's under it; with bInvertWeatherBlockers, the only places
// it falls). `fill`: started over (InitialSetUp), drops at every height.
void ScriptLevel::weatherTick(VmObject* weather, float seconds, bool fill) {
    auto f = [&](const char* name) {
        const Value* v = prop(weather, name);
        return v ? v->asFloat() : 0.0f;
    };
    auto b = [&](const char* name) {
        const Value* v = prop(weather, name);
        return v && v->asBool();
    };
    auto vec = [&](const char* name) {
        const Value* v = prop(weather, name);
        return v ? Vm::vectorOf(*v) : Vec{};
    };
    auto set = [&](VmObject* o, const char* name, const Value& v) {
        if (Value* p = prop(o, name))
            *p = v;
    };
    // How hard, coming up or down to what's asked.
    Value* intensity = prop(weather, "Intensity");
    if (!intensity)
        return;
    const float goal = b("bActive") ? f("MaxIntensity") : 0;
    const float rate = std::max(0.01f, f("DeltaIntensityRate")) * seconds;
    intensity->f = intensity->asFloat() < goal ? std::min(goal, intensity->asFloat() + rate)
                                               : std::max(goal, intensity->asFloat() - rate);
    // The wind: now and then a new one to blow toward, sideways.
    Value* wind = prop(weather, "DropVelocity");
    Value* destWind = prop(weather, "DestWind");
    const float lateral = f("MaxLateralMove"), fall = f("FallRate");
    if (wind && destWind) {
        if (frand() < f("WindActivity") * seconds * 10) {
            const float angle = frand() * 6.2831853f, strength = frand() * lateral;
            *destWind = vm_.vectorValue(std::cos(angle) * strength, std::sin(angle) * strength, fall);
        }
        Vec w = Vm::vectorOf(*wind);
        const Vec d = Vm::vectorOf(*destWind);
        const float k = std::min(1.0f, seconds * 0.5f);
        w = {w[0] + (d[0] - w[0]) * k, w[1] + (d[1] - w[1]) * k, fall};
        *wind = vm_.vectorValue(w[0], w[1], w[2]);
    }
    const Vec windNow = wind ? Vm::vectorOf(*wind) : Vec{0, 0, fall};

    // The blockers: inside one (its footprint, under its top).
    struct Block {
        Vec at;
        float radius, height;
        bool box;
        float x, y, z;
    };
    std::vector<Block> blocks;
    const Value* numBlocks = prop(weather, "NumBlocks");
    if (const VmProperty* list = weather->cls->findProperty(vm_.name("Block")); list && numBlocks) {
        const int n = std::clamp(numBlocks->asInt(), 0, list->arrayDim);
        for (int k = 0; k < n; ++k) {
            const Value& v = weather->values[size_t(list->slot + k)];
            if (!v.st)
                continue;
            auto field = [&](const char* name) -> const Value* {
                const VmProperty* p = v.st->findProperty(vm_.name(name));
                return p && size_t(p->slot) < v.fields.size() ? &v.fields[size_t(p->slot)] : nullptr;
            };
            auto ff = [&](const char* name) {
                const Value* x = field(name);
                return x ? x->asFloat() : 0.0f;
            };
            const Value* pos = field("Pos");
            const Value* box = field("bBox");
            blocks.push_back({pos ? Vm::vectorOf(*pos) : Vec{}, ff("Radius"), ff("Height"), box && box->asInt() != 0,
                              ff("XSize") / 2, ff("YSize") / 2, ff("ZSize") / 2});
        }
    }
    auto inBlock = [&](const Vec& p) {
        for (const Block& k : blocks) {
            const float dx = p[0] - k.at[0], dy = p[1] - k.at[1];
            if (k.box ? std::fabs(dx) <= k.x && std::fabs(dy) <= k.y && p[2] <= k.at[2] + k.z
                      : dx * dx + dy * dy <= k.radius * k.radius && p[2] <= k.at[2] + k.height)
                return true;
        }
        return false;
    };
    const bool inverted = b("bInvertWeatherBlockers");
    const Vec seed = vec("SeedLocation");
    const float range = std::max(1.0f, f("SpawnRange")), top = std::max(1.0f, f("DropHeight"));

    // Those blown too far from where it's falling now go (it follows the
    // player); all of them, starting over.
    std::vector<VmObject*> drops = multiMeshes(weather);
    size_t alive = 0;
    for (VmObject* mm : drops) {
        const Value* l = prop(mm, "Location");
        const Vec p = l ? Vm::vectorOf(*l) : Vec{};
        const float dx = p[0] - seed[0], dy = p[1] - seed[1];
        if (fill || dx * dx + dy * dy > range * range * 1.44f)
            vm_.callEvent(mm, "Delete");
        else
            ++alive;
    }
    // As many as the intensity asks, new ones from the top.
    constexpr float kMostDrops = 4000; // at an intensity of 1
    const size_t wanted = size_t(std::clamp(intensity->asFloat(), 0.0f, 1.0f) * kMostDrops);
    const Value* rotation = prop(weather, "CurrentRotation");
    const Value* style = prop(weather, "Style");
    int tries = 0;
    for (size_t k = alive; k < wanted && tries < int(wanted) * 2; ++tries) {
        // Nearer the middle more often: it's seen there.
        const float angle = frand() * 6.2831853f, r = range * frand();
        Vec at{seed[0] + std::cos(angle) * r, seed[1] + std::sin(angle) * r,
               seed[2] + (fill ? top * (frand() * 2 - 1) : top - frand() * 20)};
        if (inBlock(at) != inverted)
            continue;
        const float speed = std::min(-10.0f, fall + (frand() * 2 - 1) * f("FallRateVariance"));
        const Vec v{windNow[0] * (0.8f + 0.4f * frand()), windNow[1] * (0.8f + 0.4f * frand()), speed};
        // It lasts until it meets the world below (none: as far below as
        // it starts above).
        Vec hit{}, n{};
        const Vec to = add(at, mul(v, 2 * top / -speed));
        float drop = 2 * top;
        if (trace(weather, at, to, {}, false, hit, n))
            drop = at[2] - hit[2];
        if (drop <= 1)
            continue;
        VmObject* mm = newCopy(vm_, weather);
        if (!mm)
            break;
        ++k;
        set(mm, "Location", vm_.vectorValue(at[0], at[1], at[2]));
        set(mm, "Velocity", vm_.vectorValue(v[0], v[1], v[2]));
        set(mm, "Acceleration", vm_.vectorValue(0, 0, 0));
        if (rotation)
            set(mm, "Rotation", *rotation);
        set(mm, "LifeSpan", Value::floatValue(drop / -speed));
        set(mm, "DrawScale", Value::floatValue(std::max(0.01f, f("Size") + (frand() * 2 - 1) * f("SizeVariance"))));
        set(mm, "ScaleGlow", Value::floatValue(f("MaxScaleGlow")));
        if (style)
            set(mm, "Style", Value::intValue(style->asInt()));
    }
}

std::array<float, 3> ScriptLevel::waterLine(const std::array<float, 3>& start, const std::array<float, 3>& end) {
    auto water = [&](const Vec& p) {
        const Value r = regionAt(p);
        if (!r.st)
            return false;
        const VmProperty* zp = r.st->findProperty(vm_.name("Zone"));
        VmObject* zone = zp && size_t(zp->slot) < r.fields.size() ? r.fields[size_t(zp->slot)].asObject() : nullptr;
        const Value* w = zone ? prop(zone, "bWaterZone") : nullptr;
        return w && w->asBool();
    };
    const bool from = water(start);
    if (water(end) == from)
        return start;
    // Halved until it's within a fraction of a unit.
    Vec a = start, b = end;
    for (int k = 0; k < 24 && length(sub(b, a)) > 0.1f; ++k) {
        const Vec mid = mul(add(a, b), 0.5f);
        (water(mid) == from ? a : b) = mid;
    }
    return b;
}

// A decal stuck to the world (Decal.AttachDecal): the surface found within
// `distance` behind it (along its rotation's X backwards: it's turned to
// face out of the surface, rotator(HitNormal)); flat on it, the texture's
// size by DrawScale, its top along `direction` if given (a footprint's
// heading), else its rotation's Z. The surface's texture, or None if
// there's no surface there (and the decal goes).
VmObject* ScriptLevel::attachDecal(VmObject* decal, float distance, const Vec& direction) {
    const Value* loc = prop(decal, "Location");
    const Value* rot = prop(decal, "Rotation");
    if (!loc || !rot)
        return nullptr;
    const Vec at = Vm::vectorOf(*loc);
    const auto r = Vm::rotatorOf(*rot);
    const auto ax = rotationAxes(Rotator{r[0], r[1], r[2]});
    const Vec out{ax[0][0], ax[0][1], ax[0][2]};
    const Vec from = add(at, mul(out, 2)), to = sub(at, mul(out, std::max(distance, 1.0f)));
    Vec where{}, n{};
    if (trace(decal, from, to, {}, false, where, n) != levelInfo_)
        return nullptr;
    VmObject* surfaceTex = surfaceTexture(from, to);
    Decal d;
    d.at = where;
    d.normal = n;
    Vec top = (direction[0] != 0 || direction[1] != 0 || direction[2] != 0) ? direction
                                                                              : Vec{ax[2][0], ax[2][1], ax[2][2]};
    // Its top along the surface.
    const float along = top[0] * n[0] + top[1] * n[1] + top[2] * n[2];
    top = sub(top, mul(n, along));
    if (length(top) < 1e-3f)
        top = normal(cross(n, std::fabs(n[2]) < 0.9f ? Vec{0, 0, 1} : Vec{1, 0, 0}));
    d.up = normal(top);
    if (const Value* t = prop(decal, "Texture"); t && t->asObject() && t->o->package)
        d.texture = {t->o->package, t->o->exportIndex};
    const Value* scale = prop(decal, "DrawScale");
    d.scale = scale ? scale->asFloat() : 1;
    decals_[decal] = d;
    if (surfaceTex)
        return surfaceTex;
    const Value* t = prop(decal, "Texture");
    return t && t->asObject() ? t->o : levelInfo_;
}

void registerEffectNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // SpawnChain(): the arc's segments, NumSegments copies of it
    // (NewMultiMesh), laid out at once.
    reg("CTVE_ChainSpark.SpawnChain", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        const Value* n = c.self->find(c.vm, "NumSegments");
        const int count = std::clamp(n ? n->asInt() : 0, 1, 256);
        VmFunction* make = c.vm.findFunction(c.self, c.vm.name("NewMultiMesh"));
        for (int k = 0; make && k < count; ++k)
            c.vm.invoke(make, c.self, {});
        c.vm.level->chainSparkTick(c.self, 0);
    });
    // ChainSparkTick(float DeltaTime): ScriptLevel::chainSparkTick.
    reg("CTVE_ChainSpark.ChainSparkTick", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->chainSparkTick(c.self, c.f(0));
    });
    // ForceUpdate(): laid out again now.
    reg("CTVE_ChainSpark.ForceUpdate", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->chainSparkTick(c.self, 0);
    });
    // UpdateBeamSections(float DeltaTime): ScriptLevel::updateBeamSections.
    reg("Actor.UpdateBeamSections", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->updateBeamSections(c.self, c.f(0));
    });
    // SparkTick(float DeltaTime): ScriptLevel::sparkTick.
    reg("CTVE_Sparks.SparkTick", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->sparkTick(c.self, c.f(0));
    });
    // WeatherTick(float DeltaTime), InitialSetUp(): ScriptLevel::weatherTick.
    reg("CTVE_Weather.WeatherTick", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->weatherTick(c.self, c.f(0), false);
    });
    reg("CTVE_Weather.InitialSetUp", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->weatherTick(c.self, 0, true);
    });
    // GetWaterLine(Vector Start, out Vector End): End brought back to the
    // water's surface between them (ScriptLevel::waterLine).
    reg("Actor.GetWaterLine", [](NativeCall& c) {
        if (!c.vm.level || c.args.size() < 2)
            return;
        const auto at = c.vm.level->waterLine(Vm::vectorOf(c.args[0]), Vm::vectorOf(c.args[1]));
        c.args[1] = c.vm.vectorValue(at[0], at[1], at[2]);
    });
    // AttachDecal(float TraceDistance, optional Vector DecalDir): stuck to
    // the surface behind it (ScriptLevel::attachDecal); DetachDecal() off
    // again.
    reg("Decal.AttachDecal", [](NativeCall& c) {
        const bool dir = c.given.size() > 1 && c.given[1];
        c.result = Value::objectValue(
            c.vm.level && c.self
                ? c.vm.level->attachDecal(c.self, c.f(0), dir ? Vm::vectorOf(c.args[1]) : std::array<float, 3>{})
                : nullptr);
    });
    reg("Decal.DetachDecal", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->detachDecal(c.self);
    });
}

} // namespace reluva::unreal
