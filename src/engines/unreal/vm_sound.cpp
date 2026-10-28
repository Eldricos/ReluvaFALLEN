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

void registerSoundNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // PlaySound(Sound, optional ESoundSlot Slot, optional float Volume,
    // optional bool bNoOverride, optional float Radius, optional float
    // Pitch, optional bool bNoHearNoise): heard from the actor, as far as
    // Radius. Left out: Volume and Pitch are the actor's TransientSound
    // ones, Radius its TransientSoundRadius, else 25 x (SoundRadius + 1).
    // A streamed sound is music: the player's CurrentMusic, started where
    // the HUD's StartTimeOffset says (a music trigger sets it around the
    // call). PlayOwnedSound is the same, for the one player.
    auto play = [](NativeCall& c) {
        ScriptLevel* level = c.vm.level;
        VmObject* sound = c.o(0);
        if (!level || !c.self || !sound || !sound->package || sound->exportIndex < 0)
            return;
        auto given = [&](size_t k) { return k < c.given.size() && c.given[k]; };
        auto actorFloat = [&](const char* name, float otherwise) {
            const Value* v = c.self->find(c.vm, name);
            return v ? v->asFloat() : otherwise;
        };
        ScriptLevel::SoundRequest r;
        r.actor = c.self;
        r.sound = {sound->package, sound->exportIndex};
        r.slot = given(1) ? c.i(1) : 0;
        r.volume = given(2) ? c.f(2) : actorFloat("TransientSoundVolume", 1);
        r.noOverride = given(3) && c.b(3);
        r.radius = given(4) ? c.f(4) : actorFloat("TransientSoundRadius", 0);
        if (r.radius <= 0)
            r.radius = 25 * (actorFloat("SoundRadius", 32) + 1);
        r.pitch = given(5) ? c.f(5) : actorFloat("TransientSoundPitch", 1);
        if (r.pitch <= 0)
            r.pitch = 1;
        if (level->streamedSound(sound)) {
            if (VmObject* player = level->player())
                if (Value* v = player->find(c.vm, "CurrentMusic"))
                    v->o = sound;
            ScriptLevel::Music& m = level->music();
            m.volume = r.volume;
            m.startSeconds = std::max(0.0f, actorFloat("StartTimeOffset", 0));
            ++m.serial;
            return;
        }
        // A sound is a noise the AI hears too, as loud as it's played,
        // unless asked not to be (bNoHearNoise: The Fallen's weapons pass
        // it for all their sounds but the empty click; footsteps don't).
        if (!(given(6) && c.b(6)))
            level->makeNoise(c.self, r.volume);
        auto& requests = level->soundRequests();
        if (requests.size() >= kMostRequests)
            requests.erase(requests.begin());
        requests.push_back(r);
    };
    reg("Actor.PlaySound", play);
    reg("Actor.PlayOwnedSound", play);
    // GetSoundDuration(Sound): its length in seconds.
    reg("Actor.GetSoundDuration", [](NativeCall& c) {
        VmObject* s = c.o(0);
        c.result = Value::floatValue(s && s->package && s->exportIndex >= 0
                                         ? soundSeconds(c.vm.packages(), *s->package, s->exportIndex)
                                         : 0.0f);
    });
}

} // namespace reluva::unreal
