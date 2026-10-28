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
    // Loudness (0-256) and side (-127..127) from `at`, heard as far as
    // `radius`.
    void place(const Vec3f& at, float radius, float volume, int& outVolume, int& outPan) const;

    audio::Mixer& mixer_;
    const PackageSet& packages_;
    std::map<std::pair<const Package*, int>, std::shared_ptr<const audio::Sound>> sounds_; // null: unreadable

    Vec3f listener_{};
    float rightX_ = 0, rightY_ = 1;
    bool stereo_ = true, reverse_ = false;

    struct Playing {
        audio::SoundHandle handle = audio::kNoSound;
        VmObject* actor = nullptr; // followed while it lasts
        int slot = 0;
        Vec3f at{};
        float radius = 0, volume = 1;
        bool everywhere = false; // a voice: not placed
        ObjectRef sound;
    };
    std::vector<Playing> playing_;
    std::vector<std::string> recent_; // the last one-off sounds started, oldest first
    size_t started_ = 0;
    struct Ambient {
        audio::SoundHandle handle = audio::kNoSound;
        std::pair<const Package*, int> sound{};
    };
    std::map<VmObject*, Ambient> ambient_;
    std::vector<LevelPlay::Ambient> ambientNow_;

    audio::SoundHandle music_ = audio::kNoSound;
    VmObject* musicSound_ = nullptr;
    int musicSerial_ = 0;
    float musicVolume_ = 1;
    struct Fade {
        audio::SoundHandle handle;
        float volume, left; // its volume (0-256) and seconds to go
    };
    std::vector<Fade> fading_;
};

} // namespace reluva::unreal
