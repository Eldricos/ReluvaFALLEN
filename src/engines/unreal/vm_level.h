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
#include "engines/unreal/dialog_table.h"
#include "engines/unreal/game_save.h"
#include "engines/unreal/font.h"
#include "engines/unreal/mesh.h"
#include "engines/unreal/model.h"
#include "engines/unreal/vm.h"

#include <array>
#include <filesystem>
#include <map>
#include <random>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

// The level's actor list (the Level object's Actors): object references,
// 0 for empty slots.
Error readLevelActors(const Package& map, std::vector<int32_t>& out);

class ByteWriter;
class PackageWriter;

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
    // `portal`: the tag of the start to begin at (a travel URL's
    // "Map/Portal"), else the first.
    VmObject* login(const std::string& playerClass, const std::string& playerName, std::string& error,
                    const std::string& portal = {});
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
        // Buttons held: jump (Space: bExtra1, and aUp up), duck (C: bDuck,
        // and aUp down) and fire (bFire). Pressing one also calls its exec
        // function (LevelPlay::press).
        bool jump = false, duck = false, fire = false;
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
    // Whether the level's BSP is in the way from `start` to `end`, short
    // of the last `slack` units (movers aren't counted); not if `end` is
    // in solid space.
    bool worldBetween(const std::array<float, 3>& start, const std::array<float, 3>& end, float slack = 0);
    // Whether a point is in the level's solid space.
    bool inSolid(const std::array<float, 3>& point);
    VmObject* trace(VmObject* self, const std::array<float, 3>& start, const std::array<float, 3>& end,
                    const std::array<float, 3>& extent, bool actors, std::array<float, 3>& hitLocation,
                    std::array<float, 3>& hitNormal);
    // Whether a box at a point is clear of the world and the movers that
    // block (but `self`).
    bool clear(const std::array<float, 3>& point, const std::array<float, 3>& extent, VmObject* self = nullptr);
    // A new collision size (ResizeActor), its feet kept where they were
    // (alignToFloor) or its top (alignToTop); where it no longer fits,
    // moved to a place nearby that it does (adjustToFit). False, and left as
    // it was, if it can't be.
    bool resize(VmObject* actor, float radius, float height, bool alignToFloor, bool adjustToFit, bool alignToTop);
    // A pawn's way round what blocks it on its way to its Destination
    // (PickWallAdjust): a step aside, left or right, from which it can go
    // on; its Destination moved there. False if there's none.
    bool pickWallAdjust(VmObject* pawn);
    // Puts an actor somewhere it fits, near `point` if it doesn't there
    // (as the engine's FindSpot finds it, if it collides with the world;
    // or with bCollideWhenPlacing too when `spawning`); false, and left
    // where it was, if there's nowhere near.
    bool place(VmObject* actor, const std::array<float, 3>& point, bool spawning = false);
    // FindSpot: `at` moved to where a box of `extent` fits near it, as
    // Engine.dll's ULevel::FindSpot and AdjustSpot do; false if nowhere.
    bool findSpot(const std::array<float, 3>& extent, std::array<float, 3>& at);
    // A pawn moving or turning towards something until there (latent
    // MoveTo, MoveToward, TurnTo, TurnToward, WaitForLanding,
    // FinishRotation): what it's after.
    struct Goal {
        enum class Kind { Move, Turn, Land, Rotation } kind = Kind::Move;
        std::array<float, 3> point{};
        VmObject* target = nullptr;
        // While moving, facing this place or actor rather than the way it
        // goes (StrafeTo, StrafeFacing).
        bool facing = false;
        std::array<float, 3> focus{};
        VmObject* focusActor = nullptr;
        float speed = 1, timeLeft = 10;
        int stateSerial = 0;
    };
    void setGoal(VmObject* pawn, const Goal& goal);

    // The AI's paths (vm_paths.cpp): the level's navigation points, linked
    // by the reach list stored in its Level object (each reach from one
    // point to another: its length, the biggest pawn that fits along it,
    // how it's got along: walking, flying, swimming, jumping...).
    struct ReachSpec {
        int distance = 0;
        VmObject* start = nullptr;
        VmObject* end = nullptr;
        int radius = 0, height = 0, flags = 0;
        bool pruned = false;
    };
    const std::vector<VmObject*>& navigationPoints();
    const std::vector<ReachSpec>& reachSpecs() const { return reachSpecs_; }
    // The next point on a pawn's way to an actor (or a place), its route in
    // its RouteCache: the goal itself if it can go straight there, none if
    // there's no way.
    VmObject* findPath(VmObject* pawn, VmObject* goal, const std::array<float, 3>* point, bool singlePath = false,
                       bool clear = true);
    // FindPathTo/Toward as the natives: then the bShootSpecial, SpecialPause
    // and SpecialGoal bookkeeping and a node's SpecialHandling.
    VmObject* findPathNative(VmObject* pawn, VmObject* goal, const std::array<float, 3>* point, bool singlePath,
                             bool clear);
    VmObject* handleSpecial(VmObject* pawn, VmObject* result);
    void clearPaths(VmObject* pawn);
    bool pointReachable(VmObject* pawn, std::array<float, 3> p, bool knowVisible);
    bool actorReachable(VmObject* pawn, VmObject* a);
    bool actorReachableRaw(VmObject* pawn, VmObject* a, bool knowVisible);
    const std::vector<std::pair<float, VmObject*>>& closestNodes(std::array<float, 3> at, float radius);
    std::array<float, 3> eyeOf(VmObject* pawn);
    float pf(VmObject* a, const char* name, float otherwise = 0);
    bool pb(VmObject* a, const char* name);
    std::array<float, 3> pv(VmObject* a, const char* name);
    bool coinFlip() { return std::uniform_int_distribution<int>(0, 1)(barkRandom_) == 0; }
    // Nothing in the world between the two places.
    bool lineOfSight(VmObject* self, const std::array<float, 3>& from, const std::array<float, 3>& to);
    // The Fallen's dialogue (vm_dialog.cpp): lines from the game's
    // dialogue tables, picked for this mission and player
    // (FindBestDialogLine) and played (StartDialog), each said for as long
    // as its voice lasts, then the line it leads to.
    const std::vector<DialogLine>& dialogLines();
    int findDialogLine(const std::string& type, NameId filterTag, NameId listener, NameId speaker,
                       const std::string& lineId, int& index, VmObject* caller = nullptr);
    // CTVE_DialogInput.DialogTick: the AI's own lines, each tick (`input`
    // the dialogue input, whose PawnSurrenderOrPray it asks).
    void dialogTick(VmObject* input, float seconds);
    int startDialog(VmObject* self, const std::string& type, NameId filterTag, NameId listener, NameId speaker,
                    const std::string& lineId); // the thread's number, 0 if no line would do
    bool dialogPlaying(int thread) const;
    bool dialogLineSpecial(int index);
    // The lines begun so far, in order (their indexes; the last 256).
    const std::vector<int>& linesSaid() const { return dialogSaid_; }
    const std::vector<float>& linesSaidAt() const { return dialogSaidAt_; } // the level's seconds
    bool testCondition(const std::string& condition);
    // A condition about `actor`: 1 met, 0 not, -1 not one the engine knows.
    int evalCondition(const std::string& condition, VmObject* actor);
    // The lines being said now: the speaker's code, the text, the type.
    struct Subtitle {
        std::string speaker, text, type;
    };
    std::vector<Subtitle> subtitles() const;
    // The HUD (vm_canvas.cpp): each frame, the canvas handed to the
    // player's PreRender and PostRender, and what its natives drew: tiles
    // (a texture's rectangle, in texels, drawn at a rectangle of the
    // screen, in pixels; letters are tiles from their font's pages) and
    // actors drawn over the world (a held weapon, a talker's picture).
    // A view: a camera and the rectangle of the screen it's drawn in (the
    // whole screen, or one SetRenderFrame sets: a talker's portrait).
    struct HudView {
        std::array<float, 3> location{};
        std::array<int32_t, 3> rotation{};
        float fov = 90;
        int width = 640, height = 480;
        int x = 0, y = 0; // the rectangle's top left
    };
    struct HudTile {
        ObjectRef texture;
        float x = 0, y = 0, w = 0, h = 0, u = 0, v = 0, ul = 0, vl = 0;
        std::array<uint8_t, 3> color{255, 255, 255};
        uint8_t style = 1;       // Actor's ERenderStyle: 1 normal, 2 masked, 3 translucent, 4 modulated
        bool seeThrough = false; // its texture's colour 0 see-through (letters, masked tiles)
        int frame = 0;           // an FliTexture's frame showing (from 1); 0 for any other texture
    };
    struct HudActor {
        VmObject* actor = nullptr;
        bool clearZ = false;   // the depth cleared before it
        HudView view;          // the camera and rectangle it's drawn in
        size_t afterTiles = 0; // drawn after this many tiles, over them
        // How it looked as it was asked for (the HUD sets these just for
        // the call: a portrait's fade): draw type, style, glow.
        int drawType = 0, style = 1;
        float scaleGlow = 1;
    };
    struct Hud {
        std::vector<HudTile> tiles;
        std::vector<HudActor> actors;
    };
    void renderHud(const HudView& view);
    // The view DrawActor draws in now, set by SetRenderFrame; whether its
    // depth is still to be cleared (ClearZ) before the next actor.
    HudView& hudFrame() { return hudFrame_; }
    bool& hudFrameClear() { return hudFrameClear_; }
    // The FliTextures the HUD draws, played on each tick as their own
    // properties say (Mode, Rate, FrameNum, FrameTime).
    void trackFli(VmObject* texture);
    void tickFlis(float seconds);
    Hud& hud() { return hud_; }
    const HudView& hudView() const { return hudView_; }
    const Font* fontOf(VmObject* font);
    VmObject* canvas();

    // The game's text files (vm_documents.cpp): the int folder, and a
    // section of one of its .int files, its keys and values in order
    // (none if there's no such file or section).
    std::filesystem::path intFolder() const;
    const std::vector<std::pair<std::string, std::string>>* intSection(const std::string& file,
                                                                       const std::string& section);
    struct DialogStats {
        int started = 0, lines = 0, notFound = 0, unknownConditions = 0, barks = 0;
    } dialog;
    struct PathStats {
        int straight = 0, found = 0, failed = 0;
        int noStart = 0, noEnd = 0, noRoute = 0; // why it failed: no point near the pawn, near the goal, no way between
    } paths;

    // Animation (vm_anim.cpp): PlayAnim and LoopAnim's start (`rate` times
    // the sequence's own speed, after a tween of `tween` seconds),
    // TweenAnim's, whether one's playing or tweening, and a latent
    // FinishAnim's wait for its end.
    // A negative rate is scaled by the actor's speed, never below
    // `minRate` (LoopAnim's MinRate, as `rate` is: times its own speed).
    void startAnim(VmObject* actor, NameId sequence, float rate, float tween, bool loop, float minRate = 0);
    void tweenAnim(VmObject* actor, NameId sequence, float seconds);
    bool animating(VmObject* actor);
    void waitForAnim(VmObject* actor);
    // While an actor tweens into an animation (AnimFrame below 0): the
    // animation it was showing when the tween began, and where in it.
    struct TweenFrom {
        NameId sequence = 0;
        float frame = 0;
        bool loop = false;
    };
    const TweenFrom* tweenFromOf(VmObject* actor) const;

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
        float minRate = 0;                // with a negative rate (scaled by speed), the least it goes at
        std::array<bool, 3> clear{};      // its first bone keeps the actor's own turn in yaw, pitch, roll
        // The layer it replaced, held where it was under this one while
        // this one fades in over it.
        NameId fromSequence = 0;
        float fromFrame = 0, fromWeight = 0;
    };
    // A face channel, as the engine keeps it (an AnimGroupState after the
    // mesh's own groups): its sequence, frame (seconds into it) and
    // weight; while tweening (C3DTweenSkinAnim), the frame and weight it
    // started from and how far through it is; and a tween asked for to
    // start at its next tick (C3DTweenSkinAnimDelay).
    struct SkinChannel {
        NameId sequence = 0;
        float frame = 0, weight = 1;
        float fromFrame = 0, fromWeight = 0;
        float elapsed = 0, duration = 1;
        bool tweening = false;
        bool pending = false;
        NameId pendingSequence = 0;
        float pendingFrame = 0, pendingWeight = 0, pendingTween = 0;
    };
    using SkinChannels = std::vector<std::pair<NameId, SkinChannel>>; // in the order made
    const std::map<NameId, AnimLayer>* layersOf(VmObject* actor) const;
    const SkinChannels* skinsOf(VmObject* actor) const;
    void playLayer(VmObject* actor, NameId group, NameId sequence, float rate, float tween, bool loop, NameId bone,
                   float minRate = 0, std::array<bool, 3> clear = {});
    void tweenLayer(VmObject* actor, NameId group, NameId sequence, float seconds, NameId bone,
                    std::array<bool, 3> clear = {});
    // An animation's rate now: a negative one is scaled by the actor's
    // speed, at least `minRate`.
    float rateNow(VmObject* actor, float rate, float minRate);
    void tweenOutLayer(VmObject* actor, NameId group, float seconds);
    bool layerAnimating(VmObject* actor, NameId group);
    void stopLayerLooping(VmObject* actor, NameId group);
    void waitForLayer(VmObject* actor, NameId group);
    NameId newSkinChannel(VmObject* actor);
    // How: 0 at once (C3DSetSkinAnim), 1 tweened (C3DTweenSkinAnim), 2 tweened from its next tick (...Delay).
    void setSkin(VmObject* actor, NameId tag, NameId sequence, float frame, float weight, float tween, int how);
    void clearSkin(VmObject* actor, NameId tag);
    void copySkins(VmObject* to, VmObject* from);

    // Parts of an actor's mesh where they are now (vm_attach.cpp), in its
    // animation at this tick: one of its bones (C3DGetJointWorldPos), one
    // of its attachment points (C3DGetAttachPtCoords) or places worked out
    // in advance (C3DGetAttachPtCoordsPrecomputed); in the world, or (not
    // `world`) from the actor's own location, unturned. Its layered
    // animations count. False if its mesh has no such part.
    using Vec = std::array<float, 3>;
    using Rot = std::array<int32_t, 3>;
    // A bone: in the world, from the actor (unturned), from its parent
    // bone in the world's axes, or its place in its parent bone.
    enum class BonePlace { World, Local, FromParent, InParent };
    bool boneAt(VmObject* actor, NameId bone, BonePlace place, Vec& location, Rot& rotation);
    bool attachPointAt(VmObject* actor, NameId point, bool precomputed, bool world, Vec& location, Rot& rotation);
    bool hasBone(VmObject* actor, NameId bone);
    int attachPointCount(VmObject* actor);
    // C3DAttach: `child` kept, from each tick on, with its point `childPoint`
    // on `parent`'s `parentPoint` (a weapon's RightHand in the player's
    // hand); C3DDetach lets it go.
    void attach(VmObject* child, VmObject* parent, NameId parentPoint, NameId childPoint);
    void detach(VmObject* child, VmObject* parent);
    // What the scripts do to an actor's bones on top of its animations,
    // kept until changed: C3DApplyRotation turns a bone about one of its
    // own axes (degrees), C3DApplyWorldRotation by a rotator in the
    // actor's axes. An IK chain (C3DSetIkTarget...) bends a limb so its
    // hand or foot reaches a place in the world, or turns it to axes
    // there, blended in by a weight, until C3DClearIkTarget. False if
    // there's no such chain.
    void turnBone(VmObject* actor, NameId bone, int axis, float degrees);
    void turnBoneInActor(VmObject* actor, NameId bone, const Rot& rotation);
    bool setIk(VmObject* actor, NameId chain, const Vec* at, const std::array<Vec, 3>* axes, float weight);
    void clearIk(VmObject* actor, NameId chain);
    // C3DRemoveRotation: the bone's turns (C3DApplyRotation's and
    // C3DApplyWorldRotation's) taken off about those axes.
    void removeBoneTurn(VmObject* actor, NameId bone, bool yaw, bool pitch, bool roll);
    // C3DApplyIKOnBasePose: whether a chain's IK reaches from the actor's
    // own animation, the layers then over it (else after them).
    void ikOnBase(VmObject* actor, NameId chain, bool onBase);
    // All of that as the poser takes it, with the actor where it is now
    // (empty if nothing's done to its bones).
    LayeredPoser::Adjust boneAdjust(VmObject* actor);
    // Movement so far, for diagnostics.
    struct MovementStats {
        int moves = 0, blockedByWorld = 0, blockedByActors = 0, steps = 0, landings = 0, touches = 0,
            zoneChanges = 0;
        int refused = 0;       // placed (SetLocation, spawned) where there's no room, nor near
        int spawnsRefused = 0; // of those, spawns, so not made (as SpawnActor fails)
        int gaveUp = 0;  // latent moves (MoveTo...) that timed out short of their goal
    } movement;

    Vm& vm() { return vm_; }
    const Package& map() const { return map_; }
    VmObject* levelInfo() const { return levelInfo_; }
    // Paused: Level.Pauser set (SetPause: the menus). Time stands still;
    // only the player's PlayerTick and actors with bAlwaysTick (the HUD)
    // run.
    bool paused() const;
    // The console's side (vm_console.cpp): the keys held down (EInputKey
    // codes, for IsKeyDown), where the game asked to travel (ClientTravel:
    // "CM01a_SW/CM01_A_Sisko01", a map and the start to log in at), and
    // whether it asked to quit (ConsoleCommand "Exit").
    // The player's console (its viewport's), if it has one.
    VmObject* console();

    // Sounds (vm_sound.cpp). What the scripts have asked to play since the
    // page last took them (PlaySound; a dialogue line's voice), and the
    // music.
    struct SoundRequest {
        VmObject* actor = nullptr; // heard from where it is; none: everywhere (a voice)
        ObjectRef sound;
        int slot = 0;              // ESoundSlot: 0 none, 5 talk...; one sound a slot an actor
        float volume = 1, radius = 0, pitch = 1;
        bool noOverride = false;   // nothing may play over it in its slot
        bool speech = false;       // a voice (its name has DIALOG): at the speech volume
        bool loop = false;         // its name has LOOPING
        // The actor's sound filter when played (LowPass, HighPass: a
        // filtered sound is mixed flat, and walls in the way quieten it by
        // its ObstructionFactor).
        float lowPass = 0, highPass = 0, obstruction = 0;
        float startSeconds = 0; // where in it to start (a saved game's sounds)
    };
    std::vector<SoundRequest>& soundRequests() { return soundRequests_; }
    // A sound played by an actor: its lip-sync (vm_sound.cpp), if it has any.
    void lipSyncPlay(VmObject* actor, int slot, bool noOverride, const Package* package, int exportIndex);
    // What's playing as the audio driver's tracker keeps it (a saved game
    // holds it): each sound an actor played, until it's done or replaced.
    void trackSound(const SoundRequest& r, float seconds);
    void trackMusic(VmObject* actor, float startSeconds);
    // Writes the level as the original saves it (a package: game_save_write.cpp).
    Error writeGameSave(const std::string& path);
    bool reluvaFormat_ = false; // save in Reluva's own format instead
    // Sounds stopped (an actor's slot: a line cut off), for the page.
    std::vector<std::pair<VmObject*, int>>& soundStops() { return soundStops_; }
    // The music is the streamed sound the player's CurrentMusic names. One
    // the scripts play (a music trigger's PlaySound) sets it, with its
    // volume and where in it to start (the HUD's StartTimeOffset); each such
    // play counts, so the same track can start again.
    struct Music {
        float volume = 1, startSeconds = 0;
        int serial = 0;
    };
    Music& music() { return music_; }
    VmObject* currentMusic();
    void musicEnded(VmObject* sound); // CurrentMusic back to None, if it's still that
    // Whether a Sound is one of The Fallen's streamed (MP3) ones: music.
    bool streamedSound(VmObject* sound);
    // The console's sound commands: every sound stopped (KillSound), and
    // the master volume for now (SetMasterVolume; below 0: the setting's).
    bool& killSounds() { return killSounds_; }
    float& masterVolume() { return masterVolume_; }

    // Saved games (vm_save.cpp), in the folder the page gives: Reluva's
    // own files, under the game's names (QuickSave.dsa, Save<n>.dss).
    struct SaveInfo {
        std::filesystem::path file;
        std::string map, description; // the map's name; "QuickSave" or the level's title
        int64_t time = 0;              // when, in seconds since 1970
        bool byGame = false;           // the original's (a package: the level itself)
    };
    void setSaveFolder(std::filesystem::path folder) { saveFolder_ = std::move(folder); }
    static Error readSaveInfo(const std::filesystem::path& file, SaveInfo& out);
    std::vector<SaveInfo> savedGames() const;
    Error saveGame(const std::string& name);
    // The level as a saved game of this map has it, in place of
    // beginPlay() and login().
    Error restore(const std::filesystem::path& file);
    // A saved game of the original's: the level loaded is the save's own
    // package (bBegunPlay set), so instead of beginPlay and login, this.
    bool savedByGame() const;
    VmObject* resumeSavedGame(const std::string& playerName, std::string& error);
    std::set<int>& keysDown() { return keysDown_; }
    std::string& travel() { return travel_; }
    bool& quitAsked() { return quitAsked_; }
    // Skipping a cut scene (FastForward): the level run at speed until the
    // game turns it off, when the letterbox goes.
    bool& fastForward() { return fastForward_; }
    VmObject* game() const { return game_; }
    const std::vector<VmObject*>& actors() const { return actors_; }
    float time() const { return time_; }

    // An actor of a class, as Actor.Spawn makes one: its owner, tag,
    // place; the start-up events sent. nullptr if the class isn't an
    // actor's.
    VmObject* spawn(VmClass* cls, VmObject* owner, NameId tag, const Value* location, const Value* rotation);
    // The texture of the level's surface a line from start to end meets
    // first; null if it meets none (GetSurfaceTexture: what's underfoot).
    VmObject* surfaceTexture(const std::array<float, 3>& start, const std::array<float, 3>& end);
    // That surface's index in the level's model; -1 if none.
    int surfaceAt(const std::array<float, 3>& start, const std::array<float, 3>& end);
    // Textures the scripts have swapped (SetSurfaceTexture): one surface's,
    // or every surface with a texture (bReplaceAllWorld), by its old one.
    // `surfaceSerial` counts the changes.
    struct SurfaceSwaps {
        std::map<int, ObjectRef> surfaces;
        std::map<std::pair<const Package*, int>, ObjectRef> world;
        int serial = 0;
    };
    const SurfaceSwaps& surfaceSwaps() const { return surfaceSwaps_; }
    void swapSurfaceTexture(int surf, VmObject* texture, bool wholeWorld);
    void destroy(VmObject* actor);
    // Ghosts (MakeGhost): actors taken out of the level for now, not
    // ticked, drawn, collided with or found by AllActors, until made
    // actors again (GhostActors brings them back as it finds them).
    void makeGhost(VmObject* actor, bool ghost);
    const std::vector<VmObject*>& ghosts() const { return ghosts_; }
    int placedGhosts() const { return placedGhosts_; } // set aside at the start, placed as ghosts
    int localizedSet() const { return localizedSet_; } // actors' strings from the map's .int
    // The camera the line being said asks for (its table's CAMERA column:
    // "CAM(A:x,y,z,pitch,yaw,roll F:fov D:dist)" in the world, R: the same
    // from the speaker); false if it asks for none of those.
    bool dialogueCamera(std::array<float, 3>& at, std::array<int32_t, 3>& rotation, float& fov, float& dist);
    // The Fallen's effects (vm_effects.cpp). An actor's copies (its
    // MultiMesh list), in use, in order; each tick they move and age.
    std::vector<VmObject*> multiMeshes(VmObject* actor);
    void moveMultiMeshes(VmObject* actor, float seconds); // as its physics mode has them (vm_effect_physics.cpp)
    // The Fallen's physics modes that move the actor itself (trailing its
    // owner, drifting, free flight, fish, interpolation...).
    void fallenPhysics(VmObject* a, int physics, float dt);
    void interpolate(VmObject* a, float dt);
    void chainSparkTick(VmObject* spark, float seconds);
    void updateBeamSections(VmObject* beam, float seconds);
    void sparkTick(VmObject* sparks, float seconds);
    void weatherTick(VmObject* weather, float seconds, bool fill);
    // Where the water's surface is on the way from `start` to `end` (the
    // first point where it goes in or out of a water zone); `start` if it
    // doesn't cross one.
    std::array<float, 3> waterLine(const std::array<float, 3>& start, const std::array<float, 3>& end);
    // Decals stuck to the world: where, facing out which way, their top
    // which way, their texture and its scale.
    struct Decal {
        std::array<float, 3> at{}, normal{}, up{};
        ObjectRef texture;
        float scale = 1;
    };
    VmObject* attachDecal(VmObject* decal, float distance, const std::array<float, 3>& direction);
    void detachDecal(VmObject* decal) { decals_.erase(decal); }
    const std::map<VmObject*, Decal>& decals() const { return decals_; }
    // The Fallen's player climbing (vm_climbing.cpp): what's ahead (a
    // wall, a ladder on it, a ledge on top), checked as it walks or falls,
    // and the hanging, climbing and clambering physics.
    struct Ledge {
        bool ledge = false, ladder = false;
        std::array<float, 3> wall{}, normal{}; // where the wall was met, and its normal (flat)
        float height = 0, depth = 0, headRoom = 0;
    };
    bool findLedge(VmObject* a, const std::array<float, 3>& dir, float highest, Ledge& out);
    void tellLedge(VmObject* a, const Ledge& l);
    void climbCheck(VmObject* a, bool falling, float dt);
    void climbPhysics(VmObject* a, int physics, float dt);
    void endClamber(VmObject* a);
    // Moved there at once, the camera's smoothing told (LocationOffset).
    void setLocation(VmObject* a, const std::array<float, 3>& to);
    struct Clamber {
        std::array<float, 3> from{}, forward{};
        float ledge = 0, reach = 0, seconds = 0, startFrame = -1, lastFrame = 0, done = 0;
        std::array<float, 3> to(float height) const;
    };
    // The actors giving light now (a LightType and some brightness): where,
    // and their light's settings, for the lights that move and change
    // while the game runs (a spark's flash, an explosion's, a flashlight).
    struct LitActor {
        VmObject* actor = nullptr;
        std::array<float, 3> location{};
        uint8_t type = 0, brightness = 0, hue = 0, saturation = 255, radius = 0, period = 32, phase = 0, effect = 0;
        bool isStatic = false; // bStatic
    };
    void litActors(std::vector<LitActor>& out);
    // A noise the AI may hear (MakeNoise; weapons' and projectiles' sounds).
    void makeNoise(VmObject* maker, float loudness);
    // A sound played as a noise the AI hears (PlaySound, PlayDummySound).
    void soundNoise(VmObject* maker, float radius);
    // Whether the player hears a sound played by `source` as far as
    // `radius` (CheckHearSound), its volume lowered through walls.
    bool hearsSound(VmObject* source, float radius, float& volume);
    // Where the player hears from: its view target, else its camera.
    bool listenerAt(Vec& at);
    static std::string soundName(VmObject* sound);
    struct NoiseStats {
        int made = 0, heard = 0;
    } noise;
    // A script changed an actor's collision (SetCollision): the list of
    // those that collide is made again.
    void collisionChanged() { collidingDirty_ = true; }
    // One actor's tick on its own (ScriptTick: the HUD's portrait, kept
    // going while the game's paused): its scripts and its animation.
    void tickActor(VmObject* actor, float seconds);

private:
    void init(VmObject* actor); // the engine's own properties of a new actor
    void attachViewport();      // the player's viewport and console
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
        float radius = 0; // how far its brush reaches from where the mover is, however it's turned
        std::shared_ptr<Model> model; // its own BSP, if it has one
        std::shared_ptr<LevelCollision> collision;
    };
    bool brushBox(VmObject* mover, std::array<float, 3>& min, std::array<float, 3>& max);
    // A box swept by `delta` against a mover where it is now: its brush's
    // BSP if it has one, else its box.
    struct MoverSpace;
    bool moverSpace(VmObject* mover, MoverSpace& space);
    void hitWall(VmObject* a, const MoveHit& h);
    // Movement as Engine.dll's (vm_physics.cpp).
    struct ZoneFeel {
        std::array<float, 3> gravity{0, 0, -950}, velocity{};
        float groundFriction = 8, fluidFriction = 1.2f, terminal = 2500;
        bool water = false;
    };
    ZoneFeel zoneFeel(VmObject* a);
    std::array<float, 3> twoWallAdjust(const std::array<float, 3>& desired, std::array<float, 3> delta,
                                       const std::array<float, 3>& n, const std::array<float, 3>& oldN, float time);
    void calcVelocity(VmObject* a, std::array<float, 3>& v, std::array<float, 3>& accel, float dt, float maxSpeed,
                      float friction, bool fluid, bool brake, bool buoyant, const std::array<float, 3>& gravity);
    void stepUp(VmObject* a, const std::array<float, 3>& gravDir, const std::array<float, 3>& desiredDir,
                std::array<float, 3> delta, const MoveHit& hit);
    int physicsOf(VmObject* a);
    void setBase(VmObject* a, VmObject* base);
    void startZone(VmObject* a);
    void startBase(VmObject* a);
    void startAsSpawned(VmObject* a);
    std::string options_; // the URL's options, for InitGame and Login
    void walkPlayer(VmObject* a, float dt);
    void walkPawn(VmObject* a, float dt, int maxIterations);
    void landed(VmObject* a, const std::array<float, 3>& n, VmObject* on, float remaining);
    void fallPlayer(VmObject* a, float dt);
    void fall(VmObject* a, float dt);
    void swimOrFly(VmObject* a, float dt, bool swim);
    void updateBodyRegions(VmObject* a, bool events);
    bool moverNear(VmObject* mover, const std::array<float, 3>& start, const std::array<float, 3>& delta,
                   const std::array<float, 3>& extent);
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

public:
    struct CollideSlots {
        int location = -1, radius = -1, height = -1, collideActors = -1, blockActors = -1, blockPlayers = -1,
            isPlayer = -1, owner = -1, base = -1;
        bool mover = false;
    };
private:
    const CollideSlots& collideSlots(VmObject* a);
    std::unordered_map<const VmClass*, CollideSlots> collideSlots_;
    const std::vector<std::pair<VmObject*, const CollideSlots*>>& colliders();
    // Of those, the ones that collide with actors (bCollideActors) as the
    // tick began, with any spawned since and any whose collision a script
    // changed (SetCollision): the moves' loops go through only these.
    const std::vector<std::pair<VmObject*, const CollideSlots*>>& collidingActors();
    std::vector<std::pair<VmObject*, const CollideSlots*>> colliders_;
    int collidersTick_ = -1;
    std::vector<std::pair<VmObject*, const CollideSlots*>> colliding_;
    int collidingTick_ = -1;
    size_t collidingCount_ = 0;
    bool collidingDirty_ = true;
    size_t collidersCount_ = 0;
    std::map<VmObject*, std::vector<VmObject*>> touching_;
    std::map<VmObject*, Goal> goals_;
    std::set<VmObject*> fallers_; // falling since a tick before
    std::map<VmObject*, int> animWaits_; // actors in a latent FinishAnim, by their state serial then
    std::map<VmObject*, std::map<NameId, AnimLayer>> layers_;
    std::map<VmObject*, SkinChannels> skins_;
    std::map<VmObject*, TweenFrom> tweenFrom_;
    void noteTweenFrom(VmObject* actor, bool tweening);
    std::map<VmObject*, std::pair<NameId, int>> layerWaits_; // in C3DFinishAnim: the group, the state serial
    struct ActorMesh;
    ActorMesh* actorMesh(VmObject* actor);
    const std::vector<Vec>* posedVertices(VmObject* actor, ActorMesh& mesh); // packed, as this tick has them
    const std::vector<std::array<float, 12>>* posedSkeleton(VmObject* actor, ActorMesh& mesh); // its bones, the same
    std::vector<LayeredPoser::Layer> posedLayers(VmObject* actor, ActorMesh& mesh, std::vector<std::string>& names);
    bool attachFrame(VmObject* actor, ActorMesh& mesh, const MeshAttachPoint& point, Vec& at, std::array<Vec, 3>& axes);
    void toWorld(VmObject* actor, Vec& at, std::array<Vec, 3>* axes);
    void placeAttached();
    std::map<const VmObject*, std::shared_ptr<ActorMesh>> actorMeshes_; // by mesh object
    std::shared_ptr<std::vector<AnimationSet>> animationSets_;
    std::map<VmObject*, std::pair<int, std::vector<Vec>>> posed_; // each actor's vertices, and which tick's
    std::map<VmObject*, std::pair<int, std::vector<std::array<float, 12>>>> posedBones_; // its bones, the same
    void forgetPose(VmObject* actor);
    struct Attachment {
        VmObject* parent = nullptr;
        NameId parentPoint = 0, childPoint = 0;
    };
    std::map<VmObject*, Attachment> attachments_;
    struct Bends {
        // The scripts' turns this tick, in order (the engine's last until
        // the pose is worked out again: each tick the scripts make them anew).
        struct Op {
            int kind = 0; // 0 own axis, 1 in the actor's axes, 2 removed
            NameId bone = 0;
            int axis = 0;
            float degrees = 0;
            Rot rotation{};
            std::array<bool, 3> remove{}; // pitch, yaw, roll
        };
        std::vector<Op> ops;
        int64_t opsTick = -2;
        // Each limb's IK (0 left arm, 1 right arm, 2 left leg, 3 right leg).
        struct Ik {
            bool place = false, turn = false;
            Vec at{};
            std::array<Vec, 3> axes{};
            float placeWeight = 0, turnWeight = 0;
            bool onBase = false; // reaching from the actor's own animation, under the layers
        };
        std::array<Ik, 4> limbs;
        // Each limb's point in its end bone's axes, and the point's axes in
        // them (none if the mesh hasn't it: the bone itself), as this tick's
        // animation has the hand.
        struct Effector {
            int tick = 0;
            Vec offset{};
            std::array<Vec, 3> axes{Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}};
        };
        std::array<Effector, 4> effectors;
    };
    Bends::Op& newBoneOp(VmObject* actor);
    std::map<VmObject*, Bends> bends_;
    void loadPaths();
    Hud hud_;
    HudView hudView_, hudFrame_;
    bool hudFrameClear_ = false;
    struct Fli {
        int frames = 0;
        float frameSeconds = 1.0f / 30;
    };
    std::map<VmObject*, Fli> flis_;
    VmObject* canvas_ = nullptr;
    std::map<std::pair<const Package*, int>, std::shared_ptr<Font>> fonts_;
    using IntFile = std::map<std::string, std::vector<std::pair<std::string, std::string>>>; // by section, lower case
    std::map<std::string, IntFile> intFiles_; // by file name, lower case
    // A line being said (or waiting for its speaker), as the engine's
    // dialogue keeps them: its thread, whose dialogue it is (the caller:
    // its conditions, its own lines), who says it and to whom, and how
    // long until it's done.
    struct DialogThread {
        int id = 0, line = 0;
        VmObject* self = nullptr;
        VmObject* speaker = nullptr;
        VmObject* listener = nullptr;
        float seconds = 0;     // its voice's length
        float timeLeft = 0;    // until it's done: voice, pause and a quarter second
        float elapsed = 0;     // since it began
        bool queued = false;   // waiting for its speaker to be free
        bool hudClock = false; // a HUD line: timed while paused too
        int back = -1;         // the RETURNLINE pending in its thread
        std::string backPattern;
        size_t phoneme = 0;    // lip-sync: the next mouth shape
    };
    // Each line's engine side: its key (TYPE SPK LST ID MISSION, no
    // listener ___), its type's priority (the tables' Types groups),
    // whether it runs on from the line before, what follows it and what
    // it returns to (resolved once if only one line fits), its tag, event
    // and condition (none if in brackets), and its camera flags (A 1, R 2,
    // C 4, D or T 8, V 16, H 32).
    struct DialogRec {
        std::string key;
        int group = 9;
        bool runOn = false;
        int next = -1, back = -1;
        std::string nextPattern, backPattern;
        std::string tag, event, condition;
        int flags = 0;
    };
    std::vector<DialogRec> dialogRecs_;
    std::vector<int> dialogOrder_; // the lines in key order
    std::vector<std::string> dialogFaces_;
    std::array<std::vector<std::pair<int, std::string>>, 4> dialogHeaders_; // Missions, Types, Speakers, Faces
    std::map<std::string, int> dialogGroups_; // a type's priority (1 the most)
    // Lip-sync, as the audio driver does it (vm_sound.cpp): a sound's
    // words and their phonemes (its WAV's phnm chunk), and the sounds
    // playing that have them, each moving its actor's mouth.
    struct LipWord {
        float start = 0, end = 0;
        std::vector<std::pair<uint16_t, float>> phonemes; // IPA letter, start
    };
    std::map<std::pair<const Package*, int>, std::vector<LipWord>> lipWords_;
    const std::vector<LipWord>& lipWordsOf(const Package* package, int exportIndex);
    struct LipChannel {
        VmObject* actor = nullptr;
        int slot = 0;
        bool noOverride = false;
        const std::vector<LipWord>* words = nullptr;
        double start = 0;   // when it began, in level time
        float seconds = 0;  // its length
    };
    std::vector<LipChannel> lipChannels_;
    double lipLast_ = 0; // the level's time at the last update
    void lipSyncStop(LipChannel& c);
    void lipSyncUpdate();
    std::string playerSpeaker();
    bool missionFits(int index);
    int conditionTerm(const std::vector<std::string>& term, VmObject* actor);
    int bestLine(const std::string& pattern, NameId filterTag, VmObject* caller, int* count);
    // StartLine: 1 started, 2 waiting for its speaker, 0 refused.
    int startLine(int thread, int index, VmObject* caller, int back = -1, const std::string& backPattern = {});
    VmObject* speakerFor(int index, VmObject* caller);
    void endLine(DialogThread& t, bool cut);
    static bool keyFits(const std::string& pattern, const std::string& key, bool strict = false);
    struct PendingLine {
        int thread = 0, line = -1;
        VmObject* caller = nullptr;
        int back = -1;
        std::string backPattern;
    };
    std::vector<PendingLine> pendingLines_; // lines that follow, started after the tick's endings
    void writeLineFields(DialogThread& t);
    // The AI's own lines (CTVE_DialogInput.DialogTick, as the engine's):
    // a type picked from each pawn's state, its line said by it, its
    // conditions about it.
    bool talksToPlayer(VmObject* pawn, float dist, bool noAction);
    void sayOwnLine(VmObject* pawn, const std::string& type, int32_t bit, float dist);
    bool hasDialogLines(const std::string& type, const std::string& speaker);
    int ownLine(VmObject* pawn, const std::string& type);
    void playOwnLine(VmObject* pawn, int index);
    // The path search's state per navigation point (visitedWeight,
    // bEndPoint, previousPath, cost...), kept between searches as the
    // engine keeps it (bClearPaths false reuses it).
    struct NavState {
        int visited = 10000000, bestPathWeight = 0, cost = 0, previous = -1;
        bool endPoint = false;
    };
    std::vector<NavState> navState_;
    std::map<VmObject*, size_t> navIndex_;
    struct ClosestCache {
        bool filled = false;
        std::array<float, 3> center{}, last{};
        float radius = 0;
        std::vector<std::pair<float, VmObject*>> nodes;
    } closest_;
    MoveHit testMove(VmObject* a, const std::array<float, 3>& delta);
    bool waterAt(const std::array<float, 3>& p);
    int walkMove(VmObject* pawn, std::array<float, 3> delta, float threshold, float& floorZ);
    bool walkReachable(VmObject* pawn, std::array<float, 3> dest, float threshold, VmObject* goal);
    bool reachable(VmObject* pawn, const std::array<float, 3>& dest, float threshold, VmObject* goal);
    bool specFits(VmObject* pawn, const ReachSpec& s);
    const ReachSpec* specAt(VmObject* node, const char* list, int k);
    bool moverAllows(VmObject* pawn, const ReachSpec& s);
    bool canMoveTo(VmObject* pawn, VmObject* from, VmObject* to);
    std::map<VmObject*, float> noiseQuiet_; // pawns: no noise heard before this time
    std::mt19937 barkRandom_{13579};
    std::vector<std::pair<VmObject*, int>> soundStops_;
    struct TrackedSound {
        SoundRequest request;
        float start = 0, seconds = 0;
    };
    std::vector<TrackedSound> trackedSounds_;
    void writeMeshGroups(ByteWriter& b, VmObject* actor);
    bool usesC3DMesh(VmObject* actor);
    int meshStates_ = 1000; // new C3D mesh states' numbers (their names)
    VmObject meshStateMarker_; // stands for an actor's new mesh state while it's written
    void writeDialogEngine(ByteWriter& b, PackageWriter& w);
    VmObject* musicActor_ = nullptr;
    float musicStart_ = 0;
    void loadDialog();
    VmObject* dialogInput();
    float lineSeconds(const DialogLine& line);
    void beginLine(DialogThread& thread, int index);
    void tickDialog(float seconds);
    bool dialogLoaded_ = false;
    std::vector<DialogLine> dialogLines_;
    std::vector<int> dialogPlays_;      // how often each line's been said
    std::vector<int> dialogSaid_;       // the lines begun, in order
    std::vector<float> dialogSaidAt_;   // when each was begun
    std::vector<DialogThread> dialogThreads_;
    int nextDialogThread_ = 1;
    int currentThread_ = 0;             // the thread whose line the level's fields show
    int currentLine_ = -1;              // the line said last (its camera: GetDialogueCameraData)
    VmObject* currentSpeaker_ = nullptr;
    uint32_t dialogRandom_ = 1;
    std::map<std::string, float> dialogSeconds_; // each voice's length
    std::vector<ReachSpec> reachSpecs_;
    std::vector<VmObject*> navigationPoints_;
    LayeredPoser::Adjust adjustOf(VmObject* actor, ActorMesh& mesh);
    bool measureEffectors(VmObject* actor, ActorMesh& mesh, Bends& bends, const LayeredPoser::Adjust* adjust);
    int ticks_ = 0;
    int placedGhosts_ = 0;
    int localizedSet_ = 0;
    std::vector<VmObject*> movers_; // the level's, which traces of the world meet
    int editorCameras_ = 0;
    std::vector<VmObject*> actors_;
    std::vector<VmObject*> ghosts_;
    LevelExtras saved_;                       // a saved game's Level extras (a map's are empty)
    std::map<VmObject*, StateFrame> savedFrames_; // the state each saved actor was in
    SurfaceSwaps surfaceSwaps_;
    std::map<VmObject*, Decal> decals_;
    std::map<VmObject*, float> sparkFlash_; // sparks' lights, on for this long yet
    std::map<VmObject*, float> ledgePush_;  // the player pushing into a ledge, this long
    std::map<VmObject*, Clamber> clambers_; // pulls up onto ledges under way
    std::map<VmObject*, float> copyStarts_; // effects' copies: the life each started with
    float copyFloat(VmObject* mm, const char* name, float otherwise = 0);
    std::array<float, 3> copyVec(VmObject* mm, const char* name);
    void setCopyFloat(VmObject* mm, const char* name, float v);
    void setCopyVec(VmObject* mm, const char* name, const std::array<float, 3>& v);
    float copyAge(VmObject* mm);
    float copyAgeBefore(VmObject* mm, float age, float dt);
    bool stepCopy(VmObject* mm, float dt, bool accelerate, bool move, const std::array<float, 3>& extra);
    bool ageCopy(VmObject* mm, float dt);
    float time_ = 0;
    std::set<int> keysDown_;
    std::string travel_;
    bool quitAsked_ = false;
    bool fastForward_ = false;
    std::filesystem::path saveFolder_;
    std::vector<SoundRequest> soundRequests_;
    Music music_;
    std::map<const VmObject*, bool> streamed_;
    bool killSounds_ = false;
    float masterVolume_ = -1;
};

// A setting from a game's .ini ("Engine.Engine", "DefaultGame"); empty if
// it isn't there.
std::string iniSetting(const PackageSet& packages, std::string_view section, std::string_view key);
// The same from another .ini in the game's System folder ("User.ini").
std::string iniSetting(const PackageSet& packages, std::string_view file, std::string_view section,
                       std::string_view key);
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
// The AI's paths (vm_paths.cpp).
void registerPathNatives(Vm& vm);
// The Fallen's dialogue (vm_dialog.cpp).
void registerDialogNatives(Vm& vm);
// The Fallen's documents and objectives (vm_documents.cpp).
void registerDocumentNatives(Vm& vm);
// The HUD's canvas (vm_canvas.cpp).
void registerCanvasNatives(Vm& vm);
// The console's commands, keys and travel (vm_console.cpp).
void registerConsoleNatives(Vm& vm);
// The HUD's saved games list (vm_save.cpp).
void registerSaveNatives(Vm& vm);
// Sounds and music (vm_sound.cpp).
void registerSoundNatives(Vm& vm);
// The Fallen's effects: arcs, beams, decals (vm_effects.cpp).
void registerEffectNatives(Vm& vm);
// Their random numbers started again, as each level starts (and Core's).
void reseedEffectRandom();
void reseedEffectPhysicsRandom();
void reseedCoreRandom();
// A map's reach list as stored (object references, not yet actors).
struct RawReachSpec {
    int distance = 0, start = 0, end = 0, radius = 0, height = 0, flags = 0;
    bool pruned = false;
};
Error readReachSpecs(const Package& map, std::vector<RawReachSpec>& out);

} // namespace reluva::unreal
