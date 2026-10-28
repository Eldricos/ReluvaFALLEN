// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/avi.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::video {

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

struct Parser {
    Parser(const std::vector<uint8_t>& data, Avi& avi) : d(data), out(avi) {}
    const std::vector<uint8_t>& d;
    Avi& out;
    // The streams as their headers come: which is the video's and the audio's.
    int stream = -1, videoStream = -1, audioStream = -1;
    std::string streamType;
    int audioFormat = 0, audioBits = 0;

    // Chunks from `at` to `end`; a LIST's are walked in turn.
    void walk(size_t at, size_t end) {
        while (at + 8 <= end && at + 8 <= d.size()) {
            const char* id = reinterpret_cast<const char*>(d.data() + at);
            const size_t size = le32(d.data() + at + 4);
            const size_t body = at + 8, next = body + size + (size & 1);
            if (body + size > d.size())
                return; // cut short: what's read so far stands
            if (std::memcmp(id, "LIST", 4) == 0 && size >= 4) {
                if (std::memcmp(d.data() + body, "strl", 4) == 0)
                    ++stream;
                walk(body + 4, body + size);
            } else {
                chunk(id, body, size);
            }
            at = next;
        }
    }

    void chunk(const char* id, size_t at, size_t size) {
        const uint8_t* p = d.data() + at;
        if (std::memcmp(id, "avih", 4) == 0 && size >= 40) {
            const uint32_t microseconds = le32(p);
            if (microseconds)
                out.framesPerSecond = 1e6 / double(microseconds);
            out.width = int(le32(p + 32));
            out.height = int(le32(p + 36));
        } else if (std::memcmp(id, "strh", 4) == 0 && size >= 32) {
            streamType.assign(reinterpret_cast<const char*>(p), 4);
            if (streamType == "vids" && videoStream < 0) {
                videoStream = stream;
                out.codec.assign(reinterpret_cast<const char*>(p + 4), 4);
                const uint32_t scale = le32(p + 20), rate = le32(p + 24);
                if (scale && rate)
                    out.framesPerSecond = double(rate) / double(scale);
            } else if (streamType == "auds" && audioStream < 0) {
                audioStream = stream;
            }
        } else if (std::memcmp(id, "strf", 4) == 0) {
            if (stream == videoStream && size >= 20) {
                out.width = int(le32(p + 4));
                out.height = std::abs(int32_t(le32(p + 8)));
                char compression[5] = {};
                std::memcpy(compression, p + 16, 4);
                if (compression[0])
                    out.codec = compression;
            } else if (stream == audioStream && size >= 16) {
                audioFormat = le16(p);
                out.audio.channels = le16(p + 2);
                out.audio.rate = int(le32(p + 4));
                audioBits = le16(p + 14);
                out.audio.format = audioFormat;
            }
        } else if (std::strlen(id) >= 4 && id[0] >= '0' && id[0] <= '9' && id[1] >= '0' && id[1] <= '9') {
            // A stream's data: its number, then dc/db (video) or wb (audio).
            const int n = (id[0] - '0') * 10 + (id[1] - '0');
            if (n == videoStream && (id[2] == 'd')) {
                out.frames.emplace_back(p, p + size);
            } else if (n == audioStream && id[2] == 'w' && id[3] == 'b' && audioFormat == 1) {
                if (audioBits == 16) {
                    for (size_t i = 0; i + 1 < size; i += 2)
                        out.audio.samples.push_back(int16_t(le16(p + i)));
                } else if (audioBits == 8) {
                    for (size_t i = 0; i < size; ++i)
                        out.audio.samples.push_back(int16_t((int(p[i]) - 128) << 8));
                }
            }
        }
    }
};

} // namespace

Error readAvi(const std::vector<uint8_t>& data, Avi& out) {
    out = Avi{};
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "AVI ", 4) != 0)
        return Error::fail("not an AVI file");
    Parser p(data, out);
    p.walk(12, std::min(data.size(), size_t(le32(data.data() + 4)) + 8));
    if (p.videoStream < 0 || out.frames.empty())
        return Error::fail("no video stream");
    if (out.audio.channels <= 0 || out.audio.rate <= 0)
        out.audio = audio::Sound{};
    return Error::success();
}

Error readAvi(const std::filesystem::path& file, Avi& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + file.string());
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return readAvi(data, out);
}

} // namespace reluva::video
