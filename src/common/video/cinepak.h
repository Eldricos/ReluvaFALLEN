// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Cinepak ('cvid') video frames: vector quantisation of 4x4 blocks from
// codebooks of 2x2 colour patches, kept per strip across frames. Written
// from the public description of the format; see
// docs/formats/common/cinepak.md.

#include "common/image.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::video {

class CinepakDecoder {
public:
    CinepakDecoder(int width, int height);
    // Decodes one frame onto the picture (an inter frame changes only the
    // blocks it codes; an empty frame changes nothing).
    Error decode(const std::vector<uint8_t>& frame);
    const image::Image& picture() const { return picture_; }

private:
    // A codebook entry: four luma values (a 2x2 patch, left to right, top
    // to bottom) and its colour, made RGB once.
    struct Entry {
        std::array<std::array<uint8_t, 3>, 4> rgb{};
    };
    using Codebook = std::array<Entry, 256>;
    struct Strip {
        Codebook v1{}, v4{};
    };
    Error strip(const uint8_t* p, size_t size, Strip& s, int top, int bottom, int left, int right);
    void codebook(const uint8_t* p, size_t size, Codebook& book, bool partial, bool grey);
    void putV1(const Entry& e, int x, int y);
    void putV4(const Entry& a, const Entry& b, const Entry& c, const Entry& d, int x, int y);
    void put(int x, int y, const std::array<uint8_t, 3>& rgb);

    image::Image picture_;
    std::vector<Strip> strips_;
};

} // namespace reluva::video
