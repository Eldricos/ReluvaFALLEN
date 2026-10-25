// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force qagame.qvm system calls (Quake III's game order, the seen
// ones checked against the game's own calls; see
// docs/formats/idtech3/eliteforce-syscalls.md).
enum GameCall : int {
    Print = 0,
    Error_ = 1,
    Milliseconds = 2,
    CvarRegister = 3,
    CvarUpdate = 4,
    CvarSet = 5,
    CvarVariableIntegerValue = 6,
    CvarVariableStringBuffer = 7,
    Argc = 8,
    Argv = 9,
    FsFOpenFile = 10,
    FsRead = 11,
    FsWrite = 12,
    FsFCloseFile = 13,
    SendConsoleCommand = 14,
    LocateGameData = 15,
    DropClient = 16,
    SendServerCommand = 17,
    SetConfigstring = 18,
    GetConfigstring = 19,
    GetUserinfo = 20,
    SetUserinfo = 21,
    GetServerinfo = 22,
    SetBrushModel = 23,
    Trace = 24,
    PointContents = 25,
    InPvs = 26,
    InPvsIgnorePortals = 27,
    AdjustAreaPortalState = 28,
    AreasConnected = 29,
    LinkEntity = 30,
    UnlinkEntity = 31,
    EntitiesInBox = 32,
    EntityContact = 33,
    BotAllocateClient = 34,
    BotFreeClient = 35,
    GetUsercmd = 36,
    GetEntityToken = 37,
    FsGetFileList = 38,
    Ceil = 111, // Quake III's game numbers floor 110 and ceil 111; Elite Force's code uses 111
};



constexpr int32_t kTraceSize = 56; // Quake III's trace_t, as Elite Force's code reads it

} // namespace

GameHost::GameHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), vmFiles_(files) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

Error GameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/qagame.qvm", data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    return Error::success();
}

Error GameHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    return vm_.call(command, args, result);
}

Error GameHost::startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed) {
    if (!map)
        return Error::fail("No map");
    map_ = std::move(map);
    world_ = std::make_unique<CollisionWorld>(*map_);
    areaPortals_ = std::make_unique<AreaPortals>(world_->areaCount());
    closedDoorBlocks_ = 0;
    cvars_.create("cm_noAreas", "0", 0); // 1: every area joined, as the original's
    linked_.clear();
    // What the original server sets before starting the game code.
    cvars_.create("mapname", "", CvarStore::ServerInfo | CvarStore::ReadOnly);
    cvars_.set("mapname", mapName, true);
    cvars_.create("sv_maxclients", "8", CvarStore::ServerInfo | CvarStore::Latch | CvarStore::Archive);
    cvars_.create("g_gametype", "0", CvarStore::ServerInfo | CvarStore::Latch);
    cvars_.create("dedicated", "0", CvarStore::Latch);
    cvars_.create("sv_cheats", "0", CvarStore::ServerInfo | CvarStore::ReadOnly);
    // The engine's bot settings (the game reads bot_enable when starting).
    cvars_.create("bot_enable", "1", 0);
    cvars_.create("bot_thinktime", "100", 0);
    entityText_ = map_->entities;
    entityPos_ = 0;
    levelTime_ = 0;
    int32_t ignored = 0;
    if (Error err = call(Init, {levelTime_, randomSeed, 0}, ignored))
        return err;
    updateInfoConfigstrings();
    // "Run a few frames to allow everything to settle", as the original does.
    for (int i = 0; i < 3; ++i)
        if (Error err = runFrame())
            return err;
    return Error::success();
}

Error GameHost::runFrame() {
    levelTime_ += kFrameMs;
    // The bots think first (and send their commands), then the level runs.
    runBotFrame();
    int32_t ignored = 0;
    Error err = call(RunFrame, {levelTime_}, ignored);
    updateInfoConfigstrings();
    if (!err)
        runQueuedConsoleCommands();
    return err;
}

void GameHost::updateInfoConfigstrings() {
    // Configstrings 0 and 1 are the server's: every server-info cvar
    // (mapname, g_gametype...) and every system-info one, as the client
    // game reads them (it finds the map's name in 0).
    configstrings_[0] = infoString(CvarStore::ServerInfo);
    configstrings_[1] = infoString(CvarStore::SystemInfo);
}

Error GameHost::connectClient(int32_t clientNum, const std::string& userinfo, std::string& refused) {
    refused.clear();
    userinfo_[clientNum] = userinfo;
    clientsInUse_.insert(clientNum);
    int32_t reason = 0;
    if (Error err = call(ClientConnect, {clientNum, 1, 0}, reason)) // first time, not a bot
        return err;
    if (reason)
        refused = vm_.string(reason, 1024);
    return Error::success();
}

Error GameHost::clientCommand(int32_t clientNum, const std::string& text) {
    args_ = splitCommand(text);
    int32_t ignored = 0;
    Error err = call(ClientCommand, {clientNum}, ignored);
    args_.clear();
    return err;
}

Error GameHost::beginClient(int32_t clientNum) {
    int32_t ignored = 0;
    return call(ClientBegin, {clientNum}, ignored);
}

void GameHost::writeUserCommand(Qvm& vm, int32_t address, const UserCommand& cmd) {
    vm.writeInt(address, cmd.serverTime);
    // The buttons and the weapon a byte each, then two bytes of padding.
    vm.writeInt(address + 4, (cmd.buttons & 0xff) | int32_t(cmd.weapon) << 8);
    for (int i = 0; i < 3; ++i)
        vm.writeInt(address + 8 + 4 * i, cmd.angles[size_t(i)]);
    vm.memory()[size_t(address + 20)] = uint8_t(cmd.forward);
    vm.memory()[size_t(address + 21)] = uint8_t(cmd.right);
    vm.memory()[size_t(address + 22)] = uint8_t(cmd.up);
    vm.memory()[size_t(address + 23)] = 0;
}

Error GameHost::clientThink(int32_t clientNum, const UserCommand& cmd) {
    commands_[clientNum] = cmd;
    int32_t ignored = 0;
    return call(ClientThink, {clientNum}, ignored);
}

std::string GameHost::nextEntityToken() {
    const std::string& s = entityText_;
    size_t& i = entityPos_;
    while (true) {
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ' && s[i] != '\0')
            ++i;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            continue;
        }
        break;
    }
    if (i >= s.size() || s[i] == '\0')
        return {};
    std::string token;
    if (s[i] == '"') {
        ++i;
        while (i < s.size() && s[i] != '"' && s[i] != '\0')
            token += s[i++];
        if (i < s.size() && s[i] == '"')
            ++i;
        return token;
    }
    while (i < s.size() && static_cast<unsigned char>(s[i]) > ' ')
        token += s[i++];
    return token;
}

int32_t GameHost::entityNumber(int32_t address) const {
    if (!gameData_.entitySize || address < gameData_.entities)
        return -1;
    const int32_t offset = address - gameData_.entities;
    return offset % gameData_.entitySize == 0 ? offset / gameData_.entitySize : -1;
}

int32_t GameHost::entityAddress(int32_t number) const {
    if (number < 0 || number >= gameData_.count || gameData_.entitySize < ent::SharedEnd)
        return -1;
    return gameData_.entities + number * gameData_.entitySize;
}

Vec3f GameHost::readVec(int32_t address) {
    Vec3f v{};
    if (address)
        for (int i = 0; i < 3; ++i)
            v[size_t(i)] = Qvm::asFloat(vm_.readInt(address + 4 * i));
    return v;
}

void GameHost::writeVec(int32_t address, const Vec3f& v) {
    for (int i = 0; i < 3; ++i)
        vm_.writeInt(address + 4 * i, Qvm::fromFloat(v[size_t(i)]));
}

int32_t GameHost::entityInt(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? 0 : vm_.readInt(a + offset);
}

Vec3f GameHost::entityVec(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? Vec3f{} : readVec(a + offset);
}

int32_t GameHost::clientInt(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !gameData_.clientSize || offset + 4 > gameData_.clientSize)
        return 0;
    return vm_.readInt(gameData_.clients + clientNum * gameData_.clientSize + offset);
}

Vec3f GameHost::clientVec(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !gameData_.clientSize || offset + 12 > gameData_.clientSize)
        return {};
    return readVec(gameData_.clients + clientNum * gameData_.clientSize + offset);
}

std::pair<int, int> GameHost::svFlagsEvidence() {
    // svFlags holds small flag bits (Quake III's SVF_NOCLIENT is 1, for
    // triggers and other entities the client never sees); the other word
    // (linkcount or singleClient, never written by Reluva) stays 0.
    int at208 = 0, at212 = 0;
    for (int32_t n : linked_) {
        const int32_t v208 = entityInt(n, 208), v212 = entityInt(n, 212);
        at208 += v208 > 0 && v208 < 0x10000;
        at212 += v212 > 0 && v212 < 0x10000;
    }
    return {at208, at212};
}

int32_t GameHost::svFlagsOffset() {
    const auto [at208, at212] = svFlagsEvidence();
    return at208 > at212 ? 208 : 212;
}

GameHost::Snapshot GameHost::snapshot(int32_t clientNum) {
    ++snapshotsMade_;
    Snapshot snap;
    snap.serverTime = levelTime_;
    snap.playerState.assign(size_t(kPlayerStateSize), 0);
    const int32_t client = gameData_.clients + clientNum * gameData_.clientSize;
    if (gameData_.clientSize >= kPlayerStateSize && vm_.valid(client, kPlayerStateSize))
        std::memcpy(snap.playerState.data(), &vm_.memory()[size_t(client)], size_t(kPlayerStateSize));
    // The original sends the player state over its network protocol, which
    // carries only some fields: the rest reach the client game as 0, even
    // on a listen server. The word at 48, a timer of the server's (set to
    // 200 on firing, counting down without stopping at 0), is one: the
    // client game draws the phaser's beam only while it's 0 (see
    // eliteforce-syscalls.md).
    for (const int32_t at : {kPlayerStateServerOnly})
        std::memset(&snap.playerState[size_t(at)], 0, 4);
    // Every linked entity but the player's own (the client game makes that
    // one from the player state) and those flagged not to be sent
    // (SVF_NOCLIENT, 1), as the original server does; no visibility
    // culling yet. At most 256, the snapshot's room.
    const int32_t svFlags = svFlagsOffset();
    for (int32_t n : linked_) {
        if (n == clientNum || (entityInt(n, svFlags) & 1))
            continue;
        const int32_t a = entityAddress(n);
        if (a < 0 || snap.entities.size() >= 256)
            continue;
        snap.entities.emplace_back(&vm_.memory()[size_t(a)], &vm_.memory()[size_t(a)] + kEntityStateSize);
    }
    for (const auto& [to, text] : serverCommands_)
        snap.serverCommandSequence += to == clientNum || to == -1;
    // The areas: from the player's eye (taken as 26 units up, the
    // standing view height), each not joined to its area hidden. Outside
    // any area, or with cm_noAreas, none.
    if (world_ && areaPortals_ && !(cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"))) {
        Vec3f eye = entityVec(clientNum, ent::CurrentOrigin);
        eye[2] += 26;
        const int area = world_->leafArea(world_->leafAt(eye));
        if (area >= 0)
            for (int a = 0; a < std::min(world_->areaCount(), 256); ++a)
                if (!areaPortals_->connected(area, a))
                    snap.areaMask[size_t(a >> 3)] |= uint8_t(1 << (a & 7));
    }
    return snap;
}

void GameHost::linkEntity(int32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0)
        return;
    linked_.insert(n);
    // Entities without the shared part (not Elite Force's layout): only
    // recorded.
    if (gameData_.entitySize < ent::SharedEnd || !vm_.checkWrite(address, ent::SharedEnd))
        return;
    // The box it fills in the world, a unit larger all round (moves stop
    // just short of edges); a turned brush model by its farthest corner.
    const Vec3f origin = readVec(address + ent::CurrentOrigin), angles = readVec(address + ent::CurrentAngles);
    const Vec3f mins = readVec(address + ent::Mins), maxs = readVec(address + ent::Maxs);
    Vec3f absMin{}, absMax{};
    if (vm_.readInt(address + ent::BModel) && (angles[0] || angles[1] || angles[2])) {
        float radius = 0;
        for (int i = 0; i < 3; ++i) {
            const float m = std::max(std::abs(mins[size_t(i)]), std::abs(maxs[size_t(i)]));
            radius += m * m;
        }
        radius = std::sqrt(radius);
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = origin[size_t(i)] - radius;
            absMax[size_t(i)] = origin[size_t(i)] + radius;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = origin[size_t(i)] + mins[size_t(i)];
            absMax[size_t(i)] = origin[size_t(i)] + maxs[size_t(i)];
        }
    }
    for (int i = 0; i < 3; ++i) {
        absMin[size_t(i)] -= 1;
        absMax[size_t(i)] += 1;
    }
    writeVec(address + ent::AbsMin, absMin);
    writeVec(address + ent::AbsMax, absMax);
    vm_.writeInt(address + ent::Linked, 1);
    // Its size for the client game, as the original's server encodes it:
    // a brush model's marker, or a solid box's half width, depth below
    // and height above (+32), a byte each; 0 for what isn't solid.
    int32_t solid = 0;
    constexpr int32_t kSolidOrBody = 1 | 0x2000000;
    if (vm_.readInt(address + ent::BModel)) {
        solid = ent::kSolidBrushModel;
    } else if (vm_.readInt(address + ent::Contents) & kSolidOrBody) {
        const auto clampByte = [](float v) { return std::clamp(int32_t(v), 1, 255); };
        solid = (clampByte(maxs[2] + 32) << 16) | (clampByte(-mins[2]) << 8) | clampByte(maxs[0]);
    }
    vm_.writeInt(address + ent::Solid, solid);
}

std::pair<int, int> GameHost::entityAreas(int32_t n) {
    // The areas its box (as last linked) touches, as the original's
    // server keeps them: the first, and a second if it stands between two
    // (a door in its portal).
    std::pair<int, int> areas{-1, -1};
    if (!world_)
        return areas;
    std::vector<int> leafs;
    world_->boxLeafs(entityVec(n, ent::AbsMin), entityVec(n, ent::AbsMax), leafs);
    for (int leaf : leafs) {
        const int area = world_->leafArea(leaf);
        if (area < 0)
            continue;
        if (areas.first != -1 && areas.first != area)
            areas.second = area;
        else
            areas.first = area;
    }
    return areas;
}

bool GameHost::areasConnected(int area1, int area2) const {
    // The original's cm_noAreas: every area counted as joined.
    if (cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"))
        return true;
    return areaPortals_ && areaPortals_->connected(area1, area2);
}

TraceResult GameHost::traceMove(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                                int32_t passEntity, int32_t mask, int32_t& hit, bool entities) {
    TraceResult best;
    hit = kEntityNone;
    if (world_) {
        best = world_->trace(start, end, mins, maxs, 0, mask);
        hit = best.fraction < 1 ? kEntityWorld : kEntityNone;
    }
    // The bounds of the whole move, for skipping entities nowhere near.
    Vec3f moveMin{}, moveMax{};
    for (int i = 0; i < 3; ++i) {
        moveMin[size_t(i)] = std::min(start[size_t(i)], end[size_t(i)]) + mins[size_t(i)] - 1;
        moveMax[size_t(i)] = std::max(start[size_t(i)], end[size_t(i)]) + maxs[size_t(i)] + 1;
    }
    const int32_t passOwner = passEntity >= 0 ? entityInt(passEntity, ent::OwnerNum) : -1;
    for (int32_t n : linked_) {
        if (best.allSolid || !entities)
            break;
        // Not the mover itself, nor what it owns or what owns it.
        if (n == passEntity || (passEntity >= 0 && (entityInt(n, ent::OwnerNum) == passEntity || n == passOwner)))
            continue;
        const int32_t contents = entityInt(n, ent::Contents);
        if (!(contents & mask))
            continue;
        const Vec3f absMin = entityVec(n, ent::AbsMin), absMax = entityVec(n, ent::AbsMax);
        bool apart = false;
        for (int i = 0; i < 3; ++i)
            apart |= absMin[size_t(i)] > moveMax[size_t(i)] || absMax[size_t(i)] < moveMin[size_t(i)];
        if (apart)
            continue;
        const Vec3f origin = entityVec(n, ent::CurrentOrigin);
        TraceResult t;
        if (entityInt(n, ent::BModel) && world_) {
            // Its brushes, where it stands now (turning not yet followed).
            Vec3f s = start, e = end;
            for (int i = 0; i < 3; ++i) {
                s[size_t(i)] -= origin[size_t(i)];
                e[size_t(i)] -= origin[size_t(i)];
            }
            t = world_->trace(s, e, mins, maxs, entityInt(n, ent::ModelIndex), mask);
            for (int i = 0; i < 3; ++i)
                t.endPos[size_t(i)] += origin[size_t(i)];
        } else {
            Vec3f boxMin = entityVec(n, ent::Mins), boxMax = entityVec(n, ent::Maxs);
            for (int i = 0; i < 3; ++i) {
                boxMin[size_t(i)] += origin[size_t(i)];
                boxMax[size_t(i)] += origin[size_t(i)];
            }
            t = CollisionWorld::traceBox(start, end, mins, maxs, boxMin, boxMax, contents);
        }
        if (t.allSolid || t.startSolid) {
            best.allSolid |= t.allSolid;
            best.startSolid = true;
            hit = n;
        }
        if (t.fraction < best.fraction) {
            const bool startSolid = best.startSolid;
            best = t;
            best.startSolid |= startSolid;
            hit = n;
        }
    }
    for (int i = 0; i < 3; ++i)
        best.endPos[size_t(i)] = start[size_t(i)] + best.fraction * (end[size_t(i)] - start[size_t(i)]);
    return best;
}

void GameHost::clipMove(int32_t results, const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                        int32_t passEntity, int32_t mask) {
    if (!vm_.checkWrite(results, kTraceSize))
        return;
    int32_t hit = kEntityNone;
    const TraceResult best = traceMove(start, mins, maxs, end, passEntity, mask, hit);
    // Quake III's trace_t: allsolid, startsolid, fraction, endpos, the
    // plane (normal, dist, type, sign bits), surfaceFlags, contents,
    // entityNum.
    vm_.writeInt(results + 0, best.allSolid);
    vm_.writeInt(results + 4, best.startSolid);
    vm_.writeInt(results + 8, Qvm::fromFloat(best.fraction));
    writeVec(results + 12, best.endPos);
    writeVec(results + 24, best.normal);
    vm_.writeInt(results + 36, Qvm::fromFloat(best.dist));
    int type = 3, signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (best.normal[size_t(i)] == 1 || best.normal[size_t(i)] == -1)
            type = i;
        if (best.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    vm_.memory()[size_t(results + 40)] = uint8_t(type);
    vm_.memory()[size_t(results + 41)] = uint8_t(signs);
    vm_.memory()[size_t(results + 42)] = 0;
    vm_.memory()[size_t(results + 43)] = 0;
    vm_.writeInt(results + 44, best.surfaceFlags);
    vm_.writeInt(results + 48, best.contents);
    vm_.writeInt(results + 52, hit);
}

int32_t GameHost::pointContents(const Vec3f& point, int32_t passEntity) {
    int32_t result = world_ ? world_->pointContents(point, 0) : 0;
    for (int32_t n : linked_) {
        if (n == passEntity)
            continue;
        const Vec3f origin = entityVec(n, ent::CurrentOrigin);
        if (entityInt(n, ent::BModel) && world_) {
            const Vec3f local{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
            if (world_->pointContents(local, entityInt(n, ent::ModelIndex)))
                result |= entityInt(n, ent::Contents);
        } else {
            const Vec3f mins = entityVec(n, ent::Mins), maxs = entityVec(n, ent::Maxs);
            bool inside = true;
            for (int i = 0; i < 3; ++i)
                inside &= point[size_t(i)] >= origin[size_t(i)] + mins[size_t(i)] &&
                          point[size_t(i)] <= origin[size_t(i)] + maxs[size_t(i)];
            if (inside)
                result |= entityInt(n, ent::Contents);
        }
    }
    return result;
}

std::string GameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

int32_t GameHost::systemCall(Qvm& vm, int number, const int32_t* a) {
    if (trace)
        trace(number, a);
    ++callCounts_[number];
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case Print: {
        const std::string text = str(0, 4096);
        // Kept (the last 4 KB) as well as handed on.
        log_ += text;
        if (log_.size() > 8192)
            log_.erase(0, log_.size() - 4096);
        if (print)
            print(text);
        return 0;
    }
    case Error_: vm.abort("qagame.qvm: " + str(0)); return 0;
    case Milliseconds: return levelTime_;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
    case CvarVariableIntegerValue: return cvars_.integer(str(0, 256));
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case Argc: return int32_t(args_.size());
    case Argv: vm.writeString(a[1], a[0] >= 0 && size_t(a[0]) < args_.size() ? args_[size_t(a[0])] : "", a[2]); return 0;
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case FsGetFileList: return vmFiles_.list(vm, a[0], a[1], a[2], a[3]);
    case SendConsoleCommand: consoleCommands_.push_back(str(1, 4096)); return 0;
    case LocateGameData:
        gameData_ = {a[0], a[1], a[2], a[3], a[4]};
        return 0;
    case SendServerCommand: serverCommands_.emplace_back(a[0], str(1, 8192)); return 0;
    case DropClient: dropped_[a[0]] = str(1, 1024); return 0;
    case GetUserinfo: {
        const auto it = userinfo_.find(a[0]);
        vm.writeString(a[1], it == userinfo_.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case SetUserinfo: userinfo_[a[0]] = str(1, 1024); return 0;
    case GetUsercmd: { // (clientNum, usercmd_t*): the last command, or an empty one
        const auto it = commands_.find(a[0]);
        const UserCommand cmd = it == commands_.end() ? UserCommand{levelTime_} : it->second;
        if (!vm.checkWrite(a[1], 24))
            return 0;
        writeUserCommand(vm, a[1], cmd);
        return 0;
    }
    case SetConfigstring: configstrings_[a[0]] = str(1, 8192); return 0;
    case GetConfigstring: {
        const auto it = configstrings_.find(a[0]);
        vm.writeString(a[1], it == configstrings_.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case GetServerinfo: vm.writeString(a[0], infoString(CvarStore::ServerInfo), a[1]); return 0;
    case LinkEntity: linkEntity(a[0]); return 0;
    case UnlinkEntity:
        if (const int32_t n = entityNumber(a[0]); n >= 0) {
            linked_.erase(n);
            if (gameData_.entitySize >= ent::SharedEnd)
                vm.writeInt(a[0] + ent::Linked, 0);
        }
        return 0;
    case SetBrushModel: { // (ent, "*N"): one of the map's brush models
        const std::string name = str(1, 64);
        if (name.size() < 2 || name[0] != '*') {
            vm.abort("SetBrushModel: '" + name + "' isn't a brush model");
            return 0;
        }
        const int model = std::atoi(name.c_str() + 1);
        Vec3f mins{}, maxs{};
        if (!world_ || model <= 0 || !world_->modelBounds(model, mins, maxs)) {
            vm.abort("SetBrushModel: the map has no model " + name);
            return 0;
        }
        if (gameData_.entitySize < ent::SharedEnd || !vm.checkWrite(a[0], ent::SharedEnd))
            return 0;
        vm.writeInt(a[0] + ent::ModelIndex, model);
        vm.writeInt(a[0] + ent::BModel, 1);
        writeVec(a[0] + ent::Mins, mins);
        writeVec(a[0] + ent::Maxs, maxs);
        vm.writeInt(a[0] + ent::Contents, -1); // whatever its brushes hold
        linkEntity(a[0]);
        return 0;
    }
    case Trace: {
        const Vec3f zero{};
        clipMove(a[0], readVec(a[1]), a[2] ? readVec(a[2]) : zero, a[3] ? readVec(a[3]) : zero, readVec(a[4]), a[5],
              a[6]);
        return 0;
    }
    case PointContents: return pointContents(readVec(a[0]), a[1]);
    case InPvs:
    case InPvsIgnorePortals: {
        if (!world_)
            return 1;
        const int l1 = world_->leafAt(readVec(a[0])), l2 = world_->leafAt(readVec(a[1]));
        if (l1 < 0 || l2 < 0)
            return 0;
        if (!map_->clusterVisible(map_->leafs[size_t(l1)].cluster, map_->leafs[size_t(l2)].cluster))
            return 0;
        // Not through a closed door, unless asked to ignore portals.
        if (number == InPvsIgnorePortals || areasConnected(world_->leafArea(l1), world_->leafArea(l2)))
            return 1;
        ++closedDoorBlocks_;
        return 0;
    }
    case AdjustAreaPortalState: // (entity, open): the portal its door stands in
        if (const int32_t n = entityNumber(a[0]); n >= 0 && areaPortals_ && gameData_.entitySize >= ent::SharedEnd) {
            const auto [area1, area2] = entityAreas(n);
            if (area2 != -1)
                areaPortals_->adjust(area1, area2, a[1] != 0);
        }
        return 0;
    case AreasConnected: return areasConnected(a[0], a[1]);
    case EntitiesInBox: { // (mins, maxs, list, max) -> how many
        const Vec3f mins = readVec(a[0]), maxs = readVec(a[1]);
        int32_t count = 0;
        for (int32_t n : linked_) {
            if (count >= a[3])
                break;
            const Vec3f absMin = entityVec(n, ent::AbsMin), absMax = entityVec(n, ent::AbsMax);
            bool apart = false;
            for (int i = 0; i < 3; ++i)
                apart |= absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)];
            if (!apart)
                vm.writeInt(a[2] + 4 * count++, n);
        }
        return count;
    }
    case EntityContact: { // (mins, maxs, ent): whether the box touches it
        const Vec3f mins = readVec(a[0]), maxs = readVec(a[1]);
        const int32_t n = entityNumber(a[2]);
        if (n < 0)
            return 0;
        const Vec3f origin = entityVec(n, ent::CurrentOrigin);
        if (entityInt(n, ent::BModel) && world_) {
            const Vec3f at{-origin[0], -origin[1], -origin[2]};
            return world_->trace(at, at, mins, maxs, entityInt(n, ent::ModelIndex), -1).startSolid;
        }
        const Vec3f absMin = entityVec(n, ent::AbsMin), absMax = entityVec(n, ent::AbsMax);
        for (int i = 0; i < 3; ++i)
            if (absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)])
                return 0;
        return 1;
    }
    case GetEntityToken: {
        const std::string token = nextEntityToken();
        vm.writeString(a[0], token, a[1]);
        return token.empty() ? 0 : 1;
    }
    case Ceil: return Qvm::fromFloat(std::ceil(Qvm::asFloat(a[0])));
    default:
        if (number == BotAllocateClient || number == BotFreeClient || number >= 200)
            return botCall(vm, number, a);
        ++unhandled_[number];
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
