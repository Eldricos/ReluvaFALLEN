// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Textures and palettes (Texture and Palette objects, mostly in .utx
// packages; maps carry their own in the MyLevel group). See
// docs/formats/unreal/texture.md.

#include "common/image.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/properties.h"

#include <array>
#include <cstdint>
#include <string>

namespace reluva::unreal {

// How a texture's pixels are stored (its Format property).
enum class TextureFormat : uint8_t { P8 = 0, RGBA7 = 1, RGB16 = 2, DXT1 = 3, RGB8 = 4, RGBA8 = 5 };
const char* formatName(uint8_t format);

// Surface and texture flags (a texture's PolyFlags, a level surface's).
namespace polyflag {
constexpr uint32_t Invisible = 0x00000001;
constexpr uint32_t Masked = 0x00000002;
constexpr uint32_t Translucent = 0x00000004;
constexpr uint32_t NotSolid = 0x00000008;
constexpr uint32_t Environment = 0x00000010;
constexpr uint32_t Semisolid = 0x00000020;
constexpr uint32_t Modulated = 0x00000040;
constexpr uint32_t FakeBackdrop = 0x00000080;
constexpr uint32_t TwoSided = 0x00000100;
constexpr uint32_t NoSmooth = 0x00000800;
constexpr uint32_t Unlit = 0x00400000;
constexpr uint32_t Portal = 0x04000000;
constexpr uint32_t Mirrored = 0x08000000;
} // namespace polyflag

struct Palette {
    std::array<uint8_t, 256 * 4> rgba{}; // as stored: R, G, B, A
    int count = 0;
};
Error readPalette(const Package& package, int exportIndex, Palette& out);

struct TextureInfo {
    int width = 0, height = 0; // of the first mipmap
    uint8_t format = 0;
    int mipCount = 0;
    bool masked = false;     // palette index 0 is see-through
    uint32_t polyFlags = 0;  // how surfaces using it draw by default
    std::string paletteName; // the palette's path, for viewers
};

// Decodes a texture's first mipmap to RGBA. The palette is looked up
// through `packages` (it can be in another package, though it rarely is).
Error decodeTexture(const PackageSet& packages, const Package& package, int exportIndex, image::Image& out,
                    TextureInfo* info = nullptr);

// Whether a class name is a texture's (Texture and its procedural
// subclasses, which keep a first frame in their mipmaps).
bool isTextureClass(std::string_view className);

} // namespace reluva::unreal
