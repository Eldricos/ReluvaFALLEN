// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

Mixer::Mixer(int outputRate) : outputRate_(std::max(outputRate, 1)) {}

Mixer::Channel* Mixer::find(SoundHandle handle) {
    for (Channel& c : channels_)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

const Mixer::Channel* Mixer::find(SoundHandle handle) const {
    for (const Channel& c : channels_)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

SoundHandle Mixer::play(std::shared_ptr<const Sound> sound, SoundType type, int volume, int pan, bool loop) {
    if (!sound || sound->frames() == 0 || sound->rate <= 0 || sound->channels < 1 || sound->channels > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    channels_.push_back({handle, std::move(sound), type, std::clamp(volume, 0, kMaxVolume),
                         std::clamp(pan, -kMaxPan, kMaxPan), loop});
    return handle;
}

void Mixer::stop(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->done = true;
}

void Mixer::stopType(SoundType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel& c : channels_)
        if (c.type == type)
            c.done = true;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel& c : channels_)
        c.done = true;
}

bool Mixer::isPlaying(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(handle) != nullptr;
}

bool Mixer::isTypePlaying(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(channels_.begin(), channels_.end(), [&](const Channel& c) { return c.type == type && !c.done; });
}

double Mixer::position(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Channel* c = find(handle);
    return c ? c->position : 0;
}

void Mixer::setVolume(SoundHandle handle, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->volume = std::clamp(volume, 0, kMaxVolume);
}

void Mixer::setPan(SoundHandle handle, int pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->pan = std::clamp(pan, -kMaxPan, kMaxPan);
}

void Mixer::setPitch(SoundHandle handle, double pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->pitch = std::clamp(pitch, 0.05, 8.0);
}

void Mixer::setMuffle(SoundHandle handle, float muffle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->muffle = std::clamp(muffle, 0.0f, 1.0f);
}

namespace {
constexpr double kPi = 3.14159265358979323846;

// A 2nd-order Butterworth (bilinear) low-pass or high-pass at `hz`.
void butterworth(double& a0, double& a1, double& a2, double& b1, double& b2, bool high, double hz, double rate) {
    hz = std::clamp(hz, 20.0, rate / 2 - 1);
    const double r2 = std::sqrt(2.0);
    if (!high) {
        const double c = 1 / std::tan(kPi * hz / rate);
        a0 = 1 / (1 + r2 * c + c * c);
        a1 = 2 * a0;
        a2 = a0;
        b1 = 2 * a0 * (1 - c * c);
        b2 = a0 * (1 - r2 * c + c * c);
    } else {
        const double c = std::tan(kPi * hz / rate);
        a0 = 1 / (1 + r2 * c + c * c);
        a1 = -2 * a0;
        a2 = a0;
        b1 = 2 * a0 * (c * c - 1);
        b2 = a0 * (1 - r2 * c + c * c);
    }
}
} // namespace

double Mixer::Channel::Biquad::run(int side, double x) {
    double* st = state[side];
    const double y = a0 * x + a1 * st[0] + a2 * st[1] - b1 * st[2] - b2 * st[3] + 1e-10;
    st[1] = st[0];
    st[0] = x;
    st[3] = st[2];
    st[2] = y;
    return y;
}

void Mixer::setFilter(SoundHandle handle, Filter kind, float hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    Channel* c = find(handle);
    if (!c || (c->filter == kind && c->filterHz == hz))
        return;
    if (c->filter != kind)
        c->biquad = {};
    c->filter = kind;
    c->filterHz = hz;
    if (kind != Filter::None)
        butterworth(c->biquad.a0, c->biquad.a1, c->biquad.a2, c->biquad.b1, c->biquad.b2, kind == Filter::HighPass,
                    hz, double(outputRate_));
}

// The reverb's lines at the output rate: four combs (29.7, 37.1, 41.1,
// 43.7 ms), each fed back by 0.001^(its delay / the reverb's time), so
// its echoes fall 60 dB over that; two all-passes (5 and 1.7 ms, 0.7).
void Mixer::setUpReverb(Channel& c, const Reverb& r) const {
    auto& st = c.reverb;
    const bool fresh = !st;
    if (fresh)
        st = std::make_shared<Channel::ReverbState>();
    const double rate = double(outputRate_);
    auto samples = [&](double ms) { return std::max<size_t>(1, size_t(std::lround(ms * rate / 1000))); };
    if (fresh) {
        const double combs[4] = {29.7, 37.1, 41.1, 43.7};
        for (int k = 0; k < 4; ++k)
            st->comb[k].assign(samples(combs[k]), 0);
        st->pass1.assign(samples(5.0), 0);
        st->pass2[0].assign(samples(1.7), 0);
        st->pass2[1].assign(samples(1.7), 0);
    }
    if (fresh || r.predelay != st->params.predelay) {
        st->pre.assign(std::max<size_t>(1, size_t(std::lround(std::clamp(r.predelay, 0.0f, 0.25f) * rate))), 0);
        st->preAt = 0;
    }
    if (fresh || r.dampHz != st->params.dampHz)
        butterworth(st->damp.a0, st->damp.a1, st->damp.a2, st->damp.b1, st->damp.b2, false, r.dampHz, rate);
    for (int k = 0; k < 4; ++k) {
        const double delay = double(st->comb[k].size()) / rate;
        st->combGain[k] = std::pow(0.001, delay / std::max(0.01, double(r.seconds)));
    }
    st->params = r;
}

void Mixer::setReverb(SoundHandle handle, const Reverb& reverb) {
    std::lock_guard<std::mutex> lock(mutex_);
    Channel* c = find(handle);
    if (!c)
        return;
    if (reverb.mix <= 0) { // dry from now
        c->reverb.reset();
        return;
    }
    setUpReverb(*c, reverb);
}

void Mixer::seek(SoundHandle handle, double frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->position = std::max(0.0, frame);
}

void Mixer::setTypeVolume(SoundType type, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    typeVolume_[int(type)] = std::clamp(volume, 0, kMaxVolume);
}

int Mixer::typeVolume(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return typeVolume_[int(type)];
}

void Mixer::mix(int16_t* out, int frames) {
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    accumulator_.assign(size_t(frames) * 2, 0);
    for (Channel& c : channels_) {
        if (c.done)
            continue;
        const Sound& s = *c.sound;
        const size_t n = s.frames(), ch = size_t(s.channels);
        const double step = double(s.rate) / double(outputRate_) * c.pitch;
        // Gains in 1/65536ths: volume x type volume, and a linear pan that
        // keeps the louder side at full level.
        const int64_t gain = int64_t(c.volume) * typeVolume_[int(c.type)];
        const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
        const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
        auto sample = [&](size_t frame, size_t channel) { return double(s.samples[frame * ch + channel]); };
        // Muffled: a one-pole low-pass, its cutoff from the full band
        // down to about 1 kHz (k = 1 - e^(-2 pi fc / rate)).
        const bool muffled = c.muffle > 0;
        const double cutoff = 20000.0 * std::pow(1000.0 / 20000.0, double(c.muffle));
        const double k = 1 - std::exp(-2 * 3.14159265358979 * cutoff / double(outputRate_));
        for (int i = 0; i < frames; ++i) {
            // Looping: over the sound's own loop if it has one (its WAV's
            // smpl), else all of it.
            const bool ownLoop = c.loop && s.loopEnd > s.loopStart;
            const double loopFrom = ownLoop ? double(s.loopStart) : 0, loopTo = ownLoop ? double(s.loopEnd) : double(n);
            if (c.position >= (c.loop ? loopTo : double(n))) {
                if (!c.loop) {
                    c.done = true;
                    break;
                }
                c.position = loopFrom + std::fmod(c.position - loopFrom, loopTo - loopFrom);
            }
            // Linear interpolation towards the next frame (the loop's
            // start, when looping past its end; else the last holds).
            const size_t f = size_t(c.position);
            const size_t g = ownLoop ? (double(f + 1) < loopTo ? f + 1 : s.loopStart)
                                     : (f + 1 < n ? f + 1 : (c.loop ? 0 : f));
            const double t = c.position - double(f);
            double l = sample(f, 0) * (1 - t) + sample(g, 0) * t;
            double r = ch > 1 ? sample(f, 1) * (1 - t) + sample(g, 1) * t : l;
            if (muffled) { // one pole: each output moves part way to the input
                c.lowL += k * (l - c.lowL);
                c.lowR += k * (r - c.lowR);
                l = c.lowL;
                r = c.lowR;
            } else { // kept with the sound, so muffling starts smoothly
                c.lowL = l;
                c.lowR = r;
            }
            if (c.filter != Filter::None) {
                l = c.biquad.run(0, l);
                r = ch > 1 ? c.biquad.run(1, r) : l;
            }
            if (c.reverb) {
                // Its input (the sound mono; a stereo one's sides summed),
                // delayed, damped, through the combs side by side, then
                // the all-passes in turn (the second's input a quarter of
                // the first's output on the left, 0.15 on the right).
                Channel::ReverbState& rv = *c.reverb;
                double x = ch > 1 ? l + r : l;
                const double delayed = rv.pre[rv.preAt];
                rv.pre[rv.preAt] = x;
                rv.preAt = (rv.preAt + 1) % rv.pre.size();
                x = rv.damp.run(0, rv.pre.size() > 1 ? delayed : x);
                double sum = 0;
                for (int k = 0; k < 4; ++k) {
                    std::vector<double>& b = rv.comb[k];
                    const double y = b[rv.combAt[k]];
                    b[rv.combAt[k]] = x + rv.combGain[k] * y;
                    rv.combAt[k] = (rv.combAt[k] + 1) % b.size();
                    sum += y;
                }
                auto allPass = [](std::vector<double>& b, size_t& at, double in) {
                    const double v = b[at];
                    const double w = in + 0.7 * v;
                    b[at] = w;
                    at = (at + 1) % b.size();
                    return v - 0.7 * w;
                };
                const double first = allPass(rv.pass1, rv.pass1At, sum);
                const double wetL = allPass(rv.pass2[0], rv.pass2At[0], first * 0.25);
                const double wetR = allPass(rv.pass2[1], rv.pass2At[1], first * 0.15);
                const double mixWet = double(rv.params.mix);
                l = l * (1 - mixWet) + wetL * mixWet;
                r = r * (1 - mixWet) + wetR * mixWet;
            }
            accumulator_[size_t(i) * 2] += int32_t(std::lround(l * double(gainL) / 65536.0));
            accumulator_[size_t(i) * 2 + 1] += int32_t(std::lround(r * double(gainR) / 65536.0));
            c.position += step;
        }
    }
    for (size_t i = 0; i < accumulator_.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(accumulator_[i], -32768, 32767));
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(), [](const Channel& c) { return c.done; }),
                    channels_.end());
}

Error startMixerOutput(System& sys, Mixer& mixer) {
    return sys.startAudio([&mixer](int16_t* out, int frames) { mixer.mix(out, frames); });
}

} // namespace reluva::audio
