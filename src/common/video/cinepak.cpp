// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/cinepak.h"

#include <algorithm>

namespace reluva::video {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t be24(const uint8_t* p) { return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// Reads the bit flags a vector chunk carries: 32 at a time, the highest
// first, a new word read when the last is used up.
struct Flags {
    const uint8_t*& p;
    const uint8_t* end;
    uint32_t word = 0;
    int left = 0;
    bool next(bool& ok) {
        if (left == 0) {
            if (end - p < 4) {
                ok = false;
                return false;
            }
            word = be32(p);
            p += 4;
            left = 32;
        }
        --left;
        return (word >> left) & 1;
    }
};

} // namespace

CinepakDecoder::CinepakDecoder(int width, int height) {
    picture_.w = width;
    picture_.h = height;
    picture_.rgba.assign(size_t(width) * size_t(height) * 4, 255);
    for (size_t i = 0; i < picture_.rgba.size(); i += 4)
        picture_.rgba[i] = picture_.rgba[i + 1] = picture_.rgba[i + 2] = 0;
}

// A frame: a byte of flags, its length (24 bits), its width and height,
// its strip count (16 bits each, big-endian), then the strips one under the
// other. Each strip's codebooks start as the strip before's in this frame
// unless the flags' bit 0 says each keeps its own.
Error CinepakDecoder::decode(const std::vector<uint8_t>& frame) {
    if (frame.empty())
        return Error::success();
    if (frame.size() < 10)
        return Error::fail("a Cinepak frame shorter than its header");
    const uint8_t* p = frame.data();
    const uint8_t flags = p[0];
    const size_t length = std::min<size_t>(be24(p + 1), frame.size());
    const int strips = be16(p + 8);
    const uint8_t* end = frame.data() + length;
    p += 10;
    if (int(strips_.size()) < strips)
        strips_.resize(size_t(strips));
    int top = 0;
    for (int i = 0; i < strips; ++i) {
        if (end - p < 12)
            return Error::fail("a Cinepak strip's header past the frame");
        const size_t size = be16(p + 2);
        const int y1 = be16(p + 4), x1 = be16(p + 6), y2 = be16(p + 8), x2 = be16(p + 10);
        if (size < 12 || size_t(end - p) < size)
            return Error::fail("a Cinepak strip past the frame");
        if (i > 0 && !(flags & 1))
            strips_[size_t(i)] = strips_[size_t(i) - 1];
        // Its rows: under the strip before, as tall as its own corners say.
        const int height = y2 - y1;
        const int bottom = std::min(picture_.h, top + std::max(0, height));
        if (Error err = strip(p + 12, size - 12, strips_[size_t(i)], top, bottom, x1, std::min(picture_.w, x2)))
            return err;
        top = bottom;
        p += size;
    }
    return Error::success();
}

// A strip's chunks, each an id and a size (16 bits each, the size counting
// the header): codebooks (0x20xx V4, 0x22xx V1; +0x100 a partial update,
// +0x400 grey) and the vectors (0x3000 a key frame's, 0x3100 changes only,
// 0x3200 V1 only).
Error CinepakDecoder::strip(const uint8_t* p, size_t size, Strip& s, int top, int bottom, int left, int right) {
    const uint8_t* end = p + size;
    while (end - p >= 4) {
        const uint16_t id = be16(p);
        const size_t chunk = be16(p + 2);
        if (chunk < 4 || size_t(end - p) < chunk)
            return Error::fail("a Cinepak chunk past its strip");
        const uint8_t* data = p + 4;
        const uint8_t* dataEnd = p + chunk;
        switch (id & 0xff00) {
        case 0x2000: case 0x2100: case 0x2400: case 0x2500:
            codebook(data, size_t(dataEnd - data), s.v4, id & 0x0100, id & 0x0400);
            break;
        case 0x2200: case 0x2300: case 0x2600: case 0x2700:
            codebook(data, size_t(dataEnd - data), s.v1, id & 0x0100, id & 0x0400);
            break;
        case 0x3000: case 0x3100: case 0x3200: {
            const bool inter = id == 0x3100, v1Only = id == 0x3200;
            const uint8_t* q = data;
            Flags flags{q, dataEnd};
            bool ok = true;
            for (int y = top; y < bottom && ok; y += 4)
                for (int x = left; x < right && ok; x += 4) {
                    if (inter && !flags.next(ok))
                        continue; // unchanged
                    if (!ok)
                        break;
                    const bool v4 = !v1Only && flags.next(ok);
                    if (!ok)
                        break;
                    if (v4) {
                        if (dataEnd - q < 4) {
                            ok = false;
                            break;
                        }
                        putV4(s.v4[q[0]], s.v4[q[1]], s.v4[q[2]], s.v4[q[3]], x, y);
                        q += 4;
                    } else {
                        if (dataEnd - q < 1) {
                            ok = false;
                            break;
                        }
                        putV1(s.v1[q[0]], x, y);
                        q += 1;
                    }
                }
            break;
        }
        default:
            break; // not known: passed over
        }
        p += chunk;
    }
    return Error::success();
}

// Entries are four luma bytes, then (unless grey) U and V as signed bytes;
// a partial update has 32 bits of flags before each 32 entries, an entry
// read only where its bit (the highest first) is set. The colour:
// R = Y + 2V, G = Y - U/2 - V, B = Y + 2U.
void CinepakDecoder::codebook(const uint8_t* p, size_t size, Codebook& book, bool partial, bool grey) {
    const uint8_t* end = p + size;
    const size_t entry = grey ? 4 : 6;
    uint32_t mask = 0;
    int left = 0;
    for (int i = 0; i < 256; ++i) {
        if (partial) {
            if (left == 0) {
                if (end - p < 4)
                    return;
                mask = be32(p);
                p += 4;
                left = 32;
            }
            --left;
            if (!((mask >> left) & 1))
                continue;
        }
        if (size_t(end - p) < entry)
            return;
        const int u = grey ? 0 : int(int8_t(p[4])), v = grey ? 0 : int(int8_t(p[5]));
        for (size_t k = 0; k < 4; ++k) {
            const int y = p[k];
            book[size_t(i)].rgb[k] = {clamp8(y + 2 * v), clamp8(y - u / 2 - v), clamp8(y + 2 * u)};
        }
        p += entry;
    }
}

void CinepakDecoder::put(int x, int y, const std::array<uint8_t, 3>& rgb) {
    if (x < 0 || y < 0 || x >= picture_.w || y >= picture_.h)
        return;
    uint8_t* out = picture_.pixel(x, y);
    out[0] = rgb[0];
    out[1] = rgb[1];
    out[2] = rgb[2];
}

// A V1 block: one entry's 2x2 patch, each of its pixels doubled to fill
// the 4x4 block.
void CinepakDecoder::putV1(const Entry& e, int x, int y) {
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            put(x + i, y + j, e.rgb[size_t((j / 2) * 2 + i / 2)]);
}

// A V4 block: four entries' 2x2 patches, top left, top right, bottom left,
// bottom right.
void CinepakDecoder::putV4(const Entry& a, const Entry& b, const Entry& c, const Entry& d, int x, int y) {
    const Entry* quads[4] = {&a, &b, &c, &d};
    for (int q = 0; q < 4; ++q) {
        const int qx = x + (q % 2) * 2, qy = y + (q / 2) * 2;
        for (int k = 0; k < 4; ++k)
            put(qx + k % 2, qy + k / 2, quads[q]->rgb[size_t(k)]);
    }
}

} // namespace reluva::video
