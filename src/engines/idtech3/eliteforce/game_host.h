// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/vm_services.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's game logic (vm/qagame.qvm, the server side of Holomatch)
// run with the engine side of its system calls, in Elite Force's
// numbering (docs/formats/idtech3/eliteforce-syscalls.md). For now it
// loads a level: the map's entities are handed over, and what the game
// does with them is recorded. Calls that would fill in the game's own
// structures (traces, brush models) wait until their layouts are known;
// they're counted in unhandled().
class GameHost {
public:
    // vmMain's commands (Quake III's order; Init confirmed on Elite
    // Force's qagame.qvm).
    enum Command : int32_t {
        Init = 0, // (levelTime, randomSeed, restart)
        Shutdown = 1,
        ClientConnect = 2,
        ClientBegin = 3,
        ClientUserinfoChanged = 4,
        ClientDisconnect = 5,
        ClientCommand = 6,
        ClientThink = 7,
        RunFrame = 8, // (levelTime)
        ConsoleCommand = 9,
        BotAiStartFrame = 10,
    };

    GameHost(const SearchSet& files, CvarStore& cvars);

    // Reads vm/qagame.qvm.
    Error load();
    // Starts a level: sets the map's cvars, hands the game its entity
    // text (the BSP's entity lump) and calls Init, then runs a few frames
    // as the original server does to let things settle. The map is also
    // what things collide with.
    Error startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed = 0);
    Error runFrame();
    // A player joining, as the original server does it: their userinfo
    // (an info string: \\name\\...\\model\\...), then ClientConnect,
    // which may refuse them (`refused` gets the game's reason), then
    // ClientBegin, which puts them in the level.
    Error connectClient(int32_t clientNum, const std::string& userinfo, std::string& refused);
    Error beginClient(int32_t clientNum);
    // A player's input for one moment, as Elite Force's usercmd_t (24
    // bytes, see eliteforce-syscalls.md): Quake III's fields with the
    // buttons before the angles.
    struct UserCommand {
        int32_t serverTime = 0;          // ms, the level time it applies to
        std::array<int32_t, 3> angles{}; // view pitch, yaw, roll: 65536 = a full turn
        int32_t buttons = 0;
        uint8_t weapon = 0;
        int8_t forward = 0, right = 0, up = 0; // -127..127
    };
    // Hands the game a player's command and lets it act on it (ClientThink,
    // which reads it with GetUsercmd), as the server does for each
    // command a client sends.
    Error clientThink(int32_t clientNum, const UserCommand& cmd);
    // A command a player sent the server ("say hello", "kill"...), which
    // the game reads with Argc and Argv (ClientCommand).
    Error clientCommand(int32_t clientNum, const std::string& text);
    // A command typed at the server's console that the engine doesn't
    // know ("addbot munro 3", "kick"...), handed to the game
    // (ConsoleCommand); `handled` gets whether the game knew it.
    Error serverCommand(const std::string& text, bool* handled = nullptr);
    // The bots (clients the game asked for with BotAllocateClient).
    const std::set<int32_t>& botClients() const { return botClients_; }
    botlib::BotLibrary& botLibrary() { return bots_; }
    // Each system call of the bot library's ranges (200 on) with its first
    // arguments the first few times it was made, for finding out what
    // Elite Force's numbers are.
    const std::map<int, std::vector<std::vector<int32_t>>>& botCallSamples() const { return botCallSamples_; }
    // Writes a command into game code's memory, laid out as usercmd_t:
    // server time 0, buttons 4 and weapon 5 (a byte each), angles 8,
    // forward, right and up moves 20-22.
    static void writeUserCommand(Qvm& vm, int32_t address, const UserCommand& cmd);
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);

    Qvm& vm() { return vm_; }
    int32_t levelTime() const { return levelTime_; }

    // What the game has told the engine.
    struct GameData {
        int32_t entities = 0;   // address of the entity array
        int32_t count = 0;      // entities in use so far (the highest + 1)
        int32_t entitySize = 0; // bytes per entity
        int32_t clients = 0;    // address of the client array
        int32_t clientSize = 0;
    };
    const GameData& gameData() const { return gameData_; }
    // A field of an entity, read from the game's memory (see the layout
    // in game_host.cpp).
    int32_t entityInt(int32_t number, int32_t offset);
    Vec3f entityVec(int32_t number, int32_t offset);
    // A field of a client (gclient_t, which starts with the player state).
    int32_t clientInt(int32_t clientNum, int32_t offset);
    // How many snapshots have been made (snapshot()), for counting.
    int snapshotsMade() const { return snapshotsMade_; }
    Vec3f clientVec(int32_t clientNum, int32_t offset);
    // A trace as the game's Trace call makes it: the world and (unless
    // `entities` is false) linked entities; `hit` gets what stopped it
    // (an entity number, 1022 the world, 1023 nothing).
    std::pair<int, int> entityAreas(int32_t n);
    bool areasConnected(int area1, int area2) const;
    TraceResult traceMove(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                          int32_t passEntity, int32_t mask, int32_t& hit, bool entities = true);
    // What the server sends a player each frame: the level time, their
    // player state and the entities they can see, copied byte for byte
    // from the game's memory (the first 460 bytes of their client, the
    // first 204 of each entity), and how many commands the game has sent
    // them so far.
    struct Snapshot {
        int32_t serverTime = 0;
        std::vector<uint8_t> playerState;
        std::vector<std::vector<uint8_t>> entities; // entity states, in entity order
        int32_t serverCommandSequence = 0;
        // A bit set for each area the player's isn't joined to (behind a
        // closed door), as the original's server works it out; the client
        // game hands it to the renderer.
        std::array<uint8_t, 32> areaMask{};
    };
    // Milliseconds a server frame: 20 a second, Quake III's default sv_fps.
    static constexpr int32_t kFrameMs = 50;
    static constexpr int32_t kPlayerStateSize = 460, kEntityStateSize = 204;
    // A word of the player state the original's network protocol doesn't
    // send, which snapshots carry as 0.
    static constexpr int32_t kPlayerStateServerOnly = 48;
    Snapshot snapshot(int32_t clientNum);
    // Where svFlags is in Elite Force's entities (208 or 212: one of the
    // two words between `linked` and `bmodel`), found from the entities
    // themselves, and the evidence: how many entities have flags at each.
    int32_t svFlagsOffset();
    std::pair<int, int> svFlagsEvidence();
    // How many times each system call has been made since the start.
    const std::map<int, int>& callCounts() const { return callCounts_; }
    // Entity numbers linked into the world (LinkEntity), not unlinked since.
    const std::set<int32_t>& linked() const { return linked_; }
    // The map's areas (cut by its doors' portals), and how many times
    // InPvs said no only because a closed door stood between.
    int areaCount() const { return world_ ? world_->areaCount() : 0; }
    int closedDoorBlocks() const { return closedDoorBlocks_; }
    const std::map<int32_t, std::string>& configstrings() const { return configstrings_; }
    // What the game sent to clients (SendServerCommand), in order: the
    // client (-1 for all) and the command text.
    const std::vector<std::pair<int32_t, std::string>>& serverCommands() const { return serverCommands_; }
    // Clients the game dropped, with its reason.
    const std::map<int32_t, std::string>& dropped() const { return dropped_; }
    // Commands the game ran (SendConsoleCommand), in order.
    const std::vector<std::string>& consoleCommands() const { return consoleCommands_; }

    std::function<void(const std::string&)> print;
    const std::string& log() const { return log_; }
    const std::map<int, int>& unhandled() const { return unhandled_; }
    std::function<void(int number, const int32_t* args)> trace;

private:
    int32_t systemCall(Qvm& vm, int number, const int32_t* args);
    // The next token of the entity text, the way the original parses it:
    // a quoted string, or anything up to white space. Empty at the end.
    std::string nextEntityToken();
    int32_t entityNumber(int32_t address) const;
    std::string infoString(int32_t flag) const;
    void updateInfoConfigstrings();
    int32_t entityAddress(int32_t number) const;
    Vec3f readVec(int32_t address);
    void writeVec(int32_t address, const Vec3f& v);
    void linkEntity(int32_t address);
    void clipMove(int32_t results, const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
               int32_t passEntity, int32_t mask);
    int32_t pointContents(const Vec3f& point, int32_t passEntity);
    // The bot library's calls (game_bots.cpp).
    int32_t botCall(Qvm& vm, int number, const int32_t* args);
    void runBotFrame();
    void runQueuedConsoleCommands();
    std::vector<int32_t> botSnapshotEntities(int32_t clientNum);

    const SearchSet& files_;
    CvarStore& cvars_;
    Qvm vm_;
    VmFiles vmFiles_;
    std::shared_ptr<const Bsp> map_;
    std::unique_ptr<CollisionWorld> world_;
    std::unique_ptr<AreaPortals> areaPortals_; // which areas join up through open doors
    int closedDoorBlocks_ = 0; // InPvs answers a closed door made "no"
    std::string entityText_;
    std::vector<std::string> args_; // the command being handled
    size_t entityPos_ = 0;
    GameData gameData_;
    int snapshotsMade_ = 0;
    std::set<int32_t> linked_;
    std::map<int32_t, std::string> configstrings_;
    std::vector<std::string> consoleCommands_;
    std::map<int32_t, std::string> userinfo_;
    std::map<int32_t, UserCommand> commands_;
    std::vector<std::pair<int32_t, std::string>> serverCommands_;
    std::map<int32_t, std::string> dropped_;
    std::string log_;
    std::map<int, int> unhandled_;
    std::map<int, int> callCounts_;
    int32_t levelTime_ = 0;
    uint64_t startMs_ = 0;
    // Bots.
    botlib::BotLibrary bots_;
    std::set<int32_t> clientsInUse_, botClients_;
    std::map<int32_t, size_t> botMessagesRead_;           // per bot, server commands read so far
    std::map<int32_t, std::vector<int32_t>> botSnapshots_; // per bot, the entities it sees this frame
    std::vector<Entity> bspEntities_;
    std::map<int, std::vector<std::vector<uint8_t>>> goalStacks_; // per goal state, its goals' bytes
    std::map<int, std::vector<std::vector<int32_t>>> botCallSamples_;
    size_t consoleCommandsRun_ = 0;
};

} // namespace reluva::idtech3::eliteforce
