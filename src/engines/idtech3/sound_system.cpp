// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How far sounds carry and how they're panned follows the GPL Quake III
// Arena sound system's behaviour (full volume within 80 units, 0.0008 of
// it lost a unit beyond, panned by the listener's right); this is new code
// written from that description.

#include "engines/idtech3/sound_system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::idtech3 {

namespace {
constexpr float kFullVolumeDistance = 80;
constexpr float kAttenuation = 0.0008f;
} // namespace

std::shared_ptr<const audio::Sound> SoundSystem::find(const std::string& name) {
    if (name.empty())
        return nullptr;
    if (auto sound = sounds_.get(name))
        return sound;
    // Without an extension, or with the other one: the formats the games
    // use.
    const size_t slash = name.rfind('/');
    const size_t dot = name.find('.', slash == std::string::npos ? 0 : slash);
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    for (const char* ext : {".wav", ".mp3"})
        if (stem + ext != name)
            if (auto sound = sounds_.get(stem + ext))
                return sound;
    missing_.insert(name);
    return nullptr;
}

std::pair<float, float> SoundSystem::spatialize(const Vec3f& origin) const {
    Vec3f d{origin[0] - listenerOrigin_[0], origin[1] - listenerOrigin_[1], origin[2] - listenerOrigin_[2]};
    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > 1e-4f)
        for (float& c : d)
            c /= dist;
    const float volume = std::clamp(1 - std::max(dist - kFullVolumeDistance, 0.0f) * kAttenuation, 0.0f, 1.0f);
    // The listener's right is opposite its left axis.
    const Vec3f& left = listenerAxes_[1];
    const float pan = std::clamp(-(d[0] * left[0] + d[1] * left[1] + d[2] * left[2]), -1.0f, 1.0f);
    return {volume, dist > 1e-4f ? pan : 0.0f};
}

float SoundSystem::place(audio::SoundHandle handle, const std::optional<Vec3f>& origin, int entity) {
    // The listener's own sounds are heard as they are.
    if (!origin && entity == listener_) {
        mixer_.setVolume(handle, audio::kMaxVolume);
        mixer_.setPan(handle, 0);
        return 1;
    }
    // At its spot, or where its entity was last said to be: the map's
    // origin for one never placed, as the original keeps it (not at the
    // listener, which would be heard everywhere).
    Vec3f at{};
    if (origin)
        at = *origin;
    else if (const auto it = entities_.find(entity); it != entities_.end())
        at = it->second;
    const auto [volume, pan] = spatialize(at);
    mixer_.setVolume(handle, int(volume * float(audio::kMaxVolume)));
    mixer_.setPan(handle, int(pan * float(audio::kMaxPan)));
    return volume;
}

bool SoundSystem::allowed(int entity, bool local, const std::string& name, const std::optional<Vec3f>& origin) const {
    // Local sounds count as the listener's.
    const auto owner = [&](const Playing& p) { return p.local ? listener_ : p.entity; };
    const int who = local ? listener_ : entity;
    // Sounds placed at a spot (shots, impacts, often all on the world's
    // number) are only repeats at the same spot.
    auto samePlace = [&](const Playing& p) {
        if (p.origin.has_value() != origin.has_value())
            return false;
        if (!origin)
            return true;
        float d2 = 0;
        for (size_t k = 0; k < 3; ++k)
            d2 += ((*p.origin)[k] - (*origin)[k]) * ((*p.origin)[k] - (*origin)[k]);
        return d2 < 1;
    };
    for (const Playing& p : playing_)
        if (p.handle != audio::kNoSound && owner(p) == who && p.name == name && nowMs_ - p.startMs < 50 &&
            samePlace(p) && mixer_.isPlaying(p.handle))
            return false;
    return true;
}

void SoundSystem::startSound(const std::optional<Vec3f>& origin, int entity, int channel, const std::string& name) {
    const auto sound = find(name);
    const bool ok = sound && allowed(entity, false, name, origin);
    char where[64];
    if (origin)
        std::snprintf(where, sizeof where, "at %.0f %.0f %.0f", (*origin)[0], (*origin)[1], (*origin)[2]);
    else
        std::snprintf(where, sizeof where, "following it");
    const std::string what = name + " (entity " + std::to_string(entity) + ", channel " + std::to_string(channel) +
                             ", " + where + "): ";
    if (!ok) {
        note(name, entity, what + (!sound ? "not found" : "skipped as a repeat"));
        return;
    }
    const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects, 0);
    if (handle == audio::kNoSound)
        return;
    char volume[48];
    std::snprintf(volume, sizeof volume, "played at volume %.2f (listener %d)", place(handle, origin, entity),
                  listener_);
    note(name, entity, what + volume);
    playing_.push_back({handle, entity, channel, origin, false, name, nowMs_});
}

void SoundSystem::startLocalSound(const std::string& name, int channel) {
    const auto sound = find(name);
    const bool ok = sound && allowed(-1, true, name, std::nullopt);
    note(name, -1, name + " (local, channel " + std::to_string(channel) + "): " +
         (!sound ? "not found" : ok ? "played" : "skipped as a repeat"));
    if (!ok)
        return;
    const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects);
    if (handle != audio::kNoSound)
        playing_.push_back({handle, -1, channel, std::nullopt, true, name, nowMs_});
}

void SoundSystem::note(const std::string& name, int entity, std::string line) {
    // Each sound asked for in the last two seconds, by name and entity:
    // how often, and what became of the last.
    starts_.push_back({nowMs_, name, entity, std::move(line)});
    while (!starts_.empty() && starts_.front().ms + 2000 < nowMs_)
        starts_.pop_front();
}

std::vector<std::string> SoundSystem::recent() const {
    struct Summary {
        int count = 0;
        const std::string* last = nullptr;
        uint64_t lastMs = 0;
    };
    std::map<std::pair<std::string, int>, Summary> by;
    for (const Start& s : starts_) {
        Summary& sum = by[{s.name, s.entity}];
        ++sum.count;
        sum.last = &s.line;
        sum.lastMs = s.ms;
    }
    // The most recent first.
    std::vector<std::pair<uint64_t, std::string>> lines;
    for (const auto& [key, sum] : by)
        lines.emplace_back(sum.lastMs, std::to_string(sum.count) + " in 2 s, last " + *sum.last);
    std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < lines.size() && i < 6; ++i)
        out.push_back(lines[i].second);
    return out;
}

std::vector<std::string> SoundSystem::loopingNow() const {
    // Every loop the client game asked for this frame, and whether it plays.
    std::vector<std::string> out;
    for (const auto& [key, origin] : loopsAdded_)
        out.push_back((key.second.empty() ? std::string("(no name)") : key.second) + " (entity " +
                      std::to_string(key.first) + (loops_.count(key) ? ")" : ", not found)"));
    return out;
}

void SoundSystem::clearLoopingSounds() { loopsAdded_.clear(); }

void SoundSystem::addLoopingSound(int entity, const Vec3f& origin, const std::string& name) {
    loopsAdded_[{entity, name}] = origin;
}

void SoundSystem::updateEntityPosition(int entity, const Vec3f& origin) { entities_[entity] = origin; }

void SoundSystem::respatialize(int entity, const Vec3f& origin, const std::array<Vec3f, 3>& axes) {
    listener_ = entity;
    listenerOrigin_ = origin;
    listenerAxes_ = axes;
}

void SoundSystem::startBackgroundTrack(const std::string& intro, const std::string& loop) {
    stopBackgroundTrack();
    musicLoop_ = find(loop);
    auto first = find(intro);
    musicIntro_ = first != nullptr;
    if (!first)
        first = musicLoop_;
    if (first)
        music_ = mixer_.play(first, audio::SoundType::Music, audio::kMaxVolume, 0, !musicIntro_);
}

void SoundSystem::stopBackgroundTrack() {
    if (music_ != audio::kNoSound)
        mixer_.stop(music_);
    music_ = audio::kNoSound;
    musicLoop_.reset();
    musicIntro_ = false;
}

void SoundSystem::endFrame() {
    // Finished sounds go; the rest follow their entities and the listener.
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(),
                                  [&](const Playing& p) {
                                      return p.handle == audio::kNoSound || !mixer_.isPlaying(p.handle);
                                  }),
                   playing_.end());
    for (const Playing& p : playing_)
        if (!p.local)
            place(p.handle, p.origin, p.entity);

    // Looping sounds: new ones start, those not added this frame stop.
    for (auto it = loops_.begin(); it != loops_.end();) {
        if (!loopsAdded_.count(it->first)) {
            mixer_.stop(it->second);
            it = loops_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [key, origin] : loopsAdded_) {
        auto it = loops_.find(key);
        if (it == loops_.end()) {
            const auto sound = find(key.second);
            if (!sound)
                continue;
            const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects, 0, 0, true);
            if (handle == audio::kNoSound)
                continue;
            it = loops_.emplace(key, handle).first;
        }
        place(it->second, origin, key.first);
    }

    // The music: the loop once the intro is over.
    if (musicIntro_ && music_ != audio::kNoSound && !mixer_.isPlaying(music_)) {
        musicIntro_ = false;
        music_ = musicLoop_ ? mixer_.play(musicLoop_, audio::SoundType::Music, audio::kMaxVolume, 0, true)
                            : audio::kNoSound;
    }
}

void SoundSystem::setVolumes(float effects, float music) {
    mixer_.setTypeVolume(audio::SoundType::Effects, int(std::clamp(effects, 0.0f, 1.0f) * float(audio::kMaxVolume)));
    mixer_.setTypeVolume(audio::SoundType::Music, int(std::clamp(music, 0.0f, 1.0f) * float(audio::kMaxVolume)));
}

void SoundSystem::stopAll() {
    for (const Playing& p : playing_)
        if (p.handle != audio::kNoSound)
            mixer_.stop(p.handle);
    playing_.clear();
    for (const auto& [key, handle] : loops_)
        mixer_.stop(handle);
    loops_.clear();
    loopsAdded_.clear();
    stopBackgroundTrack();
}

} // namespace reluva::idtech3
