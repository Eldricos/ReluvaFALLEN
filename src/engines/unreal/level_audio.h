// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A played level's sounds through the mixer (the Maps page's): what its
// scripts play, heard from where the actor is, the actors' ambient sounds,
// the music and the dialogue's voices, at the game's volume settings. See
// docs/engines/unreal_vm.md.

#include "common/audio/mixer.h"
#include "engines/unreal/level_play.h"

#include <map>
#include <memory>
#include <vector>

namespace reluva::unreal {

class LevelAudio {
public:
    LevelAudio(audio::Mixer& mixer, const PackageSet& packages);
    ~LevelAudio();

    // A frame: the level's new sounds started, every sound's loudness and
    // side worked out again from where it is and where the listener (the
    // camera) is and faces, the ambient sounds and music brought up to
    // date. `seconds`: since the last frame.
    void update(LevelPlay& play, const Vec3f& listener, const Rotator& facing, float seconds);
    // Every sound stopped, and what it knew of the level forgotten (a level
    // stopped or another started). The decoded sounds are kept.
    void reset();

    size_t playingCount() const { return playing_.size() + ambient_.size() + (music_ ? 1 : 0); }
    // What's playing, a line each (its kind and sound), then how many
    // one-off sounds have started and the last few.
    std::vector<std::string> describe() const;

private:
    std::shared_ptr<const audio::Sound> load(const ObjectRef& ref);
    // As the game's audio driver: heard from `at` as far as `radius`,
    // how loud (0..1, before the volume settings: `volume` x (1 -
    // distance / radius), in hundredths, of the driver's 127) and to which
    // side (-127..127).
    float loudness(const Vec3f& at, float radius, float volume) const;
    int panOf(const Vec3f& at, float radius) const;
    // A sound's importance: its volume x (1 - distance / radius), at
    // least 0.0001 of it. When all channels are taken, a new sound takes
    // the least important's if that's no more important than it.
    float priority(const Vec3f& at, float radius, float volume) const;
    bool makeRoom(float priority);
    // A stream's loudness (at the music volume, twice the others').
    float streamLoudness(float volume) const;

    audio::Mixer& mixer_;
    const PackageSet& packages_;
    std::map<std::pair<const Package*, int>, std::shared_ptr<const audio::Sound>> sounds_; // null: unreadable

    Vec3f listener_{}, right_{0, 1, 0}, forward_{1, 0, 0};
    bool stereo_ = true, reverse_ = false;
    int channels_ = 16;
    float musicSetting_ = 1; // MusicVolume / 255

    struct Playing {
        audio::SoundHandle handle = audio::kNoSound;
        VmObject* actor = nullptr; // followed while it lasts
        int slot = 0;
        Vec3f at{};
        float radius = 0, volume = 1;
        bool everywhere = false; // a voice: not placed
        bool noOverride = false;
        bool filtered = false; // mixed flat: quietened by walls in the way
        float obstruction = 0;
        ObjectRef sound;
    };
    std::vector<Playing> playing_;
    std::vector<std::string> recent_; // the last one-off sounds started, oldest first
    size_t started_ = 0;
    struct Ambient {
        audio::SoundHandle handle = audio::kNoSound;
        std::pair<const Package*, int> sound{};
        bool music = false, streamed = false;
        float volume = 0, fade = 1, fadeRate = 0; // fadeRate: a second (music: 1/3 in, -1/3 out)
        float radius = 0;
        Vec3f at{};
    };
    std::map<VmObject*, Ambient> ambient_;
    std::vector<LevelPlay::Ambient> ambientNow_;

    audio::SoundHandle music_ = audio::kNoSound;
    VmObject* musicSound_ = nullptr;
    float musicVolume_ = 1;
    bool musicStreamed_ = true;
    struct Fade {
        audio::SoundHandle handle;
        float volume, left; // its loudness (0-1) and seconds to go
    };
    std::vector<Fade> fading_;
};

} // namespace reluva::unreal
