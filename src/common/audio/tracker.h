// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::audio {

// Tracker music: MOD (ProTracker and its multi-channel kin), S3M (Scream
// Tracker 3), XM (FastTracker 2) and IT (Impulse Tracker). The loaders turn
// each format into one common form, which renderTrackerModule plays.

enum class TrackerFormat { MOD, S3M, XM, IT };

// TrackerCell::note: 0 none, 1..120 a note (1 = C-0; 61 = C-5, which plays a
// sample at its c5speed), or one of these.
constexpr uint8_t kNoteFade = 253, kNoteCut = 254, kNoteOff = 255;

// One channel's entry in a pattern row. The volume column and effect are in
// the player's own codes (tracker.cpp), which the loaders translate to.
struct TrackerCell {
    uint8_t note = 0, instrument = 0; // instrument 1-based, 0 none
    uint8_t volCmd = 0, volParam = 0;
    uint8_t effect = 0, param = 0;
};

struct TrackerPattern {
    int rows = 64;
    std::vector<TrackerCell> cells; // rows x the module's channels
};

struct TrackerSample {
    std::vector<int16_t> data; // mono
    bool loop = false, pingPong = false;
    size_t loopStart = 0, loopEnd = 0; // frames, loopEnd past the last
    bool sustain = false, sustainPingPong = false; // IT's sustain loop, while the key is held
    size_t sustainStart = 0, sustainEnd = 0;
    double c5speed = 8363; // Hz at C-5 (MOD/XM finetune folded in)
    int relativeNote = 0;  // XM
    int volume = 64;       // default, 0..64
    int globalVolume = 64; // IT, 0..64
    int pan = -1;          // default pan 0 (left)..255, -1 none
};

struct TrackerEnvelope {
    struct Point {
        int tick, value; // value 0..64 (pan: 32 is the middle)
    };
    std::vector<Point> points;
    bool enabled = false, loop = false, sustain = false;
    int loopStart = 0, loopEnd = 0, sustainStart = 0, sustainEnd = 0; // point indices
};

// XM and IT instruments: which sample each note plays, and envelopes.
struct TrackerInstrument {
    std::array<uint8_t, 120> sample{}; // per note, 1-based, 0 none
    std::array<uint8_t, 120> note{};   // the note played instead (IT's note map)
    TrackerEnvelope volumeEnvelope, panEnvelope;
    int fadeout = 0;        // per tick after release, out of 65536
    int globalVolume = 128; // 0..128
    int pan = -1;           // default pan 0..255, -1 none
};

// A tracker module (MOD, S3M, XM or IT) read from its file's bytes.
struct TrackerModule {
    TrackerFormat format = TrackerFormat::MOD;
    std::string title;
    int channels = 0;
    int speed = 6, tempo = 125;
    int globalVolume = 128; // 0..128
    int mixVolume = 64;     // 64 is unity (S3M master volume, IT mix volume)
    bool linearSlides = false; // pitch in 1/64 semitones (XM/IT flag), else Amiga periods
    bool s3mEffects = false;   // S3M/IT parameter rules: DxF fine slides, shared memories
    bool effectMemory = false; // a zero parameter repeats the last (all but MOD)
    bool orderMarkers = false; // orders 254 (skip) and 255 (end), S3M/IT
    int restartOrder = 0;
    std::vector<uint8_t> orders;
    std::vector<TrackerPattern> patterns;
    std::vector<TrackerSample> samples;
    std::vector<TrackerInstrument> instruments; // empty: cells name samples directly
    std::vector<uint8_t> channelPan;            // 0..255 per channel
    std::vector<uint8_t> channelVolume;         // 0..64 per channel
};

// Reads a module, its format told by its contents.
Error loadTrackerModule(const std::vector<uint8_t>& data, TrackerModule& out);

// "MOD", "S3M", "XM" or "IT".
std::string trackerFormat(const TrackerModule& m);

// Renders the song to 16-bit stereo at `rate`, from order `startOrder` (a
// UE1 "song section"), until it ends or loops back (to a row already played)
// or `maxSeconds` pass. out.loopStart is where the loop goes back to and
// out.loopEnd its end, so the sound can be played looped.
Error renderTrackerModule(const TrackerModule& m, int rate, int startOrder, double maxSeconds, Sound& out);

} // namespace reluva::audio
