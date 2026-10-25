// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/wav.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::audio {

namespace {

uint16_t u16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t u32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(u16(d, o)) | uint32_t(u16(d, o + 2)) << 16; }

// The IMA ADPCM tables (the published IMA/DVI standard): the quantiser step
// for each of the 89 step indices, and how each code changes the index.
constexpr int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int kIndexTable[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct ImaChannel {
    int sample = 0, index = 0;
    int16_t decode(unsigned code) {
        const int step = kStepTable[index];
        int diff = step >> 3;
        if (code & 4)
            diff += step;
        if (code & 2)
            diff += step >> 1;
        if (code & 1)
            diff += step >> 2;
        sample = std::clamp(code & 8 ? sample - diff : sample + diff, -32768, 32767);
        index = std::clamp(index + kIndexTable[code & 7], 0, 88);
        return int16_t(sample);
    }
};

// Microsoft's IMA ADPCM blocks: per channel a header (s16 first sample, u8
// step index, u8 0); then groups of 4 bytes per channel in turn, each 8
// codes, low nibble first.
Error decodeIma(const std::vector<uint8_t>& d, size_t at, size_t size, int channels, size_t blockAlign,
                size_t frameLimit, std::vector<int16_t>& out) {
    if (blockAlign < 4 * size_t(channels))
        return Error::fail("ADPCM block of " + std::to_string(blockAlign) + " bytes is too small");
    for (size_t block = at; block < at + size; block += blockAlign) {
        const size_t len = std::min(blockAlign, at + size - block);
        if (len < 4 * size_t(channels))
            break;
        ImaChannel ch[2];
        std::vector<int16_t> frames;
        for (int c = 0; c < channels; ++c) {
            ch[c].sample = int16_t(u16(d, block + 4 * size_t(c)));
            ch[c].index = std::min(int(d[block + 4 * size_t(c) + 2]), 88);
        }
        // The header's sample, then 2 per byte of each channel's codes.
        const size_t blockFrames = 1 + (len - 4 * size_t(channels)) * 2 / size_t(channels);
        frames.resize(blockFrames * size_t(channels));
        for (int c = 0; c < channels; ++c)
            frames[size_t(c)] = int16_t(ch[c].sample);
        size_t p = block + 4 * size_t(channels);
        for (size_t group = 0; p + 4 * size_t(channels) <= block + len; ++group)
            for (int c = 0; c < channels; ++c, p += 4)
                for (size_t k = 0; k < 8; ++k) {
                    const unsigned code = k % 2 ? d[p + k / 2] >> 4 : d[p + k / 2] & 15;
                    const size_t frame = 1 + group * 8 + k;
                    frames[frame * size_t(channels) + size_t(c)] = ch[c].decode(code);
                }
        out.insert(out.end(), frames.begin(), frames.end());
    }
    if (frameLimit && out.size() > frameLimit * size_t(channels))
        out.resize(frameLimit * size_t(channels));
    return Error::success();
}

} // namespace

Error decodeWav(const std::vector<uint8_t>& d, Sound& out) {
    out = Sound{};
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return Error::fail("not a RIFF WAVE file");
    size_t fmt = 0, fmtSize = 0, data = 0, dataSize = 0;
    uint32_t factFrames = 0;
    for (size_t p = 12; p + 8 <= d.size();) {
        const size_t size = u32(d, p + 4);
        const size_t body = p + 8;
        const size_t avail = std::min(size, d.size() - body);
        if (std::memcmp(d.data() + p, "fmt ", 4) == 0)
            fmt = body, fmtSize = avail;
        else if (std::memcmp(d.data() + p, "data", 4) == 0)
            data = body, dataSize = avail;
        else if (std::memcmp(d.data() + p, "fact", 4) == 0 && avail >= 4)
            factFrames = u32(d, body);
        p = body + size + (size & 1);
    }
    if (!fmt || fmtSize < 16)
        return Error::fail("WAVE file without a format chunk");
    if (!data)
        return Error::fail("WAVE file without sound data");
    out.format = u16(d, fmt);
    out.channels = u16(d, fmt + 2);
    out.rate = int(u32(d, fmt + 4));
    const size_t blockAlign = u16(d, fmt + 12);
    const int bits = u16(d, fmt + 14);
    if (out.channels < 1 || out.channels > 2 || out.rate <= 0)
        return Error::fail("WAVE file with " + std::to_string(out.channels) + " channels at " +
                           std::to_string(out.rate) + " Hz");
    if (out.format == 1 && bits == 16) {
        for (size_t i = 0; i + 1 < dataSize; i += 2)
            out.samples.push_back(int16_t(u16(d, data + i)));
    } else if (out.format == 1 && bits == 8) {
        for (size_t i = 0; i < dataSize; ++i)
            out.samples.push_back(int16_t((int(d[data + i]) - 128) << 8));
    } else if (out.format == 0x11 && bits == 4) {
        if (Error err = decodeIma(d, data, dataSize, out.channels, blockAlign, factFrames, out.samples))
            return err;
    } else {
        return Error::fail("WAVE format " + std::to_string(out.format) + " with " + std::to_string(bits) +
                           " bits isn't supported");
    }
    return Error::success();
}

std::vector<uint8_t> encodePcmWav(const Sound& s) {
    std::vector<uint8_t> b;
    auto put16 = [&](uint32_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    };
    auto put32 = [&](uint32_t v) {
        put16(v & 0xffff);
        put16(v >> 16);
    };
    const uint32_t bytes = uint32_t(s.samples.size() * 2);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(36 + bytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);
    put16(uint32_t(s.channels));
    put32(uint32_t(s.rate));
    put32(uint32_t(s.rate * s.channels * 2));
    put16(uint32_t(s.channels * 2));
    put16(16);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(bytes);
    for (int16_t v : s.samples)
        put16(uint16_t(v));
    return b;
}

} // namespace reluva::audio
