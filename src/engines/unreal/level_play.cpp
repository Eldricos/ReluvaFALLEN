// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/level_play.h"

#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace reluva::unreal {

LevelPlay::LevelPlay(const PackageSet& packages, const Package& map)
    : packages_(packages), map_(map), vm_(std::make_unique<Vm>(packages)),
      level_(std::make_unique<ScriptLevel>(*vm_, map)) {
    const char* names[FieldCount] = {"Location", "PrePivot", "Rotation", "DrawScale", "DrawType",
                                     "bHidden",  "Mesh",     "Skin",     "Texture",   "MultiSkins",
                                     "AnimSequence", "AnimFrame", "AnimRate", "bAnimLoop", "Style", "ScaleGlow",
                                     "LastRenderTime"};
    for (size_t k = 0; k < FieldCount; ++k)
        names_[k] = vm_->name(names[k]);
}

Error LevelPlay::start(const std::string& portal, const std::filesystem::path& saved,
                       const std::filesystem::path& saveFolder) {
    if (Error err = level_->load())
        return err;
    level_->setSaveFolder(saveFolder);
    if (!saved.empty()) {
        if (Error err = level_->restore(saved))
            return err;
        player_ = level_->player();
        return Error::success();
    }
    level_->beginPlay(defaultGameType(packages_));
    player_ = level_->login(iniSetting(packages_, "URL", "Class"), iniSetting(packages_, "URL", "Name"), loginError_,
                            portal);
    return Error::success();
}

void LevelPlay::advance(float seconds, int maxTicks) {
    // A cut scene fast-forwarded: as many ticks as fit in a frame's time
    // (a twentieth of a second of the scripts' running), however few were
    // owed, until the game turns it off.
    if (level_->fastForward()) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        for (int ticks = 0; ticks < 300 && level_->fastForward() && std::chrono::steady_clock::now() < until; ++ticks) {
            level_->tick(kTick);
            time_ += kTick;
        }
        owed_ = 0;
        return;
    }
    owed_ += std::max(0.0f, seconds);
    int ticks = 0;
    while (owed_ >= kTick && ticks < maxTicks) {
        level_->tick(kTick);
        owed_ -= kTick;
        time_ += kTick;
        ++ticks;
    }
    // What's left over a tick is dropped: the game slows down instead.
    if (owed_ >= kTick) {
        behind_ += owed_ - std::fmod(owed_, kTick);
        owed_ = std::fmod(owed_, kTick);
    }
}

void LevelPlay::press(const char* exec) {
    if (!player_ || player_->deleted)
        return;
    if (VmFunction* f = vm_->findFunction(player_, vm_->name(exec)))
        vm_->invoke(f, player_, {Value::floatValue(0)});
}

// A setting of the player's [Engine.Input]: as the game last set it (the
// controls page), else User.ini's, else DefUser.ini's.
std::string LevelPlay::inputSetting(const std::string& key) const {
    const std::string* v = vm_->configSetting("Engine.Input", key);
    return v ? *v : std::string();
}

bool LevelPlay::pressKey(const std::string& key, bool down) {
    // "Jump | Axis aUp Speed=+300": the commands between the bars. A name
    // among the aliases (Aliases[n]=(Command="Button bShowObjectives",Alias=ShowObjectives))
    // is its command. "Button bX" holds the player's input bX while the key
    // is down; an exec function is called as the key goes down, with its
    // argument if it has one ("SwitchWeapon 2"); axes are left out (the
    // page's own controls steer).
    if (aliases_.empty())
        for (int k = 0; k < 64; ++k) {
            const std::string a = inputSetting("Aliases[" + std::to_string(k) + "]");
            const size_t c = a.find("Command=\""), n = a.find("Alias=");
            if (c == std::string::npos || n == std::string::npos)
                continue;
            const size_t cEnd = a.find('"', c + 9);
            std::string alias = a.substr(n + 6);
            alias = alias.substr(0, alias.find_first_of(",)"));
            if (cEnd != std::string::npos)
                aliases_[fs::toLower(alias)] = a.substr(c + 9, cEnd - c - 9);
        }
    std::vector<std::string> commands;
    auto split = [&](const std::string& binding, auto& self, int depth) -> void {
        size_t at = 0;
        while (at <= binding.size()) {
            size_t end = binding.find('|', at);
            if (end == std::string::npos)
                end = binding.size();
            std::string command = binding.substr(at, end - at);
            at = end + 1;
            command.erase(0, command.find_first_not_of(' '));
            command.erase(command.find_last_not_of(' ') + 1);
            if (command.empty())
                continue;
            auto alias = aliases_.find(fs::toLower(command));
            if (alias != aliases_.end() && depth < 4)
                self(alias->second, self, depth + 1);
            else
                commands.push_back(command);
        }
    };
    split(inputSetting(key), split, 0);
    bool any = false;
    for (const std::string& command : commands) {
        const size_t space = command.find(' ');
        const std::string verb = command.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : command.substr(space + 1);
        if (fs::toLower(verb) == "axis")
            continue;
        any = true;
        if (fs::toLower(verb) == "button") {
            if (player_ && !player_->deleted)
                if (Value* v = player_->find(*vm_, rest.substr(0, rest.find(' '))))
                    v->i = down ? 1 : 0;
            continue;
        }
        if (!down || !player_ || player_->deleted)
            continue;
        VmFunction* f = vm_->findFunction(player_, vm_->name(verb));
        if (!f)
            continue;
        std::vector<Value> args;
        if (!rest.empty() && !f->params.empty()) {
            const VmProperty* p = f->params[0];
            switch (p->type) {
            case VType::Byte:
            case VType::Int: args.push_back(Value::intValue(std::atoi(rest.c_str()))); break;
            case VType::Float: args.push_back(Value::floatValue(std::strtof(rest.c_str(), nullptr))); break;
            case VType::Name: args.push_back(Value::nameValue(vm_->name(rest))); break;
            case VType::String: args.push_back(Value::stringValue(rest)); break;
            default: break;
            }
        }
        vm_->invoke(f, player_, args);
    }
    return any;
}

bool LevelPlay::consoleKey(int inputKey, bool down) {
    if (!player_ || player_->deleted)
        return false;
    VmObject* console = level_->console();
    if (!console)
        return false;
    if (down)
        level_->keysDown().insert(inputKey);
    else
        level_->keysDown().erase(inputKey);
    // EInputAction: 1 pressed, 3 let go.
    return vm_->callEvent(console, "KeyEvent",
                          {Value::byteValue(inputKey), Value::byteValue(down ? 1 : 3), Value::floatValue(0)})
        .asBool();
}

bool LevelPlay::paused() const {
    return level_->paused();
}

bool LevelPlay::windowsOpen() const {
    VmObject* console = level_->console();
    const Value* v = console ? console->find(*vm_, "bUWindowActive") : nullptr;
    return v && v->asBool();
}

void LevelPlay::consoleMouse(float x, float y) {
    if (VmObject* console = level_->console()) {
        if (Value* v = console->find(*vm_, "MouseX"))
            *v = Value::floatValue(x);
        if (Value* v = console->find(*vm_, "MouseY"))
            *v = Value::floatValue(y);
    }
}

bool LevelPlay::menuShowing() const {
    const Value* v = player_ && !player_->deleted ? player_->find(*vm_, "bShowMenu") : nullptr;
    return v && v->asBool();
}

std::string LevelPlay::takeTravel() {
    return std::exchange(level_->travel(), std::string());
}

bool LevelPlay::quitAsked() const {
    return level_->quitAsked();
}

void LevelPlay::hudEvent(int code) {
    if (const Value* hud = player_ && !player_->deleted ? player_->find(*vm_, "myHUD") : nullptr; hud && hud->asObject())
        vm_->callEvent(hud->o, "HudEvent", {Value::intValue(code)});
}

LevelPlay::View LevelPlay::view() {
    View v;
    if (!player_ || player_->deleted)
        return v;
    Vm& vm = *vm_;
    // PlayerCalcView(out Actor ViewActor, out Vector CameraLocation, out Rotator CameraRotation)
    Value actor = Value::objectValue(nullptr);
    Value where = vm.vectorValue(0, 0, 0), turned = vm.rotatorValue(0, 0, 0);
    if (const Value* loc = player_->find(vm, "Location"))
        where = *loc;
    if (const Value* rot = player_->find(vm, "ViewRotation"))
        turned = *rot;
    if (VmFunction* f = vm.findFunction(player_, vm.name("PlayerCalcView")))
        vm.invoke(f, player_, {actor, where, turned}, {&actor, &where, &turned});
    const auto l = Vm::vectorOf(where);
    const auto r = Vm::rotatorOf(turned);
    v.location = {l[0], l[1], l[2]};
    v.rotation = {r[0], r[1], r[2]};
    v.actor = actor.asObject();
    if (const Value* fov = player_->find(vm, "FovAngle"); fov && fov->asFloat() > 1)
        v.fov = fov->asFloat();
    if (const Value* hud = player_->find(vm, "myHUD"); hud && hud->asObject())
        if (const Value* bars = hud->o->find(vm, "bLetterBox"))
            v.letterbox = bars->asBool();
    return v;
}

ObjectRef LevelPlay::refOf(const Value* v) const {
    if (!v || !v->asObject() || !v->o->package || v->o->exportIndex < 0)
        return {};
    return {v->o->package, v->o->exportIndex};
}

bool LevelPlay::look(VmObject* a, Look& out) {
    if (!a || a->deleted || !a->cls)
        return false;
    Vm& vm = *vm_;
    auto it = slots_.find(a->cls);
    if (it == slots_.end()) {
        std::array<int, FieldCount> slots{};
        for (size_t k = 0; k < FieldCount; ++k) {
            const VmProperty* p = a->cls->findProperty(names_[k]);
            slots[k] = p ? p->slot : -1;
        }
        it = slots_.emplace(a->cls, slots).first;
    }
    const std::array<int, FieldCount>& slot = it->second;
    auto at = [&](Field f) -> Value* {
        const int k = slot[size_t(f)];
        return k >= 0 && size_t(k) < a->values.size() ? &a->values[size_t(k)] : nullptr;
    };
    auto vec = [&](Field f, Vec3f& to) {
        if (const Value* v = at(f)) {
            const auto p = Vm::vectorOf(*v);
            to = {p[0], p[1], p[2]};
        }
    };
    vec(Location, out.location);
    vec(PrePivot, out.prePivot);
    if (const Value* v = at(Rotation)) {
        const auto r = Vm::rotatorOf(*v);
        out.rotation = {r[0], r[1], r[2]};
    }
    if (const Value* v = at(DrawScale))
        out.drawScale = v->asFloat();
    if (const Value* v = at(DrawType))
        out.drawType = v->asInt();
    if (const Value* v = at(Hidden))
        out.hidden = v->asBool();
    out.mesh = refOf(at(MeshField));
    out.skin = refOf(at(Skin));
    out.texture = refOf(at(Texture));
    if (const int k = slot[MultiSkins]; k >= 0)
        for (size_t i = 0; i < out.multiSkins.size() && size_t(k) + i < a->values.size(); ++i)
            out.multiSkins[i] = refOf(&a->values[size_t(k) + i]); // an array property's elements follow each other
    if (const Value* v = at(AnimSequence))
        out.animSequence = v->i ? vm.nameText(v->i) : std::string();
    if (const Value* v = at(AnimFrame))
        out.animFrame = v->asFloat();
    if (const Value* v = at(AnimRate))
        out.animRate = v->asFloat();
    if (const Value* v = at(AnimLoop))
        out.animLoop = v->asBool();
    if (const Value* v = at(Style))
        out.style = uint8_t(v->asInt());
    if (const Value* v = at(ScaleGlow))
        out.scaleGlow = v->asFloat();
    out.layers.clear();
    if (const auto* layers = level_->layersOf(a))
        for (const auto& [group, l] : *layers)
            if (l.weight > 0 && l.sequence)
                out.layers.push_back({vm.nameText(l.sequence), l.bone ? vm.nameText(l.bone) : std::string(), l.rest,
                                      l.frame, l.weight});
    out.adjust = level_->boneAdjust(a);
    return true;
}

void LevelPlay::drawn(VmObject* a) {
    auto it = a ? slots_.find(a->cls) : slots_.end();
    if (it == slots_.end())
        return;
    const int k = it->second[LastRenderTime];
    if (k >= 0 && size_t(k) < a->values.size())
        if (const Value* now = level_->levelInfo()->find(*vm_, "TimeSeconds"))
            a->values[size_t(k)] = Value::floatValue(now->asFloat());
}

void LevelPlay::preRender(VmObject* a) {
    if (a && !a->deleted)
        vm_->callEvent(a, "C3DPreRender");
}

int LevelPlay::health() {
    const Value* v = player_ ? player_->find(*vm_, "Health") : nullptr;
    return v ? v->asInt() : 0;
}

std::string LevelPlay::title() {
    const Value* v = level_->levelInfo() ? level_->levelInfo()->find(*vm_, "Title") : nullptr;
    return v ? v->s : std::string();
}

std::vector<std::string> LevelPlay::activeObjectives() {
    std::vector<std::string> out;
    VmObject* info = level_->levelInfo();
    const VmProperty* titles = info ? info->cls->findProperty(vm_->name("Objective_Title")) : nullptr;
    const VmProperty* states = info ? info->cls->findProperty(vm_->name("Objective_State")) : nullptr;
    if (!titles || !states)
        return out;
    for (int k = 0; k < titles->arrayDim && k < states->arrayDim; ++k) {
        const Value& t = info->values[size_t(titles->slot + k)];
        if (!t.s.empty() && info->values[size_t(states->slot + k)].asInt() == 0)
            out.push_back(t.s);
    }
    return out;
}

std::string LevelPlay::playerState() {
    return player_ && player_->state ? player_->state->name : std::string("none");
}

} // namespace reluva::unreal
