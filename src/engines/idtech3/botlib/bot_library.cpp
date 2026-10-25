// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot library's engine side. See bot_library.h.

#include "engines/idtech3/botlib/bot_library.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::idtech3::botlib {

int BotLibrary::setup() {
    setup_ = true;
    if (print)
        print("------- BotLib Initialization -------\n");
    chat.time = [this] { return time_; };
    chat.command = [this](int client, const std::string& text) { command(client, text); };
    std::string log;
    chat.setup(scriptLoader(), [this](const std::string& name) { return libVar(name); }, &log);
    if (print)
        print(log);
    return blerr::None;
}

int BotLibrary::shutdown() {
    setup_ = false;
    aas_ = AasWorld{};
    entities_.assign(size_t(kMaxEntities), {});
    levelItems_.clear();
    goalStates_.clear();
    weaponStates_.clear();
    moveStates_.clear();
    return blerr::None;
}

std::string BotLibrary::libVar(const std::string& name) const {
    const auto it = libVars_.find(name);
    return it == libVars_.end() ? std::string() : it->second;
}

int BotLibrary::loadMap(const std::string& mapName) {
    if (!setup_)
        return blerr::LibraryNotSetup;
    const std::string path = "maps/" + mapName + ".aas";
    std::vector<uint8_t> data;
    if (!readFile || !readFile(path, data)) {
        if (print)
            print("can't open " + path + "\n");
        aas_ = AasWorld{};
        return blerr::CannotOpenAasFile;
    }
    if (Error err = aas_.load(data)) {
        if (print)
            print(path + ": " + err.message + "\n");
        return blerr::WrongAasFileVersion;
    }
    if (print)
        print("loaded " + path + ": " + std::to_string(aas_.areaCount()) + " areas, " +
              std::to_string(aas_.reachabilities().size()) + " reachabilities\n");
    entities_.assign(size_t(kMaxEntities), {});
    frames_ = 0;
    // The brush models' kinds (elevators, doors...), for movement; the
    // item and weapon descriptions; the level's items.
    modelTypes_.clear();
    for (int e = 1; entityKey && e <= entityCount; ++e) {
        std::string model, classname;
        if (entityKey(e, "model", model) && model.size() > 1 && model[0] == '*' && entityKey(e, "classname", classname))
            modelTypes_[std::atoi(model.c_str() + 1)] = classname;
    }
    loadConfigs();
    initLevelItems();
    return blerr::None;
}

int BotLibrary::startFrame(float time) {
    if (!setup_)
        return blerr::LibraryNotSetup;
    time_ = time;
    // Entities not updated since the last frame count as gone.
    for (BotEntityInfo& e : entities_)
        e.valid = false;
    ++frames_;
    return blerr::None;
}

int BotLibrary::updateEntity(int number, const BotEntityState* state) {
    if (!setup_)
        return blerr::LibraryNotSetup;
    if (number < 0 || number >= kMaxEntities)
        return blerr::InvalidEntityNumber;
    if (!aas_.loaded())
        return blerr::NoAasFile;
    BotEntityInfo& e = entities_[size_t(number)];
    if (!state)
        return blerr::None;
    e.updateTime = time_ - e.lastTime;
    e.lastTime = time_;
    e.lastVisOrigin = e.origin;
    e.type = state->type;
    e.flags = state->flags;
    e.oldOrigin = state->oldOrigin;
    e.origin = state->origin;
    e.angles = state->angles;
    e.mins = state->mins;
    e.maxs = state->maxs;
    e.groundEnt = state->groundEnt;
    e.solid = state->solid;
    e.modelIndex = state->modelIndex;
    e.modelIndex2 = state->modelIndex2;
    e.frame = state->frame;
    e.event = state->event;
    e.eventParm = state->eventParm;
    e.powerups = state->powerups;
    e.weapon = state->weapon;
    e.legsAnim = state->legsAnim;
    e.torsoAnim = state->torsoAnim;
    e.number = number;
    e.valid = true;
    return blerr::None;
}

const BotEntityInfo& BotLibrary::entityInfo(int number) const {
    static const BotEntityInfo none;
    return number >= 0 && number < kMaxEntities ? entities_[size_t(number)] : none;
}

BotScript::Loader BotLibrary::scriptLoader() const {
    return [this](const std::string& path, std::string& text) {
        std::vector<uint8_t> data;
        if (!readFile || !readFile("botfiles/" + path, data))
            return false;
        text.assign(data.begin(), data.end());
        return true;
    };
}

void BotLibrary::command(int client, const std::string& text) {
    if (clientCommand)
        clientCommand(client, text);
}

void BotLibrary::jump(int client) {
    // A jump only every other frame, so the game sees the key let go.
    BotInput& in = input(client);
    if (in.actionFlags & action::JumpedLastFrame)
        in.actionFlags &= ~action::Jump;
    else
        in.actionFlags |= action::Jump;
}

void BotLibrary::delayedJump(int client) {
    BotInput& in = input(client);
    if (in.actionFlags & action::JumpedLastFrame)
        in.actionFlags &= ~action::DelayedJump;
    else
        in.actionFlags |= action::DelayedJump;
}

void BotLibrary::move(int client, const Vec3f& dir, float speed) {
    BotInput& in = input(client);
    in.dir = dir;
    in.speed = std::clamp(speed, -float(kMaxUserMove), float(kMaxUserMove));
}

BotInput BotLibrary::getInput(int client, float thinkTime) {
    BotInput& in = input(client);
    in.thinkTime = thinkTime;
    return in;
}

void BotLibrary::resetInput(int client) {
    BotInput& in = input(client);
    const bool jumped = in.actionFlags & action::Jump;
    in.thinkTime = 0;
    in.dir = {};
    in.speed = 0;
    in.actionFlags = jumped ? action::JumpedLastFrame : 0;
}

} // namespace reluva::idtech3::botlib
