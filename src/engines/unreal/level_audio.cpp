// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/level_audio.h"

#include "engines/unreal/sound.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

constexpr float kMusicFade = 3.0f; // seconds music takes to fade in or out (the driver's)

int volumeOf(float v) {
    return std::clamp(int(std::lround(v * audio::kMaxVolume)), 0, audio::kMaxVolume);
}

float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

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
    for (const auto& [actor, a] : ambient_)
        line(a.music ? "ambient music" : "ambient", a.handle, name(a.sound.first, a.sound.second));
    for (const Playing& p : playing_)
        line(p.everywhere ? "voice" : "sound", p.handle, name(p.sound.package, p.sound.exportIndex));
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

// The audio driver's: (1 - distance / radius) x volume in hundredths,
// at most 100 of its 127.
float LevelAudio::loudness(const Vec3f& at, float radius, float volume) const {
    const Vec3f d{at[0] - listener_[0], at[1] - listener_[1], at[2] - listener_[2]};
    const float distance = std::sqrt(dot(d, d));
    const float near = radius > 0 ? std::clamp(1 - distance / radius, 0.0f, 1.0f) : 0;
    return float(std::clamp(int(near * std::clamp(volume, 0.0f, 2.0f) * 100), 0, 100)) / 127;
}

// The driver's side: the angle off straight ahead (one behind as if in
// front), less within a tenth of the radius, as 0..127 about 63.
int LevelAudio::panOf(const Vec3f& at, float radius) const {
    if (!stereo_)
        return 0;
    const Vec3f d{at[0] - listener_[0], at[1] - listener_[1], at[2] - listener_[2]};
    float x = dot(d, right_);
    if (reverse_)
        x = -x;
    float angle = std::atan2(x, std::fabs(dot(d, forward_)));
    const float distance = std::sqrt(dot(d, d));
    if (radius > 0 && distance < radius * 0.1f)
        angle *= distance / (radius * 0.1f);
    const int pan = std::clamp(int(angle * 111.125f / 3.14159265f + 63), 0, 127);
    return std::clamp((pan - 64) * audio::kMaxPan / 63, -audio::kMaxPan, audio::kMaxPan);
}

float LevelAudio::priority(const Vec3f& at, float radius, float volume) const {
    const Vec3f d{at[0] - listener_[0], at[1] - listener_[1], at[2] - listener_[2]};
    const float distance = std::sqrt(dot(d, d));
    return volume * std::max(radius > 0 ? 1 - distance / radius : 0.0f, 0.0001f);
}

bool LevelAudio::makeRoom(float wanted) {
    const size_t used = playing_.size() + ambient_.size() + fading_.size() + (music_ ? 1 : 0);
    if (used < size_t(channels_))
        return true;
    // The least important one-off or ambient sound, if no more important.
    float least = wanted;
    Playing* lowest = nullptr;
    VmObject* lowestAmbient = nullptr;
    for (Playing& p : playing_)
        if (const float q = p.everywhere ? p.volume : priority(p.at, p.radius, p.volume); q <= least)
            least = q, lowest = &p, lowestAmbient = nullptr;
    for (auto& [actor, a] : ambient_)
        if (const float q = a.music ? a.volume : priority(a.at, a.radius, a.volume); q <= least)
            least = q, lowest = nullptr, lowestAmbient = actor;
    if (lowest) {
        mixer_.stop(lowest->handle);
        playing_.erase(playing_.begin() + (lowest - playing_.data()));
        return true;
    }
    if (lowestAmbient) {
        mixer_.stop(ambient_[lowestAmbient].handle);
        ambient_.erase(lowestAmbient);
        return true;
    }
    return false;
}

// A stream: its hundredths, twice, times MusicVolume / 255, of 127.
float LevelAudio::streamLoudness(float volume) const {
    const float v = float(std::min(100, int(std::max(volume, 0.0f) * 100)));
    return std::min(1.0f, v * 2 * musicSetting_ / 127);
}

void LevelAudio::update(LevelPlay& play, const Vec3f& listener, const Rotator& facing, float seconds) {
    listener_ = listener;
    const float yaw = float(facing.yaw) * 6.2831853f / 65536;
    const float pitch = float(facing.pitch) * 6.2831853f / 65536;
    right_ = {-std::sin(yaw), std::cos(yaw), 0};
    forward_ = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    auto flag = [&](const char* key, bool otherwise) {
        const std::string v = play.audioSetting(key);
        return v.empty() ? otherwise : (v == "1" || v == "True" || v == "true" || v == "TRUE");
    };
    stereo_ = flag("UseStereo", true);
    reverse_ = flag("ReverseStereo", false);
    const bool useFilter = flag("UseFilter", true);
    {
        const std::string v = play.audioSetting("Channels");
        channels_ = v.empty() ? 16 : std::max(1, std::atoi(v.c_str()));
    }
    // The volumes: each kind's setting (0-255) under the master's. A
    // stream's own is part of its loudness (streamLoudness).
    const float master = std::clamp(play.masterVolume(), 0.0f, 255.0f) / 255;
    auto setting = [&](const char* key) {
        const std::string v = play.audioSetting(key);
        return std::clamp(v.empty() ? 255.0f : std::strtof(v.c_str(), nullptr), 0.0f, 255.0f) / 255;
    };
    musicSetting_ = setting("MusicVolume");
    mixer_.setTypeVolume(audio::SoundType::Effects, volumeOf(master * setting("SFXVolume")));
    mixer_.setTypeVolume(audio::SoundType::Music, volumeOf(master));
    mixer_.setTypeVolume(audio::SoundType::Speech, volumeOf(master * setting("SpeechVolume")));

    if (play.takeKillSounds()) {
        for (const Playing& p : playing_)
            mixer_.stop(p.handle);
        playing_.clear();
    }
    // Those stopped (a line cut off: its speaker's talk slot).
    for (const auto& [actor, slot] : play.takeSoundStops())
        for (Playing& p : playing_)
            if (p.actor == actor && p.slot == slot)
                mixer_.stop(p.handle);
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(),
                                  [&](const Playing& p) { return !mixer_.isPlaying(p.handle); }),
                   playing_.end());

    // What's played: one at a time in each of an actor's slots (but the
    // none slot). One played with bNoOverride keeps its slot; otherwise a
    // new one takes it.
    for (const ScriptLevel::SoundRequest& r : play.takeSounds()) {
        Playing p;
        p.actor = r.actor;
        p.slot = r.slot;
        p.radius = r.radius;
        p.volume = std::clamp(r.volume, 0.0f, 2.0f);
        p.everywhere = !r.actor;
        p.noOverride = r.noOverride;
        p.filtered = useFilter && (r.lowPass != 0 || r.highPass != 0);
        p.obstruction = r.obstruction;
        p.sound = r.sound;
        if (r.actor && !play.locationOf(r.actor, p.at))
            continue;
        if (r.slot != 0) {
            auto same = std::find_if(playing_.begin(), playing_.end(),
                                     [&](const Playing& q) { return q.actor == r.actor && q.slot == r.slot; });
            if (same != playing_.end()) {
                if (same->noOverride)
                    continue;
                mixer_.stop(same->handle);
                playing_.erase(same);
            }
        }
        auto sound = load(r.sound);
        if (!sound || !makeRoom(p.everywhere ? p.volume : priority(p.at, p.radius, p.volume)))
            continue;
        const float loud = p.everywhere ? float(std::min(100, int(p.volume * 100))) / 127
                                        : loudness(p.at, p.radius, p.volume);
        p.handle = mixer_.play(sound, r.speech ? audio::SoundType::Speech : audio::SoundType::Effects, volumeOf(loud),
                               p.everywhere ? 0 : panOf(p.at, p.radius), r.loop);
        if (p.handle == audio::kNoSound)
            continue;
        if (r.pitch != 1)
            mixer_.setPitch(p.handle, r.pitch);
        if (r.startSeconds > 0)
            mixer_.seek(p.handle, double(r.startSeconds) * sound->rate);
        playing_.push_back(p);
        ++started_;
        recent_.push_back(r.sound.package->name() + "." + r.sound.package->path(r.sound.exportIndex + 1));
        if (recent_.size() > 12)
            recent_.erase(recent_.begin());
    }
    // Those playing follow their actors (one gone is heard where it was).
    // A filtered one is quietened by its ObstructionFactor when the level's
    // walls stand between it and the listener; the others go through the
    // driver's positional mixing, which doesn't.
    for (Playing& p : playing_) {
        if (p.everywhere)
            continue;
        if (p.actor && !play.locationOf(p.actor, p.at))
            p.actor = nullptr;
        float loud = loudness(p.at, p.radius, p.volume);
        if (p.filtered && p.obstruction > 0 && loud > 0 && play.wallBetween(listener_, p.at))
            loud *= 1 - p.obstruction;
        mixer_.setVolume(p.handle, volumeOf(loud));
        mixer_.setPan(p.handle, panOf(p.at, p.radius));
    }

    // The ambient sounds: started when the listener comes within their
    // radius, stopped when it leaves (and started afresh on coming back),
    // or when the actor's sound changes. One named MUSIC is heard alike
    // anywhere in range, fades in and out over 3 seconds, and isn't started
    // while other music plays.
    play.ambientSounds(ambientNow_);
    std::map<VmObject*, const LevelPlay::Ambient*> now;
    for (const LevelPlay::Ambient& a : ambientNow_)
        now[a.actor] = &a;
    auto inRange = [&](const Vec3f& at, float radius) {
        const Vec3f d{at[0] - listener_[0], at[1] - listener_[1], at[2] - listener_[2]};
        return dot(d, d) < radius * radius;
    };
    for (auto it = ambient_.begin(); it != ambient_.end();) {
        Ambient& am = it->second;
        auto found = now.find(it->first);
        const bool keep = found != now.end() && found->second->sound.package == am.sound.first &&
                          found->second->sound.exportIndex == am.sound.second &&
                          inRange(found->second->at, found->second->radius) && mixer_.isPlaying(am.handle);
        if (!keep && am.music && am.fade > 0 && mixer_.isPlaying(am.handle)) {
            am.fadeRate = -1 / kMusicFade; // faded out first
            ++it;
        } else if (!keep) {
            mixer_.stop(am.handle);
            it = ambient_.erase(it);
        } else {
            if (am.music && am.fadeRate < 0)
                am.fadeRate = 1 / kMusicFade;
            ++it;
        }
    }
    const bool musicOn = music_ != audio::kNoSound || !fading_.empty();
    for (const LevelPlay::Ambient& a : ambientNow_) {
        auto it = ambient_.find(a.actor);
        if (it == ambient_.end()) {
            if (!inRange(a.at, a.radius) || (a.music && musicOn))
                continue;
            auto sound = load(a.sound);
            if (!sound || !makeRoom(a.music ? a.volume : priority(a.at, a.radius, a.volume)))
                continue;
            Ambient am;
            am.sound = {a.sound.package, a.sound.exportIndex};
            am.music = a.music;
            am.streamed = a.streamed;
            if (a.music)
                am.fade = 0, am.fadeRate = 1 / kMusicFade;
            am.handle = mixer_.play(sound, a.streamed ? audio::SoundType::Music : audio::SoundType::Effects, 0,
                                    0, true);
            if (am.handle == audio::kNoSound)
                continue;
            it = ambient_.emplace(a.actor, am).first;
        }
        Ambient& am = it->second;
        am.volume = a.volume;
        am.radius = a.radius;
        am.at = a.at;
        if (am.fadeRate != 0)
            am.fade = std::clamp(am.fade + am.fadeRate * seconds, 0.0f, 1.0f);
        float loud = am.music ? (am.streamed ? streamLoudness(a.volume) : float(std::min(100, int(a.volume * 100))) / 127)
                              : (am.streamed ? streamLoudness(loudness(a.at, a.radius, a.volume) * 127 / 100)
                                             : loudness(a.at, a.radius, a.volume));
        loud *= am.fade;
        mixer_.setVolume(am.handle, volumeOf(loud));
        mixer_.setPan(am.handle, am.music ? 0 : panOf(a.at, a.radius));
        mixer_.setPitch(am.handle, a.pitch);
    }
    // Ambient music faded out, or whose actor is gone.
    for (auto it = ambient_.begin(); it != ambient_.end();) {
        Ambient& am = it->second;
        if (am.music && now.find(it->first) == now.end() && am.fadeRate < 0) {
            am.fade = std::max(0.0f, am.fade + am.fadeRate * seconds);
            mixer_.setVolume(am.handle, volumeOf(am.fade * (am.streamed ? streamLoudness(am.volume) : am.volume)));
        }
        if (am.music && am.fade <= 0 && am.fadeRate < 0) {
            mixer_.stop(am.handle);
            it = ambient_.erase(it);
        } else {
            ++it;
        }
    }

    // The music: whatever the player's CurrentMusic names. Played while it
    // plays already, it goes on; another starts at once, at full volume,
    // while the old one fades out over 3 seconds, as does one whose
    // CurrentMusic is cleared. Once it ends, CurrentMusic is None again
    // (the filler music waits for that). With CurrentMusic set and nothing
    // playing (a game loaded), it starts at full volume.
    VmObject* wanted = play.currentMusic();
    const ScriptLevel::Music& m = play.music();
    if (wanted != musicSound_) {
        if (music_ != audio::kNoSound)
            fading_.push_back({music_, musicStreamed_ ? streamLoudness(musicVolume_) : musicVolume_, kMusicFade});
        music_ = audio::kNoSound;
        musicSound_ = wanted;
        if (wanted && wanted->package && wanted->exportIndex >= 0)
            if (auto sound = load({wanted->package, wanted->exportIndex})) {
                // Ambient music gives way.
                for (auto& [actor, am] : ambient_)
                    if (am.music)
                        am.fadeRate = -1 / kMusicFade;
                musicStreamed_ = play.streamed(wanted);
                musicVolume_ = m.serial ? m.volume : 2.0f;
                const float loud = musicStreamed_ ? streamLoudness(musicVolume_)
                                                  : float(std::min(100, int(musicVolume_ * 100))) / 127;
                music_ = mixer_.play(sound, musicStreamed_ ? audio::SoundType::Music : audio::SoundType::Effects,
                                     volumeOf(loud));
                if (m.startSeconds > 0)
                    mixer_.seek(music_, double(m.startSeconds) * sound->rate);
            }
    } else if (music_ != audio::kNoSound && !mixer_.isPlaying(music_)) {
        music_ = audio::kNoSound;
        play.musicEnded(musicSound_);
        musicSound_ = nullptr;
    } else if (music_ != audio::kNoSound) {
        mixer_.setVolume(music_, volumeOf(musicStreamed_ ? streamLoudness(musicVolume_)
                                                         : float(std::min(100, int(musicVolume_ * 100))) / 127));
    }
    for (Fade& f : fading_) {
        f.left -= seconds;
        mixer_.setVolume(f.handle, volumeOf(f.volume * std::max(0.0f, f.left) / kMusicFade));
        if (f.left <= 0)
            mixer_.stop(f.handle);
    }
    fading_.erase(std::remove_if(fading_.begin(), fading_.end(), [](const Fade& f) { return f.left <= 0; }),
                  fading_.end());
}

} // namespace reluva::unreal
