// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Where parts of actors' meshes are, for a running level (ScriptLevel):
// The Fallen's bones (C3DGetJointWorldPos: a camera on a speaker's head),
// its attachment points (C3DGetAttachPtCoords: where a gun's shot or a
// kick starts) and places worked out in advance (a gun's tip in a firing
// pose), each as the mesh is in its animation now; and what's attached
// (C3DAttach: a weapon in a hand, a breather on a face), kept there each
// tick; and what the scripts do to the bones on top of the animations
// (C3DApplyRotation: a back bent; the IK chains: hands on a gun). The mesh
// is placed as the Maps page draws it (mesh_render.h), so they agree. See
// docs/engines/unreal_vm.md and mesh.md.

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

// The limbs the IK chains move: the chains' names (its place, its turn),
// the end bone and the attachment point on it that's meant to reach (the
// player's RightHand meets the gun's RightHand, as C3DAttach would put
// them). Not in the meshes: The Fallen's engine knows them by name.
struct Limb {
    const char* placeChain;
    const char* turnChain;
    const char* bone;
    const char* point;
};
// The legs' targets are the feet's bones themselves (the scripts work them
// out from C3DGetJointWorldPos), so they have none.
constexpr Limb kLimbs[4] = {
    {"LeftArm", "LeftHand", "Bip01_L_Hand", "LeftHand"},
    {"RightArm", "RightHand", "Bip01_R_Hand", "RightHand"},
    {"LeftLeg", "LeftFoot", "Bip01_L_Foot", nullptr},
    {"RightLeg", "RightFoot", "Bip01_R_Foot", nullptr},
};

// A bone's axes as The Fallen's scripts take them (its turn from
// C3DGetJointWorldPos, an IK target's turn), from its axes in the
// skeleton: X their -Z, Y their -X, Z their Y. Worked out from what the
// scripts do with them: a head's -Z is where it looks (Pawn's look-at,
// CTVE_Pawn's sight), its eyes, nose and mouth are along -Z and up its -X
// (CTV_Player's NosePos, CTV_ColdBreath); a foot's Z is the ground's
// normal when it stands flat and its X towards its toes (CalcFootZ). Each
// is so on Kira's skeleton, and none the other way.
constexpr std::array<std::array<float, 3>, 3> kScriptAxes{{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};

// Axes (rows) in the scripts' order, and a vector in a bone's axes in theirs.
Axes scriptAxes(const Axes& a) {
    Axes out{};
    for (size_t k = 0; k < 3; ++k)
        for (size_t j = 0; j < 3; ++j)
            out[k] = add(out[k], mul(a[j], kScriptAxes[k][j]));
    return out;
}
Vec inScriptAxes(const Vec& v) {
    Vec out{};
    for (size_t k = 0; k < 3; ++k)
        for (size_t j = 0; j < 3; ++j)
            out[k] += kScriptAxes[k][j] * v[j];
    return out;
}

struct ScriptLevel::ActorMesh {
    Mesh mesh;
    std::unique_ptr<LayeredPoser> poser; // built on a skeleton
    std::map<std::pair<NameId, bool>, std::vector<bool>> parts;
    // Where the skeleton's axes go in the actor's own space (the mesh's
    // turn: placeMeshVertex of the skeleton's units).
    Axes skeleton{};
    std::map<NameId, int> boneIndex; // by name, as asked for
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
            const Mesh& mesh = am->mesh;
            const Vec zero = placeMeshVertex(mesh, mesh.origin);
            for (size_t k = 0; k < 3; ++k) {
                Vec3f unitStep = mesh.origin;
                unitStep[k] += mesh.scale[k] != 0 ? 1 / mesh.scale[k] : 0;
                am->skeleton[k] = sub(placeMeshVertex(mesh, unitStep), zero);
            }
        }
        it = actorMeshes_.emplace(m, std::move(am)).first;
    }
    return it->second.get();
}

// The actor's layered animations showing (C3DPlayAnim), as the poser takes
// them; `names` keeps their animations' names while they're used.
std::vector<LayeredPoser::Layer> ScriptLevel::posedLayers(VmObject* a, ActorMesh& m, std::vector<std::string>& names) {
    std::vector<LayeredPoser::Layer> layers;
    const auto* ls = layersOf(a);
    if (!ls || !m.poser)
        return layers;
    names.clear();
    // (The layers point into it.)
    names.reserve(ls->size() * 2);
    for (const auto& [group, l] : *ls) {
        if ((l.weight <= 0 && !l.fromSequence) || !l.sequence)
            continue;
        auto part = m.parts.find({l.bone, l.rest});
        if (part == m.parts.end())
            part = m.parts.emplace(std::make_pair(l.bone, l.rest), m.poser->part(vm_.nameText(l.bone), l.rest)).first;
        if (part->second.empty())
            continue;
        // What it replaced first, held under it.
        if (l.fromSequence) {
            names.push_back(vm_.nameText(l.fromSequence));
            layers.push_back({names.back(), l.fromFrame, &part->second, l.fromWeight});
        }
        names.push_back(vm_.nameText(l.sequence));
        layers.push_back({names.back(), l.frame, &part->second, l.weight});
    }
    return layers;
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
        std::vector<std::string> names;
        const std::vector<LayeredPoser::Layer> layers = posedLayers(a, m, names);
        const LayeredPoser::Adjust adjust = adjustOf(a, m);
        if (m.poser->pose(seq, frame, layers, verts, &adjust))
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
namespace {

bool pointFrame(const Mesh& mesh, const std::vector<Vec>* verts, const MeshAttachPoint& point, Vec& at, Axes& axes) {
    if (point.type == 1) {
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

} // namespace

bool ScriptLevel::attachFrame(VmObject* a, ActorMesh& m, const MeshAttachPoint& point, Vec& at, Axes& axes) {
    return pointFrame(m.mesh, point.type == 1 ? posedVertices(a, m) : nullptr, point, at, axes);
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

bool ScriptLevel::boneAt(VmObject* a, NameId bone, BonePlace place, Vec& location, Rot& rotation) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    if (!m || !m->poser)
        return false;
    const Value* seqValue = prop(a, "AnimSequence");
    const Value* frameValue = prop(a, "AnimFrame");
    if (!seqValue || !seqValue->i)
        return false;
    const std::string seq = vm_.nameText(seqValue->i), name = vm_.nameText(bone);
    const float frame = frameValue ? frameValue->asFloat() : 0;
    std::vector<std::string> names;
    std::vector<LayeredPoser::Layer> layers;
    LayeredPoser::Adjust adjust;
    // With its layers and its scripts' bends, as it's drawn.
    auto pose = [&] {
        layers = posedLayers(a, *m, names);
        adjust = adjustOf(a, *m);
    };
    const Value* scale = prop(a, "DrawScale");
    const float ds = scale ? scale->asFloat() : 1;
    if (place == BonePlace::InParent) {
        pose();
        // Its place in its parent bone, in the parent's axes (by DrawScale).
        Vec origin{};
        Axes axes{};
        if (!m->poser->boneInParent(seq, frame, layers, name, origin, axes, &adjust))
            return false;
        // In the parent's axes as the scripts order them, and its own so.
        Axes local{};
        for (size_t k = 0; k < 3; ++k)
            local[k] = inScriptAxes(axes[k]);
        location = mul(inScriptAxes(origin), ds);
        rotation = rotOf(scriptAxes(local));
        return true;
    }
    // The skeleton posed once a tick (the heads ask after their bones every
    // tick, and posing it took as long as the rest of their scripts).
    auto& [tick, worlds] = posedBones_[a];
    if (tick != ticks_ + 1 || worlds.empty()) {
        tick = ticks_ + 1;
        pose();
        if (!m->poser->skeleton(seq, frame, layers, worlds, &adjust))
            worlds.clear();
    }
    if (worlds.empty())
        return false;
    // The bone, and for FromParent its parent, where they are.
    auto placed = [&](const std::string& which, Vec& at, Axes& axes, bool world) {
        const NameId key = vm_.name(which);
        auto found = m->boneIndex.find(key);
        if (found == m->boneIndex.end())
            found = m->boneIndex.emplace(key, m->poser->boneIndex(which)).first;
        if (found->second < 0 || size_t(found->second) >= worlds.size())
            return false;
        // Into packed units, as the vertices are (by Scale and Origin).
        const std::array<float, 12>& w = worlds[size_t(found->second)];
        const Mesh& mesh = m->mesh;
        Vec origin{};
        Axes packed{};
        for (size_t c = 0; c < 3; ++c) {
            origin[c] = w[9 + c] / mesh.scale[c] + mesh.origin[c];
            for (size_t row = 0; row < 3; ++row)
                packed[row][c] = w[row * 3 + c] / mesh.scale[c];
        }
        at = placeMeshVertex(m->mesh, origin);
        for (size_t k = 0; k < 3; ++k)
            axes[k] = unit(sub(placeMeshVertex(m->mesh, add(origin, packed[k])), at));
        if (world)
            toWorld(a, at, &axes);
        else
            at = mul(at, ds);
        return true;
    };
    Vec at{};
    Axes axes{};
    if (!placed(name, at, axes, place != BonePlace::Local))
        return false;
    if (place == BonePlace::FromParent) {
        // From its parent, in the world's axes.
        Vec parent{};
        Axes parentAxes{};
        const std::string up = m->poser->parentOf(name);
        if (!up.empty() && placed(up, parent, parentAxes, true))
            at = sub(at, parent);
    }
    location = at;
    rotation = rotOf(scriptAxes(axes));
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

namespace {

Axes axesOf(const ScriptLevel::Rot& r) {
    const auto f = rotationAxes({r[0], r[1], r[2]});
    return {Vec{f[0][0], f[0][1], f[0][2]}, Vec{f[1][0], f[1][1], f[1][2]}, Vec{f[2][0], f[2][1], f[2][2]}};
}
float dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// v (whose axes are f's rows) in f's terms.
Vec within(const Vec& v, const Axes& f) { return {dot(v, f[0]), dot(v, f[1]), dot(v, f[2])}; }
Vec3f vec3(const Vec& v) { return {v[0], v[1], v[2]}; }

int limbOf(const std::string& chain, bool& place) {
    const std::string wanted = fs::toLower(chain);
    for (size_t l = 0; l < 4; ++l) {
        if (wanted == fs::toLower(kLimbs[l].placeChain) || wanted == fs::toLower(kLimbs[l].turnChain)) {
            place = wanted == fs::toLower(kLimbs[l].placeChain);
            return int(l);
        }
    }
    return -1;
}

} // namespace

// The pose kept for the tick, gone once the scripts bend the bones.
void ScriptLevel::forgetPose(VmObject* a) {
    posed_.erase(a);
    posedBones_.erase(a);
}

void ScriptLevel::turnBone(VmObject* a, NameId bone, int axis, float degrees) {
    bends_[a].turns[{bone, axis}] = degrees;
    forgetPose(a);
}

void ScriptLevel::turnBoneInActor(VmObject* a, NameId bone, const Rot& rotation) {
    bends_[a].actorTurns[bone] = rotation;
    forgetPose(a);
}

bool ScriptLevel::setIk(VmObject* a, NameId chain, const Vec* at, const Axes* axes, float weight) {
    bool place = false;
    const int l = limbOf(vm_.nameText(chain), place);
    if (l < 0)
        return false;
    Bends::Ik& ik = bends_[a].limbs[size_t(l)];
    if (at) {
        ik.place = true;
        ik.at = *at;
        ik.placeWeight = weight;
    }
    if (axes) {
        ik.turn = true;
        ik.axes = *axes;
        ik.turnWeight = weight;
    }
    forgetPose(a);
    return true;
}

void ScriptLevel::clearIk(VmObject* a, NameId chain) {
    bool place = false;
    const int l = limbOf(vm_.nameText(chain), place);
    auto it = bends_.find(a);
    if (l < 0 || it == bends_.end())
        return;
    Bends::Ik& ik = it->second.limbs[size_t(l)];
    (place ? ik.place : ik.turn) = false;
    forgetPose(a);
}

LayeredPoser::Adjust ScriptLevel::boneAdjust(VmObject* a) {
    if (!a || bends_.find(a) == bends_.end())
        return {};
    ActorMesh* m = actorMesh(a);
    return m && m->poser ? adjustOf(a, *m) : LayeredPoser::Adjust{};
}

// The actor's bends in its skeleton's space: places and axes in the world
// brought back through the actor's placement (toWorld undone) and the
// mesh's turn; a limb's point and axes on its end bone found once, from
// the mesh posed without them.
LayeredPoser::Adjust ScriptLevel::adjustOf(VmObject* a, ActorMesh& m) {
    LayeredPoser::Adjust out;
    auto it = bends_.find(a);
    if (it == bends_.end() || !m.poser)
        return out;
    Bends& b = it->second;
    // The limbs' points, once a tick: from the pose without the reaches,
    // then again with them (where the skin at the wrist puts them once the
    // arm's bent), and the reaches worked out again from that.
    bool measured = false;
    for (size_t l = 0; l < 4 && !measured; ++l)
        if ((b.limbs[l].place || b.limbs[l].turn) && b.effectors[l].tick != ticks_ + 1) {
            if (!measureEffectors(a, m, b, nullptr))
                return out;
            measured = true;
        }
    if (measured) {
        const LayeredPoser::Adjust first = adjustOf(a, m);
        if (!first.reaches.empty())
            measureEffectors(a, m, b, &first);
    }
    for (const auto& [key, degrees] : b.turns)
        out.turns.push_back({vm_.nameText(key.first), key.second, degrees * 3.14159265f / 180});
    const Axes& sk = m.skeleton;
    for (const auto& [bone, rot] : b.actorTurns) {
        // Where each of the skeleton's axes goes: into the actor's space,
        // turned there, and back.
        const Axes turn = axesOf(rot);
        LayeredPoser::Adjust::SpaceTurn t{vm_.nameText(bone), {}};
        for (size_t k = 0; k < 3; ++k) {
            Vec turned{};
            for (size_t j = 0; j < 3; ++j)
                turned = add(turned, mul(turn[j], sk[k][j]));
            t.turn[k] = vec3(within(turned, sk));
        }
        out.spaceTurns.push_back(t);
    }
    // From the world into the actor's own space (as toWorld, undone).
    const Value* loc = prop(a, "Location");
    const Value* pivot = prop(a, "PrePivot");
    const Value* rotValue = prop(a, "Rotation");
    const Value* scale = prop(a, "DrawScale");
    const Axes frame = axesOf(rotValue ? Vm::rotatorOf(*rotValue) : Rot{});
    const float ds = scale && scale->asFloat() != 0 ? scale->asFloat() : 1;
    const Vec base = add(loc ? Vm::vectorOf(*loc) : Vec{}, pivot ? Vm::vectorOf(*pivot) : Vec{});
    for (size_t l = 0; l < 4; ++l) {
        const Bends::Ik& ik = b.limbs[l];
        if (!ik.place && !ik.turn)
            continue;
        const Bends::Effector& e = b.effectors[l];
        if (e.tick != ticks_ + 1)
            continue;
        LayeredPoser::Adjust::Reach r;
        r.bone = kLimbs[l].bone;
        r.offset = vec3(e.offset);
        if (ik.place) {
            r.at = vec3(within(mul(within(sub(ik.at, base), frame), 1 / ds), sk));
            r.placeWeight = ik.placeWeight;
        }
        if (ik.turn) {
            // The point's axes wanted, in the skeleton's space; the bone's
            // are those with the point's own turn on the bone undone.
            Axes wanted{};
            for (size_t k = 0; k < 3; ++k)
                wanted[k] = within(within(ik.axes[k], frame), sk);
            for (size_t j = 0; j < 3; ++j) {
                Vec row{};
                for (size_t k = 0; k < 3; ++k)
                    row = add(row, mul(wanted[k], e.axes[k][j]));
                r.axes[j] = vec3(row);
            }
            r.turnWeight = ik.turnWeight;
        }
        out.reaches.push_back(r);
    }
    return out;
}

// Each limb's point on its end bone, in the bone's axes: from the mesh
// posed as its animations have it this tick (the point's at the wrist, on
// skin the fingers and forearm move too), with `adjust` if given. False if
// it can't be posed now.
bool ScriptLevel::measureEffectors(VmObject* a, ActorMesh& m, Bends& b, const LayeredPoser::Adjust* adjust) {
    const Value* seqValue = prop(a, "AnimSequence");
    const Value* frameValue = prop(a, "AnimFrame");
    if (!seqValue || !seqValue->i)
        return false;
    const std::string seq = vm_.nameText(seqValue->i);
    const float frame = frameValue ? frameValue->asFloat() : 0;
    std::vector<std::string> names;
    const std::vector<LayeredPoser::Layer> layers = posedLayers(a, m, names);
    std::vector<Vec> verts;
    if (!m.poser->pose(seq, frame, layers, verts, adjust))
        return false;
    for (size_t l = 0; l < 4; ++l) {
        Bends::Effector& e = b.effectors[l];
        e = {};
        const MeshAttachPoint* point = kLimbs[l].point ? findPoint(m.mesh.attachPoints, kLimbs[l].point) : nullptr;
        Vec3f origin{};
        std::array<Vec3f, 3> bone{};
        Vec at{};
        Axes axes{};
        if (!m.poser->boneInSkeleton(seq, frame, layers, kLimbs[l].bone, origin, bone, adjust))
            continue; // no such limb
        e.tick = ticks_ + 1;
        if (!point || !pointFrame(m.mesh, &verts, *point, at, axes)) {
            // The bone itself reaches, turned as the scripts take its axes.
            for (size_t k = 0; k < 3; ++k)
                e.axes[k] = {kScriptAxes[k][0], kScriptAxes[k][1], kScriptAxes[k][2]};
            continue;
        }
        const Axes boneAxes{unit(Vec{bone[0][0], bone[0][1], bone[0][2]}),
                            unit(Vec{bone[1][0], bone[1][1], bone[1][2]}),
                            unit(Vec{bone[2][0], bone[2][1], bone[2][2]})};
        e.offset = within(sub(within(at, m.skeleton), Vec{origin[0], origin[1], origin[2]}), boneAxes);
        for (size_t k = 0; k < 3; ++k)
            e.axes[k] = within(within(axes[k], m.skeleton), boneAxes);
    }
    return true;
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
    // C3DGetJointWorldPos(name Bone, out Vector Location, out Rotator Rotation): in the world;
    // LocalPos from the actor, unturned; WorldPosRel from its parent bone, in the world's axes;
    // LocalPosRel its place in its parent bone (none of the last three called by The Fallen's
    // scripts: Reluva's reading of their names).
    const std::pair<const char*, ScriptLevel::BonePlace> joints[] = {
        {"Actor.C3DGetJointWorldPos", ScriptLevel::BonePlace::World},
        {"Actor.C3DGetJointLocalPos", ScriptLevel::BonePlace::Local},
        {"Actor.C3DGetJointWorldPosRel", ScriptLevel::BonePlace::FromParent},
        {"Actor.C3DGetJointLocalPosRel", ScriptLevel::BonePlace::InParent},
    };
    for (const auto& [joint, place] : joints) {
        const bool world = place == ScriptLevel::BonePlace::World;
        reg(joint, [=](NativeCall& c) {
            ScriptLevel::Vec at{};
            ScriptLevel::Rot r{};
            const bool found = c.vm.level && c.self && c.vm.level->boneAt(c.self, c.i(0), place, at, r);
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
    // C3DApplyRotation(name Bone, name Axis, float Angle): degrees about one
    // of the bone's axes as the scripts take them (kScriptAxes), turning as
    // a rotator's Pitch, Yaw or Roll would. In the skeleton's own axes that
    // comes to Pitch about X, Yaw Y and Roll Z, each the right-handed way:
    // a positive Pitch bends the back and nods the head forward, as the
    // scripts mean when the player looks down.
    reg("Actor.C3DApplyRotation", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        const std::string axis = fs::toLower(c.vm.nameText(c.i(1)));
        const int k = axis == "pitch" ? 0 : axis == "yaw" ? 1 : axis == "roll" ? 2 : -1;
        if (k >= 0)
            c.vm.level->turnBone(c.self, c.i(0), k, c.f(2));
    });
    // C3DApplyWorldRotation(name Bone, Rotator Rotation): the bone turned
    // where it is, its children with it. Taken in the actor's axes, not
    // the world's (the hips' and spine's twist and lean, the head's turn
    // to look at someone are all made so).
    reg("Actor.C3DApplyWorldRotation", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->turnBoneInActor(c.self, c.i(0), Vm::rotatorOf(c.args[1]));
    });
    // The IK chains: C3DSetIkTargetPositionAbs(name IkChain, Vector Location,
    // float BlendFactor), C3DSetIkTargetOrientationAbs(.., Rotator Rotation, ..)
    // in the world; C3DSetIkTargetPosition(.., Actor Target, Vector Offset, ..)
    // and C3DSetIkTargetOrientation(.., Actor Target, Rotator RelRotation, ..)
    // from an actor (the player's gun).
    reg("Actor.C3DSetIkTargetPositionAbs", [](NativeCall& c) {
        if (c.vm.level && c.self) {
            const ScriptLevel::Vec at = Vm::vectorOf(c.args[1]);
            c.vm.level->setIk(c.self, c.i(0), &at, nullptr, c.f(2));
        }
    });
    reg("Actor.C3DSetIkTargetOrientationAbs", [](NativeCall& c) {
        if (c.vm.level && c.self) {
            const Axes axes = axesOf(Vm::rotatorOf(c.args[1]));
            c.vm.level->setIk(c.self, c.i(0), nullptr, &axes, c.f(2));
        }
    });
    auto frameOf = [](NativeCall& c, VmObject* target) {
        const Value* r = target->find(c.vm, "Rotation");
        return axesOf(r ? Vm::rotatorOf(*r) : ScriptLevel::Rot{});
    };
    reg("Actor.C3DSetIkTargetPosition", [=](NativeCall& c) {
        VmObject* target = c.o(1);
        if (!c.vm.level || !c.self || !target)
            return;
        const Value* l = target->find(c.vm, "Location");
        const Axes f = frameOf(c, target);
        const ScriptLevel::Vec offset = Vm::vectorOf(c.args[2]);
        ScriptLevel::Vec at = l ? Vm::vectorOf(*l) : ScriptLevel::Vec{};
        for (size_t j = 0; j < 3; ++j)
            at = add(at, mul(f[j], offset[j]));
        c.vm.level->setIk(c.self, c.i(0), &at, nullptr, c.f(3));
    });
    reg("Actor.C3DSetIkTargetOrientation", [=](NativeCall& c) {
        VmObject* target = c.o(1);
        if (!c.vm.level || !c.self || !target)
            return;
        const Axes axes = inFrame(axesOf(Vm::rotatorOf(c.args[2])), frameOf(c, target));
        c.vm.level->setIk(c.self, c.i(0), nullptr, &axes, c.f(3));
    });
    // InterpolateRotation(Rotator A, Rotator B, float Alpha): Alpha of the
    // way from A to B, each of pitch, yaw and roll the shorter way round
    // (the scripts ease the IK targets' and the head's turns with it).
    reg("Actor.InterpolateRotation", [](NativeCall& c) {
        const auto a = Vm::rotatorOf(c.args[0]), b = Vm::rotatorOf(c.args[1]);
        const float t = c.f(2);
        ScriptLevel::Rot out{};
        for (size_t k = 0; k < 3; ++k) {
            const auto d = int32_t(int16_t(uint16_t(uint32_t(b[k] - a[k]) & 0xffff)));
            out[k] = a[k] + int32_t(std::lround(float(d) * t));
        }
        c.result = c.vm.rotatorValue(out[0], out[1], out[2]);
    });
    reg("Actor.C3DClearIkTarget", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->clearIk(c.self, c.i(0));
    });
    // Setting up The Fallen's meshes for its renderer: nothing to do here.
    for (const char* none : {"Actor.C3DActorInit", "Actor.C3DInitAltMeshes"})
        reg(none, [](NativeCall&) {});
}

} // namespace reluva::unreal
