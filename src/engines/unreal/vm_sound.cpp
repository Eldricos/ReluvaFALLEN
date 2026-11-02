// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sounds, for a running level (ScriptLevel): what the scripts ask to play
// (PlaySound, PlayOwnedSound), kept for the page to play (level_audio.cpp),
// and the music, which is The Fallen's streamed sounds: one played sets
// the player's CurrentMusic, and the page plays whatever that names. See
// docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/sound.h"

namespace reluva::unreal {

namespace {

constexpr size_t kMostRequests = 256; // kept when nothing takes them (no page)

// A sound's length in seconds: its WAVE's data over its byte rate, or a
// streamed one's decoded length.
float soundSeconds(const PackageSet& packages, const Package& package, int exportIndex) {
    EmbeddedFile file;
    if (!readEmbeddedFile(package, exportIndex, file) && !file.isPlaceholder()) {
        const std::vector<uint8_t>& d = file.data;
        auto le32 = [&](size_t i) { return uint32_t(d[i]) | uint32_t(d[i + 1]) << 8 | uint32_t(d[i + 2]) << 16 | uint32_t(d[i + 3]) << 24; };
        uint32_t byteRate = 0;
        for (size_t i = 12; i + 8 <= d.size();) {
            const uint32_t size = le32(i + 4);
            if (std::equal(d.begin() + long(i), d.begin() + long(i) + 4, "fmt ") && i + 16 <= d.size())
                byteRate = le32(i + 16);
            if (std::equal(d.begin() + long(i), d.begin() + long(i) + 4, "data"))
                return byteRate ? float(size) / float(byteRate) : 0;
            i += 8 + size + (size & 1);
        }
        return 0;
    }
    audio::Sound s;
    if (!loadSound(packages, package, exportIndex, s) && s.rate > 0)
        return float(s.frames()) / float(s.rate);
    return 0;
}

} // namespace

VmObject* ScriptLevel::currentMusic() {
    const Value* v = player_ && !player_->deleted ? prop(player_, "CurrentMusic") : nullptr;
    return v ? v->asObject() : nullptr;
}

void ScriptLevel::musicEnded(VmObject* sound) {
    if (Value* v = player_ && !player_->deleted ? prop(player_, "CurrentMusic") : nullptr; v && v->asObject() == sound)
        v->o = nullptr;
}

bool ScriptLevel::streamedSound(VmObject* sound) {
    if (!sound || !sound->package || sound->exportIndex < 0)
        return false;
    auto it = streamed_.find(sound);
    if (it == streamed_.end())
        it = streamed_.emplace(sound, streamedFile(vm_.packages(), *sound->package, sound->exportIndex).has_value()).first;
    return it->second;
}

// The sound's name (its object's, with its group's), upper case.
std::string ScriptLevel::soundName(VmObject* sound) {
    if (!sound || !sound->package || sound->exportIndex < 0)
        return {};
    std::string n = sound->package->path(sound->exportIndex + 1);
    for (char& ch : n)
        ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return n;
}

bool ScriptLevel::listenerAt(Vec& at) {
    if (!player_ || player_->deleted)
        return false;
    VmObject* from = player_;
    if (const Value* v = prop(player_, "ViewTarget"); v && v->asObject() && !v->asObject()->deleted)
        from = v->asObject();
    else if (const Value* c = prop(player_, "Camera"); c && c->asObject() && !c->asObject()->deleted)
        from = c->asObject();
    const Value* l = prop(from, "Location");
    if (!l)
        return false;
    at = Vm::vectorOf(*l);
    return true;
}

// CheckHearSound: within Radius / sqrt(1.3) of where the player hears from.
// Through the level's walls (unless from a SpecialEvent or Keypoint) it's
// at 0.35 of its volume, and, unless the player set it off, heard only
// within sqrt(0.6) of that.
bool ScriptLevel::hearsSound(VmObject* source, float radius, float& volume) {
    Vec ear{};
    const Value* l = prop(source, "Location");
    if (!l || !listenerAt(ear))
        return true; // no one listening: kept for the page to place
    const Vec at = Vm::vectorOf(*l);
    const float dx = at[0] - ear[0], dy = at[1] - ear[1], dz = at[2] - ear[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    float r2 = radius * radius / 1.3f;
    if (d2 >= r2)
        return false;
    if (!vm_.isA(source, "SpecialEvent") && !vm_.isA(source, "Keypoint") && worldBetween(ear, at)) {
        volume *= 0.35f;
        const Value* i = prop(source, "Instigator");
        if (!i || i->asObject() != player_) {
            r2 *= 0.6f;
            if (d2 >= r2)
                return false;
        }
    }
    return true;
}

// A sound as a noise (PlaySound unless bNoHearNoise, PlayDummySound): to
// each pawn that isn't a player, the sound's actor or its owner, when the
// actor has an owner and it or its owner is a player, a weapon or a
// projectile. Heard at 1 - (distance / Radius)^2, less 0.65 through walls,
// if that's above 0 and above 1 - HearingThreshold. A CTVE_Pawn whose
// enemy isn't the actor first has its DLG_Command set to FHE.
void ScriptLevel::soundNoise(VmObject* maker, float radius) {
    if (!maker || maker->deleted || radius <= 0)
        return;
    const Value* ownerValue = prop(maker, "Owner");
    VmObject* owner = ownerValue ? ownerValue->asObject() : nullptr;
    if (!owner)
        return;
    auto loud = [&](VmObject* a) {
        return a && (vm_.isA(a, "PlayerPawn") || vm_.isA(a, "Weapon") || vm_.isA(a, "Projectile"));
    };
    if (!loud(maker) && !loud(owner))
        return;
    const Value* at = prop(maker, "Location");
    if (!at)
        return;
    const Vec from = Vm::vectorOf(*at);
    const NameId hear = vm_.name("HearNoise");
    ++noise.made;
    const std::vector<VmObject*> now = actors_;
    for (VmObject* p : now) {
        if (p == maker || p == owner || p->deleted || !vm_.isA(p, "Pawn"))
            continue;
        if (const Value* b = prop(p, "bIsPlayer"); b && b->asBool())
            continue;
        if (p->disabled.count(hear) || !vm_.findFunction(p, hear))
            continue;
        const Value* loc = prop(p, "Location");
        if (!loc)
            continue;
        const Vec to = Vm::vectorOf(*loc);
        const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
        float ratio = (dx * dx + dy * dy + dz * dz) / (radius * radius);
        const Value* threshold = prop(p, "HearingThreshold");
        if (ratio >= 1 || ratio >= (threshold ? threshold->asFloat() : 1))
            continue;
        if (worldBetween(from, to)) {
            ratio += 0.65f;
            if (ratio >= 1)
                continue;
        }
        if (vm_.isA(p, "CTVE_Pawn")) {
            const Value* e = prop(p, "Enemy");
            if (!e || e->asObject() != maker)
                if (Value* c = prop(p, "DLG_Command"))
                    *c = Value::stringValue("FHE");
        }
        ++noise.heard;
        vm_.callEvent(p, "HearNoise", {Value::floatValue(1 - ratio), Value::objectValue(maker)});
    }
}

void registerSoundNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // PlaySound(Sound, optional ESoundSlot Slot, optional float Volume,
    // optional bool bNoOverride, optional float Radius, optional float
    // Pitch, optional bool bNoHearNoise), as Engine.dll's: the slot
    // SLOT_Misc if left out; Volume and Pitch the actor's TransientSound
    // ones, Radius its TransientSoundRadius, and a Radius of 0 is 1600.
    // Heard only if the player hears it (hearsSound); a noise to the AI
    // unless bNoHearNoise. The actor's LowPass, HighPass and
    // ObstructionFactor go with it. A sound named MUSIC, or streamed, is
    // music: the player's CurrentMusic, started where the actor's
    // StartTimeOffset says. PlayOwnedSound is the same, for the one player.
    auto play = [](NativeCall& c, bool owned) {
        ScriptLevel* level = c.vm.level;
        VmObject* sound = c.o(0);
        if (!level || !c.self || c.self->deleted || !sound || !sound->package || sound->exportIndex < 0)
            return;
        auto given = [&](size_t k) { return k < c.given.size() && c.given[k]; };
        auto actorFloat = [&](const char* name, float otherwise) {
            const Value* v = c.self->find(c.vm, name);
            return v ? v->asFloat() : otherwise;
        };
        ScriptLevel::SoundRequest r;
        r.actor = c.self;
        r.sound = {sound->package, sound->exportIndex};
        r.slot = given(1) ? c.i(1) : 1;
        r.volume = given(2) ? c.f(2) : actorFloat("TransientSoundVolume", 1);
        r.noOverride = given(3) && c.b(3);
        r.radius = given(4) ? c.f(4) : actorFloat("TransientSoundRadius", 0);
        if (r.radius == 0)
            r.radius = 1600;
        r.pitch = given(5) ? c.f(5) : actorFloat("TransientSoundPitch", 1);
        if (r.pitch <= 0)
            r.pitch = 1;
        r.lowPass = actorFloat("LowPass", 0);
        r.highPass = actorFloat("HighPass", 0);
        r.obstruction = actorFloat("ObstructionFactor", 0);
        const std::string name = ScriptLevel::soundName(sound);
        r.speech = name.find("DIALOG") != std::string::npos;
        r.loop = name.find("LOOPING") != std::string::npos;
        // A noise the AI hears, unless asked not to be (The Fallen's
        // weapons pass bNoHearNoise for all their sounds but the empty
        // click).
        if (!owned && !(given(6) && c.b(6)))
            level->soundNoise(c.self, r.radius);
        if (!level->hearsSound(c.self, r.radius, r.volume))
            return;
        if (name.find("MUSIC") != std::string::npos || level->streamedSound(sound)) {
            if (VmObject* player = level->player())
                if (Value* v = player->find(c.vm, "CurrentMusic"))
                    v->o = sound;
            ScriptLevel::Music& m = level->music();
            m.volume = r.volume;
            m.startSeconds = std::max(0.0f, actorFloat("StartTimeOffset", 0));
            ++m.serial;
            return;
        }
        auto& requests = level->soundRequests();
        if (requests.size() >= kMostRequests)
            requests.erase(requests.begin());
        requests.push_back(r);
    };
    reg("Actor.PlaySound", [play](NativeCall& c) { play(c, false); });
    reg("Actor.PlayOwnedSound", [play](NativeCall& c) { play(c, true); });
    // PlayDummySound(Volume, Radius): only the noise.
    reg("Actor.PlayDummySound", [](NativeCall& c) {
        if (c.vm.level && c.self) {
            const float radius = c.f(1);
            c.vm.level->soundNoise(c.self, radius == 0 ? 1600 : radius);
        }
    });
    // GetSoundDuration(Sound): its length in seconds.
    reg("Actor.GetSoundDuration", [](NativeCall& c) {
        VmObject* s = c.o(0);
        c.result = Value::floatValue(s && s->package && s->exportIndex >= 0
                                         ? soundSeconds(c.vm.packages(), *s->package, s->exportIndex)
                                         : 0.0f);
    });
}

} // namespace reluva::unreal
