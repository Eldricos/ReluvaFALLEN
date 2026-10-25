// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_chat.h"
#include "engines/idtech3/botlib/bot_config.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// The bot library's error codes (0: none), as the game reads them.
namespace blerr {
constexpr int None = 0, LibraryNotSetup = 1, InvalidEntityNumber = 2, NoAasFile = 3, CannotOpenAasFile = 4,
              WrongAasFileId = 5, WrongAasFileVersion = 6, CannotReadAasLump = 7, CannotLoadInitialChat = 8,
              CannotLoadItemWeights = 9, CannotLoadItemConfig = 10, CannotLoadWeaponWeights = 11,
              CannotLoadWeaponConfig = 12;
} // namespace blerr

// What a bot does this frame (the elementary actions), which the game
// turns into its user command. Elite Force's values, the bot library's
// older ones (read from the game's conversion; eliteforce-syscalls.md):
// jumping and moving up share a bit, as do crouching and moving down.
namespace action {
constexpr int Attack = 0x1, Use = 0x2, Respawn = 0x4, Jump = 0x8, MoveUp = 0x8, Crouch = 0x10, MoveDown = 0x10,
              MoveForward = 0x20, MoveBack = 0x40, MoveLeft = 0x80, MoveRight = 0x100, DelayedJump = 0x200,
              Talk = 0x400, Gesture = 0x800, Walk = 0x1000, AltAttack = 0x2000, JumpedLastFrame = 0x10000000;
} // namespace action

struct BotInput {
    float thinkTime = 0;
    Vec3f dir{};
    float speed = 0; // 0..400
    Vec3f viewAngles{};
    int actionFlags = 0;
    int weapon = 0;
};

// What a bot goes for: an item, a spot.
struct BotGoal {
    Vec3f origin{};
    int area = 0;
    Vec3f mins{}, maxs{};
    int entity = 0, number = 0, flags = 0, itemInfo = 0;
};
namespace goalflag {
constexpr int Item = 1, Roam = 2, Dropped = 4;
} // namespace goalflag

// How moving went this frame, for the game.
struct BotMoveResult {
    bool failure = false;
    int type = 0;
    bool blocked = false;
    int blockEntity = 0;
    int travelType = 0, flags = 0, weapon = 0;
    Vec3f moveDir{}, idealViewAngles{};
};
namespace moveflag {
constexpr int BarrierJump = 1, OnGround = 2, Swimming = 4, AgainstLadder = 8, WaterJump = 16, Teleported = 32,
              Walk = 512;
} // namespace moveflag
namespace moveresult {
constexpr int MovementView = 1, SwimView = 2, Waiting = 4, MovementViewSet = 8, MovementWeapon = 16,
              OnTopOfObstacle = 32, OnTopOfFuncBob = 64, OnTopOfElevator = 128;
constexpr int TypeInSolidArea = 7, TypeElevatorUp = 2; // result types
} // namespace moveresult
namespace movetype {
constexpr int Walk = 1, Crouch = 2, Jump = 4;
} // namespace movetype
// The state the game sets each frame before moving (bot_initmove_t).
struct BotInitMove {
    Vec3f origin{}, velocity{}, viewOffset{};
    int entity = 0, client = 0;
    float thinkTime = 0;
    int presence = 0;
    Vec3f viewAngles{};
    int orMoveFlags = 0;
};

// Why a movement prediction stopped (what the game asked to stop at).
namespace stopevent {
constexpr int None = 0, HitGround = 1, LeaveGround = 2, EnterWater = 4, EnterSlime = 8, EnterLava = 16,
              HitGroundDamage = 32, Gap = 64, TouchJumpPad = 128, TouchTeleporter = 256, EnterArea = 512,
              HitGroundArea = 1024, HitBoundingBox = 2048, TouchClusterPortal = 4096;
} // namespace stopevent

// Where a player moving as given ends up (aas_clientmove_t).
struct BotClientMove {
    Vec3f endPos{};
    int endArea = 0;
    Vec3f velocity{};
    // The last trace: started in solid, how far, where, what it hit.
    bool traceStartSolid = false;
    float traceFraction = 1;
    Vec3f traceEnd{};
    int traceEntity = 0;
    int presence = 0, stopEvent = 0, endContents = 0;
    float time = 0;
    int frames = 0;
};

// Contents the bot library traces against (Quake III's numbers).
namespace contents {
constexpr int Solid = 1, Lava = 8, Slime = 16, Water = 32, PlayerClip = 0x10000, Body = 0x2000000;
} // namespace contents

// An entity as the game describes it to the bot library each frame.
struct BotEntityState {
    int type = 0, flags = 0;
    Vec3f origin{}, angles{}, oldOrigin{}, mins{}, maxs{};
    int groundEnt = 0, solid = 0, modelIndex = 0, modelIndex2 = 0, frame = 0, event = 0, eventParm = 0, powerups = 0,
        weapon = 0, legsAnim = 0, torsoAnim = 0;
};

// And as the bot library tells the game about it.
struct BotEntityInfo {
    bool valid = false;
    int type = 0, flags = 0;
    float lastTime = 0, updateTime = 0;
    int number = 0;
    Vec3f origin{}, angles{}, oldOrigin{}, lastVisOrigin{}, mins{}, maxs{};
    int groundEnt = 0, solid = 0, modelIndex = 0, modelIndex2 = 0, frame = 0, event = 0, eventParm = 0, powerups = 0,
        weapon = 0, legsAnim = 0, torsoAnim = 0;
};

// The engine side of Quake III's bot library, as Elite Force's game code
// uses it: its settings, the level's .aas map, the entities, the bots'
// characters and what each bot does each frame. Behaviour read from the
// GPL ioquake3 bot library (be_interface.c, be_aas_*.c, be_ea.c,
// be_ai_char.c); new code.
class BotLibrary {
public:
    static constexpr int kMaxClients = 64, kMaxEntities = 1024, kMaxUserMove = 400;

    // Reads a file of the game's (pk3s and folders), for the scripts and maps.
    std::function<bool(const std::string& path, std::vector<uint8_t>& data)> readFile;
    // A command a bot sends as if typed ("say hello").
    std::function<void(int client, const std::string& command)> clientCommand;
    std::function<void(const std::string& text)> print;
    // A box swept through the world and its entities (the engine's trace).
    struct Trace {
        float fraction = 1;
        Vec3f end{};
        bool startSolid = false;
        int entity = 1023; // 1022 the world, 1023 nothing
        Vec3f normal{};    // of what it hit
    };
    std::function<Trace(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end, int passEntity,
                        int mask)>
        trace;
    std::function<int(const Vec3f& point)> pointContents;
    // A brush model's box (*1, *2... as numbers), unmoved, and where the
    // brush entity with it (a lift) is now.
    std::function<bool(int model, Vec3f& mins, Vec3f& maxs)> modelBounds;
    std::function<bool(int model, Vec3f& origin)> moverOrigin;
    // The map's entities (classname, origin...), from 1 as the original
    // numbers them.
    std::function<bool(int entity, const std::string& key, std::string& value)> entityKey;
    int entityCount = 0;

    int setup();
    int shutdown();
    bool isSetup() const { return setup_; }
    void setLibVar(const std::string& name, const std::string& value) { libVars_[name] = value; }
    std::string libVar(const std::string& name) const;
    void addGlobalDefine(const std::string& text) { defines_.push_back(text); }
    // Loads maps/<name>.aas.
    int loadMap(const std::string& mapName);
    int startFrame(float time);
    int updateEntity(int number, const BotEntityState* state);
    float time() const { return time_; }

    AasWorld& aas() { return aas_; }
    bool aasInitialized() const { return aas_.loaded(); }
    const BotEntityInfo& entityInfo(int number) const;

    // A loader for the bot scripts: paths under botfiles/.
    BotScript::Loader scriptLoader() const;
    const std::vector<std::string>& globalDefines() const { return defines_; }
    BotCharacters& characters() { return characters_; }

    // Elementary actions.
    BotInput& input(int client) { return inputs_[size_t(std::clamp(client, 0, kMaxClients - 1))]; }
    void say(int client, const std::string& text) { command(client, "say " + text); }
    void sayTeam(int client, const std::string& text) { command(client, "say_team " + text); }
    void command(int client, const std::string& text);
    void jump(int client);
    void delayedJump(int client);
    void move(int client, const Vec3f& dir, float speed);
    BotInput getInput(int client, float thinkTime);
    void resetInput(int client);

    // Chat (bot_chat.cpp): its shared files read at setup.
    BotChat chat;

    // Goals (bot_goals.cpp): the level's items, each bot's goal stack and
    // goals to avoid for a while, and choosing items by their weights.
    int allocGoalState(int client);
    void freeGoalState(int handle);
    void resetGoalState(int handle);
    void resetAvoidGoals(int handle);
    void pushGoal(int handle, const BotGoal& goal);
    void popGoal(int handle);
    void emptyGoalStack(int handle);
    bool topGoal(int handle, BotGoal& goal, int depth = 1) const;
    int loadItemWeights(int handle, const std::string& file);
    void freeItemWeights(int handle);
    bool chooseLtgItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags);
    bool chooseNbgItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags,
                       const BotGoal* ltg, float maxTime);
    static bool touchingGoal(const Vec3f& origin, const BotGoal& goal);
    bool itemGoalInVisButNotVisible(int viewer, const Vec3f& eye, const BotGoal& goal);
    int levelItemGoal(int index, const std::string& name, BotGoal& goal) const;
    float avoidGoalTime(int handle, int number) const;
    void removeFromAvoidGoals(int handle, int number);
    void initLevelItems();
    void updateEntityItems();
    std::string goalName(int number) const;
    int nextCampSpotGoal(int num, BotGoal& goal) const;
    bool mapLocationGoal(const std::string& name, BotGoal& goal) const;
    // The area a bot at a point is in, or its last one with reachabilities.
    int reachabilityArea(const Vec3f& origin) const;

    // Weapons (bot_weapons.cpp).
    int allocWeaponState();
    void freeWeaponState(int handle);
    int loadWeaponWeights(int handle, const std::string& file);
    int chooseBestFightWeapon(int handle, const std::vector<int>& inventory) const;
    const WeaponInfo* weaponInfo(int weapon) const;

    // Movement (bot_move.cpp): following routes, per kind of travel.
    int allocMoveState();
    void freeMoveState(int handle);
    void resetMoveState(int handle);
    void initMoveState(int handle, const BotInitMove& init);
    BotMoveResult moveToGoal(int handle, const BotGoal& goal, int travelFlags);
    bool moveInDirection(int handle, const Vec3f& dir, float speed, int type);
    void resetAvoidReach(int handle);
    void resetLastAvoidReach(int handle);
    bool movementViewTarget(int handle, const BotGoal& goal, int travelFlags, float lookahead, Vec3f& target);
    bool predictVisiblePosition(const Vec3f& origin, int area, const BotGoal& goal, int travelFlags, Vec3f& target);
    // A player's movement simulated frame by frame (`frameTime` seconds
    // each): moving by `cmdMove` (a direction and speed; up to jump, down
    // a lot to crouch) for the first `cmdFrames`, falling, sliding along
    // walls and up steps, until one of the `stopEvents` or `maxFrames`.
    bool predictClientMovement(BotClientMove& move, int entity, const Vec3f& origin, int presenceType, bool onGround,
                               const Vec3f& velocity, const Vec3f& cmdMove, int cmdFrames, int maxFrames,
                               float frameTime, int stopEvents, int stopArea);

private:
    struct LevelItem {
        int number = 0, itemInfo = 0, flags = 0;
        float weight = 0;
        Vec3f origin{};
        int goalArea = 0;
        Vec3f goalOrigin{};
        int entity = 0;
        float timeout = 0;
    };
    struct GoalState {
        int client = 0, lastArea = 0;
        std::shared_ptr<WeightConfig> weights;
        std::vector<int> weightIndex; // per item info
        std::vector<BotGoal> stack;
        std::map<int, float> avoid; // goal number -> until when
    };
    struct WeaponState {
        std::shared_ptr<WeightConfig> weights;
        std::vector<int> weightIndex; // per weapon
    };
    struct MoveState {
        Vec3f origin{}, velocity{}, viewOffset{}, viewAngles{}, lastOrigin{};
        int entity = 0, client = 0, presence = 0, moveFlags = 0;
        float thinkTime = 0;
        int area = 0, lastArea = 0, lastGoalArea = 0, lastReach = 0, reachArea = 0, jumpReach = 0;
        float reachTime = 0;
        std::map<int, std::pair<float, int>> avoidReach; // reachability -> until when, tries
    };
    struct Spot {
        Vec3f origin{};
        int area = 0;
        std::string name;
    };
    template <class T> static T* state(std::vector<std::unique_ptr<T>>& v, int handle) {
        return handle >= 1 && size_t(handle) <= v.size() ? v[size_t(handle) - 1].get() : nullptr;
    }
    template <class T> static const T* state(const std::vector<std::unique_ptr<T>>& v, int handle) {
        return handle >= 1 && size_t(handle) <= v.size() ? v[size_t(handle) - 1].get() : nullptr;
    }
    template <class T> static int alloc(std::vector<std::unique_ptr<T>>& v) {
        for (size_t i = 0; i < v.size(); ++i)
            if (!v[i]) {
                v[i] = std::make_unique<T>();
                return int(i) + 1;
            }
        v.push_back(std::make_unique<T>());
        return int(v.size());
    }
    std::shared_ptr<WeightConfig> weights(const std::string& file);
    void loadConfigs();
    int bestReachableArea(const Vec3f& origin, const Vec3f& mins, const Vec3f& maxs, Vec3f& goalOrigin) const;
    bool itemAllowed(const LevelItem& li) const;
    bool chooseItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags,
                    const BotGoal* ltg, float maxTime, bool nearby);
    int reachabilityToGoal(const Vec3f& origin, int area, int lastGoalArea, int lastArea, MoveState* ms,
                           const BotGoal& goal, int travelFlags);
    bool onGround(const MoveState& ms) const;
    void checkBlocked(const MoveState& ms, const Vec3f& dir, bool checkBottom, BotMoveResult& result) const;
    float gapDistance(const MoveState& ms, const Vec3f& hordir) const;
    BotMoveResult travel(MoveState& ms, const AasReach& reach, bool finishing);
    int entityOnTopOf(const MoveState& ms) const;

    bool setup_ = false;
    std::map<std::string, std::string> libVars_;
    std::vector<std::string> defines_;
    AasWorld aas_;
    float time_ = 0;
    int frames_ = 0;
    std::vector<BotEntityInfo> entities_ = std::vector<BotEntityInfo>(size_t(kMaxEntities));
    std::array<BotInput, kMaxClients> inputs_{};
    BotCharacters characters_;
    bool configsLoaded_ = false;
    std::vector<ItemInfo> items_;
    std::vector<WeaponInfo> weapons_;
    std::map<std::string, std::shared_ptr<WeightConfig>> weightFiles_;
    std::vector<LevelItem> levelItems_;
    std::vector<Spot> mapLocations_, campSpots_;
    std::vector<std::unique_ptr<GoalState>> goalStates_;
    std::vector<std::unique_ptr<WeaponState>> weaponStates_;
    std::vector<std::unique_ptr<MoveState>> moveStates_;
    std::map<int, std::string> modelTypes_; // brush model number -> its entity's class
    // Lifts, as the original's movement sees them: where the mover with
    // brush model `model` is now; whether it's down (its top below the
    // reachability's start); the middle of its top at the start's height;
    // whether a bot at `origin` stands on it.
    bool moverDown(const AasReach& reach) const;
    Vec3f moverBottomCenter(const AasReach& reach) const;
    bool onMover(const Vec3f& origin, int entity, const AasReach& reach) const;
};

} // namespace reluva::idtech3::botlib
