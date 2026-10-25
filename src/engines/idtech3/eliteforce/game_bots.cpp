// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot library's system calls for Elite Force's game logic: bots
// joining (BotAllocateClient), the library's own calls (200 on: setting
// up, the level's .aas map, the entities, the bots' commands), the map
// queries (300 on), the elementary actions (400 on) and the AI (500 on).
// Quake III's numbering, which Elite Force's game uses in the ranges
// 200-211, 303-318, 401-427 and 500-572 (see eliteforce-syscalls.md);
// where Elite Force's may differ it's marked. The structures are Quake
// III's (botlib.h, be_aas.h, be_ai_*.h of the GPL ioquake3 source), read
// for their layout; the code is new.

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

enum BotCall : int {
    BotAllocateClient = 34,
    BotFreeClient = 35,
    // The library.
    LibSetup = 200,
    LibShutdown = 201,
    LibVarSet = 202,
    LibVarGet = 203,
    LibAddGlobalDefine = 204,
    LibStartFrame = 205,
    LibLoadMap = 206,
    LibUpdateEntity = 207,
    LibTest = 208,
    GetSnapshotEntity = 209,
    GetConsoleMessage = 210,
    LibUserCommand = 211,
    // The map.
    AasEntityInfo = 303,
    AasInitialized = 304,
    AasPresenceTypeBoundingBox = 305,
    AasTime = 306,
    AasPointAreaNum = 307,
    AasTraceAreas = 308,
    AasPointContents = 309,
    AasNextBspEntity = 310,
    AasValueForBspEpairKey = 311,
    AasVectorForBspEpairKey = 312,
    AasFloatForBspEpairKey = 313,
    AasIntForBspEpairKey = 314,
    AasAreaReachability = 315,
    AasAreaTravelTimeToGoalArea = 316,
    AasSwimming = 317,
    AasPredictClientMovement = 318,
    // Elementary actions: Elite Force's order (found from its calls: what
    // each passes and where; see eliteforce-syscalls.md), with an alt
    // attack at the end.
    EaSay = 400,
    EaSayTeam = 401,
    EaUseItem = 402,
    EaDropItem = 403,
    EaUseInv = 404,
    EaDropInv = 405,
    EaGesture = 406,
    EaCommand = 407,
    EaSelectWeapon = 408,
    EaTalk = 409,
    EaAttack = 410,
    EaUse = 411,
    EaRespawn = 412,
    EaJump = 413,
    EaDelayedJump = 414,
    EaCrouch = 415,
    EaMoveUp = 416,
    EaMoveDown = 417,
    EaMoveForward = 418,
    EaMoveBack = 419,
    EaMoveLeft = 420,
    EaMoveRight = 421,
    EaMove = 422,
    EaView = 423,
    EaEndRegular = 424,
    EaGetInput = 425,
    EaResetInput = 426,
    EaAltAttack = 427,
    // AI.
    AiLoadCharacter = 500,
    AiFreeCharacter = 501,
    AiCharacteristicFloat = 502,
    AiCharacteristicBFloat = 503,
    AiCharacteristicInteger = 504,
    AiCharacteristicBInteger = 505,
    AiCharacteristicString = 506,
    AiAllocChatState = 507,
    AiFreeChatState = 508,
    AiQueueConsoleMessage = 509,
    AiRemoveConsoleMessage = 510,
    AiNextConsoleMessage = 511,
    AiNumConsoleMessage = 512,
    AiInitialChat = 513,
    AiReplyChat = 514,
    AiChatLength = 515,
    AiEnterChat = 516,
    AiStringContains = 517,
    AiFindMatch = 518,
    AiMatchVariable = 519,
    AiUnifyWhiteSpaces = 520,
    AiReplaceSynonyms = 521,
    AiLoadChatFile = 522,
    AiSetChatGender = 523,
    AiSetChatName = 524,
    AiResetGoalState = 525,
    AiResetAvoidGoals = 526,
    AiPushGoal = 527,
    AiPopGoal = 528,
    AiEmptyGoalStack = 529,
    AiDumpAvoidGoals = 530,
    AiDumpGoalStack = 531,
    AiGoalName = 532,
    AiGetTopGoal = 533,
    AiGetSecondGoal = 534,
    AiChooseLtgItem = 535,
    AiChooseNbgItem = 536,
    AiTouchingGoal = 537,
    AiItemGoalInVisButNotVisible = 538,
    AiGetLevelItemGoal = 539,
    AiAvoidGoalTime = 540,
    AiInitLevelItems = 541,
    AiUpdateEntityItems = 542,
    AiLoadItemWeights = 543,
    AiFreeItemWeights = 544,
    AiSaveGoalFuzzyLogic = 545,
    AiAllocGoalState = 546,
    AiFreeGoalState = 547,
    AiResetMoveState = 548,
    AiMoveToGoal = 549,
    AiMoveInDirection = 550,
    AiResetAvoidReach = 551,
    AiResetLastAvoidReach = 552,
    AiReachabilityArea = 553,
    AiMovementViewTarget = 554,
    AiAllocMoveState = 555,
    AiFreeMoveState = 556,
    AiInitMoveState = 557,
    AiChooseBestFightWeapon = 558,
    AiGetWeaponInfo = 559,
    AiLoadWeaponWeights = 560,
    AiAllocWeaponState = 561,
    AiFreeWeaponState = 562,
    AiResetWeaponState = 563,
    AiGeneticParentsAndChildSelection = 564,
    AiInterbreedGoalFuzzyLogic = 565,
    AiMutateGoalFuzzyLogic = 566,
    AiGetNextCampSpotGoal = 567,
    AiGetMapLocationGoal = 568,
    AiNumInitialChats = 569,
    AiGetChatMessage = 570,
    AiRemoveFromAvoidGoals = 571,
    AiPredictVisiblePosition = 572,
};

// Structure sizes and layouts (Quake III's).
constexpr int32_t kEntityStateSize = 108; // bot_entitystate_t
constexpr int32_t kEntityInfoSize = 140;  // aas_entityinfo_t
constexpr int32_t kInputSize = 40;        // bot_input_t
constexpr int32_t kGoalSize = 56;         // bot_goal_t: origin, area, mins, maxs, entity, number, flags, item
constexpr int32_t kMoveResultSize = 52;   // bot_moveresult_t
constexpr int32_t kUserCommandSize = 24;
// Bodies, water, slime and lava (Quake III's contents).
constexpr int32_t kContentsLiquid = 8 | 16 | 32;
constexpr int32_t kInitMoveSize = 68;    // bot_initmove_t
constexpr int32_t kWeaponInfoSize = 552; // weaponinfo_t: strings of 80
constexpr int32_t kInventorySize = 256;  // the bots' inventory counts
constexpr int32_t kMatchSize = 328;      // bot_match_t: text, type, subtype, 8 x (offset byte, length)


} // namespace

Error GameHost::serverCommand(const std::string& text, bool* handled) {
    args_ = splitCommand(text);
    int32_t result = 0;
    Error err = call(ConsoleCommand, {}, result);
    args_.clear();
    if (handled)
        *handled = result != 0;
    return err;
}

void GameHost::runQueuedConsoleCommands() {
    // What the game asked the server to run (SendConsoleCommand): the
    // engine's own commands aren't here yet, so each goes back to the game
    // as a console command (addbot, kick...), as the original does with
    // commands it doesn't know.
    while (consoleCommandsRun_ < consoleCommands_.size()) {
        const std::string text = consoleCommands_[consoleCommandsRun_++];
        for (const std::string& command : splitCommands(text)) {
            if (command.empty())
                continue;
            bool handled = false;
            serverCommand(command, &handled);
            if (!handled && print)
                print("(console command not run: " + command + ")\n");
        }
    }
}

std::vector<int32_t> GameHost::botSnapshotEntities(int32_t clientNum) {
    // What the bot would get in a snapshot: linked entities the client
    // game is sent (not SVF_NOCLIENT), but itself, that its eyes could see
    // (the PVS); brush models always.
    std::vector<int32_t> out;
    const Vec3f origin = entityVec(clientNum, ent::CurrentOrigin);
    const Vec3f eye{origin[0], origin[1], origin[2] + 26};
    const int32_t svFlags = svFlagsOffset();
    const int eyeLeaf = world_ ? world_->leafAt(eye) : -1;
    for (const int32_t n : linked_) {
        if (n == clientNum || (entityInt(n, svFlags) & 1))
            continue;
        if (eyeLeaf >= 0 && !entityInt(n, ent::BModel)) {
            const int leaf = world_->leafAt(entityVec(n, ent::CurrentOrigin));
            if (leaf >= 0 && !map_->clusterVisible(map_->leafs[size_t(eyeLeaf)].cluster, map_->leafs[size_t(leaf)].cluster))
                continue;
        }
        out.push_back(n);
        if (out.size() >= 256)
            break;
    }
    return out;
}

void GameHost::runBotFrame() {
    if (!bots_.isSetup() || botClients_.empty())
        return;
    for (const int32_t c : botClients_)
        botSnapshots_[c] = botSnapshotEntities(c);
    int32_t ignored = 0;
    call(BotAiStartFrame, {levelTime_}, ignored);
}

int32_t GameHost::botCall(Qvm& vm, int number, const int32_t* a) {
    auto& samples = botCallSamples_[number];
    if (samples.size() < 3)
        samples.emplace_back(a, a + 6);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto flt = [&](int i) { return Qvm::asFloat(a[i]); };
    auto vec = [&](int32_t address) { return readVec(address); };
    auto fail = [&]() {
        ++unhandled_[number];
        return 0;
    };
    botlib::BotLibrary& b = bots_;
    botlib::AasWorld& aas = b.aas();
    // bot_goal_t: origin, area, mins, maxs, entity, number, flags, item info.
    auto readGoal = [&](int32_t p, botlib::BotGoal& g) {
        if (!vm.valid(p, kGoalSize))
            return false;
        g.origin = vec(p);
        g.area = vm.readInt(p + 12);
        g.mins = vec(p + 16);
        g.maxs = vec(p + 28);
        g.entity = vm.readInt(p + 40);
        g.number = vm.readInt(p + 44);
        g.flags = vm.readInt(p + 48);
        g.itemInfo = vm.readInt(p + 52);
        return true;
    };
    auto writeGoal = [&](int32_t p, const botlib::BotGoal& g) {
        if (!vm.checkWrite(p, kGoalSize))
            return 0;
        writeVec(p, g.origin);
        vm.writeInt(p + 12, g.area);
        writeVec(p + 16, g.mins);
        writeVec(p + 28, g.maxs);
        vm.writeInt(p + 40, g.entity);
        vm.writeInt(p + 44, g.number);
        vm.writeInt(p + 48, g.flags);
        vm.writeInt(p + 52, g.itemInfo);
        return 1;
    };
    // bot_match_t: the text (256), type, subtype, then each variable's
    // offset (a signed byte) and length (an int after 3 bytes' padding).
    auto writeMatch = [&](int32_t p, const botlib::BotMatch& m) {
        std::memset(&vm.memory()[size_t(p)], 0, size_t(kMatchSize));
        vm.writeString(p, m.text, 256);
        vm.writeInt(p + 256, m.type);
        vm.writeInt(p + 260, m.subtype);
        for (int i = 0; i < botlib::BotMatch::kVariables; ++i) {
            vm.memory()[size_t(p + 264 + 8 * i)] = uint8_t(int8_t(m.offset[size_t(i)]));
            vm.writeInt(p + 268 + 8 * i, m.length[size_t(i)]);
        }
    };
    auto readMatch = [&](int32_t p, botlib::BotMatch& m) {
        if (!vm.valid(p, kMatchSize))
            return false;
        m.text = vm.string(p, 256);
        m.type = vm.readInt(p + 256);
        m.subtype = vm.readInt(p + 260);
        for (int i = 0; i < botlib::BotMatch::kVariables; ++i) {
            m.offset[size_t(i)] = int8_t(vm.memory()[size_t(p + 264 + 8 * i)]);
            m.length[size_t(i)] = vm.readInt(p + 268 + 8 * i);
        }
        return true;
    };
    auto inventory = [&](int32_t p) {
        std::vector<int> counts(size_t(kInventorySize), 0);
        if (vm.valid(p, 4 * kInventorySize))
            for (int i = 0; i < kInventorySize; ++i)
                counts[size_t(i)] = vm.readInt(p + 4 * i);
        return counts;
    };
    auto writeWeaponInfo = [&](int32_t p, const botlib::WeaponInfo& w) {
        // valid, number, name, model, level, weaponindex, flags,
        // projectile, numprojectiles, spreads, speed, acceleration,
        // recoil, offset, angleoffset, extrazvelocity, ammo amount and
        // index, activate, reload, spinup, spindown, then the projectile:
        // name, model, flags, gravity, damage, radius, visdamage,
        // damagetype, healthinc, push, detonation, bounce, bouncefric,
        // bouncestop. Strings are 80 bytes.
        std::memset(&vm.memory()[size_t(p)], 0, size_t(kWeaponInfoSize));
        int32_t at = p;
        auto i32 = [&](int v) { vm.writeInt(at, v), at += 4; };
        auto f32 = [&](float v) { vm.writeInt(at, Qvm::fromFloat(v)), at += 4; };
        auto text = [&](const std::string& t) { vm.writeString(at, t, 80), at += 80; };
        auto v3 = [&](const Vec3f& v) { writeVec(at, v), at += 12; };
        i32(w.valid), i32(w.number), text(w.name), text(w.model), i32(w.level), i32(w.weaponIndex), i32(w.flags);
        text(w.projectile), i32(w.numProjectiles), f32(w.hspread), f32(w.vspread), f32(w.speed), f32(w.acceleration);
        v3(w.recoil), v3(w.offset), v3(w.angleOffset), f32(w.extraZVelocity), i32(w.ammoAmount), i32(w.ammoIndex);
        f32(w.activate), f32(w.reload), f32(w.spinUp), f32(w.spinDown);
        text(w.proj.name), text(w.proj.model), i32(w.proj.flags), f32(w.proj.gravity), i32(w.proj.damage);
        f32(w.proj.radius), i32(w.proj.visDamage), i32(w.proj.damageType), i32(w.proj.healthInc), f32(w.proj.push);
        f32(w.proj.detonation), f32(w.proj.bounce), f32(w.proj.bounceFric), f32(w.proj.bounceStop);
    };

    switch (number) {
    case BotAllocateClient: { // -> a free client number for a bot, -1 if none
        const int32_t max = std::max(1, cvars_.integer("sv_maxclients"));
        for (int32_t c = 0; c < max; ++c)
            if (!clientsInUse_.count(c)) {
                clientsInUse_.insert(c);
                botClients_.insert(c);
                botMessagesRead_[c] = serverCommands_.size();
                return c;
            }
        return -1;
    }
    case BotFreeClient:
        clientsInUse_.erase(a[0]);
        botClients_.erase(a[0]);
        botSnapshots_.erase(a[0]);
        return 0;
    case LibSetup:
        b.readFile = [this](const std::string& path, std::vector<uint8_t>& data) { return !files_.read(path, data); };
        b.clientCommand = [this](int client, const std::string& text) { clientCommand(client, text); };
        b.print = [this](const std::string& text) {
            if (print)
                print(text);
            else
                log_ += text;
        };
        return b.setup();
    case LibShutdown: return b.shutdown();
    case LibVarSet: b.setLibVar(str(0, 256), str(1, 256)); return 0;
    case LibVarGet: vm.writeString(a[1], b.libVar(str(0, 256)), a[2]); return 0;
    case LibAddGlobalDefine: b.addGlobalDefine(str(0, 1024)); return 0;
    case LibStartFrame: return b.startFrame(flt(0));
    case LibLoadMap: {
        if (map_)
            parseEntities(map_->entities, bspEntities_);
        b.entityCount = int(bspEntities_.size());
        b.entityKey = [this](int e, const std::string& key, std::string& value) {
            if (e < 1 || size_t(e) > bspEntities_.size() || !bspEntities_[size_t(e) - 1].has(key))
                return false;
            value = bspEntities_[size_t(e) - 1].get(key);
            return true;
        };
        b.trace = [this](const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end, int pass,
                         int mask) {
            int32_t hit = kEntityNone;
            const TraceResult t = traceMove(start, mins, maxs, end, pass, mask, hit);
            return botlib::BotLibrary::Trace{t.fraction, t.endPos, t.startSolid, hit, t.normal};
        };
        b.pointContents = [this](const Vec3f& p) { return world_ ? world_->pointContents(p, 0) : 0; };
        b.modelBounds = [this](int model, Vec3f& mins, Vec3f& maxs) {
            return world_ && world_->modelBounds(model, mins, maxs);
        };
        b.moverOrigin = [this](int model, Vec3f& origin) {
            // The linked brush entity with that model, as the original's
            // bot library finds a lift.
            for (const int32_t n : linked_)
                if (entityInt(n, ent::BModel) && entityInt(n, ent::ModelIndex) == model) {
                    origin = entityVec(n, ent::CurrentOrigin);
                    return true;
                }
            return false;
        };
        return b.loadMap(str(0, 256));
    }
    case LibUpdateEntity: {
        if (!a[1])
            return b.updateEntity(a[0], nullptr);
        if (!vm.valid(a[1], kEntityStateSize))
            return botlib::blerr::InvalidEntityNumber;
        // type, flags, origin, angles, old_origin, mins, maxs, then ints.
        botlib::BotEntityState s;
        const int32_t p = a[1];
        s.type = vm.readInt(p);
        s.flags = vm.readInt(p + 4);
        s.origin = vec(p + 8);
        s.angles = vec(p + 20);
        s.oldOrigin = vec(p + 32);
        s.mins = vec(p + 44);
        s.maxs = vec(p + 56);
        int32_t* ints[] = {&s.groundEnt, &s.solid, &s.modelIndex, &s.modelIndex2, &s.frame, &s.event,
                           &s.eventParm, &s.powerups, &s.weapon, &s.legsAnim, &s.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            *ints[i] = vm.readInt(p + 68 + 4 * int32_t(i));
        return b.updateEntity(a[0], &s);
    }
    case LibTest: return 0;
    case GetSnapshotEntity: { // (client, sequence) -> entity number, -1 past the end
        const auto it = botSnapshots_.find(a[0]);
        if (it == botSnapshots_.end() || a[1] < 0 || size_t(a[1]) >= it->second.size())
            return -1;
        return it->second[size_t(a[1])];
    }
    case GetConsoleMessage: { // (client, buffer, size) -> whether there was one
        size_t& read = botMessagesRead_[a[0]];
        while (read < serverCommands_.size()) {
            const auto& [to, text] = serverCommands_[read++];
            if (to == a[0] || to == -1) {
                vm.writeString(a[1], text, a[2]);
                return 1;
            }
        }
        return 0;
    }
    case LibUserCommand: { // (client, usercmd_t*): the bot's command for this frame
        if (!vm.valid(a[1], kUserCommandSize))
            return 0;
        UserCommand cmd;
        cmd.serverTime = vm.readInt(a[1]);
        const int32_t bw = vm.readInt(a[1] + 4);
        cmd.buttons = bw & 0xff;
        cmd.weapon = uint8_t(bw >> 8);
        for (int i = 0; i < 3; ++i)
            cmd.angles[size_t(i)] = vm.readInt(a[1] + 8 + 4 * i);
        cmd.forward = int8_t(vm.memory()[size_t(a[1] + 20)]);
        cmd.right = int8_t(vm.memory()[size_t(a[1] + 21)]);
        cmd.up = int8_t(vm.memory()[size_t(a[1] + 22)]);
        clientThink(a[0], cmd);
        return 0;
    }

    case AasEntityInfo: {
        if (!vm.checkWrite(a[1], kEntityInfoSize))
            return 0;
        const botlib::BotEntityInfo& e = b.entityInfo(a[0]);
        const int32_t p = a[1];
        vm.writeInt(p, e.valid);
        vm.writeInt(p + 4, e.type);
        vm.writeInt(p + 8, e.flags);
        vm.writeInt(p + 12, Qvm::fromFloat(e.lastTime));
        vm.writeInt(p + 16, Qvm::fromFloat(e.updateTime));
        vm.writeInt(p + 20, e.number);
        const Vec3f* vecs[] = {&e.origin, &e.angles, &e.oldOrigin, &e.lastVisOrigin, &e.mins, &e.maxs};
        for (size_t i = 0; i < std::size(vecs); ++i)
            writeVec(p + 24 + 12 * int32_t(i), *vecs[i]);
        const int ints[] = {e.groundEnt, e.solid, e.modelIndex, e.modelIndex2, e.frame, e.event,
                            e.eventParm, e.powerups, e.weapon, e.legsAnim, e.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            vm.writeInt(p + 96 + 4 * int32_t(i), ints[i]);
        return 0;
    }
    case AasInitialized: return b.aasInitialized();
    case AasPresenceTypeBoundingBox: {
        Vec3f mins{}, maxs{};
        botlib::AasWorld::presenceBox(a[0], mins, maxs);
        if (vm.checkWrite(a[1], 12) && vm.checkWrite(a[2], 12)) {
            writeVec(a[1], mins);
            writeVec(a[2], maxs);
        }
        return 0;
    }
    case AasTime: return Qvm::fromFloat(b.time());
    case AasPointAreaNum: return aas.pointArea(vec(a[0]));
    case AasTraceAreas: { // (start, end, areas*, points*, max) -> count
        std::vector<int> areas;
        std::vector<Vec3f> points;
        const int n = aas.traceAreas(vec(a[0]), vec(a[1]), areas, a[3] ? &points : nullptr, a[4]);
        if (!vm.checkWrite(a[2], 4 * n) || (a[3] && !vm.checkWrite(a[3], 12 * n)))
            return 0;
        for (int i = 0; i < n; ++i) {
            vm.writeInt(a[2] + 4 * i, areas[size_t(i)]);
            if (a[3])
                writeVec(a[3] + 12 * i, points[size_t(i)]);
        }
        return n;
    }
    case AasPointContents: return world_ ? world_->pointContents(vec(a[0]), 0) : 0;
    case AasNextBspEntity: {
        const int next = a[0] + 1;
        return next >= 1 && size_t(next) <= bspEntities_.size() ? next : 0;
    }
    case AasValueForBspEpairKey:
    case AasVectorForBspEpairKey:
    case AasFloatForBspEpairKey:
    case AasIntForBspEpairKey: {
        // Entities from 1, in the map's order.
        const int e = a[0] - 1;
        const std::string key = str(1, 256);
        const bool found = e >= 0 && size_t(e) < bspEntities_.size() && bspEntities_[size_t(e)].has(key);
        const std::string value = found ? bspEntities_[size_t(e)].get(key) : std::string();
        if (number == AasValueForBspEpairKey) {
            vm.writeString(a[2], value, a[3]);
        } else if (number == AasVectorForBspEpairKey) {
            float v[3] = {0, 0, 0};
            std::sscanf(value.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
            if (vm.checkWrite(a[2], 12))
                writeVec(a[2], {v[0], v[1], v[2]});
        } else if (vm.checkWrite(a[2], 4)) {
            vm.writeInt(a[2], number == AasFloatForBspEpairKey ? Qvm::fromFloat(float(std::atof(value.c_str())))
                                                               : std::atoi(value.c_str()));
        }
        return found;
    }
    case AasAreaReachability: return aas.validArea(a[0]) ? aas.settings(a[0]).numReach : 0;
    case AasAreaTravelTimeToGoalArea: return aas.travelTime(a[0], vec(a[1]), a[2], a[3]);
    case AasSwimming: {
        Vec3f p = vec(a[0]);
        p[2] -= 2;
        return world_ && (world_->pointContents(p, 0) & kContentsLiquid) ? 1 : 0;
    }
    case AasPredictClientMovement: {
        // (move*, entity, origin, presencetype, onground, velocity, cmdmove,
        // cmdframes, maxframes, frametime, stopevent, stoparea, visualize)
        botlib::BotClientMove m;
        const bool ok = b.predictClientMovement(m, a[1], vec(a[2]), a[3], a[4] != 0, vec(a[5]), vec(a[6]), a[7], a[8],
                                                flt(9), a[10], a[11]);
        // aas_clientmove_t: endpos, endarea, velocity, the trace
        // (startsolid, fraction, endpos, ent, lastarea, area, planenum),
        // presencetype, stopevent, endcontents, time, frames.
        if (!vm.checkWrite(a[0], 84))
            return 0;
        const int32_t p = a[0];
        std::memset(&vm.memory()[size_t(p)], 0, 84);
        writeVec(p, m.endPos);
        vm.writeInt(p + 12, m.endArea);
        writeVec(p + 16, m.velocity);
        vm.writeInt(p + 28, m.traceStartSolid);
        vm.writeInt(p + 32, Qvm::fromFloat(m.traceFraction));
        writeVec(p + 36, m.traceEnd);
        vm.writeInt(p + 48, m.traceEntity);
        vm.writeInt(p + 64, m.presence);
        vm.writeInt(p + 68, m.stopEvent);
        vm.writeInt(p + 72, m.endContents);
        vm.writeInt(p + 76, Qvm::fromFloat(m.time));
        vm.writeInt(p + 80, m.frames);
        return ok;
    }

    case EaSay: b.say(a[0], str(1)); return 0;
    case EaSayTeam: b.sayTeam(a[0], str(1)); return 0;
    case EaCommand: b.command(a[0], str(1)); return 0;
    case EaUseItem: b.command(a[0], "use " + str(1)); return 0;
    case EaDropItem: b.command(a[0], "drop " + str(1)); return 0;
    case EaUseInv: b.command(a[0], "invuse " + str(1)); return 0;
    case EaDropInv: b.command(a[0], "invdrop " + str(1)); return 0;
    case EaGesture: b.input(a[0]).actionFlags |= botlib::action::Gesture; return 0;
    case EaTalk: b.input(a[0]).actionFlags |= botlib::action::Talk; return 0;
    case EaAttack: b.input(a[0]).actionFlags |= botlib::action::Attack; return 0;
    case EaAltAttack: b.input(a[0]).actionFlags |= botlib::action::AltAttack; return 0;
    case EaUse: b.input(a[0]).actionFlags |= botlib::action::Use; return 0;
    case EaRespawn: b.input(a[0]).actionFlags |= botlib::action::Respawn; return 0;
    case EaCrouch: b.input(a[0]).actionFlags |= botlib::action::Crouch; return 0;
    case EaMoveUp: b.input(a[0]).actionFlags |= botlib::action::MoveUp; return 0;
    case EaMoveDown: b.input(a[0]).actionFlags |= botlib::action::MoveDown; return 0;
    case EaMoveForward: b.input(a[0]).actionFlags |= botlib::action::MoveForward; return 0;
    case EaMoveBack: b.input(a[0]).actionFlags |= botlib::action::MoveBack; return 0;
    case EaMoveLeft: b.input(a[0]).actionFlags |= botlib::action::MoveLeft; return 0;
    case EaMoveRight: b.input(a[0]).actionFlags |= botlib::action::MoveRight; return 0;
    case EaSelectWeapon: b.input(a[0]).weapon = a[1]; return 0;
    case EaJump: b.jump(a[0]); return 0;
    case EaDelayedJump: b.delayedJump(a[0]); return 0;
    case EaMove: b.move(a[0], vec(a[1]), flt(2)); return 0;
    case EaView: b.input(a[0]).viewAngles = vec(a[1]); return 0;
    case EaEndRegular: return 0;
    case EaGetInput: {
        if (!vm.checkWrite(a[2], kInputSize))
            return 0;
        const botlib::BotInput in = b.getInput(a[0], flt(1));
        vm.writeInt(a[2], Qvm::fromFloat(in.thinkTime));
        writeVec(a[2] + 4, in.dir);
        vm.writeInt(a[2] + 16, Qvm::fromFloat(in.speed));
        writeVec(a[2] + 20, in.viewAngles);
        vm.writeInt(a[2] + 32, in.actionFlags);
        vm.writeInt(a[2] + 36, in.weapon);
        return 0;
    }
    case EaResetInput: b.resetInput(a[0]); return 0;

    case AiLoadCharacter: {
        std::string log;
        const int h = b.characters().load(b.scriptLoader(), str(0, 256), flt(1), &log);
        if (!h && print)
            print("couldn't load bot character " + str(0, 256) + ":\n" + log);
        return h;
    }
    case AiFreeCharacter: b.characters().free(a[0]); return 0;
    case AiCharacteristicFloat: return Qvm::fromFloat(b.characters().floatValue(a[0], a[1]));
    case AiCharacteristicBFloat: return Qvm::fromFloat(b.characters().boundedFloat(a[0], a[1], flt(2), flt(3)));
    case AiCharacteristicInteger: return b.characters().integer(a[0], a[1]);
    case AiCharacteristicBInteger: return b.characters().boundedInteger(a[0], a[1], a[2], a[3]);
    case AiCharacteristicString: vm.writeString(a[2], b.characters().string(a[0], a[1]), a[3]); return 0;

    // Chat.
    case AiAllocChatState: return b.chat.allocState();
    case AiFreeChatState: b.chat.freeState(a[0]); return 0;
    case AiLoadChatFile: // (chatstate, file, name)
        if (b.chat.loadChatFile(a[0], b.scriptLoader(), str(1, 256), str(2, 256)))
            return botlib::blerr::None;
        if (print)
            print("couldn't load chat " + str(2, 256) + " from " + str(1, 256) + "\n");
        return botlib::blerr::CannotLoadInitialChat;
    case AiSetChatGender: b.chat.setGender(a[0], a[1]); return 0;
    case AiSetChatName: b.chat.setName(a[0], str(1, 256)); return 0;
    case AiQueueConsoleMessage: b.chat.queueConsoleMessage(a[0], a[1], str(2, 256)); return 0;
    case AiRemoveConsoleMessage: b.chat.removeConsoleMessage(a[0], a[1]); return 0;
    case AiNextConsoleMessage: { // (chatstate, bot_consolemessage_t*): handle, time, type, text
        const botlib::BotChat::ConsoleMessage* m = b.chat.nextConsoleMessage(a[0]);
        if (!m || !vm.checkWrite(a[1], 12 + 256))
            return 0;
        vm.writeInt(a[1], m->handle);
        vm.writeInt(a[1] + 4, Qvm::fromFloat(m->time));
        vm.writeInt(a[1] + 8, m->type);
        vm.writeString(a[1] + 12, m->text, 256);
        return m->handle;
    }
    case AiNumConsoleMessage: return b.chat.numConsoleMessages(a[0]);
    case AiInitialChat: // (chatstate, type, mcontext, var0..var7)
    case AiReplyChat: { // (chatstate, message, mcontext, vcontext, var0..var7)
        const int first = number == AiInitialChat ? 3 : 4;
        std::vector<std::string> texts(8);
        std::vector<const char*> vars(8, nullptr);
        for (int i = 0; i < 8; ++i)
            if (a[first + i]) {
                texts[size_t(i)] = str(first + i, 256);
                vars[size_t(i)] = texts[size_t(i)].c_str();
            }
        if (number == AiInitialChat) {
            b.chat.initialChat(a[0], str(1, 256), a[2], vars);
            return 0;
        }
        return b.chat.replyChat(a[0], str(1, 256), a[2], a[3], vars);
    }
    case AiChatLength: return b.chat.chatLength(a[0]);
    case AiEnterChat: b.chat.enterChat(a[0], a[1], a[2]); return 0; // (chatstate, the bot's client, to whom)
    case AiStringContains: return botlib::BotChat::stringContains(str(0), str(1), a[2] != 0);
    case AiFindMatch: { // (text, bot_match_t*, context)
        botlib::BotMatch m;
        const bool found = b.chat.findMatch(str(0, 256), m, a[2]);
        if (vm.checkWrite(a[1], kMatchSize))
            writeMatch(a[1], m);
        return found;
    }
    case AiMatchVariable: { // (bot_match_t*, variable, buffer, size)
        botlib::BotMatch m;
        if (!readMatch(a[0], m))
            return 0;
        vm.writeString(a[2], botlib::BotChat::matchVariable(m, a[1]), a[3]);
        return 0;
    }
    case AiUnifyWhiteSpaces: { // (text): in place, never longer
        std::string text = str(0, 1024);
        botlib::BotChat::unifyWhiteSpaces(text);
        vm.writeString(a[0], text, int32_t(text.size()) + 1);
        return 0;
    }
    case AiReplaceSynonyms: { // (text, context): in place, kept to a message's length
        std::string text = str(0, 256);
        b.chat.replaceSynonyms(text, a[1]);
        vm.writeString(a[0], text, 256);
        return 0;
    }
    case AiNumInitialChats: return b.chat.numInitialChats(a[0], str(1, 256));
    case AiGetChatMessage: vm.writeString(a[1], b.chat.takeChatMessage(a[0]), a[2]); return 0;

    // Goals.
    case AiAllocGoalState: return b.allocGoalState(a[0]);
    case AiFreeGoalState: b.freeGoalState(a[0]); return 0;
    case AiResetGoalState: b.resetGoalState(a[0]); return 0;
    case AiResetAvoidGoals: b.resetAvoidGoals(a[0]); return 0;
    case AiEmptyGoalStack: b.emptyGoalStack(a[0]); return 0;
    case AiPushGoal: {
        botlib::BotGoal g;
        if (readGoal(a[1], g))
            b.pushGoal(a[0], g);
        return 0;
    }
    case AiPopGoal: b.popGoal(a[0]); return 0;
    case AiGetTopGoal:
    case AiGetSecondGoal: {
        botlib::BotGoal g;
        if (!b.topGoal(a[0], g, number == AiGetTopGoal ? 1 : 2))
            return 0;
        return writeGoal(a[1], g);
    }
    case AiChooseLtgItem: return b.chooseLtgItem(a[0], vec(a[1]), inventory(a[2]), a[3]);
    case AiChooseNbgItem: {
        botlib::BotGoal ltg;
        const bool haveLtg = a[4] && readGoal(a[4], ltg);
        return b.chooseNbgItem(a[0], vec(a[1]), inventory(a[2]), a[3], haveLtg ? &ltg : nullptr, flt(5));
    }
    case AiTouchingGoal: {
        botlib::BotGoal g;
        return readGoal(a[1], g) && botlib::BotLibrary::touchingGoal(vec(a[0]), g);
    }
    case AiItemGoalInVisButNotVisible: {
        botlib::BotGoal g;
        return readGoal(a[3], g) && b.itemGoalInVisButNotVisible(a[0], vec(a[1]), g);
    }
    case AiGetLevelItemGoal: {
        botlib::BotGoal g;
        const int n = b.levelItemGoal(a[0], str(1, 256), g);
        if (n >= 0)
            writeGoal(a[2], g);
        return n;
    }
    case AiAvoidGoalTime: return Qvm::fromFloat(b.avoidGoalTime(a[0], a[1]));
    case AiRemoveFromAvoidGoals: b.removeFromAvoidGoals(a[0], a[1]); return 0;
    case AiInitLevelItems: b.initLevelItems(); return 0;
    case AiUpdateEntityItems: b.updateEntityItems(); return 0;
    case AiLoadItemWeights: return b.loadItemWeights(a[0], str(1, 256));
    case AiFreeItemWeights: b.freeItemWeights(a[0]); return 0;
    case AiGoalName: vm.writeString(a[1], b.goalName(a[0]), a[2]); return 0;
    case AiGetNextCampSpotGoal: {
        botlib::BotGoal g;
        const int n = b.nextCampSpotGoal(a[0], g);
        if (n)
            writeGoal(a[1], g);
        return n;
    }
    case AiGetMapLocationGoal: {
        botlib::BotGoal g;
        return b.mapLocationGoal(str(0, 256), g) && writeGoal(a[1], g);
    }
    case AiDumpAvoidGoals:
    case AiDumpGoalStack:
    case AiSaveGoalFuzzyLogic: return 0;

    // Movement.
    case AiAllocMoveState: return b.allocMoveState();
    case AiFreeMoveState: b.freeMoveState(a[0]); return 0;
    case AiResetMoveState: b.resetMoveState(a[0]); return 0;
    case AiInitMoveState: {
        if (!vm.valid(a[1], kInitMoveSize))
            return 0;
        botlib::BotInitMove m;
        m.origin = vec(a[1]);
        m.velocity = vec(a[1] + 12);
        m.viewOffset = vec(a[1] + 24);
        m.entity = vm.readInt(a[1] + 36);
        m.client = vm.readInt(a[1] + 40);
        m.thinkTime = Qvm::asFloat(vm.readInt(a[1] + 44));
        m.presence = vm.readInt(a[1] + 48);
        m.viewAngles = vec(a[1] + 52);
        m.orMoveFlags = vm.readInt(a[1] + 64);
        b.initMoveState(a[0], m);
        return 0;
    }
    case AiMoveToGoal: { // (result*, movestate, goal*, travelflags)
        botlib::BotGoal g;
        botlib::BotMoveResult r;
        if (readGoal(a[2], g))
            r = b.moveToGoal(a[1], g, a[3]);
        else
            r.failure = true;
        if (!vm.checkWrite(a[0], kMoveResultSize))
            return 0;
        const int32_t p = a[0];
        vm.writeInt(p, r.failure);
        vm.writeInt(p + 4, r.type);
        vm.writeInt(p + 8, r.blocked);
        vm.writeInt(p + 12, r.blockEntity);
        vm.writeInt(p + 16, r.travelType);
        vm.writeInt(p + 20, r.flags);
        vm.writeInt(p + 24, r.weapon);
        writeVec(p + 28, r.moveDir);
        writeVec(p + 40, r.idealViewAngles);
        return 0;
    }
    case AiMoveInDirection: return b.moveInDirection(a[0], vec(a[1]), flt(2), a[3]);
    case AiResetAvoidReach: b.resetAvoidReach(a[0]); return 0;
    case AiResetLastAvoidReach: b.resetLastAvoidReach(a[0]); return 0;
    case AiReachabilityArea: return b.reachabilityArea(vec(a[0]));
    case AiMovementViewTarget: { // (movestate, goal*, travelflags, lookahead, target*)
        botlib::BotGoal g;
        Vec3f target{};
        if (!readGoal(a[1], g) || !b.movementViewTarget(a[0], g, a[2], flt(3), target) || !vm.checkWrite(a[4], 12))
            return 0;
        writeVec(a[4], target);
        return 1;
    }
    case AiPredictVisiblePosition: { // (origin, area, goal*, travelflags, target*)
        botlib::BotGoal g;
        Vec3f target{};
        if (!readGoal(a[2], g) || !b.predictVisiblePosition(vec(a[0]), a[1], g, a[3], target) ||
            !vm.checkWrite(a[4], 12))
            return 0;
        writeVec(a[4], target);
        return 1;
    }

    // Weapons.
    case AiAllocWeaponState: return b.allocWeaponState();
    case AiFreeWeaponState: b.freeWeaponState(a[0]); return 0;
    case AiResetWeaponState: return 0;
    case AiLoadWeaponWeights: return b.loadWeaponWeights(a[0], str(1, 256));
    case AiChooseBestFightWeapon: return b.chooseBestFightWeapon(a[0], inventory(a[1]));
    case AiGetWeaponInfo: { // (weaponstate, weapon, weaponinfo_t*)
        const botlib::WeaponInfo* w = b.weaponInfo(a[1]);
        if (!w || !vm.checkWrite(a[2], kWeaponInfoSize))
            return 0;
        writeWeaponInfo(a[2], *w);
        return 0;
    }

    case AiGeneticParentsAndChildSelection:
    case AiInterbreedGoalFuzzyLogic:
    case AiMutateGoalFuzzyLogic: return 0;
    default: return fail();
    }
}

} // namespace reluva::idtech3::eliteforce
