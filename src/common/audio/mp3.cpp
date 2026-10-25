// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/mp3.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <string>

namespace reluva::audio {

Error decodeMp3(const std::vector<uint8_t>& data, Sound& out) {
    out = Sound{};
    out.format = kFormatMp3;
    size_t at = 0;
    // ID3v2: "ID3", version (2 bytes), flags, then a 28-bit size in four
    // 7-bit bytes, not counting the 10-byte header (or a footer, flag 0x10).
    if (data.size() >= 10 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        const size_t size = size_t(data[6] & 0x7f) << 21 | size_t(data[7] & 0x7f) << 14 |
                            size_t(data[8] & 0x7f) << 7 | size_t(data[9] & 0x7f);
        at = 10 + size + ((data[5] & 0x10) ? 10 : 0);
        if (at > data.size())
            return Error::fail("ID3 tag runs past the end of the file");
    }
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (at < data.size()) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&dec, data.data() + at, int(data.size() - at), pcm, &info);
        if (info.frame_bytes == 0)
            break; // no more frames
        at += size_t(info.frame_bytes);
        if (samples == 0)
            continue; // skipped data, or a frame decoding needs more of
        if (out.channels == 0) {
            out.channels = info.channels;
            out.rate = info.hz;
        } else if (info.channels != out.channels || info.hz != out.rate) {
            return Error::fail("the MP3 changes from " + std::to_string(out.rate) + " Hz, " +
                               std::to_string(out.channels) + " channel(s) to " + std::to_string(info.hz) +
                               " Hz, " + std::to_string(info.channels));
        }
        out.samples.insert(out.samples.end(), pcm, pcm + size_t(samples) * size_t(info.channels));
    }
    if (out.channels == 0)
        return Error::fail("no MP3 frames found");
    return Error::success();
}

} // namespace reluva::audio
