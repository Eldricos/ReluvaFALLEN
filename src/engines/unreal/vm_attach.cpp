// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Where parts of actors' meshes are, for a running level (ScriptLevel):
// The Fallen's bones (C3DGetJointWorldPos: a camera on a speaker's head),
// its attachment points (C3DGetAttachPtCoords: where a gun's shot or a
// kick starts) and places worked out in advance (a gun's tip in a firing
// pose), each as the mesh is in its animation now; and what's attached
// (C3DAttach: a weapon in a hand, a breather on a face), kept there each
// tick. The mesh is placed as the Maps page draws it (mesh_render.h), so
// they agree. See docs/engines/unreal_vm.md and mesh.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/mesh.h"
#include "engines/unreal/mesh_render.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = ScriptLevel::Vec;
using Axes = std::array<Vec, 3>;

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec mul(const Vec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec unit(const Vec& a) {
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return l > 1e-6f ? mul(a, 1 / l) : Vec{0, 0, 0};
}
// a's rows in the frame whose axes are f's rows: row k = sum over j of a[k][j] f[j].
Axes inFrame(const Axes& a, const Axes& f) {
    Axes out{};
    for (size_t k = 0; k < 3; ++k)
        for (size_t j = 0; j < 3; ++j)
            out[k] = add(out[k], mul(f[j], a[k][j]));
    return out;
}

ScriptLevel::Rot rotOf(const Axes& axes) {
    const Rotator r = rotatorFromAxes({unit(axes[0]), unit(axes[1]), unit(axes[2])});
    return {r.pitch, r.yaw, r.roll};
}

const MeshAttachPoint* findPoint(const std::vector<MeshAttachPoint>& points, const std::string& name) {
    const std::string wanted = fs::toLower(name);
    for (const MeshAttachPoint& p : points)
        if (fs::toLower(p.name) == wanted)
            return &p;
    return nullptr;
}

} // namespace

struct ScriptLevel::ActorMesh {
    Mesh mesh;
    std::unique_ptr<LayeredPoser> poser; // built on a skeleton
    std::map<std::pair<NameId, bool>, std::vector<bool>> parts;
};

ScriptLevel::ActorMesh* ScriptLevel::actorMesh(VmObject* a) {
    const Value* meshValue = prop(a, "Mesh");
    VmObject* m = meshValue ? meshValue->asObject() : nullptr;
    if (!m || !m->package)
        return nullptr;
    auto it = actorMeshes_.find(m);
    if (it == actorMeshes_.end()) {
        auto am = std::make_shared<ActorMesh>();
        if (readMesh(*m->package, m->exportIndex, am->mesh)) {
            am.reset();
        } else if (am->mesh.bones) {
            if (!animationSets_)
                animationSets_ = std::make_shared<std::vector<AnimationSet>>(findAnimationSets(vm_.packages()));
            am->poser = LayeredPoser::open(*m->package, m->exportIndex, *animationSets_);
        }
        it = actorMeshes_.emplace(m, std::move(am)).first;
    }
    return it->second.get();
}

// The mesh's vertices as its animation has them this tick (packed units),
// worked out once a tick: posed from its bones with its layers, else
// between its frames.
const std::vector<ScriptLevel::Vec>* ScriptLevel::posedVertices(VmObject* a, ActorMesh& m) {
    auto& [tick, verts] = posed_[a];
    if (tick == ticks_ + 1 && !verts.empty())
        return &verts;
    tick = ticks_ + 1;
    verts.clear();
    const Value* seqValue = prop(a, "AnimSequence");
    const std::string seq = seqValue && seqValue->i ? vm_.nameText(seqValue->i) : std::string();
    const Value* frameValue = prop(a, "AnimFrame");
    const float frame = frameValue ? frameValue->asFloat() : 0;
    if (m.poser && !seq.empty()) {
        std::vector<LayeredPoser::Layer> layers;
        std::vector<std::string> names; // the layers' animations' names, kept while posing
        if (const auto* ls = layersOf(a)) {
            names.reserve(ls->size());
            for (const auto& [group, l] : *ls) {
                if (l.weight <= 0 || !l.sequence)
                    continue;
                auto part = m.parts.find({l.bone, l.rest});
                if (part == m.parts.end())
                    part = m.parts.emplace(std::make_pair(l.bone, l.rest), m.poser->part(vm_.nameText(l.bone), l.rest)).first;
                if (part->second.empty())
                    continue;
                names.push_back(vm_.nameText(l.sequence));
                layers.push_back({names.back(), l.frame, &part->second, l.weight});
            }
        }
        if (m.poser->pose(seq, frame, layers, verts))
            return &verts;
    }
    // Between its frames (its own animations).
    const Mesh& mesh = m.mesh;
    const MeshAnimSeq* anim = seq.empty() ? nullptr : mesh.findAnim(seq);
    const Value* loop = prop(a, "bAnimLoop");
    const MeshPose pose = poseAtFraction(mesh, anim, frame, loop && loop->asBool());
    const auto n = size_t(mesh.frameVerts);
    if (n == 0 || size_t(pose.next + 1) * n > mesh.verts.size() || size_t(pose.frame + 1) * n > mesh.verts.size())
        return nullptr;
    verts.resize(n);
    for (size_t v = 0; v < n; ++v)
        for (size_t c = 0; c < 3; ++c) {
            const float x0 = mesh.verts[size_t(pose.frame) * n + v][c], x1 = mesh.verts[size_t(pose.next) * n + v][c];
            verts[v][c] = x0 + (x1 - x0) * pose.lerp;
        }
    return &verts;
}

// From the actor's own space (as the mesh is placed, before DrawScale) to
// the world: Location + PrePivot + its rotation of (DrawScale * the point),
// as the mesh is drawn (meshPlacement).
void ScriptLevel::toWorld(VmObject* a, Vec& at, Axes* axes) {
    const Value* loc = prop(a, "Location");
    const Value* pivot = prop(a, "PrePivot");
    const Value* rot = prop(a, "Rotation");
    const Value* scale = prop(a, "DrawScale");
    const auto r = rot ? Vm::rotatorOf(*rot) : std::array<int32_t, 3>{};
    const auto f = rotationAxes({r[0], r[1], r[2]});
    const Axes frame{Vec{f[0][0], f[0][1], f[0][2]}, Vec{f[1][0], f[1][1], f[1][2]}, Vec{f[2][0], f[2][1], f[2][2]}};
    const float ds = scale ? scale->asFloat() : 1;
    Vec w = add(loc ? Vm::vectorOf(*loc) : Vec{}, pivot ? Vm::vectorOf(*pivot) : Vec{});
    for (size_t j = 0; j < 3; ++j)
        w = add(w, mul(frame[j], at[j] * ds));
    at = w;
    if (axes)
        *axes = inFrame(*axes, frame);
}

// An attachment point's place and axes in the actor's own space (before
// DrawScale). On a triangle: from the triangle's middle, its axes X along
// its first edge, Z along its normal (Reluva's reading: the data doesn't
// say), the point's turn and offset in them. On the mesh's origin: its
// turn and offset from the float copy's space.
bool ScriptLevel::attachFrame(VmObject* a, ActorMesh& m, const MeshAttachPoint& point, Vec& at, Axes& axes) {
    const Mesh& mesh = m.mesh;
    if (point.type == 1) {
        const std::vector<Vec>* verts = posedVertices(a, m);
        if (!verts || point.triangle < 0 || size_t(point.triangle) >= mesh.tris.size())
            return false;
        const MeshTri& t = mesh.tris[size_t(point.triangle)];
        Vec c[3];
        for (size_t k = 0; k < 3; ++k) {
            if (size_t(t.vertex[k]) >= verts->size())
                return false;
            c[k] = placeMeshVertex(mesh, (*verts)[size_t(t.vertex[k])]);
        }
        const Vec x = unit(sub(c[1], c[0]));
        const Vec z = unit(cross(sub(c[1], c[0]), sub(c[2], c[0])));
        const Axes frame{x, cross(z, x), z};
        at = mul(add(add(c[0], c[1]), c[2]), 1.0f / 3);
        for (size_t j = 0; j < 3; ++j)
            at = add(at, mul(frame[j], point.offset[j]));
        axes = inFrame(point.axes, frame);
        return true;
    }
    at = placeMeshFloatVector(mesh, point.offset);
    for (size_t k = 0; k < 3; ++k)
        axes[k] = unit(placeMeshFloatVector(mesh, point.axes[k]));
    return true;
}

bool ScriptLevel::attachPointAt(VmObject* a, NameId name, bool precomputed, bool world, Vec& location, Rot& rotation) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    const MeshAttachPoint* point =
        m ? findPoint(precomputed ? m->mesh.precomputedPoints : m->mesh.attachPoints, vm_.nameText(name)) : nullptr;
    Vec at{};
    Axes axes{};
    if (!point || !attachFrame(a, *m, *point, at, axes))
        return false;
    if (world) {
        toWorld(a, at, &axes);
    } else if (const Value* scale = prop(a, "DrawScale")) {
        at = mul(at, scale->asFloat());
    }
    location = at;
    rotation = rotOf(axes);
    return true;
}

bool ScriptLevel::boneAt(VmObject* a, NameId bone, bool world, Vec& location, Rot& rotation) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    if (!m || !m->poser)
        return false;
    const Value* seqValue = prop(a, "AnimSequence");
    const Value* frameValue = prop(a, "AnimFrame");
    Vec origin{};
    Axes packed{};
    // (Its layers aren't taken into account: a bone's place under the
    // actor's own animation.)
    if (!seqValue || !seqValue->i ||
        !m->poser->bone(vm_.nameText(seqValue->i), frameValue ? frameValue->asFloat() : 0, {}, vm_.nameText(bone),
                        origin, packed))
        return false;
    Vec at = placeMeshVertex(m->mesh, origin);
    Axes axes{};
    for (size_t k = 0; k < 3; ++k)
        axes[k] = unit(sub(placeMeshVertex(m->mesh, add(origin, packed[k])), at));
    if (world) {
        toWorld(a, at, &axes);
    } else if (const Value* scale = prop(a, "DrawScale")) {
        at = mul(at, scale->asFloat());
    }
    location = at;
    rotation = rotOf(axes);
    return true;
}

bool ScriptLevel::hasBone(VmObject* a, NameId bone) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    return m && m->poser && m->poser->hasBone(vm_.nameText(bone));
}

int ScriptLevel::attachPointCount(VmObject* a) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    return m ? int(m->mesh.attachPoints.size()) : 0;
}

void ScriptLevel::attach(VmObject* child, VmObject* parent, NameId parentPoint, NameId childPoint) {
    if (child && parent && child != parent)
        attachments_[child] = {parent, parentPoint, childPoint};
}

void ScriptLevel::detach(VmObject* child, VmObject* parent) {
    auto it = attachments_.find(child);
    if (it != attachments_.end() && (!parent || it->second.parent == parent))
        attachments_.erase(it);
}

// Each attached actor turned so its point's axes are its holder's point's,
// and moved so the points meet: its rotation's axes are its point's (in its
// own space) undone, then the holder's point's; its location what's left.
void ScriptLevel::placeAttached() {
    for (auto it = attachments_.begin(); it != attachments_.end();) {
        VmObject* child = it->first;
        const Attachment& at = it->second;
        if (child->deleted || !at.parent || at.parent->deleted) {
            it = attachments_.erase(it);
            continue;
        }
        ++it;
        ActorMesh* pm = actorMesh(at.parent);
        ActorMesh* cm = actorMesh(child);
        const MeshAttachPoint* pp = pm ? findPoint(pm->mesh.attachPoints, vm_.nameText(at.parentPoint)) : nullptr;
        const MeshAttachPoint* cp = cm ? findPoint(cm->mesh.attachPoints, vm_.nameText(at.childPoint)) : nullptr;
        Vec where{}, mine{};
        Axes theirs{}, ours{};
        if (!pp || !attachFrame(at.parent, *pm, *pp, where, theirs))
            continue;
        toWorld(at.parent, where, &theirs);
        if (!cp || !attachFrame(child, *cm, *cp, mine, ours)) {
            mine = {};
            ours = {Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
        }
        // Its axes: row j = sum over k of ours[k][j] theirs[k] (ours undone, then theirs).
        Axes frame{};
        for (size_t j = 0; j < 3; ++j)
            for (size_t k = 0; k < 3; ++k)
                frame[j] = add(frame[j], mul(unit(theirs[k]), unit(ours[k])[j]));
        const Value* scale = prop(child, "DrawScale");
        const Value* pivot = prop(child, "PrePivot");
        const float ds = scale ? scale->asFloat() : 1;
        Vec loc = sub(where, pivot ? Vm::vectorOf(*pivot) : Vec{});
        for (size_t j = 0; j < 3; ++j)
            loc = sub(loc, mul(frame[j], mine[j] * ds));
        if (Value* l = prop(child, "Location"))
            *l = vm_.vectorValue(loc[0], loc[1], loc[2]);
        if (Value* r = prop(child, "Rotation")) {
            const Rot rot = rotOf(frame);
            *r = vm_.rotatorValue(rot[0], rot[1], rot[2]);
        }
    }
}

void registerAttachNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // Found, the place and turn; else the actor's own (so what starts
    // there starts at it, not at the world's origin).
    auto answer = [](NativeCall& c, bool found, const ScriptLevel::Vec& at, const ScriptLevel::Rot& r, size_t loc,
                     size_t rot, bool world) {
        if (!found && c.self) {
            const Value* l = c.self->find(c.vm, "Location");
            const Value* t = c.self->find(c.vm, "Rotation");
            c.args[loc] = world && l ? *l : c.vm.vectorValue(0, 0, 0);
            c.args[rot] = t ? *t : c.vm.rotatorValue(0, 0, 0);
            return;
        }
        c.args[loc] = c.vm.vectorValue(at[0], at[1], at[2]);
        c.args[rot] = c.vm.rotatorValue(r[0], r[1], r[2]);
    };
    // C3DGetJointWorldPos(name Bone, out Vector Location, out Rotator Rotation), and the
    // local ones (from the actor, unturned); the Rel ones taken as the same.
    for (const char* joint : {"Actor.C3DGetJointWorldPos", "Actor.C3DGetJointLocalPos", "Actor.C3DGetJointWorldPosRel",
                              "Actor.C3DGetJointLocalPosRel"}) {
        const bool world = std::string_view(joint).find("World") != std::string_view::npos;
        reg(joint, [=](NativeCall& c) {
            ScriptLevel::Vec at{};
            ScriptLevel::Rot r{};
            const bool found = c.vm.level && c.self && c.vm.level->boneAt(c.self, c.i(0), world, at, r);
            answer(c, found, at, r, 1, 2, world);
        });
    }
    reg("Actor.C3DDoesBoneExist", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->hasBone(c.self, c.i(0)));
    });
    // C3DGetAttachPtCoords(name AttachPtName, out Vector Pos, out Rotator Orientation)
    reg("Actor.C3DGetAttachPtCoords", [=](NativeCall& c) {
        ScriptLevel::Vec at{};
        ScriptLevel::Rot r{};
        const bool found = c.vm.level && c.self && c.vm.level->attachPointAt(c.self, c.i(0), false, true, at, r);
        answer(c, found, at, r, 1, 2, true);
    });
    reg("Actor.C3DGetAttachPtCoordsPrecomputed", [=](NativeCall& c) {
        ScriptLevel::Vec at{};
        ScriptLevel::Rot r{};
        const bool found = c.vm.level && c.self && c.vm.level->attachPointAt(c.self, c.i(0), true, true, at, r);
        answer(c, found, at, r, 1, 2, true);
    });
    // C3DGetAttachPtOffset(name AttachPtName, out Vector Location, out Rotator Rotation)
    reg("Actor.C3DGetAttachPtOffset", [=](NativeCall& c) {
        ScriptLevel::Vec at{};
        ScriptLevel::Rot r{};
        const bool found = c.vm.level && c.self && c.vm.level->attachPointAt(c.self, c.i(0), false, false, at, r);
        answer(c, found, at, r, 1, 2, false);
    });
    reg("Actor.C3DGetNumAttachPts", [](NativeCall& c) {
        c.args[0] = Value::intValue(c.vm.level && c.self ? c.vm.level->attachPointCount(c.self) : 0);
    });
    // C3DAttach(Actor Obj, optional name ObjPt, optional name ThisPt): this
    // actor held at Obj's point ObjPt by its own ThisPt.
    reg("Actor.C3DAttach", [](NativeCall& c) {
        if (c.vm.level && c.self && c.o(0))
            c.vm.level->attach(c.self, c.o(0), c.given.size() > 1 && c.given[1] ? c.i(1) : 0,
                               c.given.size() > 2 && c.given[2] ? c.i(2) : 0);
    });
    reg("Actor.C3DDetach", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->detach(c.self, c.o(0));
    });
    // Setting up The Fallen's meshes for its renderer: nothing to do here.
    for (const char* none : {"Actor.C3DActorInit", "Actor.C3DInitAltMeshes"})
        reg(none, [](NativeCall&) {});
}

} // namespace reluva::unreal
