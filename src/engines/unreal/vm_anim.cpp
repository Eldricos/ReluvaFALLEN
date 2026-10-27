// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Actors' animation for a running level (ScriptLevel): PlayAnim, LoopAnim
// and TweenAnim start an animation of the actor's mesh; each tick moves it
// on at its rate, calls the functions its mesh names along the way
// (notifications: footsteps, weapons fired, jumps' push), sends AnimEnd at
// its end (and each loop's), and ends a latent FinishAnim waiting for it.
// Reluva's reading of the public UnrealScript documentation of these
// natives and of what The Fallen's scripts expect: see
// docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/mesh.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::unreal {

namespace {

// A mesh's animations by name (lower case), its own and those its frames
// don't have (borrowed from the shared sets when drawn): read once per
// mesh.
using AnimTable = std::map<std::string, MeshAnimSeq>;

const AnimTable* animsOf(Vm& vm, VmObject* a) {
    static std::map<std::pair<const Package*, int>, AnimTable> cache;
    Value* m = a ? a->find(vm, "Mesh") : nullptr;
    VmObject* mesh = m ? m->asObject() : nullptr;
    if (!mesh || !mesh->package)
        return nullptr;
    const auto key = std::make_pair(mesh->package, mesh->exportIndex);
    auto it = cache.find(key);
    if (it == cache.end()) {
        Mesh read;
        AnimTable table;
        if (!readMesh(*mesh->package, mesh->exportIndex, read))
            for (const auto* list : {&read.anims, &read.otherAnims})
                for (const MeshAnimSeq& s : *list)
                    table.emplace(fs::toLower(s.name), s);
        it = cache.emplace(key, std::move(table)).first;
    }
    return &it->second;
}

const MeshAnimSeq* findAnim(Vm& vm, VmObject* a, NameId sequence) {
    const AnimTable* table = animsOf(vm, a);
    if (!table)
        return nullptr;
    auto it = table->find(fs::toLower(vm.nameText(sequence)));
    return it == table->end() ? nullptr : &it->second;
}

bool boolOf(const Value* v) { return v && v->asBool(); }

// Where through an animation its last frame is (AnimLast): a still one
// is there from the start.
float lastOf(const MeshAnimSeq& s) { return s.numFrames > 1 ? 1 - 1.0f / float(s.numFrames) : 0; }

} // namespace

// PlayAnim, LoopAnim: the sequence from its start (after a tween into it
// of `tween` seconds), `rate` times its own speed.
void ScriptLevel::startAnim(VmObject* a, NameId sequence, float rate, float tween, bool loop) {
    Value* seqValue = prop(a, "AnimSequence");
    if (!seqValue)
        return;
    const MeshAnimSeq* s = findAnim(vm_, a, sequence);
    // Looping it already: only its speed changes.
    if (loop && s && seqValue->i == sequence && boolOf(prop(a, "bAnimLoop")) && animating(a)) {
        if (Value* r = prop(a, "AnimRate"))
            r->f = rate * s->rate / float(std::max(1, s->numFrames));
        return;
    }
    seqValue->i = sequence;
    auto set = [&](const char* name, float v) {
        if (Value* p = prop(a, name))
            p->f = v;
    };
    auto setBool = [&](const char* name, bool v) {
        if (Value* p = prop(a, name))
            p->i = v ? 1 : 0;
    };
    setBool("bAnimLoop", loop);
    if (!s) { // a sequence its mesh hasn't (or no mesh): nothing to play, done at once
        set("AnimFrame", 0);
        set("AnimRate", 0);
        set("AnimLast", 0);
        setBool("bAnimFinished", true);
        return;
    }
    const float n = float(std::max(1, s->numFrames));
    set("AnimRate", std::max(0.0f, rate) * s->rate / n);
    set("AnimLast", lastOf(*s));
    setBool("bAnimFinished", false);
    if (tween > 0) {
        set("TweenRate", 1 / tween);
        set("AnimFrame", -1 / n); // below 0: tweening into the first frame
    } else {
        set("AnimFrame", 0);
    }
}

// TweenAnim: from wherever it is to the sequence's first frame over
// `seconds`, then still (AnimEnd when there).
void ScriptLevel::tweenAnim(VmObject* a, NameId sequence, float seconds) {
    Value* seqValue = prop(a, "AnimSequence");
    if (!seqValue)
        return;
    seqValue->i = sequence;
    const MeshAnimSeq* s = findAnim(vm_, a, sequence);
    const float n = s ? float(std::max(1, s->numFrames)) : 1;
    if (Value* p = prop(a, "bAnimLoop"))
        p->i = 0;
    if (Value* p = prop(a, "bAnimFinished"))
        p->i = 0;
    if (Value* p = prop(a, "AnimRate"))
        p->f = 0;
    if (Value* p = prop(a, "AnimLast"))
        p->f = s ? lastOf(*s) : 0;
    // A tween of no time ends at the next tick.
    if (Value* p = prop(a, "TweenRate"))
        p->f = seconds > 0 ? 1 / seconds : 1e6f;
    if (Value* p = prop(a, "AnimFrame"))
        p->f = -1 / n;
}

bool ScriptLevel::animating(VmObject* a) {
    const Value* frame = prop(a, "AnimFrame");
    const Value* rate = prop(a, "AnimRate");
    return (frame && frame->asFloat() < 0) || (rate && rate->asFloat() > 0);
}

void ScriptLevel::waitForAnim(VmObject* a) {
    animWaits_[a] = a->stateSerial;
}

void ScriptLevel::animate(VmObject* a, float dt) {
    Value* frame = prop(a, "AnimFrame");
    Value* rate = prop(a, "AnimRate");
    if (!frame || !rate || (frame->asFloat() >= 0 && rate->asFloat() <= 0)) {
        endAnimWait(a, false);
        return;
    }
    const Value* seqValue = prop(a, "AnimSequence");
    const MeshAnimSeq* s = seqValue ? findAnim(vm_, a, seqValue->i) : nullptr;
    const float n = s ? float(std::max(1, s->numFrames)) : 1;
    float left = dt;
    // Tweening into the first frame.
    if (frame->f < 0) {
        const Value* tweenRate = prop(a, "TweenRate");
        const float speed = tweenRate ? std::max(0.0f, tweenRate->asFloat()) : 0;
        const float towards = -frame->f * n; // 1 at the tween's start, 0 at its end
        if (speed <= 0 || towards > speed * left) {
            frame->f = -(towards - speed * left) / n;
            return;
        }
        left -= speed > 0 ? towards / speed : left;
        frame->f = 0;
        if (rate->f <= 0) { // TweenAnim: there
            if (Value* f = prop(a, "bAnimFinished"))
                f->i = 1;
            vm_.callEvent(a, "AnimEnd");
            endAnimWait(a, true);
            return;
        }
    }
    // Playing: on by its rate, past the notifications on the way.
    const bool loop = boolOf(prop(a, "bAnimLoop"));
    const float last = s ? lastOf(*s) : 0;
    const float end = loop ? 1.0f : last; // a loop goes round through its last frame back to its first
    const float from = frame->f;
    float to = from + rate->f * left;
    auto notify = [&](float lo, float hi) {
        if (!s)
            return;
        for (const MeshAnimSeq::Notify& note : s->notifies)
            if (note.time >= lo && note.time < hi && !note.function.empty())
                if (VmFunction* fn = vm_.findFunction(a, vm_.name(note.function)))
                    vm_.invoke(fn, a, {});
    };
    if (to < end) {
        frame->f = to;
        notify(from, to);
        return;
    }
    notify(from, end);
    if (a->deleted)
        return;
    if (loop) {
        const NameId playing = seqValue ? seqValue->i : 0;
        frame->f = std::fmod(to - end, 1.0f);
        vm_.callEvent(a, "AnimEnd");
        endAnimWait(a, true);
        // Still the same loop (AnimEnd may have started another): on into
        // its next round.
        if (!a->deleted && seqValue && seqValue->i == playing && boolOf(prop(a, "bAnimLoop")) && frame->f > 0)
            notify(0, frame->f);
    } else {
        frame->f = last;
        rate->f = 0;
        if (Value* f = prop(a, "bAnimFinished"))
            f->i = 1;
        vm_.callEvent(a, "AnimEnd");
        endAnimWait(a, true);
    }
}

// A latent FinishAnim waiting on this actor ends: at the end of its
// animation (or a loop of it), or at once if it isn't animating.
void ScriptLevel::endAnimWait(VmObject* a, bool ended) {
    auto it = animWaits_.find(a);
    if (it == animWaits_.end())
        return;
    if (!ended && animating(a))
        return;
    if (a->latent && a->stateSerial == it->second)
        a->sleeping = 0;
    animWaits_.erase(it);
}

void registerAnimationNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // PlayAnim(name Sequence, optional float Rate, optional float TweenTime)
    reg("Actor.PlayAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->startAnim(c.self, c.i(0), c.given.size() > 1 && c.given[1] ? c.f(1) : 1,
                                  c.given.size() > 2 && c.given[2] ? c.f(2) : 0, false);
    });
    // LoopAnim(name Sequence, optional float Rate, optional float TweenTime, optional float MinRate)
    reg("Actor.LoopAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->startAnim(c.self, c.i(0), c.given.size() > 1 && c.given[1] ? c.f(1) : 1,
                                  c.given.size() > 2 && c.given[2] ? c.f(2) : 0, true);
    });
    // TweenAnim(name Sequence, float Time)
    reg("Actor.TweenAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->tweenAnim(c.self, c.i(0), c.f(1));
    });
    reg("Actor.IsAnimating", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->animating(c.self));
    });
    // Latent: until the animation (or this loop of it) ends.
    reg("Actor.FinishAnim", [](NativeCall& c) {
        if (!c.vm.level || !c.self || !c.vm.level->animating(c.self)) {
            c.latentSeconds = 0;
            return;
        }
        c.vm.level->waitForAnim(c.self);
        c.latentSeconds = 1e9f;
    });
    // GetAnimDuration(name Sequence, optional float Rate): its length in
    // seconds played at `Rate` times its speed (0 for one it hasn't).
    reg("Actor.GetAnimDuration", [](NativeCall& c) {
        const MeshAnimSeq* s = findAnim(c.vm, c.self, c.i(0));
        const float rate = c.given.size() > 1 && c.given[1] ? c.f(1) : 1;
        c.result = Value::floatValue(s && s->rate > 0 && rate > 0 ? float(s->numFrames) / (s->rate * rate) : 0);
    });
    reg("Actor.HasAnim", [](NativeCall& c) { c.result = Value::boolValue(findAnim(c.vm, c.self, c.i(0)) != nullptr); });
    reg("Actor.GetAnimGroup", [](NativeCall& c) {
        const MeshAnimSeq* s = findAnim(c.vm, c.self, c.i(0));
        c.result = Value::nameValue(c.vm.name(s && !s->group.empty() ? s->group : "None"));
    });
}

} // namespace reluva::unreal
