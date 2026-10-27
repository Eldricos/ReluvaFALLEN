// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/level_play.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

LevelPlay::LevelPlay(const PackageSet& packages, const Package& map)
    : packages_(packages), map_(map), vm_(std::make_unique<Vm>(packages)),
      level_(std::make_unique<ScriptLevel>(*vm_, map)) {
    const char* names[FieldCount] = {"Location", "PrePivot", "Rotation", "DrawScale", "DrawType",
                                     "bHidden",  "Mesh",     "Skin",     "Texture",   "MultiSkins",
                                     "AnimSequence", "AnimFrame", "AnimRate", "bAnimLoop", "Style", "ScaleGlow"};
    for (size_t k = 0; k < FieldCount; ++k)
        names_[k] = vm_->name(names[k]);
}

Error LevelPlay::start() {
    if (Error err = level_->load())
        return err;
    level_->beginPlay(defaultGameType(packages_));
    player_ = level_->login(iniSetting(packages_, "URL", "Class"), iniSetting(packages_, "URL", "Name"), loginError_);
    return Error::success();
}

void LevelPlay::advance(float seconds, int maxTicks) {
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
    return true;
}

int LevelPlay::health() {
    const Value* v = player_ ? player_->find(*vm_, "Health") : nullptr;
    return v ? v->asInt() : 0;
}

std::string LevelPlay::playerState() {
    return player_ && player_->state ? player_->state->name : std::string("none");
}

} // namespace reluva::unreal
