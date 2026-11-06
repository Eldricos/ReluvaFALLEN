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
// the end bone, and for a skeleton without the chains (Reluva's stand-in
// solve) the attachment point on it that reaches. Skeletons with them
// (Shared.Male's, which the player borrows) reach from the end bone's
// origin, as Engine.dll's solver does (mesh.cpp).
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
            if (am->poser)
                am->poser->setPlacement({am->skeleton[0], am->skeleton[1], am->skeleton[2]});
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
        layers.push_back({names.back(), l.frame, &part->second, l.weight, l.clear});
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
// DrawScale), as Engine.dll's PointMatrix: worked out in the skeleton's
// space (the C3D model's: packed vertices less Origin, times Scale), then
// placed as the mesh's vertices are. On a triangle (type 1) its turn and
// offset are in the triangle's frame as posed: from its corner 0, X
// toward corner 2, Y its normal, Z = X x Y. On the mesh (type 2) they're
// from the skeleton's origin as they are. A kind 7 point is no turn and
// no offset (the matrix's identity flag). `raw`: its axes in the
// skeleton's space, unplaced (C3DGetAttachPtOffset's rotation).
// reluva-tool ue-points checks this against the meshes' precomputed
// places: all 65 within 5 units, every one's axes the same.
namespace {

bool pointFrame(const Mesh& mesh, const std::vector<Vec>* verts, const MeshAttachPoint& point, Vec& at, Axes& axes,
                Axes* raw = nullptr) {
    const bool identity = point.kind == 7;
    const Axes turn = identity ? Axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}}
                               : Axes{point.axes[0], point.axes[1], point.axes[2]};
    const Vec offset = identity ? Vec{} : Vec{point.offset[0], point.offset[1], point.offset[2]};
    Vec pos{};
    Axes rows = turn;
    if (point.type == 1) {
        if (!verts || point.triangle < 0 || size_t(point.triangle) >= mesh.tris.size())
            return false;
        const MeshTri& t = mesh.tris[size_t(point.triangle)];
        Vec c[3];
        for (size_t k = 0; k < 3; ++k) {
            if (size_t(t.vertex[k]) >= verts->size())
                return false;
            const Vec& v = (*verts)[size_t(t.vertex[k])];
            for (size_t q = 0; q < 3; ++q)
                c[k][q] = (v[q] - mesh.origin[q]) * mesh.scale[q];
        }
        const Vec x = unit(sub(c[2], c[0]));
        const Vec y = unit(cross(sub(c[1], c[0]), sub(c[2], c[0])));
        const Axes frame{x, y, cross(x, y)};
        pos = c[0];
        for (size_t j = 0; j < 3; ++j)
            pos = add(pos, mul(frame[j], offset[j]));
        rows = inFrame(turn, frame);
    } else if (point.type == 2) {
        pos = offset;
    } else {
        // Any other type: Engine.dll leaves the identity it started with.
        pos = {};
        rows = Axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
    }
    if (raw)
        *raw = rows;
    const Vec3f placed = placeMeshScaled(mesh, {pos[0], pos[1], pos[2]});
    at = {placed[0], placed[1], placed[2]};
    for (size_t k = 0; k < 3; ++k) {
        const Vec3f r = placeMeshScaled(mesh, {rows[k][0], rows[k][1], rows[k][2]});
        axes[k] = unit({r[0], r[1], r[2]});
    }
    return true;
}

// The IK's effectors (measureEffectors) keep Reluva's own reading of a
// hand's point: from the triangle's middle, in the axes of the bone it
// moves with. How Engine.dll's IK takes its chains' ends isn't read.
bool ikPointFrame(const Mesh& mesh, const std::vector<Vec>* verts, const MeshAttachPoint& point, Vec& at, Axes& axes,
                const std::array<float, 12>* bone = nullptr, const Axes* skeleton = nullptr) {
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
        Axes frame{x, cross(z, x), z};
        if (bone && skeleton)
            for (size_t r = 0; r < 3; ++r) {
                Vec row{};
                for (size_t k = 0; k < 3; ++k)
                    row = add(row, mul((*skeleton)[k], (*bone)[r * 3 + k]));
                frame[r] = unit(row);
            }
        at = mul(add(add(c[0], c[1]), c[2]), 1.0f / 3);
        for (size_t j = 0; j < 3; ++j)
            at = add(at, mul(frame[j], point.offset[j]));
        axes = inFrame(point.axes, frame);
        return true;
    }
    // From the origin, in the skeleton's units (Scale applied, the import
    // turn not): the precomputed places and type 2 points alike (mesh.md).
    at = placeMeshScaled(mesh, point.offset);
    for (size_t k = 0; k < 3; ++k)
        axes[k] = unit(placeMeshScaled(mesh, point.axes[k]));
    return true;
}

} // namespace

bool ScriptLevel::attachFrame(VmObject* a, ActorMesh& m, const MeshAttachPoint& point, Vec& at, Axes& axes,
                              Axes* raw) {
    return pointFrame(m.mesh, point.type == 1 ? posedVertices(a, m) : nullptr, point, at, axes, raw);
}

bool ScriptLevel::attachPointAt(VmObject* a, NameId name, bool precomputed, bool world, Vec& location, Rot& rotation) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    const MeshAttachPoint* point =
        m ? findPoint(precomputed ? m->mesh.precomputedPoints : m->mesh.attachPoints, vm_.nameText(name)) : nullptr;
    Vec at{};
    Axes axes{}, raw{};
    if (!point || !attachFrame(a, *m, *point, at, axes, &raw))
        return false;
    if (world) {
        // C3DGetAttachPtCoords: in the world, its axes in the scripts'
        // order (X the point's -Z, Y its -X, Z its Y), as the bones'.
        toWorld(a, at, &axes);
        location = at;
        rotation = rotOf(scriptAxes(axes));
    } else {
        // C3DGetAttachPtOffset: from the mesh's origin, no DrawScale, and
        // the turn as the skeleton has it (Engine.dll swaps the place's
        // axes but not the turn's).
        location = at;
        rotation = rotOf(raw);
    }
    return true;
}

// The skeleton posed once a tick, with its layers and its scripts' bends,
// as it's drawn (the heads ask after their bones every tick, and posing it
// took as long as the rest of their scripts). None if it can't be posed.
const std::vector<std::array<float, 12>>* ScriptLevel::posedSkeleton(VmObject* a, ActorMesh& m) {
    auto& [tick, worlds] = posedBones_[a];
    if (tick != ticks_ + 1 || worlds.empty()) {
        tick = ticks_ + 1;
        worlds.clear();
        const Value* seqValue = prop(a, "AnimSequence");
        const Value* frameValue = prop(a, "AnimFrame");
        if (!m.poser || !seqValue || !seqValue->i)
            return nullptr;
        std::vector<std::string> names;
        const std::vector<LayeredPoser::Layer> layers = posedLayers(a, m, names);
        const LayeredPoser::Adjust adjust = adjustOf(a, m);
        if (!m.poser->skeleton(vm_.nameText(seqValue->i), frameValue ? frameValue->asFloat() : 0, layers, worlds,
                               &adjust))
            worlds.clear();
    }
    return worlds.empty() ? nullptr : &worlds;
}

bool ScriptLevel::boneAt(VmObject* a, NameId bone, BonePlace place, Vec& location, Rot& rotation) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    if (!m || !m->poser)
        return false;
    const Value* seqValue = prop(a, "AnimSequence");
    if (!seqValue || !seqValue->i)
        return false;
    const std::string name = vm_.nameText(bone);
    const std::vector<std::array<float, 12>>* posed = posedSkeleton(a, *m);
    if (!posed)
        return false;
    const std::vector<std::array<float, 12>>& worlds = *posed;
    if (place == BonePlace::Local || place == BonePlace::InParent) {
        // Engine.dll's: the bone in the skeleton's own space, only its axes
        // swapped to Unreal's (no Location, Rotation, DrawScale, mesh Scale
        // or PrePivot). LocalPosRel the same place, and its turn from its
        // bind pose (Bind^-1 Current) for the rotation.
        const int b = m->poser->boneIndex(name);
        if (b < 0 || size_t(b) >= worlds.size())
            return false;
        const std::array<float, 12>& w = worlds[size_t(b)];
        std::array<float, 12> r = w;
        if (place == BonePlace::InParent) {
            const std::vector<std::array<float, 12>> binds = m->poser->bindSkeleton();
            if (size_t(b) >= binds.size())
                return false;
            const std::array<float, 12>& bind = binds[size_t(b)];
            for (size_t row = 0; row < 3; ++row)
                for (size_t c = 0; c < 3; ++c) {
                    float sum = 0;
                    for (size_t j = 0; j < 3; ++j)
                        sum += bind[j * 3 + row] * w[j * 3 + c];
                    r[row * 3 + c] = sum;
                }
        }
        Axes axes{};
        for (size_t k = 0; k < 3; ++k)
            axes[k] = inScriptAxes({r[k * 3], r[k * 3 + 1], r[k * 3 + 2]});
        location = inScriptAxes({w[9], w[10], w[11]});
        rotation = rotOf(scriptAxes(axes));
        return true;
    }
    // The bone where it is; for WorldRel its axes turned only as from its
    // bind pose (Bind^-1 x posed, as Engine.dll's
    // execC3DGetJointWorldPosRel), its place as it is.
    const NameId key = vm_.name(name);
    auto found = m->boneIndex.find(key);
    if (found == m->boneIndex.end())
        found = m->boneIndex.emplace(key, m->poser->boneIndex(name)).first;
    if (found->second < 0 || size_t(found->second) >= worlds.size())
        return false;
    std::array<float, 12> w = worlds[size_t(found->second)];
    if (place == BonePlace::WorldRel) {
        const std::vector<std::array<float, 12>> binds = m->poser->bindSkeleton();
        if (size_t(found->second) >= binds.size())
            return false;
        const std::array<float, 12>& bind = binds[size_t(found->second)];
        const std::array<float, 12> posedBone = w;
        for (size_t row = 0; row < 3; ++row)
            for (size_t c = 0; c < 3; ++c) {
                float sum = 0;
                for (size_t j = 0; j < 3; ++j)
                    sum += bind[j * 3 + row] * posedBone[j * 3 + c];
                w[row * 3 + c] = sum;
            }
    }
    // Into packed units, as the vertices are (by Scale and Origin), then
    // placed as the mesh is and into the world.
    const Mesh& mesh = m->mesh;
    Vec origin{};
    Axes packed{};
    for (size_t c = 0; c < 3; ++c) {
        origin[c] = w[9 + c] / mesh.scale[c] + mesh.origin[c];
        for (size_t row = 0; row < 3; ++row)
            packed[row][c] = w[row * 3 + c] / mesh.scale[c];
    }
    Vec at = placeMeshVertex(m->mesh, origin);
    Axes axes{};
    for (size_t k = 0; k < 3; ++k)
        axes[k] = unit(sub(placeMeshVertex(m->mesh, add(origin, packed[k])), at));
    toWorld(a, at, &axes);
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
        // A point not named is the mesh's first; one that isn't there, or
        // a mesh with none, ends the attachment (as Engine.dll's C3DAttach).
        auto pointOf = [&](ActorMesh* mm, NameId name) -> const MeshAttachPoint* {
            if (!mm || mm->mesh.attachPoints.empty())
                return nullptr;
            return name ? findPoint(mm->mesh.attachPoints, vm_.nameText(name)) : &mm->mesh.attachPoints.front();
        };
        const MeshAttachPoint* pp = pointOf(pm, at.parentPoint);
        const MeshAttachPoint* cp = pointOf(cm, at.childPoint);
        if (!pp || !cp) {
            attachments_.erase(child);
            continue;
        }
        Vec where{}, mine{};
        Axes theirs{}, ours{};
        if (!attachFrame(at.parent, *pm, *pp, where, theirs) || !attachFrame(child, *cm, *cp, mine, ours))
            continue;
        toWorld(at.parent, where, &theirs);
        // Its axes: row j = sum over k of ours[k][j] theirs[k] (ours undone, then theirs).
        Axes frame{};
        for (size_t j = 0; j < 3; ++j)
            for (size_t k = 0; k < 3; ++k)
                frame[j] = add(frame[j], mul(unit(theirs[k]), unit(ours[k])[j]));
        // Its own point undone in the parent's scale: Engine.dll places the
        // child's origin so, its own DrawScale and PrePivot not allowed for.
        const Value* scale = prop(at.parent, "DrawScale");
        const float ds = scale ? scale->asFloat() : 1;
        Vec loc = where;
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

// The scripts' bone turns: kept in order for this tick (a tick's first
// starts the list afresh), as the engine applies each to the pose at once
// and works the pose out again next frame.
ScriptLevel::Bends::Op& ScriptLevel::newBoneOp(VmObject* a) {
    Bends& b = bends_[a];
    if (b.opsTick != int64_t(ticks_)) {
        b.ops.clear();
        b.opsTick = int64_t(ticks_);
    }
    forgetPose(a);
    return b.ops.emplace_back();
}

void ScriptLevel::clearStaleBoneOps(VmObject* a) {
    auto it = bends_.find(a);
    if (it == bends_.end() || it->second.opsTick == int64_t(ticks_) || it->second.ops.empty())
        return;
    it->second.ops.clear();
    forgetPose(a);
}

void ScriptLevel::turnBone(VmObject* a, NameId bone, int axis, float degrees) {
    Bends::Op& op = newBoneOp(a);
    op.kind = 0, op.bone = bone, op.axis = axis, op.degrees = degrees;
}

void ScriptLevel::turnBoneInActor(VmObject* a, NameId bone, const Rot& rotation) {
    Bends::Op& op = newBoneOp(a);
    op.kind = 1, op.bone = bone, op.rotation = rotation;
}

void ScriptLevel::removeBoneTurn(VmObject* a, NameId bone, bool yaw, bool pitch, bool roll) {
    Bends::Op& op = newBoneOp(a);
    op.kind = 2, op.bone = bone, op.remove = {pitch, yaw, roll};
}

void ScriptLevel::ikOnBase(VmObject* a, NameId chain, bool onBase) {
    bool place = false;
    const int l = limbOf(vm_.nameText(chain), place);
    if (l < 0)
        return;
    bends_[a].limbs[size_t(l)].onBase = onBase;
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

LayeredPoser* ScriptLevel::poserOf(VmObject* a) {
    ActorMesh* m = a ? actorMesh(a) : nullptr;
    return m ? m->poser.get() : nullptr;
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
    // With the skeleton's own chains (Engine.dll's), the effector is the
    // end bone's origin and its axes as the scripts take them.
    for (size_t l = 0; l < 4; ++l)
        if ((b.limbs[l].place || b.limbs[l].turn) && m.poser->hasChain(kLimbs[l].placeChain)) {
            Bends::Effector& e = b.effectors[l];
            e.tick = ticks_ + 1;
            e.offset = {};
            for (size_t k = 0; k < 3; ++k)
                e.axes[k] = {kScriptAxes[k][0], kScriptAxes[k][1], kScriptAxes[k][2]};
        }
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
    // This tick's turns (or the last tick's, until the next one's scripts run).
    const Axes& sk = m.skeleton;
    if (b.opsTick == int64_t(ticks_) || b.opsTick + 1 == int64_t(ticks_))
        for (const Bends::Op& op : b.ops) {
            LayeredPoser::Adjust::Op o;
            o.bone = vm_.nameText(op.bone);
            if (op.kind == 0) {
                o.kind = LayeredPoser::Adjust::Op::Own;
                o.axis = op.axis;
                o.radians = op.degrees * 3.14159265f / 180;
            } else if (op.kind == 1) {
                // Where each of the skeleton's axes goes: into the actor's
                // space, turned there, and back.
                o.kind = LayeredPoser::Adjust::Op::Space;
                const Axes turn = axesOf(op.rotation);
                for (size_t k = 0; k < 3; ++k) {
                    Vec turned{};
                    for (size_t j = 0; j < 3; ++j)
                        turned = add(turned, mul(turn[j], sk[k][j]));
                    o.turn[k] = vec3(within(turned, sk));
                }
            } else {
                o.kind = LayeredPoser::Adjust::Op::Remove;
                o.remove = op.remove;
            }
            out.ops.push_back(o);
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
        r.placeChain = kLimbs[l].placeChain;
        r.turnChain = kLimbs[l].turnChain;
        r.offset = vec3(e.offset);
        r.onBase = ik.onBase;
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
    std::vector<std::array<float, 12>> worlds;
    if (!m.poser->pose(seq, frame, layers, verts, adjust) || !m.poser->skeleton(seq, frame, layers, worlds, adjust))
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
        const int pointBone = point && point->type == 1 && point->triangle >= 0 &&
                                      size_t(point->triangle) < m.mesh.tris.size()
                                  ? m.poser->mainBone(m.mesh.tris[size_t(point->triangle)].vertex)
                                  : -1;
        const std::array<float, 12>* boneWorld =
            pointBone >= 0 && size_t(pointBone) < worlds.size() ? &worlds[size_t(pointBone)] : nullptr;
        if (!point || !ikPointFrame(m.mesh, &verts, *point, at, axes, boneWorld, &m.skeleton)) {
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
    // WorldPosRel the same place, turned only as from its bind pose, in the world's axes;
    // LocalPos in the skeleton's space, in Unreal's axes; LocalPosRel that place, turned as from
    // its bind pose (all as Engine.dll's). None of the last three is called by The Fallen's
    // scripts. A bone that isn't there leaves the out values as they were, as Engine.dll does
    // (it logs "Bone doesn't exist"); but WorldPos answers with the actor's own place, as
    // Reluva may have no skeleton for a mesh the engine poses (so what starts there starts at
    // the actor).
    const std::pair<const char*, ScriptLevel::BonePlace> joints[] = {
        {"Actor.C3DGetJointWorldPos", ScriptLevel::BonePlace::World},
        {"Actor.C3DGetJointLocalPos", ScriptLevel::BonePlace::Local},
        {"Actor.C3DGetJointWorldPosRel", ScriptLevel::BonePlace::WorldRel},
        {"Actor.C3DGetJointLocalPosRel", ScriptLevel::BonePlace::InParent},
    };
    for (const auto& [joint, place] : joints) {
        const bool world = place == ScriptLevel::BonePlace::World;
        reg(joint, [=](NativeCall& c) {
            ScriptLevel::Vec at{};
            ScriptLevel::Rot r{};
            const bool found = c.vm.level && c.self && c.vm.level->boneAt(c.self, c.i(0), place, at, r);
            if (found || place == ScriptLevel::BonePlace::World)
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
    // way from A to B, the shorter way round, as one even turn: Engine.dll
    // slerps the two as quaternions (the scripts ease the IK targets', the
    // head's and the camera's turns with it).
    reg("Actor.InterpolateRotation", [](NativeCall& c) {
        const auto a = Vm::rotatorOf(c.args[0]), b = Vm::rotatorOf(c.args[1]);
        const Rotator r = slerpRotators({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, c.f(2));
        c.result = c.vm.rotatorValue(r.pitch, r.yaw, r.roll);
    });
    reg("Actor.C3DClearIkTarget", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->clearIk(c.self, c.i(0));
    });
    // C3DRemoveRotation(name Bone, bool bYawAxis, bool bPitchAxis, bool
    // bRollAxis) and C3DApplyIKOnBasePose(name IkChain, int Val): no script
    // calls them; these are Reluva's readings of their names.
    reg("Actor.C3DRemoveRotation", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->removeBoneTurn(c.self, c.i(0), c.b(1), c.b(2), c.b(3));
    });
    reg("Actor.C3DApplyIKOnBasePose", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->ikOnBase(c.self, c.i(0), c.i(1) != 0);
    });
    // Setting up The Fallen's meshes for its renderer: nothing to do here.
    for (const char* none : {"Actor.C3DActorInit", "Actor.C3DInitAltMeshes"})
        reg(none, [](NativeCall&) {});
}

} // namespace reluva::unreal
