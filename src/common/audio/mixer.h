// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/system.h"
#include "common/types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace reluva::audio {

// What a sound is, for the per-type volumes (a game's music, speech and
// effects settings).
enum class SoundType { Music, Speech, Effects };
constexpr int kSoundTypes = 3;

// A playing sound, as returned by Mixer::play; 0 is never one.
using SoundHandle = uint32_t;
constexpr SoundHandle kNoSound = 0;

constexpr int kMaxVolume = 256; // volumes run 0 (silent) .. 256 (as recorded)
constexpr int kMaxPan = 127;    // pan runs -127 (left) .. 127 (right)

// Mixes any number of decoded sounds into the System's stereo output:
// each resampled (linearly) to the output rate, scaled by its own volume and
// pan and its type's volume, summed and clamped to 16 bits. Shared by every
// engine. Thread-safe: mix() runs on the audio thread while the engine plays
// and stops sounds; a finished sound's channel is freed by the next mix().
class Mixer {
public:
    explicit Mixer(int outputRate);

    // Starts a sound; returns kNoSound for an empty one. A looping sound
    // plays until stopped.
    SoundHandle play(std::shared_ptr<const Sound> sound, SoundType type, int volume = kMaxVolume, int pan = 0,
                     bool loop = false);
    void stop(SoundHandle handle);
    void stopType(SoundType type);
    void stopAll();

    bool isPlaying(SoundHandle handle) const;
    bool isTypePlaying(SoundType type) const;
    // How far a sound has played, in its own frames (0 if it isn't playing).
    double position(SoundHandle handle) const;

    void setVolume(SoundHandle handle, int volume);
    void setPan(SoundHandle handle, int pan);
    // Its speed and pitch together: 1 as recorded, 2 an octave up.
    void setPitch(SoundHandle handle, double pitch);
    // How muffled it is, 0 (as recorded) to 1: a low-pass filter whose
    // cutoff falls to about a kilohertz (a sound heard through a wall).
    void setMuffle(SoundHandle handle, float muffle);
    // A 2nd-order Butterworth filter on it: low-pass or high-pass at
    // `hz` (clamped to 20 Hz - half the output rate); kind None removes it.
    enum class Filter { None, LowPass, HighPass };
    void setFilter(SoundHandle handle, Filter kind, float hz);
    // A reverb on it (a Schroeder reverb: four combs, two all-passes),
    // `mix` of it wet (0 none): `seconds` for its tail to fall 60 dB,
    // the wet sound low-passed at `dampHz`, after `predelay` seconds.
    struct Reverb {
        float mix = 0, seconds = 3, dampHz = 5000, predelay = 0;
    };
    void setReverb(SoundHandle handle, const Reverb& reverb);
    // Moves it to `frame` of its own frames (past the end: it ends, or
    // wraps if it loops).
    void seek(SoundHandle handle, double frame);
    void setTypeVolume(SoundType type, int volume);
    int typeVolume(SoundType type) const;

    int outputRate() const { return outputRate_; }

    // Fills `frames` stereo frames (interleaved signed 16-bit) with every
    // playing sound; silence when none is. The System's audio callback.
    void mix(int16_t* out, int frames);

private:
    struct Channel {
        SoundHandle handle;
        std::shared_ptr<const Sound> sound;
        SoundType type;
        int volume, pan;
        bool loop;
        double position = 0; // in the sound's frames
        double pitch = 1;
        float muffle = 0;
        double lowL = 0, lowR = 0; // the low-pass filter's last output
        bool done = false;
        // A biquad's coefficients and its state per side (x1 x2 y1 y2).
        struct Biquad {
            double a0 = 1, a1 = 0, a2 = 0, b1 = 0, b2 = 0;
            double state[2][4] = {};
            double run(int side, double x);
        };
        Filter filter = Filter::None;
        float filterHz = 0;
        Biquad biquad;
        struct ReverbState {
            Reverb params;
            Biquad damp;
            std::vector<double> pre;
            size_t preAt = 0;
            std::vector<double> comb[4];
            size_t combAt[4] = {};
            double combGain[4] = {};
            std::vector<double> pass1, pass2[2];
            size_t pass1At = 0, pass2At[2] = {};
        };
        std::shared_ptr<ReverbState> reverb; // none: dry
    };
    void setUpReverb(Channel& c, const Reverb& r) const;
    Channel* find(SoundHandle handle);
    const Channel* find(SoundHandle handle) const;

    const int outputRate_;
    mutable std::mutex mutex_;
    std::vector<Channel> channels_;
    SoundHandle nextHandle_ = 1;
    int typeVolume_[kSoundTypes] = {kMaxVolume, kMaxVolume, kMaxVolume};
    std::vector<int32_t> accumulator_; // mix()'s working buffer, stereo
};

// Starts the System's audio output with the mixer as its source. The mixer
// must outlive the output: call sys.stopAudio() before destroying it.
Error startMixerOutput(System& sys, Mixer& mixer);

} // namespace reluva::audio
