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

// What the animation natives need of an actor's mesh, read once per mesh:
// its animations by name (lower case), its own and those its frames don't
// have (borrowed from the shared sets when drawn), and what kind it is.
struct MeshInfo {
    std::map<std::string, MeshAnimSeq> anims;
    bool boned = false; // built on a skeleton
    bool c3d = false;   // in The Fallen's own layout (its imported-model block)
};

const MeshInfo* meshInfoOf(Vm& vm, VmObject* a) {
    static std::map<std::pair<const Package*, int>, MeshInfo> cache;
    Value* m = a ? a->find(vm, "Mesh") : nullptr;
    VmObject* mesh = m ? m->asObject() : nullptr;
    if (!mesh || !mesh->package)
        return nullptr;
    const auto key = std::make_pair(mesh->package, mesh->exportIndex);
    auto it = cache.find(key);
    if (it == cache.end()) {
        Mesh read;
        MeshInfo info;
        if (!readMesh(*mesh->package, mesh->exportIndex, read)) {
            for (const auto* list : {&read.anims, &read.otherAnims})
                for (const MeshAnimSeq& s : *list)
                    info.anims.emplace(fs::toLower(s.name), s);
            info.boned = read.bones > 0;
            info.c3d = read.importBlock > 0;
        }
        it = cache.emplace(key, std::move(info)).first;
    }
    return &it->second;
}

const MeshAnimSeq* findAnim(Vm& vm, VmObject* a, NameId sequence) {
    const MeshInfo* info = meshInfoOf(vm, a);
    if (!info)
        return nullptr;
    auto it = info->anims.find(fs::toLower(vm.nameText(sequence)));
    return it == info->anims.end() ? nullptr : &it->second;
}

// Calls the functions an animation's notifications name, for those from
// `lo` (included) to `hi` through it.
void notifyBetween(Vm& vm, VmObject* a, const MeshAnimSeq* s, float lo, float hi) {
    if (!s)
        return;
    for (const MeshAnimSeq::Notify& note : s->notifies)
        if (note.time >= lo && note.time < hi && !note.function.empty() && !a->deleted)
            if (VmFunction* fn = vm.findFunction(a, vm.name(note.function)))
                vm.invoke(fn, a, {});
}

bool boolOf(const Value* v) { return v && v->asBool(); }

} // namespace

// PlayAnim, LoopAnim, as the engine's: a sequence the mesh hasn't
// changes nothing. AnimRate is Rate x its speed / its frames; AnimLast
// 1 - 1/frames. TweenTime -1 (the default) tweens by the old rate (or a
// fortieth of the sequence); 0 starts at once (just past 0); a tween is
// in units of 1/(TweenTime x frames). None playing before: no tween. A
// one-frame sequence only tweens (1/TweenTime, else 10 a second).
void ScriptLevel::startAnim(VmObject* a, NameId sequence, float rate, float tween, bool loop, float minRate) {
    Value* seqValue = prop(a, "AnimSequence");
    if (!seqValue)
        return;
    const MeshAnimSeq* s = findAnim(vm_, a, sequence);
    if (!s)
        return;
    auto get = [&](const char* name) {
        const Value* p = prop(a, name);
        return p ? p->asFloat() : 0.0f;
    };
    auto set = [&](const char* name, float v) {
        if (Value* p = prop(a, name))
            p->f = v;
    };
    auto setBool = [&](const char* name, bool v) {
        if (Value* p = prop(a, name))
            p->i = v ? 1 : 0;
    };
    const float n = float(std::max(1, s->numFrames));
    const float perRate = s->rate / n;
    // Looping it already: only its speed changes.
    if (loop && seqValue->i == sequence && boolOf(prop(a, "bAnimLoop")) && animating(a)) {
        set("AnimRate", rate * perRate);
        setBool("bAnimFinished", false);
        set("AnimMinRate", minRate != 0 ? minRate * perRate : 0);
        set("OldAnimRate", rate * perRate);
        return;
    }
    if (seqValue->i == 0)
        tween = 0;
    seqValue->i = sequence;
    set("AnimRate", rate * perRate);
    set("AnimLast", 1 - 1 / n);
    set("TweenAnimFrame", 0);
    setBool("bAnimFinished", false);
    setBool("bAnimLoop", loop);
    setBool("bAnimReversed", false);
    setBool("bAnimNotify", !s->notifies.empty());
    set("AnimMinRate", loop && minRate != 0 ? minRate * perRate : 0);
    if (s->numFrames <= 1) {
        set("AnimMinRate", 0);
        set("OldAnimRate", 0);
        setBool("bAnimNotify", false);
        set("AnimRate", 0);
        set("AnimFrame", -1 / n);
        set("TweenRate", tween > 0 ? 1 / tween : 10);
    } else if (tween > 0) {
        set("TweenRate", 1 / (tween * n));
        set("AnimFrame", -1 / n);
    } else if (tween == -1) {
        set("AnimFrame", -1 / n);
        const float old = get("OldAnimRate");
        float speed = 0;
        if (const Value* v = prop(a, "Velocity")) {
            const auto vel = Vm::vectorOf(*v);
            speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        }
        set("TweenRate", old > 0 ? old : old < 0 ? std::max(0.5f * get("AnimRate"), -old * speed) : 1 / (0.025f * n));
    } else {
        set("TweenRate", 0);
        set("AnimFrame", loop ? 0.0001f : 0.001f);
    }
    set("OldAnimRate", get("AnimRate"));
    noteTweenFrom(a, get("AnimFrame") < 0);
}

// TweenAnim: from wherever it is into the sequence over `seconds`, then
// still (AnimEnd when there); no time: there at once, not animating.
void ScriptLevel::tweenAnim(VmObject* a, NameId sequence, float seconds) {
    Value* seqValue = prop(a, "AnimSequence");
    if (!seqValue)
        return;
    noteTweenFrom(a, seconds > 0);
    seqValue->i = sequence;
    const MeshAnimSeq* s = findAnim(vm_, a, sequence);
    const float n = s ? float(std::max(1, s->numFrames)) : 1;
    for (const char* b : {"bAnimFinished", "bAnimLoop", "bAnimNotify"})
        if (Value* p = prop(a, b))
            p->i = 0;
    for (const char* f : {"AnimLast", "AnimMinRate", "AnimRate", "OldAnimRate", "TweenAnimFrame"})
        if (Value* p = prop(a, f))
            p->f = 0;
    if (Value* p = prop(a, "TweenRate"))
        p->f = seconds > 0 ? 1 / (n * seconds) : 0;
    if (Value* p = prop(a, "AnimFrame"))
        p->f = seconds > 0 ? -1 / n : 0;
}

// Where a tween about to begin starts from: the animation showing now. One
// begun mid-tween starts from where that one started (the pose between
// isn't kept).
void ScriptLevel::noteTweenFrom(VmObject* a, bool tweening) {
    if (!tweening) {
        tweenFrom_.erase(a);
        return;
    }
    const Value* seq = prop(a, "AnimSequence");
    const Value* frame = prop(a, "AnimFrame");
    if (!seq || !seq->i || !frame)
        return;
    if (frame->asFloat() < 0 && tweenFrom_.count(a))
        return;
    tweenFrom_[a] = {seq->i, std::max(0.0f, frame->asFloat()), boolOf(prop(a, "bAnimLoop"))};
}

const ScriptLevel::TweenFrom* ScriptLevel::tweenFromOf(VmObject* a) const {
    auto it = tweenFrom_.find(a);
    return it == tweenFrom_.end() ? nullptr : &it->second;
}

bool ScriptLevel::animating(VmObject* a) {
    const Value* seq = prop(a, "AnimSequence");
    const Value* frame = prop(a, "AnimFrame");
    const Value* rate = prop(a, "AnimRate");
    const Value* tween = prop(a, "TweenRate");
    if (!seq || !seq->i || !frame)
        return false;
    return frame->asFloat() >= 0 ? (rate && rate->asFloat() != 0) : (tween && tween->asFloat() != 0);
}

float ScriptLevel::rateNow(VmObject* a, float rate, float minRate) {
    if (rate >= 0)
        return rate;
    float speed = 0;
    if (const Value* v = prop(a, "Velocity")) {
        const auto vel = Vm::vectorOf(*v);
        speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    }
    return std::max(-rate * speed, minRate);
}

void ScriptLevel::waitForAnim(VmObject* a) {
    animWaits_[a] = a->stateSerial;
}

void ScriptLevel::updateHead(VmObject* a) {
    auto it = pawnClasses_.find(a->cls);
    if (it == pawnClasses_.end())
        it = pawnClasses_.emplace(a->cls, vm_.isA(a, "Pawn")).first;
    if (!it->second)
        return;
    // (The event itself returns at once for a pawn not drawn in the last
    // four seconds, by its LastRenderTime, which drawing sets: LevelPlay.)
    // Whether its mesh is one of The Fallen's, by the mesh object: this
    // runs for every pawn every tick.
    const Value* mesh = prop(a, "Mesh");
    VmObject* m = mesh ? mesh->asObject() : nullptr;
    if (!m)
        return;
    auto c3d = c3dMeshes_.find(m);
    if (c3d == c3dMeshes_.end()) {
        const MeshInfo* info = meshInfoOf(vm_, a);
        c3d = c3dMeshes_.emplace(m, info && info->c3d).first;
    }
    if (!c3d->second)
        return;
    if (const Value* drawn = prop(a, "LastRenderTime"); drawn && levelInfo_)
        if (const Value* now = prop(levelInfo_, "TimeSeconds"); now && drawn->asFloat() < now->asFloat() - 4)
            return; // what the event would do first: not drawn lately
    vm_.callEvent(a, "UpdateHeadAnimation");
}

void ScriptLevel::animate(VmObject* a, float dt) {
    updateHead(a);
    if (a->deleted)
        return;
    animateLayers(a, dt);
    if (a->deleted)
        return;
    // As the engine's (in AActor::Tick): up to four passes a tick, each
    // to the next tween's end, notification or end of the sequence.
    Value* frame = prop(a, "AnimFrame");
    Value* rate = prop(a, "AnimRate");
    Value* tweenRate = prop(a, "TweenRate");
    const Value* seqValue = prop(a, "AnimSequence");
    if (!frame || !rate || !seqValue) {
        endAnimWait(a, false);
        return;
    }
    float left = dt;
    for (int pass = 0; pass < 4 && left > 0 && !a->deleted; ++pass) {
        if (!seqValue->i)
            break;
        const bool tweening = frame->f < 0;
        if (tweening ? (!tweenRate || tweenRate->f == 0) : rate->f == 0)
            break;
        const float old = frame->f;
        if (tweening) {
            frame->f += left * tweenRate->f;
            if (frame->f < 0) {
                left = 0;
                break;
            }
            left = frame->f * left / (frame->f - old);
            const Value* landing = prop(a, "TweenAnimFrame");
            frame->f = landing ? landing->asFloat() : 0;
            if (rate->f == 0) {
                if (Value* f = prop(a, "bAnimFinished"))
                    f->i = 1;
                vm_.callEvent(a, "AnimEnd");
                endAnimWait(a, true);
                break;
            }
            continue;
        }
        const bool reversed = boolOf(prop(a, "bAnimReversed"));
        float r = reversed ? -rate->f : rate->f;
        if (rate->f < 0) {
            const Value* minRate = prop(a, "AnimMinRate");
            r = rateNow(a, rate->f, minRate ? minRate->asFloat() : 0);
        }
        const float next = frame->f + r * left;
        // The nearest notification passed (old, next].
        const MeshAnimSeq* s = boolOf(prop(a, "bAnimNotify")) ? findAnim(vm_, a, seqValue->i) : nullptr;
        const MeshAnimSeq::Notify* note = nullptr;
        if (s)
            for (const MeshAnimSeq::Notify& n : s->notifies) {
                const bool passed = reversed ? (n.time >= next && n.time < old) : (n.time > old && n.time <= next);
                if (passed && (!note || std::fabs(n.time - old) < std::fabs(note->time - old)))
                    note = &n;
            }
        if (note) {
            left = std::fabs((next - note->time) * left / (next - old));
            frame->f = note->time;
            if (!note->function.empty())
                if (VmFunction* fn = vm_.findFunction(a, vm_.name(note->function)))
                    vm_.invoke(fn, a, {});
            continue;
        }
        if (reversed ? next <= 0 : next >= 1) {
            if (boolOf(prop(a, "bAnimLoop"))) {
                left = std::fabs((next - (reversed ? 0.0f : 1.0f)) * left / (next - old));
                frame->f = reversed ? 1.0f : 0.0f;
                if (reversed ? old > 0 : old < 1) {
                    if (animWaits_.count(a))
                        if (Value* f = prop(a, "bAnimFinished"))
                            f->i = 1;
                    vm_.callEvent(a, "AnimEnd");
                    endAnimWait(a, true);
                }
                continue;
            }
            frame->f = reversed ? 0.0f : 1.0f;
            rate->f = 0;
            if (Value* f = prop(a, "bAnimFinished"))
                f->i = 1;
            vm_.callEvent(a, "AnimEnd");
            endAnimWait(a, true);
            break;
        }
        frame->f = next;
        left = 0;
    }
    if (!a->deleted && !animating(a))
        endAnimWait(a, false);
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

// --- The Fallen's layers and face channels ---------------------------------------------------

const std::map<NameId, ScriptLevel::AnimLayer>* ScriptLevel::layersOf(VmObject* a) const {
    auto it = layers_.find(a);
    return it == layers_.end() || it->second.empty() ? nullptr : &it->second;
}

const std::map<NameId, ScriptLevel::SkinChannel>* ScriptLevel::skinsOf(VmObject* a) const {
    auto it = skins_.find(a);
    return it == skins_.end() || it->second.empty() ? nullptr : &it->second;
}

// C3DPlayAnim, C3DLoopAnim: as PlayAnim and LoopAnim, on a part of the
// skeleton, fading in over the tween.
void ScriptLevel::playLayer(VmObject* a, NameId group, NameId sequence, float rate, float tween, bool loop, NameId bone,
                            float minRate, std::array<bool, 3> clear) {
    const MeshAnimSeq* s = findAnim(vm_, a, sequence);
    AnimLayer& l = layers_[a][group];
    const float n = s ? float(std::max(1, s->numFrames)) : 1;
    if (loop && s && l.sequence == sequence && l.loop && !l.finished && l.weightRate >= 0) {
        l.rate = rate * s->rate / n; // looping it already: only its speed changes
        l.minRate = std::max(0.0f, minRate) * s->rate / n;
        l.clear = clear;
        return;
    }
    // Replacing one that shows: it stays as it is under this one, which
    // fades in over it from nothing.
    l.fromSequence = 0;
    if (tween > 0 && l.sequence && l.weight > 0 && (l.sequence != sequence || l.bone != bone)) {
        l.fromSequence = l.sequence;
        l.fromFrame = std::max(0.0f, l.frame);
        l.fromWeight = l.weight;
        l.weight = 0;
    }
    l.sequence = sequence;
    l.bone = bone;
    l.rest = fs::toLower(vm_.nameText(group)).find("lower") != std::string::npos;
    l.loop = loop;
    l.frame = 0;
    l.finished = !s;
    l.rate = s ? rate * s->rate / n : 0; // negative: scaled by speed, as an actor's own
    l.minRate = s ? std::max(0.0f, minRate) * s->rate / n : 0;
    l.clear = clear;
    if (tween > 0) {
        l.weightRate = 1 / tween;
    } else {
        l.weight = 1;
        l.weightRate = 0;
    }
}

// C3DTweenAnim: the part to the sequence's first frame, fading in over
// `seconds`, then still (C3DAnimEnd when there).
void ScriptLevel::tweenLayer(VmObject* a, NameId group, NameId sequence, float seconds, NameId bone,
                             std::array<bool, 3> clear) {
    playLayer(a, group, sequence, 0, seconds, false, bone, 0, clear);
    AnimLayer& l = layers_[a][group];
    l.rate = 0;
    l.finished = false;
    if (seconds <= 0)
        l.weightRate = 1e6f;
}

// C3DTweenOutAnim: the part fades back to the actor's own animation, and
// the layer goes.
void ScriptLevel::tweenOutLayer(VmObject* a, NameId group, float seconds) {
    auto it = layers_.find(a);
    if (it == layers_.end())
        return;
    auto l = it->second.find(group);
    if (l == it->second.end())
        return;
    l->second.weightRate = seconds > 0 ? -1 / seconds : -1e6f;
    l->second.fromSequence = 0;
}

void ScriptLevel::stopLayerLooping(VmObject* a, NameId group) {
    auto it = layers_.find(a);
    if (it == layers_.end())
        return;
    if (auto l = it->second.find(group); l != it->second.end())
        l->second.loop = false;
}

bool ScriptLevel::layerAnimating(VmObject* a, NameId group) {
    auto it = layers_.find(a);
    if (it == layers_.end())
        return false;
    auto l = it->second.find(group);
    return l != it->second.end() && !l->second.finished && (l->second.rate != 0 || l->second.weightRate > 0);
}

void ScriptLevel::waitForLayer(VmObject* a, NameId group) { layerWaits_[a] = {group, a->stateSerial}; }

void ScriptLevel::endLayerWait(VmObject* a, NameId group) {
    auto it = layerWaits_.find(a);
    if (it == layerWaits_.end() || it->second.first != group)
        return;
    if (a->latent && a->stateSerial == it->second.second)
        a->sleeping = 0;
    layerWaits_.erase(it);
}

void ScriptLevel::animateLayers(VmObject* a, float dt) {
    if (auto it = skins_.find(a); it != skins_.end())
        for (auto& [tag, c] : it->second) {
            // Tweening to its frame and weight.
            const float step = std::min(1.0f, c.tweenRate * dt);
            c.frame += (c.target - c.frame) * (c.tweenRate > 0 ? step : 1);
            c.weight += (c.targetWeight - c.weight) * (c.tweenRate > 0 ? step : 1);
        }
    auto it = layers_.find(a);
    if (it == layers_.end())
        return;
    std::vector<NameId> ended, gone;
    for (auto& [group, l] : it->second) {
        l.weight = std::clamp(l.weight + l.weightRate * dt, 0.0f, 1.0f);
        if (l.weightRate < 0 && l.weight <= 0) {
            gone.push_back(group);
            continue;
        }
        if (l.weightRate > 0 && l.weight >= 1) {
            l.weightRate = 0;
            l.fromSequence = 0; // faded in: what it replaced is gone
        }
        if (l.finished)
            continue;
        if (l.rate == 0) {
            if (l.weightRate == 0) // a tween there: still, no C3DAnimEnd
                l.finished = true;
            continue;
        }
        if (l.weightRate > 0)
            continue; // tweening in: its time starts once it's there
        const MeshAnimSeq* s = findAnim(vm_, a, l.sequence);
        const float end = 1.0f;
        const float from = l.frame, to = from + l.rate * dt; // Rate multiplies time: not by speed
        if (to < end) {
            l.frame = to;
            notifyBetween(vm_, a, s, from, to);
            continue;
        }
        notifyBetween(vm_, a, s, from, end);
        if (l.loop) {
            l.frame = std::fmod(to - end, 1.0f); // round again; no C3DAnimEnd for a loop
            continue;
        }
        l.frame = end; // held there, at full strength, until faded out
        l.rate = 0;
        l.finished = true;
        ended.push_back(group);
    }
    for (NameId group : gone) {
        it->second.erase(group);
        endLayerWait(a, group);
    }
    // A layer played once, at its end: C3DAnimEnd(group), whose usual
    // answer is to fade the layer out.
    for (NameId group : ended) {
        if (a->deleted)
            return;
        vm_.callEvent(a, "C3DAnimEnd", {Value::nameValue(group)});
        endLayerWait(a, group);
    }
}

NameId ScriptLevel::newSkinChannel(VmObject* a) {
    const NameId tag = vm_.name("SkinChannel" + std::to_string(++skinChannels_));
    skins_[a][tag] = {};
    return tag;
}

// C3DSetSkinAnim, C3DTweenSkinAnim(Delay): a face channel to a frame of a
// sequence (seconds into it), at a weight, over `tween` seconds.
void ScriptLevel::setSkin(VmObject* a, NameId tag, NameId sequence, float frame, float weight, float tween) {
    SkinChannel& c = skins_[a][tag];
    if (c.sequence != sequence) {
        c.sequence = sequence;
        c.frame = frame;
    }
    c.target = frame;
    c.targetWeight = weight;
    c.tweenRate = tween > 0 ? 1 / tween : 0;
    if (tween <= 0) {
        c.frame = frame;
        c.weight = weight;
    }
}

void ScriptLevel::clearSkin(VmObject* a, NameId tag) {
    if (auto it = skins_.find(a); it != skins_.end())
        it->second.erase(tag);
}

void ScriptLevel::copySkins(VmObject* to, VmObject* from) {
    if (auto it = skins_.find(from); it != skins_.end())
        skins_[to] = it->second;
    else
        skins_.erase(to);
}

void registerAnimationNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // PlayAnim(name Sequence, optional float Rate, optional float TweenTime)
    reg("Actor.PlayAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->startAnim(c.self, c.i(0), c.given.size() > 1 && c.given[1] ? c.f(1) : 1,
                                  c.given.size() > 2 && c.given[2] ? c.f(2) : -1, false);
    });
    // LoopAnim(name Sequence, optional float Rate, optional float TweenTime, optional float MinRate)
    reg("Actor.LoopAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->startAnim(c.self, c.i(0), c.given.size() > 1 && c.given[1] ? c.f(1) : 1,
                                  c.given.size() > 2 && c.given[2] ? c.f(2) : -1, true,
                                  c.given.size() > 3 && c.given[3] ? c.f(3) : 0);
    });
    // TweenAnim(name Sequence, float Time)
    reg("Actor.TweenAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->tweenAnim(c.self, c.i(0), c.f(1));
    });
    reg("Actor.IsAnimating", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->animating(c.self));
    });
    // Latent: until the animation ends; a loop is made to end at the end
    // of this round (bAnimLoop off).
    reg("Actor.FinishAnim", [](NativeCall& c) {
        if (c.self)
            if (Value* loop = c.self->find(c.vm, "bAnimLoop"); loop && loop->asBool()) {
                loop->i = 0;
                if (Value* f = c.self->find(c.vm, "bAnimFinished"))
                    f->i = 0;
            }
        const Value* frame = c.self ? c.self->find(c.vm, "AnimFrame") : nullptr;
        if (!c.vm.level || !c.self || !c.vm.level->animating(c.self) || (frame && frame->asFloat() >= 1)) {
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
    // The Fallen's: C3DPlayAnim(name AnimGroup, name Sequence, bool Boned, optional float Rate,
    // optional float TweenTime, optional name InheritBone, optional bool ClearYaw, ClearPitch, ClearRoll)
    auto f = [](NativeCall& c, size_t k, float fallback) { return c.given.size() > k && c.given[k] ? c.f(k) : fallback; };
    auto n = [](NativeCall& c, size_t k) { return c.given.size() > k && c.given[k] ? c.i(k) : 0; };
    // ClearYaw, ClearPitch, ClearRoll from argument k on.
    auto clears = [](NativeCall& c, size_t k) {
        std::array<bool, 3> out{};
        for (size_t j = 0; j < 3; ++j)
            out[j] = c.given.size() > k + j && c.given[k + j] && c.b(k + j);
        return out;
    };
    reg("Actor.C3DPlayAnim", [=](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->playLayer(c.self, c.i(0), c.i(1), f(c, 3, 1), f(c, 4, 0.2f), false, n(c, 5), 0, clears(c, 6));
    });
    // C3DLoopAnim(group, sequence, boned, rate, tween time, min rate, inherit bone, clear...)
    reg("Actor.C3DLoopAnim", [=](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->playLayer(c.self, c.i(0), c.i(1), f(c, 3, 1), f(c, 4, 0.2f), true, n(c, 6), 0, // MinRate: unused
                                  clears(c, 7));
    });
    // C3DTweenAnim(group, sequence, boned, time, inherit bone, clear...)
    reg("Actor.C3DTweenAnim", [=](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->tweenLayer(c.self, c.i(0), c.i(1), c.f(3), n(c, 4), clears(c, 5));
    });
    reg("Actor.C3DTweenOutAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->tweenOutLayer(c.self, c.i(0), c.f(1));
    });
    reg("Actor.C3DIsAnimating", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.self && c.vm.level->layerAnimating(c.self, c.i(0)));
    });
    // Latent: a loop made to end at the end of this round, then until the
    // group's animation ends.
    reg("Actor.C3DFinishAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->stopLayerLooping(c.self, c.i(0));
        if (!c.vm.level || !c.self || !c.vm.level->layerAnimating(c.self, c.i(0))) {
            c.latentSeconds = 0;
            return;
        }
        c.vm.level->waitForLayer(c.self, c.i(0));
        c.latentSeconds = 1e9f;
    });
    // The face's channels: a new one (its tag), set, tweened, cleared, copied.
    reg("Actor.C3DPlaySkinAnim", [](NativeCall& c) {
        c.result = Value::nameValue(c.vm.level && c.self ? c.vm.level->newSkinChannel(c.self) : 0);
    });
    // C3DSetSkinAnim(name Tag, name Sequence, float FrameTime, float Weight)
    reg("Actor.C3DSetSkinAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->setSkin(c.self, c.i(0), c.i(1), c.f(2), c.f(3), 0);
    });
    // C3DTweenSkinAnim(Delay)(name Tag, name Sequence, float FrameTime, float Weight, float TweenTime)
    for (const char* tween : {"Actor.C3DTweenSkinAnim", "Actor.C3DTweenSkinAnimDelay"})
        reg(tween, [](NativeCall& c) {
            if (c.vm.level && c.self)
                c.vm.level->setSkin(c.self, c.i(0), c.i(1), c.f(2), c.f(3), c.f(4));
        });
    reg("Actor.C3DClearSkinAnim", [](NativeCall& c) {
        if (c.vm.level && c.self)
            c.vm.level->clearSkin(c.self, c.i(0));
    });
    reg("Actor.C3DCopySkinAnimState", [](NativeCall& c) {
        if (c.vm.level && c.self && c.o(0))
            c.vm.level->copySkins(c.self, c.o(0));
    });
    reg("Actor.C3DSetFullBodyAnimType", [](NativeCall& c) {
        if (Value* v = c.self ? c.self->find(c.vm, "AnimType") : nullptr)
            v->i = c.i(0);
    });
    // The HUD's portraits ask for their actors' animation to be brought up
    // to date: done each tick anyway.
    reg("Actor.C3DUpdateAnimationState", [](NativeCall&) {});
    reg("Actor.C3DIsBoned", [](NativeCall& c) {
        const MeshInfo* info = meshInfoOf(c.vm, c.self);
        c.result = Value::boolValue(info && info->boned);
    });
    reg("Actor.C3DUsesC3DMesh", [](NativeCall& c) {
        const MeshInfo* info = meshInfoOf(c.vm, c.self);
        c.result = Value::boolValue(info && info->c3d);
    });
    reg("Actor.GetAnimGroup", [](NativeCall& c) {
        const MeshAnimSeq* s = findAnim(c.vm, c.self, c.i(0));
        c.result = Value::nameValue(c.vm.name(s && !s->group.empty() ? s->group : "None"));
    });
}

} // namespace reluva::unreal
