// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/vm_level.h"

#include "engines/unreal/mesh.h"

#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <cmath>
#include <map>

namespace reluva::unreal {

namespace {
std::string lowerText(const std::string& s) { return fs::toLower(s); }
} // namespace

Error readLevelActors(const Package& map, std::vector<int32_t>& out) {
    out.clear();
    const std::vector<int> levels = map.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(map.name() + ": no Level object");
    const int index = levels.front();
    std::vector<uint8_t> data;
    if (Error err = map.readExport(index, data))
        return err;
    Reader r(data, size_t(map.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(map, index, r, props))
        return err;
    // A transaction array: how many, how many allocated, the references.
    const int32_t count = r.i32();
    r.i32();
    if (!r.ok() || count < 0 || size_t(count) > r.remaining())
        return Error::fail(map.name() + ": " + std::to_string(count) + " actors don't fit");
    out.reserve(size_t(count));
    for (int32_t i = 0; i < count && r.ok(); ++i)
        out.push_back(r.index());
    return r.ok() ? Error::success() : Error::fail(map.name() + ": the actor list runs past the end");
}

ScriptLevel::ScriptLevel(Vm& vm, const Package& map) : vm_(vm), map_(map) {
    vm_.level = this;
    registerEngineNatives(vm_);
    reseedEffectRandom();
    reseedEffectPhysicsRandom();
    reseedCoreRandom();
}

ScriptLevel::~ScriptLevel() {
    if (vm_.level == this)
        vm_.level = nullptr;
}

Error ScriptLevel::load() {
    hasModel_ = findLevelModel(map_, &model_) >= 0 && !model_.nodes.empty();
    if (hasModel_)
        collision_ = std::make_unique<LevelCollision>(model_);
    // Movers' brushes' boxes, for colliding with them where they've moved.
    for (const Mover& m : findMovers(vm_.packages(), map_, nullptr)) {
        BrushBox b;
        b.min = {1e30f, 1e30f, 1e30f};
        b.max = {-1e30f, -1e30f, -1e30f};
        for (const Poly& p : m.polys)
            for (const Vec3f& v : p.vertices)
                for (size_t k = 0; k < 3; ++k) {
                    b.min[k] = std::min(b.min[k], v[k]);
                    b.max[k] = std::max(b.max[k], v[k]);
                }
        b.prePivot = m.prePivot;
        b.mainScale = m.mainScale;
        b.postScale = m.postScale;
        float scale = 1;
        for (size_t k = 0; k < 3; ++k)
            scale = std::max(scale, std::fabs(m.mainScale[k]) * std::max({std::fabs(m.postScale[0]),
                                                                          std::fabs(m.postScale[1]),
                                                                          std::fabs(m.postScale[2])}));
        for (const Poly& p : m.polys)
            for (const Vec3f& v : p.vertices) {
                const float dx = v[0] - m.prePivot[0], dy = v[1] - m.prePivot[1], dz = v[2] - m.prePivot[2];
                b.radius = std::max(b.radius, std::sqrt(dx * dx + dy * dy + dz * dz) * scale);
            }
        for (size_t i = 0; i < map_.exports().size(); ++i)
            if (map_.path(int32_t(i) + 1) == m.name) {
                brushes_[int(i)] = b;
                break;
            }
    }
    std::vector<int32_t> refs;
    if (Error err = readLevelActors(map_, refs))
        return err;
    // First every actor, so they can refer to each other; then their
    // properties.
    std::vector<std::pair<VmObject*, int>> made;
    std::vector<VmObject*> level; // their Outer: the level, set once it's made
    for (int32_t ref : refs) {
        if (ref <= 0)
            continue;
        const int index = ref - 1;
        const Export& e = map_.exports()[size_t(index)];
        const ObjectRef cref = vm_.packages().resolve(map_, e.classRef);
        VmClass* cls = cref ? vm_.classAt(*cref.package, cref.exportIndex) : nullptr;
        if (!cls) {
            vm_.reportError("the level's actor " + map_.path(ref) + " has no class that loads");
            continue;
        }
        // UnrealEd keeps its viewports' cameras in the level as Camera
        // actors, which are PlayerPawns: left out, or the player logging
        // in would take one over (GameInfo.Login reuses a PlayerPawn
        // without a player).
        bool camera = false;
        for (const VmClass* c = cls; c && !camera; c = c->parent())
            camera = lowerText(c->name) == "camera";
        if (camera) {
            ++editorCameras_;
            continue;
        }
        VmObject* a = vm_.create(cls, vm_.name(map_.objectName(ref)));
        vm_.adopt(map_, index, a);
        level.push_back(a);
        made.push_back({a, index});
        actors_.push_back(a);
    }
    for (const auto& [a, index] : made) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (Error err = map_.readExport(index, data)) {
            vm_.reportError(err.message);
            continue;
        }
        Reader r(data, size_t(map_.exports()[size_t(index)].serialOffset));
        if (Error err = beginObject(map_, index, r, props)) {
            vm_.reportError(err.message);
            continue;
        }
        vm_.applyProperties(a, map_, props);
        if (!levelInfo_ && vm_.isA(a, "LevelInfo"))
            levelInfo_ = a;
        // A mover's brush's own BSP (UnrealEd builds one for each), to
        // collide with it where it's moved to.
        if (vm_.isA(a, "Mover"))
            movers_.push_back(a);
        if (auto it = brushes_.find(index); it != brushes_.end())
            if (Value* brush = a->find(vm_, "Brush"); brush && brush->asObject() && brush->o->package) {
                auto model = std::make_shared<Model>();
                if (!readModel(*brush->o->package, brush->o->exportIndex, *model, false) && !model->nodes.empty()) {
                    it->second.collision = std::make_shared<LevelCollision>(*model);
                    it->second.model = std::move(model);
                }
            }
    }
    if (!levelInfo_)
        return Error::fail(map_.name() + ": no LevelInfo among its actors");
    const std::vector<int> levels = map_.exportsOfClass("Level");
    levelObject_ = levels.empty() ? nullptr : vm_.objectAt(map_, levels.front() + 1);
    for (VmObject* a : level)
        if (Value* v = a->find(vm_, "Outer"))
            v->o = levelObject_;
    for (VmObject* a : actors_)
        init(a);
    loadPaths();
    return Error::success();
}

void ScriptLevel::init(VmObject* a) {
    if (Value* v = a->find(vm_, "Level"))
        v->o = levelInfo_;
    if (Value* v = a->find(vm_, "XLevel"))
        v->o = levelObject_;
}

void ScriptLevel::beginPlay(const std::string& defaultGame) {
    // While the level starts, bStartup; once it has, bBegunPlay (which
    // scripts check before acting on ticks: CTVE_Spline's).
    if (Value* v = levelInfo_->find(vm_, "bStartup"))
        *v = Value::boolValue(true);
    // High detail, as the engine sets it from the client's settings
    // (LowDetailTextures off, as Default.ini has it): the effects in full
    // (an arc's 25 segments, not 5). Reluva's reading of which setting.
    if (Value* v = levelInfo_->find(vm_, "bHighDetailMode")) {
        const std::string* client = vm_.configSetting("Engine.Engine", "ViewportManager");
        const std::string* low = client ? vm_.configSetting(*client, "LowDetailTextures") : nullptr;
        *v = Value::boolValue(!low || lowerText(*low) != "true");
    }
    // The game type: the level's own, else the default.
    VmClass* gameClass = nullptr;
    if (Value* v = levelInfo_->find(vm_, "DefaultGameType"); v && v->asObject() && v->o->asClass)
        gameClass = v->o->asClass;
    if (!gameClass)
        gameClass = vm_.findClass(defaultGame);
    if (gameClass) {
        game_ = spawn(gameClass, nullptr, 0, nullptr, nullptr);
        if (Value* v = levelInfo_->find(vm_, "Game"))
            v->o = game_;
        if (game_) {
            Value error = Value::stringValue("");
            if (VmFunction* f = vm_.findFunction(game_, vm_.name("InitGame")))
                vm_.invoke(f, game_, {Value::stringValue(""), error}, {nullptr, &error});
        }
    } else {
        vm_.reportError("no game type " + defaultGame);
    }
    // The level's own actors, in order (spawned ones got theirs already).
    const std::vector<VmObject*> start = actors_;
    for (VmObject* a : start) {
        if (a == game_)
            continue;
        vm_.callEvent(a, "PreBeginPlay");
        vm_.callEvent(a, "BeginPlay");
    }
    for (VmObject* a : start)
        if (a != game_)
            vm_.callEvent(a, "PostBeginPlay");
    for (VmObject* a : start)
        if (a != game_)
            vm_.callEvent(a, "SetInitialState");
    if (Value* v = levelInfo_->find(vm_, "bStartup"))
        *v = Value::boolValue(false);
    if (Value* v = levelInfo_->find(vm_, "bBegunPlay"))
        *v = Value::boolValue(true);
}

VmObject* ScriptLevel::login(const std::string& playerClass, const std::string& playerName, std::string& error,
                            const std::string& portal) {
    error.clear();
    if (!game_) {
        error = "no game type to log in to";
        return nullptr;
    }
    // Which start, and so which character.
    VmClass* cls = nullptr;
    if (VmFunction* find = vm_.findFunction(game_, vm_.name("FindPlayerStart"))) {
        const Value start = vm_.invoke(find, game_, {Value::objectValue(nullptr), Value::byteValue(255), Value::stringValue(portal)});
        if (VmObject* s = start.asObject())
            if (Value* p = s->find(vm_, "Player"); p && p->asObject() && p->o->asClass)
                cls = p->o->asClass;
    }
    if (!cls && !playerClass.empty())
        cls = vm_.findClass(playerClass);
    if (!cls)
        if (Value* p = game_->find(vm_, "DefaultPlayerClass"); p && p->asObject() && p->o->asClass)
            cls = p->o->asClass;
    if (!cls) {
        error = "no player class";
        return nullptr;
    }
    VmFunction* login = vm_.findFunction(game_, vm_.name("Login"));
    if (!login) {
        error = game_->cls->name + " has no Login";
        return nullptr;
    }
    const std::string options = "?Name=" + playerName + "?Class=" + cls->package->name() + "." + cls->name;
    Value message = Value::stringValue("");
    const Value pawn = vm_.invoke(login, game_,
                                  {Value::stringValue(portal), Value::stringValue(options), message, Value::objectValue(cls->object)},
                                  {nullptr, nullptr, &message, nullptr});
    player_ = pawn.asObject();
    if (!player_) {
        error = "Login refused: " + (message.s.empty() ? std::string("no reason given") : message.s);
        return nullptr;
    }
    attachViewport();
    vm_.callEvent(player_, "Possess");
    vm_.callEvent(game_, "AcceptInventory", {Value::objectValue(player_)});
    vm_.callEvent(player_, "TravelPostAccept");
    vm_.callEvent(game_, "PostLogin", {Value::objectValue(player_)});
    return player_;
}

// The engine's side of the player: a viewport (a class with no script,
// made up as a kind of Player) with the .ini's console.
void ScriptLevel::attachViewport() {
    VmClass* playerBase = vm_.findClass("Engine.Player");
    VmClass* viewportClass = vm_.defineIntrinsic("Viewport", playerBase);
    VmObject* viewport = vm_.create(viewportClass, vm_.name("Viewport0"));
    if (Value* v = viewport->find(vm_, "Actor"))
        v->o = player_;
    if (VmClass* consoleClass = vm_.findClass(iniSetting(vm_.packages(), "Engine.Engine", "Console"))) {
        VmObject* console = vm_.create(consoleClass, 0, viewport);
        if (Value* v = viewport->find(vm_, "Console"))
            v->o = console;
        if (Value* v = console->find(vm_, "Viewport"))
            v->o = viewport;
    }
    if (Value* v = player_->find(vm_, "Player"))
        v->o = viewport;
}

Value ScriptLevel::regionAt(const std::array<float, 3>& p) {
    int zone = 0, leaf = -1;
    if (hasModel_) {
        int32_t n = 0;
        for (int guard = 0; guard < 100000 && n >= 0 && size_t(n) < model_.nodes.size(); ++guard) {
            const BspNode& node = model_.nodes[size_t(n)];
            const bool front = node.plane[0] * p[0] + node.plane[1] * p[1] + node.plane[2] * p[2] - node.plane[3] >= 0;
            zone = node.zone[front ? 1 : 0];
            leaf = node.leaf[front ? 1 : 0];
            n = front ? node.front : node.back;
            if (n <= 0)
                break;
        }
    }
    Value v;
    Value* saved = levelInfo_ ? levelInfo_->find(vm_, "Region") : nullptr;
    if (saved && saved->type == VType::Struct) {
        v = *saved;
        if (const VmStruct* st = v.st) {
            VmObject* zoneActor = size_t(zone) < model_.zones.size() ? vm_.objectAt(map_, model_.zones[size_t(zone)].actor) : nullptr;
            // A zone with no ZoneInfo of its own (or outside them all) is
            // the level's: LevelInfo is the default zone, as in UE1.
            // Scripts read Region.Zone everywhere (the player's ViewFlash,
            // UnderWaterSounds, HandleWalking took None 150,000 times).
            if (!zoneActor)
                zoneActor = levelInfo_;
            if (VmProperty* f = st->findProperty(vm_.name("Zone")))
                v.fields[size_t(f->slot)] = Value::objectValue(zoneActor);
            if (VmProperty* f = st->findProperty(vm_.name("iLeaf")))
                v.fields[size_t(f->slot)] = Value::intValue(leaf);
            if (VmProperty* f = st->findProperty(vm_.name("ZoneNumber")))
                v.fields[size_t(f->slot)] = Value::byteValue(zone);
        }
    }
    return v;
}

void ScriptLevel::litActors(std::vector<LitActor>& out) {
    out.clear();
    auto byte = [&](VmObject* a, const char* name, uint8_t otherwise) {
        const Value* v = prop(a, name);
        return v ? uint8_t(v->asInt()) : otherwise;
    };
    for (VmObject* a : actors_) {
        if (a->deleted)
            continue;
        const Value* type = prop(a, "LightType");
        if (!type || type->asInt() == 0)
            continue;
        LitActor l;
        l.brightness = byte(a, "LightBrightness", 0);
        const Value* loc = prop(a, "Location");
        if (!l.brightness || !loc)
            continue;
        l.actor = a;
        l.location = Vm::vectorOf(*loc);
        l.type = uint8_t(type->asInt());
        l.hue = byte(a, "LightHue", 0);
        l.saturation = byte(a, "LightSaturation", 255);
        l.radius = byte(a, "LightRadius", 0);
        l.period = byte(a, "LightPeriod", 32);
        l.phase = byte(a, "LightPhase", 0);
        l.effect = byte(a, "LightEffect", 0);
        const Value* isStatic = prop(a, "bStatic");
        l.isStatic = isStatic && isStatic->asBool();
        out.push_back(l);
    }
}

// A noise an actor makes (MakeNoise; a sound played without bNoHearNoise):
// each pawn but the player and whoever made it hears it if it's loud
// enough where the pawn is: Loudness x 800 over the distance, halved with
// the world between, against its HearingThreshold (with the usual 1, a
// shot of loudness 1 carries 800 units in the open). Its HearNoise
// (Loudness, the maker): The Fallen's AI turns to search, or attacks if
// it can see the maker. One noise a pawn every fifth of a second. Reluva's
// reading: UE1's rule isn't in the scripts.
void ScriptLevel::makeNoise(VmObject* maker, float loudness) {
    if (!maker || maker->deleted || loudness <= 0)
        return;
    const Value* at = prop(maker, "Location");
    if (!at)
        return;
    const auto from = Vm::vectorOf(*at);
    const Value* ownerValue = prop(maker, "Owner");
    VmObject* owner = ownerValue ? ownerValue->asObject() : nullptr;
    const Value* instigatorValue = prop(maker, "Instigator");
    VmObject* instigator = instigatorValue ? instigatorValue->asObject() : nullptr;
    // Only a pawn's noise is heard (one's own, its weapon's or shot's, or
    // what it set off: an Instigator), as UE1's MakeNoise asks; the
    // level's ambient sounds aren't.
    auto isPawn = [&](VmObject* a) {
        if (!a || a->deleted)
            return false;
        auto it = pawnClasses_.find(a->cls);
        if (it == pawnClasses_.end())
            it = pawnClasses_.emplace(a->cls, vm_.isA(a, "Pawn")).first;
        return it->second;
    };
    if (!isPawn(maker) && !isPawn(instigator) && !isPawn(owner))
        return;
    ++noise.made;
    const std::vector<VmObject*> now = actors_;
    for (VmObject* p : now) {
        if (p == maker || p == owner || p == instigator || p == player_ || p->deleted)
            continue;
        if (!isPawn(p))
            continue;
        const Value* health = prop(p, "Health");
        if (health && health->asInt() <= 0)
            continue;
        float& quiet = noiseQuiet_[p];
        if (quiet > time_)
            continue;
        const Value* loc = prop(p, "Location");
        if (!loc)
            continue;
        const auto to = Vm::vectorOf(*loc);
        const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
        const float dist = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
        float heard = loudness * 800 / dist;
        const Value* threshold = prop(p, "HearingThreshold");
        const float need = threshold ? std::max(0.01f, threshold->asFloat()) : 1;
        if (heard < need)
            continue;
        if (!lineOfSight(p, from, to))
            heard *= 0.5f;
        if (heard < need)
            continue;
        quiet = time_ + 0.2f;
        ++noise.heard;
        vm_.callEvent(p, "HearNoise", {Value::floatValue(heard), Value::objectValue(maker)});
        // A guard with no enemy yet wonders aloud ("What was that?").
        const Value* enemy = p->deleted ? nullptr : prop(p, "Enemy");
        if (enemy && !enemy->asObject() && dialogThreads_.empty() && (maker == player_ || owner == player_ ||
                                                                       instigator == player_))
            startBark(p, "FHE");
    }
}

void ScriptLevel::updateRegion(VmObject* a) {
    if (!a || !hasModel_)
        return;
    Value* location = prop(a, "Location");
    Value* region = prop(a, "Region");
    if (location && region)
        vm_.assign(*region, regionAt(Vm::vectorOf(*location)));
    updateBodyRegions(a, false);
}

// A pawn's feet, chest (The Fallen's: ChestHeight up its height) and head
// (its eyes) are each in a zone too, which the engine keeps up to date
// (FootRegion, ChestRegion, HeadRegion: wading, swimming, its breath
// underwater), telling its scripts as each changes zone
// (FootZoneChange, ChestZoneChange, HeadZoneChange) when `events`.
void ScriptLevel::updateBodyRegions(VmObject* a, bool events) {
    Value* foot = prop(a, "FootRegion");
    Value* head = prop(a, "HeadRegion");
    const Value* location = prop(a, "Location");
    if (!foot || !head || !location || !hasModel_)
        return;
    const auto at = Vm::vectorOf(*location);
    const Value* h = prop(a, "CollisionHeight");
    const float height = h ? h->asFloat() : 0;
    const Value* eye = prop(a, "EyeHeight");
    const Value* chestHeight = prop(a, "ChestHeight");
    struct Part {
        Value* region;
        float z;
        const char* event;
    };
    Part parts[3] = {{foot, at[2] - height, "FootZoneChange"},
                     {prop(a, "ChestRegion"), at[2] - height + 2 * height * (chestHeight ? chestHeight->asFloat() : 0.65f),
                      "ChestZoneChange"},
                     {head, at[2] + (eye ? eye->asFloat() : height * 0.5f), "HeadZoneChange"}};
    for (Part& p : parts) {
        if (!p.region)
            continue;
        const VmObject* before = !p.region->fields.empty() ? p.region->fields[0].asObject() : nullptr;
        vm_.assign(*p.region, regionAt({at[0], at[1], p.z}));
        VmObject* now = !p.region->fields.empty() ? p.region->fields[0].asObject() : nullptr;
        if (events && now && before && now != before && !a->deleted)
            vm_.callEvent(a, p.event, {Value::objectValue(now)});
    }
}

bool ScriptLevel::paused() const {
    const Value* v = levelInfo_ ? levelInfo_->find(vm_, "Pauser") : nullptr;
    return v && !v->s.empty();
}

void ScriptLevel::tick(float seconds) {
    const bool stopped = paused();
    if (!stopped) {
        time_ += seconds;
        if (Value* v = levelInfo_->find(vm_, "TimeSeconds"))
            v->f += seconds;
    }
    if (Value* v = levelInfo_->find(vm_, "DeltaTime"))
        v->f = seconds;
    // Actors spawned during the tick wait for the next. The player's
    // pawn first gets PlayerTick (its input and view); then each actor's
    // scripts, then its physics (and the player's UpdateEyeHeight).
    const std::vector<VmObject*> now = actors_;
    for (VmObject* a : now) {
        if (a->deleted)
            continue;
        if (stopped && a != player_) {
            const Value* always = prop(a, "bAlwaysTick");
            if (!always || !always->asBool())
                continue;
        }
        if (stopped && a == player_) {
            vm_.callEvent(a, "PlayerTick", {Value::floatValue(seconds)});
            continue;
        }
        if (a == player_) {
            // The input axes, as the engine sets them each frame: those
            // the keys are bound to (DefUser.ini's MoveForward is "Axis
            // aBaseY Speed=+300", TurnRight "Axis aBaseX Speed=+150"...),
            // the rest 0; PlayerInput works out the others from them. A
            // held key adds its speed times the frame's time, and the
            // engine then scales the axes by 20 over the frame's time: 20
            // times the speed in all (the scripts' climbing speed,
            // aForward x 0.02, is 120 a second).
            constexpr float kHeld = 20;
            const std::pair<const char*, float> axes[] = {
                {"aBaseY", controls.forward * 300 * kHeld}, {"aStrafe", controls.strafe * 300 * kHeld},
                {"aBaseX", controls.turn * 150 * kHeld},    {"aLookUp", controls.lookUp * 300 * kHeld},
                {"aMouseX", controls.mouseX}, {"aMouseY", controls.mouseY},
                {"aUp", controls.jump ? 300.0f * kHeld : 0.0f},
                {"aForward", 0}, {"aTurn", 0}, {"aBaseZ", 0},
            };
            for (const auto& [axis, value] : axes)
                if (Value* v = a->find(vm_, axis))
                    v->f = value;
            controls.mouseX = controls.mouseY = 0; // the mouse's movement goes into one tick
            for (const auto& [button, held] : {std::pair{"bExtra1", controls.jump}, std::pair{"bFire", controls.fire}})
                if (Value* v = a->find(vm_, button))
                    v->i = held ? 1 : 0;
            vm_.callEvent(a, "PlayerInput", {Value::floatValue(seconds)});
            vm_.callEvent(a, "PlayerTick", {Value::floatValue(seconds)});
        }
        vm_.tick(a, seconds);
        if (!a->deleted)
            physics(a, seconds);
        // Once the player has moved, UpdateEyeHeight: the view's height,
        // and in The Fallen the arms' IK, which puts the weapon in hand.
        if (a == player_ && !a->deleted)
            vm_.callEvent(a, "UpdateEyeHeight", {Value::floatValue(seconds)});
        if (!a->deleted)
            animate(a, seconds);
        if (!a->deleted)
            moveMultiMeshes(a, seconds);
        // A life span running out: Expired, then gone.
        if (Value* life = a->deleted ? nullptr : prop(a, "LifeSpan"); life && life->asFloat() > 0) {
            life->f -= seconds;
            if (life->f <= 0) {
                life->f = 0;
                vm_.callEvent(a, "Expired");
                destroy(a);
            }
        }
    }
    // What's attached (C3DAttach) goes where its holder now is.
    placeAttached();
    // The console ticks as the client does, paused or not (its UWindow
    // menus).
    if (VmObject* con = console())
        vm_.callEvent(con, "Tick", {Value::floatValue(seconds)});
    if (!stopped) {
        tickDialog(seconds);
        tickBarks(seconds);
    }
    tickFlis(seconds);
    ++ticks_;
    actors_.erase(std::remove_if(actors_.begin(), actors_.end(), [](VmObject* a) { return a->deleted; }),
                  actors_.end());
}

VmObject* ScriptLevel::spawn(VmClass* cls, VmObject* owner, NameId tag, const Value* location, const Value* rotation) {
    if (!cls || (cls->classFlags & 0x1)) { // abstract
        vm_.reportError("Spawn of " + std::string(cls ? "abstract class " + cls->name : "no class"));
        return nullptr;
    }
    VmObject* a = vm_.create(cls, 0, levelObject_);
    if (!a->isActor) {
        a->deleted = true;
        vm_.reportError("Spawn of " + cls->name + ", not an actor");
        return nullptr;
    }
    actors_.push_back(a);
    init(a);
    if (Value* v = a->find(vm_, "Owner"))
        v->o = owner;
    if (Value* v = a->find(vm_, "Tag"))
        v->i = tag ? tag : cls->nameId;
    if (location) // where it fits, if it collides with the world
        place(a, Vm::vectorOf(*location));
    if (rotation)
        if (Value* v = a->find(vm_, "Rotation"))
            vm_.assign(*v, *rotation);
    updateRegion(a);
    if (owner && !owner->deleted)
        vm_.callEvent(owner, "GainedChild", {Value::objectValue(a)});
    for (const char* event : {"Spawned", "PreBeginPlay", "BeginPlay", "PostBeginPlay", "SetInitialState"}) {
        if (a->deleted)
            break;
        vm_.callEvent(a, event);
    }
    return a->deleted ? nullptr : a;
}

void ScriptLevel::destroy(VmObject* a) {
    if (!a || a->deleted)
        return;
    vm_.callEvent(a, "Destroyed");
    if (Value* owner = a->find(vm_, "Owner"); owner && owner->asObject())
        vm_.callEvent(owner->o, "LostChild", {Value::objectValue(a)});
    if (Value* v = a->find(vm_, "bDeleteMe"))
        v->i = 1;
    a->deleted = true;
    bends_.erase(a);
    ghosts_.erase(std::remove(ghosts_.begin(), ghosts_.end(), a), ghosts_.end());
    decals_.erase(a);
    sparkFlash_.erase(a);
    ledgePush_.erase(a);
    clambers_.erase(a);
    noiseQuiet_.erase(a);
    barks_.erase(a);
}

void ScriptLevel::makeGhost(VmObject* a, bool ghost) {
    if (!a || a->deleted)
        return;
    const bool now = std::find(ghosts_.begin(), ghosts_.end(), a) != ghosts_.end();
    if (ghost == now)
        return;
    if (ghost) {
        actors_.erase(std::remove(actors_.begin(), actors_.end(), a), actors_.end());
        ghosts_.push_back(a);
        touching_.erase(a);
    } else {
        ghosts_.erase(std::remove(ghosts_.begin(), ghosts_.end(), a), ghosts_.end());
        actors_.push_back(a);
    }
    if (Value* v = prop(a, "bIsGhost"))
        *v = Value::boolValue(ghost);
    collidersTick_ = -1;
}

void ScriptLevel::tickActor(VmObject* a, float seconds) {
    if (!a || a->deleted)
        return;
    vm_.tick(a, seconds);
    if (!a->deleted)
        animate(a, seconds);
}

namespace {

std::string iniSettingIn(const std::filesystem::path& file, std::string_view section, std::string_view key) {
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string wantSection = "[" + lowerText(std::string(section)) + "]";
    const std::string wantKey = lowerText(std::string(key)) + "=";
    bool inSection = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty() && line.front() == '[') {
            inSection = lowerText(line) == wantSection;
            continue;
        }
        if (inSection && lowerText(line).rfind(wantKey, 0) == 0)
            return line.substr(wantKey.size());
    }
    return "";
}

} // namespace

std::string iniSetting(const PackageSet& packages, std::string_view section, std::string_view key) {
    return iniSettingIn(packages.iniFile(), section, key);
}

std::string iniSetting(const PackageSet& packages, std::string_view file, std::string_view section,
                       std::string_view key) {
    return iniSettingIn(packages.iniFile().parent_path() / std::string(file), section, key);
}

std::string defaultGameType(const PackageSet& packages) {
    const std::string game = iniSetting(packages, "Engine.Engine", "DefaultGame");
    return game.empty() ? "Engine.GameInfo" : game;
}

Error runLevelStart(const PackageSet& packages, const Package& map, const std::string& defaultGame, float seconds,
                    LevelStart& out) {
    out = {};
    const auto t0 = std::chrono::steady_clock::now();
    Vm vm(packages);
    ScriptLevel level(vm, map);
    if (Error err = level.load())
        return err;
    struct Saved {
        VmObject* actor;
        int drawType, hidden, lightType, lightBrightness;
    };
    auto byteOf = [&](VmObject* a, const char* name) {
        const Value* v = a->find(vm, name);
        return v ? v->asInt() : -1;
    };
    std::vector<std::pair<int, Saved>> before;
    for (VmObject* a : level.actors())
        if (a->package == &map)
            before.push_back({a->exportIndex, {a, byteOf(a, "DrawType"), byteOf(a, "bHidden"), byteOf(a, "LightType"),
                                               byteOf(a, "LightBrightness")}});
    level.beginPlay(defaultGame);
    std::string loginError;
    if (VmObject* p = level.login(iniSetting(packages, "URL", "Class"), iniSetting(packages, "URL", "Name"), loginError))
        out.player = p->cls->name;
    out.loginError = loginError;
    for (int i = 0; i < int(seconds * 30); ++i)
        level.tick(1.0f / 30);
    for (const auto& [index, was] : before) {
        VmObject* a = was.actor;
        ScriptedActor c;
        bool any = false;
        if (a->deleted) {
            c.removed = any = true;
            ++out.removed;
        } else {
            if (byteOf(a, "bHidden") != was.hidden) {
                c.hidden = byteOf(a, "bHidden");
                any = true;
                if (c.hidden == 1)
                    ++out.hidden;
                else
                    ++out.shown;
            }
            if (byteOf(a, "DrawType") != was.drawType) {
                c.drawType = byteOf(a, "DrawType");
                any = true;
                ++out.drawTypes;
            }
            const int type = byteOf(a, "LightType"), brightness = byteOf(a, "LightBrightness");
            if (type != was.lightType || brightness != was.lightBrightness) {
                c.lightType = type;
                c.lightBrightness = brightness;
                any = true;
                ++out.lights;
            }
        }
        if (any)
            out.changed[index] = c;
    }
    out.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    out.errors = vm.stats.errors.size();
    out.missingNatives = vm.stats.missingNatives.size();
    out.accessedNone = vm.stats.accessedNone;
    return Error::success();
}

// --- Engine natives -------------------------------------------------------------------------

namespace {

using Vec = std::array<float, 3>;

Vec location(Vm& vm, VmObject* a) {
    Value* v = a ? a->find(vm, "Location") : nullptr;
    return v ? Vm::vectorOf(*v) : Vec{0, 0, 0};
}

float distance(const Vec& a, const Vec& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The level's live actors of a class (the class argument a class value).
std::vector<Value> actorsOf(Vm& vm, const Value& classArg) {
    std::vector<Value> out;
    if (!vm.level)
        return out;
    VmObject* c = classArg.asObject();
    const VmClass* cls = c && c->asClass ? c->asClass : nullptr;
    for (VmObject* a : vm.level->actors())
        if (!a->deleted && (!cls || (a->cls && a->cls->isA(cls))))
            out.push_back(Value::objectValue(a));
    return out;
}

} // namespace

void registerEngineNatives(Vm& vm) {
    if (vm.hasNative("Actor.Spawn"))
        return;
    registerMovementNatives(vm);
    registerSplineNatives(vm);
    registerAnimationNatives(vm);
    registerAttachNatives(vm);
    registerPathNatives(vm);
    registerDialogNatives(vm);
    registerDocumentNatives(vm);
    registerCanvasNatives(vm);
    registerConsoleNatives(vm);
    registerSaveNatives(vm);
    registerSoundNatives(vm);
    registerEffectNatives(vm);
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto B = [](bool v) { return Value::boolValue(v); };

    reg("Actor.Spawn", [](NativeCall& c) {
        VmObject* cls = c.o(0);
        if (!c.vm.level || !cls || !cls->asClass) {
            c.result = Value::objectValue(nullptr);
            return;
        }
        const bool hasLoc = c.given.size() > 3 && c.given[3], hasRot = c.given.size() > 4 && c.given[4];
        // Where it's spawned: given, or the spawner's place.
        Value here = c.self && c.self->find(c.vm, "Location") ? *c.self->find(c.vm, "Location") : Value();
        Value turn = c.self && c.self->find(c.vm, "Rotation") ? *c.self->find(c.vm, "Rotation") : Value();
        VmObject* owner = c.given.size() > 1 && c.given[1] ? c.o(1) : c.self;
        c.result = Value::objectValue(c.vm.level->spawn(cls->asClass, owner, c.given.size() > 2 && c.given[2] ? c.i(2) : 0,
                                                        hasLoc ? &c.args[3] : &here, hasRot ? &c.args[4] : &turn));
    });
    reg("Actor.Destroy", [=](NativeCall& c) {
        VmObject* a = c.self;
        Value* noDelete = a ? a->find(c.vm, "bNoDelete") : nullptr;
        Value* isStatic = a ? a->find(c.vm, "bStatic") : nullptr;
        if (!a || !c.vm.level || (noDelete && noDelete->asBool()) || (isStatic && isStatic->asBool())) {
            c.result = B(false);
            return;
        }
        c.vm.level->destroy(a);
        c.result = B(true);
    });
    reg("Actor.SetTimer", [](NativeCall& c) {
        if (!c.self)
            return;
        c.self->timerRate = std::max(0.0f, c.f(0));
        c.self->timerCount = 0;
        c.self->timerLoop = c.b(1);
    });
    reg("Actor.Sleep", [](NativeCall& c) { c.latentSeconds = std::max(0.0f, c.f(0)); });
    // Latent functions that wait for what isn't simulated yet: done at once.
    for (const char* done : {"Actor.FinishInterpolation"})
        reg(done, [](NativeCall& c) { c.latentSeconds = 0; });
    reg("Actor.SetRotation", [=](NativeCall& c) {
        if (Value* v = c.self ? c.self->find(c.vm, "Rotation") : nullptr)
            c.vm.assign(*v, c.args[0]);
        c.result = B(true);
    });
    reg("Actor.SetCollision", [](NativeCall& c) {
        if (!c.self)
            return;
        const char* names[] = {"bCollideActors", "bBlockActors", "bBlockPlayers"};
        for (size_t k = 0; k < 3; ++k)
            if (c.given.size() > k && c.given[k])
                if (Value* v = c.self->find(c.vm, names[k]))
                    v->i = c.b(k);
        if (c.vm.level)
            c.vm.level->collisionChanged();
    });
    reg("Actor.SetCollisionSize", [=](NativeCall& c) {
        if (c.self) {
            if (Value* v = c.self->find(c.vm, "CollisionRadius"))
                v->f = c.f(0);
            if (Value* v = c.self->find(c.vm, "CollisionHeight"))
                v->f = c.f(1);
        }
        c.result = B(true);
    });
    reg("Actor.SetOwner", [](NativeCall& c) {
        if (Value* v = c.self ? c.self->find(c.vm, "Owner") : nullptr)
            v->o = c.o(0);
    });
    reg("Actor.SetBase", [](NativeCall& c) {
        if (Value* v = c.self ? c.self->find(c.vm, "Base") : nullptr)
            v->o = c.o(0);
    });
    // StopWaiting(): a Sleep its state code is in ends now (it goes on at
    // the next tick), as UE1's does.
    reg("Pawn.StopWaiting", [](NativeCall& c) {
        if (c.self && c.self->latent && c.self->sleeping > 0)
            c.self->sleeping = 0;
    });
    // MakeNoise(float Loudness): the AI hears it (ScriptLevel::makeNoise).
    reg("Actor.MakeNoise", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->makeNoise(c.self, c.f(0));
    });
    // Not heard: a demo's sounds, a dummy sound. Sounds themselves are
    // vm_sound.cpp's.
    for (const char* quiet : {"Actor.DemoPlaySound", "Actor.PlayDummySound"})
        reg(quiet, [](NativeCall&) {});
    reg("Actor.GetRegion", [](NativeCall& c) { // GetRegion(Vector Point, out PointRegion Region)
        if (c.vm.level)
            c.args[1] = c.vm.level->regionAt(Vm::vectorOf(c.args[0]));
    });
    reg("Actor.GetMapName", [](NativeCall& c) { c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : ""); });
    reg("Actor.GetURLMap", [](NativeCall& c) { c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : ""); });
    reg("Actor.PlayerCanSeeMe", [=](NativeCall& c) { c.result = B(false); });
    reg("Actor.Error", [](NativeCall& c) {
        c.vm.reportError("Error() from " + (c.self && c.self->cls ? c.self->cls->name : std::string("?")) + ": " + c.s(0));
        if (c.vm.level && c.self)
            c.vm.level->destroy(c.self);
    });

    // Iterators: their results, put in the out parameter in turn.
    reg("Actor.AllActors", [](NativeCall& c) {
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        for (Value& v : actorsOf(c.vm, c.args[0])) {
            Value* tag = byTag ? v.o->find(c.vm, "Tag") : nullptr;
            if (!byTag || (tag && tag->i == c.i(2)))
                c.items.push_back(v);
        }
    });
    // ListActors(class<Actor> BaseClass, out Actor Actor, out ActorListEntry
    // ListEntry, Actor inActor): the actors in inActor's ActorList (its
    // scripts fill it with what it's seen: ScanForActors) of the class, each
    // with its entry; empty entries left out.
    reg("Actor.ListActors", [](NativeCall& c) {
        VmObject* of = c.o(3);
        const VmObject* cls = c.o(0);
        const VmProperty* p = of && of->cls ? of->cls->findProperty(c.vm.name("ActorList")) : nullptr;
        if (!p || !p->structType)
            return;
        const VmProperty* item = p->structType->findProperty(c.vm.name("Item"));
        if (!item)
            return;
        for (int k = 0; k < p->arrayDim && size_t(p->slot + k) < of->values.size(); ++k) {
            const Value& entry = of->values[size_t(p->slot + k)];
            if (size_t(item->slot) >= entry.fields.size())
                continue;
            VmObject* a = entry.fields[size_t(item->slot)].asObject();
            if (!a || a->deleted || (cls && cls->asClass && !(a->cls && a->cls->isA(cls->asClass))))
                continue;
            c.items.push_back(Value::objectValue(a));
            c.items2.push_back(entry);
        }
    });
    // GhostActors(class<Actor> BaseClass, out Actor Actor, optional name
    // MatchTag, optional bool bDontUnghostAutomatically): the ghosts of the
    // class with that tag, each an actor again unless asked not to.
    reg("Actor.GhostActors", [](NativeCall& c) {
        if (!c.vm.level)
            return;
        const VmObject* cls = c.o(0);
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        const std::vector<VmObject*> ghosts = c.vm.level->ghosts();
        for (VmObject* a : ghosts) {
            if (a->deleted || (cls && cls->asClass && !(a->cls && a->cls->isA(cls->asClass))))
                continue;
            if (const Value* tag = a->find(c.vm, "Tag"); byTag && (!tag || tag->i != c.i(2)))
                continue;
            if (!c.b(3))
                c.vm.level->makeGhost(a, false);
            c.items.push_back(Value::objectValue(a));
        }
    });
    // MakeGhost(bool bGhost): out of the level, or back.
    reg("Actor.MakeGhost", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->makeGhost(c.self, c.b(0));
    });
    // ScriptTick(float DeltaTime): the actor ticked by its owner, the HUD
    // its portrait, so it moves while the game's paused; when it isn't,
    // the level ticks it already, and once a tick is enough.
    reg("Actor.ScriptTick", [](NativeCall& c) {
        if (c.vm.level && c.vm.level->paused())
            c.vm.level->tickActor(c.self, c.f(0));
    });
    // GetRendererFlags(): what the renderer does itself. Bit 2 hides The
    // Fallen's blob shadows (CTV_Shadow) where it draws shadows of its
    // own; Reluva's renderers draw none, so no flags.
    reg("Actor.GetRendererFlags", [](NativeCall& c) { c.result = Value::intValue(0); });
    // UpdateMultiSkins(): its MultiSkins changed. The page reads them each
    // frame, so there's nothing to bring up to date.
    reg("Actor.UpdateMultiSkins", [](NativeCall&) {});
    reg("Actor.ChildActors", [](NativeCall& c) {
        for (Value& v : actorsOf(c.vm, c.args[0]))
            if (Value* owner = v.o->find(c.vm, "Owner"); owner && owner->o == c.self)
                c.items.push_back(v);
    });
    reg("Actor.BasedActors", [](NativeCall& c) {
        for (Value& v : actorsOf(c.vm, c.args[0]))
            if (Value* base = v.o->find(c.vm, "Base"); base && base->o == c.self)
                c.items.push_back(v);
    });
    // Within a radius (no line of sight yet, so the visible ones too).
    for (const char* radius : {"Actor.RadiusActors", "Actor.VisibleActors", "Actor.VisibleCollidingActors"})
        reg(radius, [](NativeCall& c) {
            const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : location(c.vm, c.self);
            for (Value& v : actorsOf(c.vm, c.args[0]))
                if (distance(location(c.vm, v.o), at) <= c.f(2))
                    c.items.push_back(v);
        });
    // RadiusPawns(class<Actor> BaseClass, out Actor Actor, float Radius,
    // optional Vector Loc): the pawns among those (a pawn's head looks for
    // someone to look at with it).
    reg("Actor.RadiusPawns", [](NativeCall& c) {
        const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : location(c.vm, c.self);
        for (Value& v : actorsOf(c.vm, c.args[0]))
            if (c.vm.isA(v.o, "Pawn") && distance(location(c.vm, v.o), at) <= c.f(2))
                c.items.push_back(v);
    });
    reg("Actor.TouchingActors", [](NativeCall&) {}); // no collision yet
    reg("Actor.AllPawns", [](NativeCall& c) {
        for (Value& v : actorsOf(c.vm, c.args[0]))
            if (c.vm.isA(v.o, "Pawn"))
                c.items.push_back(v);
    });

    // Pawns keep a list in the LevelInfo.
    reg("Pawn.AddPawn", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        Value* list = c.vm.level->levelInfo()->find(c.vm, "PawnList");
        Value* next = c.self->find(c.vm, "nextPawn");
        if (list && next) {
            next->o = list->asObject();
            list->o = c.self;
        }
    });
    reg("Pawn.RemovePawn", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        Value* link = c.vm.level->levelInfo()->find(c.vm, "PawnList");
        for (int guard = 0; link && link->asObject() && guard < 100000; ++guard) {
            if (link->o == c.self) {
                Value* next = c.self->find(c.vm, "nextPawn");
                link->o = next ? next->asObject() : nullptr;
                return;
            }
            link = link->o->find(c.vm, "nextPawn");
        }
    });
    reg("LevelInfo.GetLocalURL", [](NativeCall& c) {
        c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : "");
    });
    reg("LevelInfo.GetAddressURL", [](NativeCall& c) { c.result = Value::stringValue("127.0.0.1"); });
}

} // namespace reluva::unreal
