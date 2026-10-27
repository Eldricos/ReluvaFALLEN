// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Meshes (Mesh and LodMesh objects): vertex-animated models, every frame
// keeping every vertex's position. Characters, items and decorations.
// See docs/formats/unreal/mesh.md.

#include "engines/unreal/model.h"
#include "engines/unreal/properties.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::unreal {

struct MeshAnimSeq {
    std::string name, group;
    int32_t startFrame = 0, numFrames = 0;
    float rate = 0; // frames per second
    // Functions called on the actor as the animation passes their time
    // (0-1 through it).
    struct Notify {
        float time = 0;
        std::string function;
    };
    std::vector<Notify> notifies;
};

// A triangle: three of a frame's vertices, each corner's texture
// coordinates (0-1), which of the mesh's textures it uses, and its flags.
struct MeshTri {
    std::array<int32_t, 3> vertex{};
    std::array<std::array<float, 2>, 3> uv{};
    int32_t textureIndex = 0;
    uint32_t polyFlags = 0;
};

// The Fallen's attachment points (kept in its imported-model block): named
// places on a mesh, moving with it, where what's held or worn is put
// (C3DAttach) and where effects start (C3DGetAttachPtCoords: GunTip,
// FirePoint, KickPoint...). See mesh.md.
struct MeshAttachPoint {
    std::string name;
    int type = 1;          // 1: on a triangle, following it as the mesh moves; 2: on the mesh's own origin
    int32_t triangle = -1; // which (type 1)
    std::array<Vec3f, 3> axes{Vec3f{1, 0, 0}, Vec3f{0, 1, 0}, Vec3f{0, 0, 1}}; // its turn (rows: X, Y, Z)
    Vec3f offset{};        // from the triangle or the origin, in the float copy's space
    int32_t kind = 0;
};

struct Mesh {
    bool lod = false;           // a LodMesh
    int32_t frameVerts = 0;     // vertices per frame
    int32_t animFrames = 0;
    std::vector<Vec3f> verts;   // animFrames x frameVerts, as stored (before Scale and Origin)
    int vertexBytes = 4;        // how each vertex was stored: 4 (packed 11/11/10 bits) or 8
    std::vector<MeshTri> tris;  // vertex indices into one frame
    std::vector<MeshAnimSeq> anims;
    std::vector<int32_t> textures; // object references, by texture index
    Vec3f scale{1, 1, 1}, origin{};
    Rotator rotOrigin;
    size_t trailingBytes = 0; // left after the last field read
    // The Fallen's layout: how many bytes of imported-model block (holding
    // the vertices) came between the textures and the standard fields; 0
    // for the standard layout.
    size_t importBlock = 0;
    // Its float copy of the vertices shows they're turned from how the
    // packed ones lie: that turn (rows: output axes), applied after Scale
    // and Origin and before RotOrigin. None for the standard layout.
    std::optional<std::array<Vec3f, 3>> importAxes;
    // Without a float copy to fit (most keep per-vertex weights where
    // KiraCellLight keeps its copy), the turn most of them show is taken.
    bool importAxesGuessed = false;
    // The Fallen's keyframed meshes: how many frames of vertices were
    // found, fewer than animFrames; the first is used for every frame
    // until the rest is worked out. 0: every frame was found.
    int keyFrames = 0;
    // The Fallen's meshes built on a skeleton: how many bones they were
    // posed from (0 for the others), and whether every frame was posed
    // from their animation keys (else all are the pose they're bound in).
    int bones = 0;
    bool bonesAnimated = false;
    // The Fallen's frames filled from their clips by name (vertex keys or
    // bones; see mesh.md), 0 if none.
    int framesFromClips = 0;
    int droppedAnims = 0; // animations left out, their frames past the mesh's
    std::vector<MeshAnimSeq> otherAnims; // those animations
    std::vector<MeshAttachPoint> attachPoints;
    // Places worked out in advance (C3DGetAttachPtCoordsPrecomputed: the
    // gun's tip in a firing pose, FireGunTipLoc...), from the mesh's origin
    // as a type 2 point.
    std::vector<MeshAttachPoint> precomputedPoints;
    std::vector<std::string> animationSetNames; // the shared sets its animations come from ("shared.Male")

    const MeshAnimSeq* findAnim(std::string_view name) const;
};

// Reads a Mesh or LodMesh export.
Error readMesh(const Package& package, int exportIndex, Mesh& out);

// The Fallen's shared animations: Mesh exports with a skeleton and no
// vertices (Male, Female, MaleTwo in Shared.u), whose clips, end to end,
// are the frames characters' animations past their own frames point into
// (see mesh.md).
class AnimationSet {
public:
    AnimationSet();
    ~AnimationSet();
    AnimationSet(AnimationSet&&) noexcept;
    AnimationSet& operator=(AnimationSet&&) noexcept;
    int frames() const; // its clips' frames together
    // The clip whose frames a sequence's are exactly (and, where the set
    // names its clips, of its name), or -1.
    int clipFor(int32_t startFrame, int32_t numFrames, std::string_view name = {}) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
};
Error readAnimationSet(const Package& package, int exportIndex, AnimationSet& out);

// Adds an animation to a mesh built on a skeleton (read from `package`'s
// `exportIndex`): `set`'s clip `clip`, its bones' keys given to the mesh's
// bones of the same names, as frames after the mesh's; the animation is
// `seq` over them.
Error borrowAnimation(const Package& package, int exportIndex, const AnimationSet& set, int clip,
                      const MeshAnimSeq& seq, Mesh& mesh);

// Every animation set in the packages' .u files (Mesh exports with a
// skeleton, clips and no vertices).
class PackageSet;
std::vector<AnimationSet> findAnimationSets(const PackageSet& packages);

// For a mesh built on a skeleton: borrows the animation `name` (one of
// those its frames don't have, Mesh::otherAnims) from the set with a clip
// for exactly its frames. Whether one did.
bool borrowNamedAnimation(const Package& package, int exportIndex, const std::vector<AnimationSet>& sets,
                          std::string_view name, Mesh& mesh);

// A mesh built on a skeleton, posed at draw time from more than one
// animation: The Fallen's layered animation (C3DPlayAnim) plays one on a
// part of the skeleton over the actor's own, which the frames posed when
// it's read (one animation each) can't show.
class LayeredPoser {
public:
    LayeredPoser();
    ~LayeredPoser();
    // Its skeleton and animation tables (read again from its export); the
    // shared sets for those past its own frames. None if it has no bones.
    static std::unique_ptr<LayeredPoser> open(const Package& package, int exportIndex,
                                              const std::vector<AnimationSet>& sets);
    // The bones from `bone` down (with `rest`, all the others); empty if
    // there's no bone of that name.
    std::vector<bool> part(std::string_view bone, bool rest) const;
    struct Layer {
        std::string_view anim;
        float fraction = 0;             // through it, as an actor's AnimFrame
        const std::vector<bool>* part = nullptr;
        float weight = 1;               // how much it shows over what's under it
    };
    // What's done to the pose on top of its animations (The Fallen's
    // scripts bend the back, turn the head, reach for a gun), in the
    // skeleton's space: the bones' own units and axes, as placed vertices
    // are before the mesh's turn. Done in this order.
    struct Adjust {
        // A bone turned about one of its own axes (0 X, 1 Y, 2 Z).
        struct Turn {
            std::string bone;
            int axis = 0;
            float radians = 0;
        };
        // A bone turned about its origin by `turn` (rows: where the
        // skeleton's X, Y and Z go), its children with it.
        struct SpaceTurn {
            std::string bone;
            std::array<Vec3f, 3> turn{};
        };
        // A limb's end bone reaching (the two bones above it bent so that
        // `offset`, in its own axes, comes to `at`) and turned so its axes
        // are `axes`; each blended in by its weight (0 none, 1 all).
        struct Reach {
            std::string bone;
            float placeWeight = 0, turnWeight = 0;
            Vec3f at{}, offset{};
            std::array<Vec3f, 3> axes{};
        };
        std::vector<Turn> turns;
        std::vector<SpaceTurn> spaceTurns;
        std::vector<Reach> reaches;
        bool empty() const { return turns.empty() && spaceTurns.empty() && reaches.empty(); }
    };
    // One frame of vertices (FrameVerts, in packed units as Mesh::verts):
    // every bone placed as `base` has it `fraction` through, then the
    // bones of each layer's part blended towards where its animation has
    // them, then `adjust`. False if an animation isn't the mesh's.
    bool pose(std::string_view base, float fraction, const std::vector<Layer>& layers, std::vector<Vec3f>& out,
              const Adjust* adjust = nullptr) const;
    // A bone's place in the same pose: its origin and axes (rows X, Y, Z),
    // in packed units as the vertices. False if there's no such bone or an
    // animation isn't the mesh's.
    bool bone(std::string_view base, float fraction, const std::vector<Layer>& layers, std::string_view name,
              Vec3f& origin, std::array<Vec3f, 3>& axes, const Adjust* adjust = nullptr) const;
    // Every bone's place in the skeleton's space in the same pose (rows X,
    // Y, Z and the origin, each bone in order), and a bone's index (-1 if
    // there's none), for asking after many bones of one pose.
    bool skeleton(std::string_view base, float fraction, const std::vector<Layer>& layers,
                  std::vector<std::array<float, 12>>& worlds, const Adjust* adjust = nullptr) const;
    int boneIndex(std::string_view name) const;
    // The same in the skeleton's space (as Adjust).
    bool boneInSkeleton(std::string_view base, float fraction, const std::vector<Layer>& layers, std::string_view name,
                        Vec3f& origin, std::array<Vec3f, 3>& axes, const Adjust* adjust = nullptr) const;
    bool hasBone(std::string_view name) const;
    // Its place in its parent bone in the same pose (the bone's own units,
    // the skeleton's: as placed vertices' before the mesh's turn), and its
    // parent's name (empty for the root).
    bool boneInParent(std::string_view base, float fraction, const std::vector<Layer>& layers, std::string_view name,
                      Vec3f& origin, std::array<Vec3f, 3>& axes, const Adjust* adjust = nullptr) const;
    std::string parentOf(std::string_view name) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

bool isMeshClass(std::string_view className);

// For working out a mesh's layout (reluva-tool ue-probe): the arrays
// that look like an imported-model block's ((n, n, flags) headers), and
// every place the standard tail could start and hold together.
std::vector<std::string> describeMeshLayout(const Package& package, int exportIndex);

// For a mesh built on a skeleton: its bones as a tree, each with its
// vertices, placement and animation clips (key counts, times, and the
// first rotation keys' bytes, their encoding not yet worked out).
std::vector<std::string> describeBones(const Package& package, int exportIndex);

} // namespace reluva::unreal
