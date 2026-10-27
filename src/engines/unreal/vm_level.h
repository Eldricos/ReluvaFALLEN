// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level running its scripts: its actors made into VM objects (each with
// its class's defaults and its own saved properties), the game type
// spawned, the start-up events sent in order, and the world ticked. The
// engine side scripts call into (spawning, destroying, iterating over the
// actors, timers) is in vm_level.cpp, movement in vm_physics.cpp, The
// Fallen's splines in vm_splines.cpp. See docs/engines/unreal_vm.md.

#include "engines/unreal/actors.h"
#include "engines/unreal/collision.h"
#include "engines/unreal/mesh.h"
#include "engines/unreal/model.h"
#include "engines/unreal/vm.h"

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

// The level's actor list (the Level object's Actors): object references,
// 0 for empty slots.
Error readLevelActors(const Package& map, std::vector<int32_t>& out);

class ScriptLevel {
public:
    ScriptLevel(Vm& vm, const Package& map);
    ~ScriptLevel();

    // Makes the level's actors.
    Error load();
    // Spawns the game type (the LevelInfo's DefaultGameType, else
    // `defaultGame`, "Package.Class") and sends every actor the start-up
    // events: the game's InitGame, then each actor's PreBeginPlay and
    // BeginPlay, then PostBeginPlay, then SetInitialState.
    void beginPlay(const std::string& defaultGame);
    // A player joining, as the engine logs one in (after beginPlay): the
    // start the game type picks (its FindPlayerStart), the character that
    // start names (The Fallen's CTVE_PlayerStart.Player; else `playerClass`,
    // "Package.Class", the .ini's [URL] Class), spawned by the game type's
    // Login; a viewport and console given to it; then Possess,
    // TravelPostAccept and the game type's PostLogin. nullptr, with
    // `error`, if Login refuses.
    VmObject* login(const std::string& playerClass, const std::string& playerName, std::string& error);
    VmObject* player() const { return player_; }

    // Where a point is in the level: its zone and leaf, found down the
    // level's BSP (the zone and leaf on the point's side of the last node).
    // A PointRegion value (Zone, iLeaf, ZoneNumber).
    Value regionAt(const std::array<float, 3>& point);
    // Sets an actor's Region from its Location (as the engine does when
    // actors are spawned or move).
    void updateRegion(VmObject* actor);
    int editorCameras() const { return editorCameras_; } // UnrealEd's cameras, left out

    // The player's controls this tick, as the engine's input sets them
    // from the keys held (-1 to 1 each: back/forward, left/right, turning
    // left/right, looking down/up); at full, the .ini's aliases give 300
    // (150 turning).
    struct Controls {
        float forward = 0, strafe = 0, turn = 0, lookUp = 0;
        // The mouse's movement since the last tick (the input's aMouseX,
        // aMouseY), used up by the next.
        float mouseX = 0, mouseY = 0;
        // Buttons held: jump (Space: bExtra1, and aUp up) and fire (bFire).
        // Pressing one also calls its exec function (LevelPlay::press).
        bool jump = false, fire = false;
    } controls;

    // Moves the world on: the player's input (PlayerInput) and PlayerTick,
    // each actor's timer, Tick and state code, then its physics
    // (vm_physics.cpp).
    void tick(float seconds);

    // Movement against the level (vm_physics.cpp). An actor's collision is
    // a cylinder (CollisionRadius, CollisionHeight), a box of those half
    // sizes against the world.
    struct MoveHit {
        float time = 1;          // 0-1 of the move made
        std::array<float, 3> normal{};
        VmObject* actor = nullptr; // what stopped it: an actor, or the LevelInfo for the world
        bool hit() const { return time < 1; }
    };
    // Moves an actor by `delta` until something blocks it (the world if it
    // collides with it, actors and movers that block it), then updates its
    // region (ZoneChange) and what it touches (Touch, UnTouch).
    MoveHit moveActor(VmObject* actor, const std::array<float, 3>& delta);
    // As moveActor, stepping up ledges and sliding along walls.
    MoveHit moveSmooth(VmObject* actor, const std::array<float, 3>& delta);
    // Runs an actor's physics (its Physics) for a time.
    void physics(VmObject* actor, float seconds);
    // A line or box traced through the level (and, if asked, its actors):
    // what it hit (the LevelInfo for the world), where and facing which way.
    VmObject* trace(VmObject* self, const std::array<float, 3>& start, const std::array<float, 3>& end,
                    const std::array<float, 3>& extent, bool actors, std::array<float, 3>& hitLocation,
                    std::array<float, 3>& hitNormal);
    // Whether a box at a point is clear of the world and the movers that
    // block (but `self`).
    bool clear(const std::array<float, 3>& point, const std::array<float, 3>& extent, VmObject* self = nullptr);
    // Puts an actor somewhere it fits, near `point` if it doesn't there;
    // false if there's nowhere near.
    bool place(VmObject* actor, const std::array<float, 3>& point);
    // A pawn moving or turning towards something until there (latent
    // MoveTo, MoveToward, TurnTo, TurnToward, WaitForLanding,
    // FinishRotation): what it's after.
    struct Goal {
        enum class Kind { Move, Turn, Land, Rotation } kind = Kind::Move;
        std::array<float, 3> point{};
        VmObject* target = nullptr;
        float speed = 1, timeLeft = 10;
        int stateSerial = 0;
    };
    void setGoal(VmObject* pawn, const Goal& goal);

    // Animation (vm_anim.cpp): PlayAnim and LoopAnim's start (`rate` times
    // the sequence's own speed, after a tween of `tween` seconds),
    // TweenAnim's, whether one's playing or tweening, and a latent
    // FinishAnim's wait for its end.
    void startAnim(VmObject* actor, NameId sequence, float rate, float tween, bool loop);
    void tweenAnim(VmObject* actor, NameId sequence, float seconds);
    bool animating(VmObject* actor);
    void waitForAnim(VmObject* actor);

    // The Fallen's layered animation (vm_anim.cpp, C3DPlayAnim and kin):
    // an animation on part of a skeleton (a group, 'UpperTorso': a bone
    // and those under it; 'LowerTorso': the rest), over the actor's own,
    // fading in and out by its weight; and its face's channels (a frame of
    // Blink, EyeBrows or Phonemes each), set by C3DTweenSkinAnimDelay.
    struct AnimLayer {
        NameId sequence = 0;
        NameId bone = 0;      // where the part starts
        bool rest = false;    // the part is the skeleton but that (LowerTorso)
        float frame = 0, rate = 0; // as AnimFrame (below 0: tweening into it) and AnimRate
        bool loop = false, finished = false;
        float weight = 0, weightRate = 0; // how much it shows (0-1), and its change a second
    };
    struct SkinChannel {
        NameId sequence = 0;
        float frame = 0, weight = 0;       // where it is: seconds into the sequence
        float target = 0, targetWeight = 0, tweenRate = 0; // where it's tweening to, and how fast
    };
    const std::map<NameId, AnimLayer>* layersOf(VmObject* actor) const;
    const std::map<NameId, SkinChannel>* skinsOf(VmObject* actor) const;
    void playLayer(VmObject* actor, NameId group, NameId sequence, float rate, float tween, bool loop, NameId bone);
    void tweenLayer(VmObject* actor, NameId group, NameId sequence, float seconds, NameId bone);
    void tweenOutLayer(VmObject* actor, NameId group, float seconds);
    bool layerAnimating(VmObject* actor, NameId group);
    void waitForLayer(VmObject* actor, NameId group);
    NameId newSkinChannel(VmObject* actor);
    void setSkin(VmObject* actor, NameId tag, NameId sequence, float frame, float weight, float tween);
    void clearSkin(VmObject* actor, NameId tag);
    void copySkins(VmObject* to, VmObject* from);

    // Parts of an actor's mesh where they are now (vm_attach.cpp), in its
    // animation at this tick: one of its bones (C3DGetJointWorldPos), one
    // of its attachment points (C3DGetAttachPtCoords) or places worked out
    // in advance (C3DGetAttachPtCoordsPrecomputed); in the world, or (not
    // `world`) from the actor's own location, unturned. False if its mesh
    // has no such part.
    using Vec = std::array<float, 3>;
    using Rot = std::array<int32_t, 3>;
    bool boneAt(VmObject* actor, NameId bone, bool world, Vec& location, Rot& rotation);
    bool attachPointAt(VmObject* actor, NameId point, bool precomputed, bool world, Vec& location, Rot& rotation);
    bool hasBone(VmObject* actor, NameId bone);
    int attachPointCount(VmObject* actor);
    // C3DAttach: `child` kept, from each tick on, with its point `childPoint`
    // on `parent`'s `parentPoint` (a weapon's RightHand in the player's
    // hand); C3DDetach lets it go.
    void attach(VmObject* child, VmObject* parent, NameId parentPoint, NameId childPoint);
    void detach(VmObject* child, VmObject* parent);
    // Movement so far, for diagnostics.
    struct MovementStats {
        int moves = 0, blockedByWorld = 0, blockedByActors = 0, steps = 0, landings = 0, touches = 0,
            zoneChanges = 0;
        int refused = 0; // placed (SetLocation, spawned) where there's no room, nor near
        int gaveUp = 0;  // latent moves (MoveTo...) that timed out short of their goal
    } movement;

    Vm& vm() { return vm_; }
    const Package& map() const { return map_; }
    VmObject* levelInfo() const { return levelInfo_; }
    VmObject* game() const { return game_; }
    const std::vector<VmObject*>& actors() const { return actors_; }
    float time() const { return time_; }

    // An actor of a class, as Actor.Spawn makes one: its owner, tag,
    // place; the start-up events sent. nullptr if the class isn't an
    // actor's.
    VmObject* spawn(VmClass* cls, VmObject* owner, NameId tag, const Value* location, const Value* rotation);
    void destroy(VmObject* actor);

private:
    void init(VmObject* actor); // the engine's own properties of a new actor
    Value* prop(VmObject* a, const char* name);
    void updateTouching(VmObject* a);
    void steer(VmObject* pawn, float seconds);
    void rotate(VmObject* a, float seconds);
    void moveBrush(VmObject* mover, float seconds);
    void animate(VmObject* actor, float seconds); // its animation on by a tick
    void endAnimWait(VmObject* actor, bool ended);
    void animateLayers(VmObject* actor, float seconds);
    // A pawn with one of The Fallen's meshes gets UpdateHeadAnimation each
    // tick: where its head looks, and its face (blinks, expressions).
    void updateHead(VmObject* actor);
    std::map<const VmClass*, bool> pawnClasses_;
    std::map<const VmObject*, bool> c3dMeshes_; // mesh objects in The Fallen's layout
    void endLayerWait(VmObject* actor, NameId group);
    std::array<float, 3> extentOf(VmObject* a);
    struct BrushBox {
        std::array<float, 3> min{}, max{}; // in the brush's own coordinates
        std::array<float, 3> prePivot{}, mainScale{1, 1, 1}, postScale{1, 1, 1};
        std::shared_ptr<Model> model; // its own BSP, if it has one
        std::shared_ptr<LevelCollision> collision;
    };
    bool brushBox(VmObject* mover, std::array<float, 3>& min, std::array<float, 3>& max);
    // A box swept by `delta` against a mover where it is now: its brush's
    // BSP if it has one, else its box.
    struct MoverSpace;
    bool moverSpace(VmObject* mover, MoverSpace& space);
    bool moverOverlaps(VmObject* mover, const std::array<float, 3>& centre, const std::array<float, 3>& extent);
    bool moverHit(VmObject* mover, const std::array<float, 3>& start, const std::array<float, 3>& delta,
                  const std::array<float, 3>& extent, float& time, std::array<float, 3>& normal);

    Vm& vm_;
    const Package& map_;
    VmObject* levelInfo_ = nullptr;
    VmObject* game_ = nullptr;
    VmObject* levelObject_ = nullptr;
    VmObject* player_ = nullptr;
    Model model_;
    bool hasModel_ = false;
    std::unique_ptr<LevelCollision> collision_;
    std::map<int, BrushBox> brushes_; // movers' boxes, by export index
    // Property slots by class and name (the address of the name's text:
    // prop() is only given string literals).
    struct SlotKey {
        const VmClass* cls;
        const char* name;
        bool operator==(const SlotKey&) const = default;
    };
    struct SlotHash {
        size_t operator()(const SlotKey& k) const {
            return std::hash<const void*>()(k.cls) * 31 + std::hash<const void*>()(k.name);
        }
    };
    std::unordered_map<SlotKey, int, SlotHash> slots_;
    std::map<VmObject*, std::vector<VmObject*>> touching_;
    std::map<VmObject*, Goal> goals_;
    std::set<VmObject*> fallers_; // falling since a tick before
    std::map<VmObject*, int> animWaits_; // actors in a latent FinishAnim, by their state serial then
    std::map<VmObject*, std::map<NameId, AnimLayer>> layers_;
    std::map<VmObject*, std::map<NameId, SkinChannel>> skins_;
    std::map<VmObject*, std::pair<NameId, int>> layerWaits_; // in C3DFinishAnim: the group, the state serial
    struct ActorMesh;
    ActorMesh* actorMesh(VmObject* actor);
    const std::vector<Vec>* posedVertices(VmObject* actor, ActorMesh& mesh); // packed, as this tick has them
    bool attachFrame(VmObject* actor, ActorMesh& mesh, const MeshAttachPoint& point, Vec& at, std::array<Vec, 3>& axes);
    void toWorld(VmObject* actor, Vec& at, std::array<Vec, 3>* axes);
    void placeAttached();
    std::map<const VmObject*, std::shared_ptr<ActorMesh>> actorMeshes_; // by mesh object
    std::shared_ptr<std::vector<AnimationSet>> animationSets_;
    std::map<VmObject*, std::pair<int, std::vector<Vec>>> posed_; // each actor's vertices, and which tick's
    struct Attachment {
        VmObject* parent = nullptr;
        NameId parentPoint = 0, childPoint = 0;
    };
    std::map<VmObject*, Attachment> attachments_;
    int ticks_ = 0;
    int skinChannels_ = 0;
    std::vector<VmObject*> movers_; // the level's, which traces of the world meet
    int editorCameras_ = 0;
    std::vector<VmObject*> actors_;
    float time_ = 0;
};

// A setting from a game's .ini ("Engine.Engine", "DefaultGame"); empty if
// it isn't there.
std::string iniSetting(const PackageSet& packages, std::string_view section, std::string_view key);
// The game type a game's levels run with: its .ini's [Engine.Engine]
// DefaultGame ("DS9.DS9_GameInfo"), else Engine's GameInfo.
std::string defaultGameType(const PackageSet& packages);

// A level's start, run for what it changes: its scripts' start-up events,
// a player logging in, and `seconds` of ticks, then each placed actor compared with how it was
// saved (destroyed, hidden, its draw type, its light's type and
// brightness). For drawing a level as it looks when play begins.
struct LevelStart {
    ScriptedActors changed;
    int removed = 0, hidden = 0, shown = 0, drawTypes = 0, lights = 0; // actors changed each way
    double milliseconds = 0;
    size_t errors = 0, missingNatives = 0;
    int accessedNone = 0;
    std::string player;     // the player's class, once logged in
    std::string loginError; // why the player couldn't log in, if not
};
Error runLevelStart(const PackageSet& packages, const Package& map, const std::string& defaultGame, float seconds,
                    LevelStart& out);

// The natives of Engine's Actor and its subclasses that a level needs
// (registered once per VM; they work on vm.level).
void registerEngineNatives(Vm& vm);
// The movement natives (Move, MoveSmooth, SetLocation, Trace, PointCheck,
// SetPhysics, Pawn's MoveTo and kin): vm_physics.cpp.
void registerMovementNatives(Vm& vm);
// The Fallen's splines (vm_splines.cpp).
void registerSplineNatives(Vm& vm);
// The animation natives (vm_anim.cpp).
void registerAnimationNatives(Vm& vm);
// Bones, attachment points and attaching (vm_attach.cpp).
void registerAttachNatives(Vm& vm);

} // namespace reluva::unreal
