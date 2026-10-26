// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/actors.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::unreal {

namespace {

constexpr float kPi = 3.14159265358979f;

Vec3f readVector(Reader& r) {
    Vec3f v;
    for (float& f : v)
        f = r.f32();
    return v;
}

} // namespace

Error readPolys(const Package& package, int index, std::vector<Poly>& out, size_t* trailing) {
    out.clear();
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const std::string what = package.path(index + 1);
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    // The count twice (in use, allocated), then the polygons.
    const int32_t count = r.i32();
    r.i32();
    // Each takes at least 1 + 4 vectors + 4 + 5 compact indices + 4 bytes.
    if (!r.ok() || count < 0 || size_t(count) * 62 > r.remaining())
        return Error::fail(what + ": " + std::to_string(count) + " polygons don't fit");
    out.resize(size_t(count));
    for (Poly& p : out) {
        const int32_t n = r.index();
        if (n < 0 || n > 64 || size_t(n) * 12 > r.remaining())
            return Error::fail(what + ": a polygon with " + std::to_string(n) + " vertices");
        p.base = readVector(r);
        p.normal = readVector(r);
        p.textureU = readVector(r);
        p.textureV = readVector(r);
        p.vertices.resize(size_t(n));
        for (Vec3f& v : p.vertices)
            v = readVector(r);
        p.polyFlags = r.u32();
        p.actor = r.index();
        p.texture = r.index();
        p.itemName = r.index();
        p.link = r.index();
        p.brushPoly = r.index();
        p.panU = r.i16();
        p.panV = r.i16();
        if (!r.ok())
            return Error::fail(what + ": polygons run past the end");
        // A normal is unit length; if it isn't, the layout above is off.
        const float len = std::sqrt(p.normal[0] * p.normal[0] + p.normal[1] * p.normal[1] + p.normal[2] * p.normal[2]);
        if (n >= 3 && std::fabs(len - 1) > 0.01f)
            return Error::fail(what + ": a polygon's normal isn't unit length");
    }
    if (trailing)
        *trailing = r.remaining();
    return Error::success();
}

Error readActorProperties(const Package& package, int index, PropertyList& out) {
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    return beginObject(package, index, r, out);
}

bool isA(const PackageSet& packages, const Package& package, int index, std::string_view className) {
    const std::string wanted = fs::toLower(className);
    const Package* pkg = &package;
    int32_t cls = package.exports()[size_t(index)].classRef;
    for (int depth = 0; depth < 32 && cls != 0; ++depth) {
        if (fs::toLower(pkg->objectName(cls)) == wanted)
            return true;
        const ObjectRef def = packages.resolve(*pkg, cls);
        if (!def)
            return false;
        pkg = def.package;
        cls = pkg->exports()[size_t(def.exportIndex)].superRef;
    }
    return false;
}

Vec3f Mover::basePosition() const {
    const Vec3f& k = keyPos[size_t(keyNum)];
    return {location[0] - k[0], location[1] - k[1], location[2] - k[2]};
}

Rotator Mover::baseRotation() const {
    const Rotator& k = keyRot[size_t(keyNum)];
    return {rotation.pitch - k.pitch, rotation.yaw - k.yaw, rotation.roll - k.roll};
}

std::vector<Mover> findMovers(const PackageSet& packages, const Package& package, std::vector<std::string>* errors) {
    std::vector<Mover> out;
    std::map<int32_t, bool> moverClass; // by class reference, as many actors share one
    for (size_t i = 0; i < package.exports().size(); ++i) {
        const int32_t cls = package.exports()[i].classRef;
        if (cls == 0)
            continue;
        auto it = moverClass.find(cls);
        if (it == moverClass.end())
            it = moverClass.emplace(cls, isA(packages, package, int(i), "Mover")).first;
        if (!it->second)
            continue;
        const std::string what = package.path(int32_t(i) + 1);
        auto fail = [&](const std::string& why) {
            if (errors)
                errors->push_back(what + ": " + why);
        };
        PropertyList props;
        if (Error err = readActorProperties(package, int(i), props)) {
            fail(err.message);
            continue;
        }
        Mover m;
        m.name = what;
        props.vector("Location", m.location);
        props.rotator("Rotation", m.rotation);
        props.vector("PrePivot", m.prePivot);
        props.vector("MainScale", m.mainScale); // a Scale struct: its vector first
        props.vector("PostScale", m.postScale);
        m.keyNum = std::min(int(props.byte("KeyNum")), 7);
        m.numKeys = std::clamp(int(props.byte("NumKeys", 2)), 1, 8);
        m.moveTime = props.number("MoveTime", 1);
        for (int k = 0; k < 8; ++k) {
            props.vector("KeyPos", m.keyPos[size_t(k)], k);
            props.rotator("KeyRot", m.keyRot[size_t(k)], k);
        }
        // The brush: a Model whose Polys hold the polygons.
        const ObjectRef brush = packages.resolve(package, props.object("Brush"));
        if (!brush) {
            fail("no brush");
            continue;
        }
        Model model;
        // Only its Polys are drawn, so a brush with a broken BSP of its own
        // (M07_SiskoL3A's Mover128) still does.
        if (Error err = readModel(*brush.package, brush.exportIndex, model, false)) {
            fail(err.message);
            continue;
        }
        const ObjectRef polys = packages.resolve(*brush.package, model.polys);
        if (!polys) {
            fail("its brush has no polygons" + (model.hasLighting() ? std::string() : " (" + model.lightingError + ")"));
            continue;
        }
        if (Error err = readPolys(*polys.package, polys.exportIndex, m.polys)) {
            fail(err.message);
            continue;
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::optional<Vec3f> findSkyZone(const PackageSet& packages, const Package& package) {
    for (size_t i = 0; i < package.exports().size(); ++i) {
        if (package.exports()[i].classRef == 0 || !isA(packages, package, int(i), "SkyZoneInfo"))
            continue;
        PropertyList props;
        Vec3f location;
        if (!readActorProperties(package, int(i), props) && props.vector("Location", location))
            return location;
    }
    return std::nullopt;
}

std::array<Vec3f, 3> rotationAxes(const Rotator& r) {
    const float p = float(r.pitch) * 2 * kPi / 65536, y = float(r.yaw) * 2 * kPi / 65536,
                rl = float(r.roll) * 2 * kPi / 65536;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(rl),
                cr = std::cos(rl);
    return {Vec3f{cp * cy, cp * sy, sp}, Vec3f{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
            Vec3f{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
}

} // namespace reluva::unreal
