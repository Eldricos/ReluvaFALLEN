// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/level_audio.h"

#include "engines/unreal/sound.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

constexpr float kMusicFade = 1.0f; // seconds music takes to fade when it's stopped

int volumeOf(float v) {
    return std::clamp(int(std::lround(v * audio::kMaxVolume)), 0, audio::kMaxVolume);
}

} // namespace

LevelAudio::LevelAudio(audio::Mixer& mixer, const PackageSet& packages) : mixer_(mixer), packages_(packages) {}

LevelAudio::~LevelAudio() {
    reset();
}

std::vector<std::string> LevelAudio::describe() const {
    std::vector<std::string> out;
    auto name = [](const Package* p, int e) { return p ? p->name() + "." + p->path(e + 1) : std::string("?"); };
    auto line = [&](const char* kind, audio::SoundHandle h, const std::string& what) {
        if (mixer_.isPlaying(h))
            out.push_back(std::string(kind) + " " + what);
    };
    if (music_)
        line("music", music_, musicSound_ && musicSound_->package ? name(musicSound_->package, musicSound_->exportIndex) : "?");
    auto muffled = [](float m) { return m > 0 ? " (muffled " + std::to_string(int(m * 100 + 0.5f)) + "%)" : std::string(); };
    for (const auto& [actor, a] : ambient_)
        line("ambient", a.handle, name(a.sound.first, a.sound.second) + muffled(a.muffle));
    for (const Playing& p : playing_)
        line(p.everywhere ? "voice" : "sound", p.handle, name(p.sound.package, p.sound.exportIndex) + muffled(p.muffle));
    std::string last = std::to_string(started_) + " started; last:";
    for (const std::string& r : recent_)
        last += " " + r;
    out.push_back(last);
    return out;
}

void LevelAudio::reset() {
    for (const Playing& p : playing_)
        mixer_.stop(p.handle);
    for (const auto& [actor, a] : ambient_)
        mixer_.stop(a.handle);
    for (const Fade& f : fading_)
        mixer_.stop(f.handle);
    mixer_.stop(music_);
    playing_.clear();
    ambient_.clear();
    fading_.clear();
    music_ = audio::kNoSound;
    musicSound_ = nullptr;
    musicSerial_ = 0;
}

std::shared_ptr<const audio::Sound> LevelAudio::load(const ObjectRef& ref) {
    if (!ref)
        return nullptr;
    const auto key = std::make_pair(ref.package, ref.exportIndex);
    auto it = sounds_.find(key);
    if (it == sounds_.end()) {
        auto s = std::make_shared<audio::Sound>();
        std::shared_ptr<const audio::Sound> loaded;
        if (!loadSound(packages_, *ref.package, ref.exportIndex, *s) && s->frames() > 0)
            loaded = std::move(s);
        it = sounds_.emplace(key, std::move(loaded)).first;
    }
    return it->second;
}

// Fading with distance to nothing at `radius`, and to the side the listener
// hears it on: Reluva's reading of the engine's (whose code isn't known).
void LevelAudio::place(const Vec3f& at, float radius, float volume, int& outVolume, int& outPan) const {
    const float dx = at[0] - listener_[0], dy = at[1] - listener_[1], dz = at[2] - listener_[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float near = radius > 0 ? std::max(0.0f, 1 - d / radius) : 0;
    outVolume = volumeOf(volume * near);
    float side = d > 1 ? (dx * rightX_ + dy * rightY_) / d : 0;
    if (reverse_)
        side = -side;
    outPan = stereo_ ? std::clamp(int(std::lround(side * audio::kMaxPan)), -audio::kMaxPan, audio::kMaxPan) : 0;
}

// Heard through a wall: the level's BSP between the listener and the
// sound (movers aren't counted, so a door's own sounds aren't muffled by
// the door), within the last 24 units not counted (sounds placed in a
// wall's face). Half as loud and low-passed, eased in and out over a
// quarter of a second. Reluva's reading: how the original muffled
// sounds isn't known.
void LevelAudio::muffle(LevelPlay& play, const Vec3f& at, float seconds, float& muffle, int& volume,
                        audio::SoundHandle handle) {
    if (volume > 0) {
        const float target = play.wallBetween(listener_, at) ? 1.0f : 0.0f;
        const float step = std::max(seconds, 0.0f) * 4;
        muffle = target > muffle ? std::min(target, muffle + step) : std::max(target, muffle - step);
    }
    volume = int(float(volume) * (1 - 0.5f * muffle));
    mixer_.setMuffle(handle, muffle);
}

void LevelAudio::update(LevelPlay& play, const Vec3f& listener, const Rotator& facing, float seconds) {
    listener_ = listener;
    const float yaw = float(facing.yaw) * 6.2831853f / 65536;
    rightX_ = -std::sin(yaw);
    rightY_ = std::cos(yaw);
    auto flag = [&](const char* key, bool otherwise) {
        const std::string v = play.audioSetting(key);
        return v.empty() ? otherwise : (v == "1" || v == "True" || v == "true" || v == "TRUE");
    };
    stereo_ = flag("UseStereo", true);
    reverse_ = flag("ReverseStereo", false);
    // The volumes: each kind's setting, under the master's (0-255 each).
    const float master = std::clamp(play.masterVolume(), 0.0f, 255.0f) / 255;
    auto setting = [&](const char* key) {
        const std::string v = play.audioSetting(key);
        return std::clamp(v.empty() ? 255.0f : std::strtof(v.c_str(), nullptr), 0.0f, 255.0f) / 255;
    };
    mixer_.setTypeVolume(audio::SoundType::Effects, volumeOf(master * setting("SFXVolume")));
    mixer_.setTypeVolume(audio::SoundType::Music, volumeOf(master * setting("MusicVolume")));
    mixer_.setTypeVolume(audio::SoundType::Speech, volumeOf(master * setting("SpeechVolume")));

    if (play.takeKillSounds()) {
        for (const Playing& p : playing_)
            mixer_.stop(p.handle);
        playing_.clear();
    }

    // What's played: one at a time in each of an actor's slots (but the
    // none slot), a new one stopping the old unless it's asked not to
    // override.
    for (const ScriptLevel::SoundRequest& r : play.takeSounds()) {
        Playing p;
        p.actor = r.actor;
        p.slot = r.slot;
        p.radius = r.radius;
        p.volume = r.volume;
        p.everywhere = !r.actor;
        p.sound = r.sound;
        if (r.actor && !play.locationOf(r.actor, p.at))
            continue;
        if (r.slot != 0) {
            auto same = std::find_if(playing_.begin(), playing_.end(), [&](const Playing& q) {
                return q.actor == r.actor && q.slot == r.slot && mixer_.isPlaying(q.handle);
            });
            if (same != playing_.end()) {
                if (r.noOverride)
                    continue;
                mixer_.stop(same->handle);
                playing_.erase(same);
            }
        }
        auto sound = load(r.sound);
        if (!sound)
            continue;
        int volume = volumeOf(r.volume), pan = 0;
        if (!p.everywhere)
            place(p.at, p.radius, p.volume, volume, pan);
        p.handle = mixer_.play(sound, r.speech ? audio::SoundType::Speech : audio::SoundType::Effects, volume, pan);
        if (p.handle == audio::kNoSound)
            continue;
        if (r.pitch != 1)
            mixer_.setPitch(p.handle, r.pitch);
        playing_.push_back(p);
        ++started_;
        recent_.push_back(r.sound.package->name() + "." + r.sound.package->path(r.sound.exportIndex + 1));
        if (recent_.size() > 12)
            recent_.erase(recent_.begin());
    }
    // Those playing follow their actors, and are heard from where they are.
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(),
                                  [&](const Playing& p) { return !mixer_.isPlaying(p.handle); }),
                   playing_.end());
    for (Playing& p : playing_) {
        if (p.everywhere)
            continue;
        if (p.actor && !play.locationOf(p.actor, p.at))
            p.actor = nullptr; // gone: heard where it was
        int volume = 0, pan = 0;
        place(p.at, p.radius, p.volume, volume, pan);
        muffle(play, p.at, seconds, p.muffle, volume, p.handle);
        mixer_.setVolume(p.handle, volume);
        mixer_.setPan(p.handle, pan);
    }

    // The ambient sounds: looping while their actors have them, heard from
    // where they are.
    play.ambientSounds(ambientNow_);
    std::map<VmObject*, const LevelPlay::Ambient*> now;
    for (const LevelPlay::Ambient& a : ambientNow_)
        now[a.actor] = &a;
    for (auto it = ambient_.begin(); it != ambient_.end();) {
        auto found = now.find(it->first);
        if (found == now.end() || found->second->sound.package != it->second.sound.first ||
            found->second->sound.exportIndex != it->second.sound.second || !mixer_.isPlaying(it->second.handle)) {
            mixer_.stop(it->second.handle);
            it = ambient_.erase(it);
        } else {
            ++it;
        }
    }
    for (const LevelPlay::Ambient& a : ambientNow_) {
        int volume = 0, pan = 0;
        place(a.at, a.radius, a.volume, volume, pan);
        auto it = ambient_.find(a.actor);
        if (it == ambient_.end()) {
            if (volume == 0)
                continue; // out of hearing: started once it's near
            auto sound = load(a.sound);
            if (!sound)
                continue;
            Ambient am;
            am.sound = {a.sound.package, a.sound.exportIndex};
            am.handle = mixer_.play(sound, a.streamed ? audio::SoundType::Music : audio::SoundType::Effects, volume,
                                    pan, true);
            if (am.handle == audio::kNoSound)
                continue;
            if (a.pitch != 1)
                mixer_.setPitch(am.handle, a.pitch);
            ambient_[a.actor] = am;
        } else {
            muffle(play, a.at, seconds, it->second.muffle, volume, it->second.handle);
            mixer_.setVolume(it->second.handle, volume);
            mixer_.setPan(it->second.handle, pan);
            mixer_.setPitch(it->second.handle, a.pitch);
        }
    }

    // The music: whatever the player's CurrentMusic names, started again
    // each time the scripts play it, faded out when it's stopped; once it
    // ends, CurrentMusic is None again (the filler music waits for that).
    VmObject* wanted = play.currentMusic();
    const ScriptLevel::Music& m = play.music();
    if (wanted != musicSound_ || (wanted && m.serial != musicSerial_)) {
        if (music_ != audio::kNoSound)
            fading_.push_back({music_, float(volumeOf(musicVolume_)), kMusicFade});
        music_ = audio::kNoSound;
        musicSound_ = wanted;
        musicSerial_ = m.serial;
        if (wanted && wanted->package && wanted->exportIndex >= 0)
            if (auto sound = load({wanted->package, wanted->exportIndex})) {
                musicVolume_ = m.volume;
                music_ = mixer_.play(sound, audio::SoundType::Music, volumeOf(musicVolume_));
                if (m.startSeconds > 0)
                    mixer_.seek(music_, double(m.startSeconds) * sound->rate);
            }
    } else if (music_ != audio::kNoSound && !mixer_.isPlaying(music_)) {
        music_ = audio::kNoSound;
        play.musicEnded(musicSound_);
        musicSound_ = nullptr;
    }
    for (Fade& f : fading_) {
        f.left -= seconds;
        mixer_.setVolume(f.handle, volumeOf(f.volume / audio::kMaxVolume * std::max(0.0f, f.left) / kMusicFade));
        if (f.left <= 0)
            mixer_.stop(f.handle);
    }
    fading_.erase(std::remove_if(fading_.begin(), fading_.end(), [](const Fade& f) { return f.left <= 0; }),
                  fading_.end());
}

} // namespace reluva::unreal
