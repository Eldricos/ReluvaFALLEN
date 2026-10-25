// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/collision.h"
#include "engines/idtech3/sounds.h"

#include "common/audio/mixer.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// The sounds of a level, as the original's sound system plays what the
// client game asks for (see docs/engines/idtech3.md): sounds placed in the
// world or on entities, heard from the listener's position, louder near
// and panned to the side they're on; looping sounds the client game
// re-adds each frame; the music.
class SoundSystem {
public:
    SoundSystem(audio::Mixer& mixer, SoundLibrary& sounds) : mixer_(mixer), sounds_(sounds) {}
    ~SoundSystem() { stopAll(); }
    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    // A new sound never cuts one already playing, on any channel; the same
    // sound the entity started under 50 ms ago (at the same spot, if it
    // was given one) is a repeat and skipped, as the original's. (ioq3 also caps each entity at 5 of one sound at
    // once, which silenced rapid fire here, so that's left out.) Channel 0
    // is the original's "any channel".
    static constexpr int kAutoChannel = 0;
    // The time now, for telling repeats; each frame before the client game.
    void setTime(uint64_t ms) { nowMs_ = ms; }

    // A sound at `origin`, or (without one) following entity `entity`.
    void startSound(const std::optional<Vec3f>& origin, int entity, int channel, const std::string& name);
    // A sound heard as it is, wherever the listener is (the announcer,
    // the menus' sounds).
    void startLocalSound(const std::string& name, int channel);
    // Looping sounds: those added between clearLoopingSounds() and
    // endFrame() play; the rest stop.
    void clearLoopingSounds();
    void addLoopingSound(int entity, const Vec3f& origin, const std::string& name);
    void updateEntityPosition(int entity, const Vec3f& origin);
    // Where the listener is (entity `entity`) and which way it faces:
    // forward, left, up.
    void respatialize(int entity, const Vec3f& origin, const std::array<Vec3f, 3>& axes);
    // The music: `intro` once, then `loop` round and round (either may be
    // empty); names without an extension are tried as .mp3 and .wav.
    void startBackgroundTrack(const std::string& intro, const std::string& loop);
    void stopBackgroundTrack();
    // Each frame, after the client game's: positions and volumes brought
    // up to date, looping sounds not re-added stopped, music moved on.
    void endFrame();
    // The effects' and music's volumes, 0 to 1.
    void setVolumes(float effects, float music);
    void stopAll();
    size_t playingCount() const { return playing_.size(); }
    // Sounds asked for that couldn't be found or read.
    const std::set<std::string>& missing() const { return missing_; }
    // The sounds asked for in the last two seconds, by name and entity
    // (the latest six): how many times, and what became of the last.
    std::vector<std::string> recent() const;
    // The looping sounds playing now.
    std::vector<std::string> loopingNow() const;

    // How loud (0..1) and where (-1 left .. 1 right) a sound at `origin`
    // is heard from the listener: full volume within 80 units, then
    // fading by 0.0008 a unit; panned by how far to the listener's right
    // it is.
    std::pair<float, float> spatialize(const Vec3f& origin) const;

private:
    struct Playing {
        audio::SoundHandle handle = audio::kNoSound;
        int entity = -1, channel = 0;
        std::optional<Vec3f> origin; // fixed; else the entity's
        bool local = false;
        std::string name;
        uint64_t startMs = 0;
    };
    // Whether `entity` may start `name` now (see kAutoChannel).
    bool allowed(int entity, bool local, const std::string& name, const std::optional<Vec3f>& origin) const;
    std::shared_ptr<const audio::Sound> find(const std::string& name);
    // Sets the sound's volume and pan for where it is; the volume.
    float place(audio::SoundHandle handle, const std::optional<Vec3f>& origin, int entity);

    audio::Mixer& mixer_;
    SoundLibrary& sounds_;
    std::vector<Playing> playing_;
    std::map<int, Vec3f> entities_;
    // Looping sounds by (entity, name): playing, and added this frame.
    std::map<std::pair<int, std::string>, audio::SoundHandle> loops_;
    std::map<std::pair<int, std::string>, Vec3f> loopsAdded_;
    void note(const std::string& name, int entity, std::string line);
    struct Start {
        uint64_t ms = 0;
        std::string name;
        int entity = 0;
        std::string line;
    };
    std::deque<Start> starts_;
    std::set<std::string> missing_;
    int listener_ = -1;
    uint64_t nowMs_ = 0;
    Vec3f listenerOrigin_{};
    std::array<Vec3f, 3> listenerAxes_{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    audio::SoundHandle music_ = audio::kNoSound;
    std::shared_ptr<const audio::Sound> musicLoop_;
    bool musicIntro_ = false;
};

} // namespace reluva::idtech3
