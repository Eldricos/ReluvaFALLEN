// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' goals: the level's items (from the map's entities and the
// item descriptions, items.c), linked to the item entities the game has
// spawned, and chosen by each bot's item weights against the time to get
// there. Behaviour read from the GPL ioquake3 bot library (be_ai_goal.c);
// new code.

#include "engines/idtech3/botlib/bot_library.h"

#include <cmath>
#include <cstdlib>

namespace reluva::idtech3::botlib {

namespace {

constexpr int kTypeItem = 2; // the entity type of items (Quake III's ET_ITEM)
// Item flags from the map.
constexpr int kNotFree = 1, kNotTeam = 2, kNotSingle = 4, kNotBot = 8, kRoam = 16;
constexpr float kAvoidMinimum = 10, kAvoidDefault = 30, kAvoidDropped = 10;
constexpr float kDroppedWeight = 1000;
constexpr int kGameSinglePlayer = 2, kGameTeam = 3;

float length(const Vec3f& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
Vec3f sub(const Vec3f& a, const Vec3f& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

} // namespace

std::shared_ptr<WeightConfig> BotLibrary::weights(const std::string& file) {
    if (const auto it = weightFiles_.find(file); it != weightFiles_.end())
        return it->second;
    std::string error;
    std::shared_ptr<WeightConfig> config = readWeightConfig(scriptLoader(), file, &error);
    if (!config && print)
        print("couldn't load weights " + file + ": " + error + "\n");
    if (config)
        weightFiles_[file] = config;
    return config;
}

void BotLibrary::loadConfigs() {
    if (configsLoaded_)
        return;
    configsLoaded_ = true;
    std::string error;
    const std::string itemFile = libVar("itemconfig").empty() ? "items.c" : libVar("itemconfig");
    if (!readItemConfig(scriptLoader(), itemFile, items_, &error) && print)
        print("couldn't load " + itemFile + ": " + error + "\n");
    const std::string weaponFile = libVar("weaponconfig").empty() ? "weapons.c" : libVar("weaponconfig");
    if (!readWeaponConfig(scriptLoader(), weaponFile, weapons_, &error)) {
        if (print)
            print("couldn't load " + weaponFile + ": " + error + "\n");
        weapons_.clear();
    }
    if (print)
        print(std::to_string(items_.size()) + " item infos, " +
              std::to_string(std::count_if(weapons_.begin(), weapons_.end(), [](const WeaponInfo& w) { return w.valid; })) +
              " weapon infos\n");
}

int BotLibrary::reachabilityArea(const Vec3f& origin) const {
    // The point's area if it leads anywhere; else one just below it
    // (in the air) or around it.
    const int a = aas_.pointArea(origin);
    if (a && aas_.settings(a).numReach)
        return a;
    std::vector<int> areas;
    for (const float down : {16.0f, 64.0f, 128.0f}) {
        aas_.traceAreas(origin, {origin[0], origin[1], origin[2] - down}, areas, nullptr, 16);
        for (const int b : areas)
            if (aas_.settings(b).numReach)
                return b;
    }
    for (int z = 1; z >= -1; --z)
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y) {
                aas_.traceAreas(origin, {origin[0] + 8.0f * float(x), origin[1] + 8.0f * float(y), origin[2] + 12.0f * float(z)},
                                areas, nullptr, 10);
                for (const int b : areas)
                    if (aas_.settings(b).numReach)
                        return b;
            }
    return a;
}

int BotLibrary::bestReachableArea(const Vec3f& origin, const Vec3f& mins, const Vec3f& maxs, Vec3f& goalOrigin) const {
    // A point where a player standing would touch the item, in an area
    // that leads anywhere: the item's place, raised to a player's height,
    // then round it within its box.
    for (const float dz : {0.0f, 9.0f, 16.0f, 24.0f, 32.0f, -8.0f})
        for (const float dx : {0.0f, mins[0], maxs[0]})
            for (const float dy : {0.0f, mins[1], maxs[1]}) {
                const Vec3f p{origin[0] + dx, origin[1] + dy, origin[2] + dz};
                const int a = aas_.pointArea(p);
                if (a && aas_.settings(a).numReach) {
                    goalOrigin = p;
                    return a;
                }
            }
    goalOrigin = origin;
    return 0;
}

void BotLibrary::initLevelItems() {
    levelItems_.clear();
    mapLocations_.clear();
    campSpots_.clear();
    if (!aas_.loaded() || !entityKey)
        return;
    auto key = [&](int e, const char* k) {
        std::string v;
        return entityKey(e, k, v) ? v : std::string();
    };
    auto vec = [&](int e, const char* k, Vec3f& out) {
        std::string v;
        if (!entityKey(e, k, v))
            return false;
        out = {};
        std::sscanf(v.c_str(), "%f %f %f", &out[0], &out[1], &out[2]);
        return true;
    };
    int number = 0;
    for (int e = 1; e <= entityCount; ++e) {
        const std::string classname = key(e, "classname");
        Vec3f origin{};
        if (classname == "target_location" || classname == "info_camp") {
            // Places the bots know by name, and spots to camp at.
            if (!vec(e, "origin", origin))
                continue;
            Spot spot{origin, reachabilityArea(origin), key(e, classname == "target_location" ? "message" : "name")};
            (classname == "target_location" ? mapLocations_ : campSpots_).push_back(spot);
            continue;
        }
        size_t info = 0;
        while (info < items_.size() && items_[info].classname != classname)
            ++info;
        if (info >= items_.size() || !vec(e, "origin", origin))
            continue;
        const ItemInfo& item = items_[info];
        LevelItem li;
        li.number = ++number;
        li.itemInfo = int(info);
        auto flag = [&](const char* k, int f) {
            if (std::atoi(key(e, k).c_str()))
                li.flags |= f;
        };
        flag("notfree", kNotFree);
        flag("notteam", kNotTeam);
        flag("notsingle", kNotSingle);
        flag("notbot", kNotBot);
        if (classname == "item_botroam") {
            li.flags |= kRoam;
            li.weight = float(std::atof(key(e, "weight").c_str()));
        }
        // Items not hanging in the air drop to the floor, as the game puts them.
        if (!(std::atoi(key(e, "spawnflags").c_str()) & 1) && trace) {
            const Trace t = trace(origin, item.mins, item.maxs, {origin[0], origin[1], origin[2] - 4096}, -1,
                                  contents::Solid | contents::PlayerClip);
            if (!t.startSolid)
                origin = t.end;
        }
        li.origin = origin;
        li.goalArea = bestReachableArea(origin, item.mins, item.maxs, li.goalOrigin);
        levelItems_.push_back(li);
    }
    if (print)
        print(std::to_string(levelItems_.size()) + " level items, " + std::to_string(mapLocations_.size()) +
              " locations, " + std::to_string(campSpots_.size()) + " camp spots\n");
}

bool BotLibrary::itemAllowed(const LevelItem& li) const {
    const int gametype = std::atoi(libVar("g_gametype").c_str());
    const int notHere = gametype == kGameSinglePlayer ? kNotSingle : gametype >= kGameTeam ? kNotTeam : kNotFree;
    return !(li.flags & (notHere | kNotBot));
}

void BotLibrary::updateEntityItems() {
    // Dropped items gone after their time; then each item entity the game
    // has linked to the level item it stands for, or added as a dropped one.
    std::erase_if(levelItems_, [&](const LevelItem& li) { return li.timeout && li.timeout < time_; });
    if (items_.empty())
        return;
    const int maxNumber = int(levelItems_.size());
    for (int ent = 1; ent < kMaxEntities; ++ent) {
        const BotEntityInfo& e = entities_[size_t(ent)];
        if (!e.valid || e.type != kTypeItem || !e.modelIndex)
            continue;
        // Only once it has stopped moving.
        if (e.origin != e.lastVisOrigin)
            continue;
        LevelItem* found = nullptr;
        for (LevelItem& li : levelItems_)
            if (li.entity == ent) {
                found = &li;
                break;
            }
        if (found) {
            if (items_[size_t(found->itemInfo)].modelIndex != e.modelIndex) {
                const int gone = found->number;
                std::erase_if(levelItems_, [&](const LevelItem& li) { return li.number == gone; });
            } else if (found->origin != e.origin) {
                found->origin = e.origin;
                const ItemInfo& item = items_[size_t(found->itemInfo)];
                found->goalArea = bestReachableArea(found->origin, item.mins, item.maxs, found->goalOrigin);
            }
            continue;
        }
        for (LevelItem& li : levelItems_) {
            if (li.entity || !itemAllowed(li) || items_[size_t(li.itemInfo)].modelIndex != e.modelIndex)
                continue;
            if (length(sub(li.origin, e.origin)) < 30) {
                li.entity = ent;
                if (li.origin != e.origin) {
                    li.origin = e.origin;
                    const ItemInfo& item = items_[size_t(li.itemInfo)];
                    li.goalArea = bestReachableArea(li.origin, item.mins, item.maxs, li.goalOrigin);
                }
                found = &li;
                break;
            }
        }
        if (found)
            continue;
        // A dropped item: known for 30 seconds.
        size_t info = 0;
        while (info < items_.size() && items_[info].modelIndex != e.modelIndex)
            ++info;
        if (info >= items_.size())
            continue;
        LevelItem li;
        li.entity = ent;
        li.number = maxNumber + ent;
        li.itemInfo = int(info);
        li.origin = e.origin;
        li.goalArea = bestReachableArea(li.origin, items_[info].mins, items_[info].maxs, li.goalOrigin);
        if (li.goalArea && (aas_.settings(li.goalArea).contents & areacontents::JumpPad))
            continue;
        li.timeout = time_ + 30;
        levelItems_.push_back(li);
    }
}

int BotLibrary::allocGoalState(int client) {
    const int h = alloc(goalStates_);
    goalStates_[size_t(h) - 1]->client = client;
    return h;
}

void BotLibrary::freeGoalState(int handle) {
    if (state(goalStates_, handle))
        goalStates_[size_t(handle) - 1].reset();
}

void BotLibrary::resetGoalState(int handle) {
    if (GoalState* gs = state(goalStates_, handle)) {
        gs->stack.clear();
        gs->avoid.clear();
    }
}

void BotLibrary::resetAvoidGoals(int handle) {
    if (GoalState* gs = state(goalStates_, handle))
        gs->avoid.clear();
}

void BotLibrary::pushGoal(int handle, const BotGoal& goal) {
    if (GoalState* gs = state(goalStates_, handle); gs && gs->stack.size() < 8)
        gs->stack.push_back(goal);
}

void BotLibrary::popGoal(int handle) {
    if (GoalState* gs = state(goalStates_, handle); gs && !gs->stack.empty())
        gs->stack.pop_back();
}

void BotLibrary::emptyGoalStack(int handle) {
    if (GoalState* gs = state(goalStates_, handle))
        gs->stack.clear();
}

bool BotLibrary::topGoal(int handle, BotGoal& goal, int depth) const {
    const GoalState* gs = state(goalStates_, handle);
    if (!gs || int(gs->stack.size()) < depth)
        return false;
    goal = gs->stack[gs->stack.size() - size_t(depth)];
    return true;
}

int BotLibrary::loadItemWeights(int handle, const std::string& file) {
    GoalState* gs = state(goalStates_, handle);
    if (!gs)
        return blerr::CannotLoadItemWeights;
    loadConfigs();
    gs->weights = weights(file);
    if (!gs->weights)
        return blerr::CannotLoadItemWeights;
    // Each item description's weight, by its class name.
    gs->weightIndex.clear();
    for (const ItemInfo& item : items_)
        gs->weightIndex.push_back(gs->weights->find(item.classname));
    return blerr::None;
}

void BotLibrary::freeItemWeights(int handle) {
    if (GoalState* gs = state(goalStates_, handle)) {
        gs->weights.reset();
        gs->weightIndex.clear();
    }
}

float BotLibrary::avoidGoalTime(int handle, int number) const {
    const GoalState* gs = state(goalStates_, handle);
    if (!gs)
        return 0;
    const auto it = gs->avoid.find(number);
    return it != gs->avoid.end() && it->second >= time_ ? it->second - time_ : 0;
}

void BotLibrary::removeFromAvoidGoals(int handle, int number) {
    if (GoalState* gs = state(goalStates_, handle))
        gs->avoid.erase(number);
}

bool BotLibrary::chooseItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags,
                            const BotGoal* ltg, float maxTime, bool nearby) {
    GoalState* gs = state(goalStates_, handle);
    if (!gs || !gs->weights || items_.empty())
        return false;
    int area = reachabilityArea(origin);
    if (!area || !aas_.settings(area).numReach)
        area = gs->lastArea;
    gs->lastArea = area;
    if (!area)
        return false;
    const int ltgTime = nearby && ltg ? aas_.travelTime(area, origin, ltg->area, travelFlags) : 99999;
    // The item worth most for the time it takes to get to it, not avoided
    // (for a while after it was last chosen) unless it's nearly there.
    const LevelItem* best = nullptr;
    float bestWeight = 0;
    for (const LevelItem& li : levelItems_) {
        if (!itemAllowed(li) || !li.goalArea || (!li.entity && !(li.flags & kRoam)))
            continue;
        const int w = size_t(li.itemInfo) < gs->weightIndex.size() ? gs->weightIndex[size_t(li.itemInfo)] : -1;
        if (w < 0)
            continue;
        float weight = gs->weights->evaluate(w, inventory);
        if (li.timeout)
            weight += kDroppedWeight;
        if (li.flags & kRoam)
            weight *= li.weight;
        if (weight <= 0)
            continue;
        const int t = aas_.travelTime(area, origin, li.goalArea, travelFlags);
        if (t <= 0 || (nearby && float(t) >= maxTime))
            continue;
        if (avoidGoalTime(handle, li.number) - float(t) * 0.009f > 0)
            continue;
        weight /= float(t) * 0.01f;
        if (weight <= bestWeight)
            continue;
        if (nearby && ltg && !li.timeout && aas_.travelTime(li.goalArea, li.goalOrigin, ltg->area, travelFlags) > ltgTime)
            continue;
        bestWeight = weight;
        best = &li;
    }
    if (!best)
        return false;
    const ItemInfo& item = items_[size_t(best->itemInfo)];
    BotGoal goal;
    goal.origin = best->goalOrigin;
    goal.mins = item.mins;
    goal.maxs = item.maxs;
    goal.area = best->goalArea;
    goal.entity = best->entity;
    goal.number = best->number;
    goal.flags = goalflag::Item | (best->timeout ? goalflag::Dropped : 0) | (best->flags & kRoam ? goalflag::Roam : 0);
    goal.itemInfo = best->itemInfo;
    // Not chosen again until it has had time to come back.
    float avoid = best->timeout ? kAvoidDropped : item.respawnTime;
    if (!avoid)
        avoid = kAvoidDefault;
    gs->avoid[best->number] = time_ + std::max(avoid, kAvoidMinimum);
    pushGoal(handle, goal);
    return true;
}

bool BotLibrary::chooseLtgItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags) {
    return chooseItem(handle, origin, inventory, travelFlags, nullptr, 0, false);
}

bool BotLibrary::chooseNbgItem(int handle, const Vec3f& origin, const std::vector<int>& inventory, int travelFlags,
                               const BotGoal* ltg, float maxTime) {
    return chooseItem(handle, origin, inventory, travelFlags, ltg, maxTime, true);
}

bool BotLibrary::touchingGoal(const Vec3f& origin, const BotGoal& goal) {
    // Inside the goal's box grown by a standing player's.
    Vec3f mins{}, maxs{};
    AasWorld::presenceBox(presence::Normal, mins, maxs);
    for (size_t i = 0; i < 3; ++i)
        if (origin[i] < goal.origin[i] + goal.mins[i] - maxs[i] || origin[i] > goal.origin[i] + goal.maxs[i] - mins[i])
            return false;
    return true;
}

bool BotLibrary::itemGoalInVisButNotVisible(int viewer, const Vec3f& eye, const BotGoal& goal) {
    // In plain view, yet the item's entity hasn't been seen for half a
    // second: it has been taken.
    if (!(goal.flags & goalflag::Item) || !trace)
        return false;
    const Vec3f middle{goal.origin[0] + goal.mins[0], goal.origin[1] + goal.mins[1], goal.origin[2] + goal.mins[2]};
    const Trace t = trace(eye, {}, {}, middle, viewer, contents::Solid);
    if (t.fraction < 1 || goal.entity <= 0)
        return false;
    return entityInfo(goal.entity).lastTime < time_ - 0.5f;
}

int BotLibrary::levelItemGoal(int index, const std::string& name, BotGoal& goal) const {
    // The next level item after number `index` with that name.
    auto lower = [](std::string s) {
        for (char& c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    size_t i = 0;
    if (index >= 0) {
        while (i < levelItems_.size() && levelItems_[i].number != index)
            ++i;
        ++i;
    }
    for (; i < levelItems_.size(); ++i) {
        const LevelItem& li = levelItems_[i];
        if (!itemAllowed(li) || lower(items_[size_t(li.itemInfo)].name) != lower(name))
            continue;
        const ItemInfo& item = items_[size_t(li.itemInfo)];
        goal = {};
        goal.area = li.goalArea;
        goal.origin = li.goalOrigin;
        goal.entity = li.entity;
        goal.mins = item.mins;
        goal.maxs = item.maxs;
        goal.number = li.number;
        goal.flags = goalflag::Item | (li.timeout ? goalflag::Dropped : 0);
        goal.itemInfo = li.itemInfo;
        return li.number;
    }
    return -1;
}

std::string BotLibrary::goalName(int number) const {
    for (const LevelItem& li : levelItems_)
        if (li.number == number)
            return items_[size_t(li.itemInfo)].name;
    return {};
}

int BotLibrary::nextCampSpotGoal(int num, BotGoal& goal) const {
    num = std::max(num, 0);
    if (size_t(num) >= campSpots_.size())
        return 0;
    goal = {};
    goal.origin = campSpots_[size_t(num)].origin;
    goal.area = campSpots_[size_t(num)].area;
    goal.mins = {-8, -8, -8};
    goal.maxs = {8, 8, 8};
    return num + 1;
}

bool BotLibrary::mapLocationGoal(const std::string& name, BotGoal& goal) const {
    for (const Spot& s : mapLocations_)
        if (s.name.size() == name.size() &&
            std::equal(s.name.begin(), s.name.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            goal = {};
            goal.origin = s.origin;
            goal.area = s.area;
            goal.mins = {-8, -8, -8};
            goal.maxs = {8, 8, 8};
            return true;
        }
    return false;
}

} // namespace reluva::idtech3::botlib
