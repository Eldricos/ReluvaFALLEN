// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/texture.h"

#include "common/fs.h"

#include <algorithm>

namespace reluva::unreal {

const char* formatName(uint8_t format) {
    static const char* const kNames[] = {"P8", "RGBA7", "RGB16", "DXT1", "RGB8", "RGBA8"};
    return format < 6 ? kNames[format] : "unknown";
}

bool isTextureClass(std::string_view className) {
    const std::string c = fs::toLower(className);
    return c == "texture" || c == "firetexture" || c == "watertexture" || c == "wavetexture" ||
           c == "wettexture" || c == "icetexture" || c == "scriptedtexture";
}

Error readPalette(const Package& package, int index, Palette& out) {
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const Export& e = package.exports()[size_t(index)];
    Reader r(data, size_t(e.serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    const int32_t count = r.index();
    if (!r.ok() || count < 0 || count > 256)
        return Error::fail("palette " + package.path(index + 1) + " has " + std::to_string(count) + " colours");
    out.count = count;
    out.rgba.fill(0);
    r.bytes(out.rgba.data(), size_t(count) * 4);
    if (!r.ok())
        return Error::fail("palette " + package.path(index + 1) + " is cut short");
    return Error::success();
}

namespace {

void rgb565(uint16_t c, uint8_t* out) {
    out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
    out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
    out[2] = uint8_t((c & 31) * 255 / 31);
    out[3] = 255;
}

// DXT1 (BC1): 4x4 blocks of two 565 colours and 2-bit indices; when the
// first colour is not greater, index 3 is transparent black.
void decodeDxt1(const uint8_t* src, size_t size, int w, int h, image::Image& out) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const size_t at = (size_t(by) * size_t(bw) + size_t(bx)) * 8;
            if (at + 8 > size)
                return;
            const uint8_t* b = src + at;
            const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
            uint8_t pal[4][4];
            rgb565(c0, pal[0]);
            rgb565(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1) {
                    pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
                    pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
                } else {
                    pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
                    pal[3][k] = 0;
                }
            }
            pal[2][3] = 255;
            pal[3][3] = c0 > c1 ? 255 : 0;
            const uint32_t bits = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x, py = by * 4 + y;
                    if (px >= w || py >= h)
                        continue;
                    const uint8_t* c = pal[(bits >> (2 * (y * 4 + x))) & 3];
                    std::copy_n(c, 4, out.pixel(px, py));
                }
        }
}

} // namespace

Error decodeTexture(const PackageSet& packages, const Package& package, int index, image::Image& out,
                    TextureInfo* info) {
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const Export& e = package.exports()[size_t(index)];
    const std::string what = package.path(index + 1);
    Reader r(data, size_t(e.serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;

    TextureInfo ti;
    ti.format = props.byte("Format", 0);
    ti.masked = props.boolean("bMasked");
    ti.polyFlags = uint32_t(props.integer("PolyFlags", 0));
    const int32_t paletteRef = props.object("Palette");
    ti.paletteName = paletteRef ? package.path(paletteRef) : std::string();

    // The mipmaps, largest first; only the first is decoded.
    ti.mipCount = r.index();
    if (!r.ok() || ti.mipCount <= 0 || ti.mipCount > 16)
        return Error::fail(what + ": " + std::to_string(ti.mipCount) + " mipmaps");
    if (package.version() >= 63)
        r.i32(); // where the data ends, to skip it
    const int32_t size = r.index();
    if (!r.ok() || size < 0 || size_t(size) > r.remaining())
        return Error::fail(what + ": mipmap data runs past the end");
    const size_t dataAt = r.pos();
    r.skip(size_t(size));
    ti.width = r.i32();
    ti.height = r.i32();
    if (!r.ok() || ti.width <= 0 || ti.height <= 0 || ti.width > 8192 || ti.height > 8192)
        return Error::fail(what + ": bad size " + std::to_string(ti.width) + "x" + std::to_string(ti.height));
    if (info)
        *info = ti;
    const uint8_t* src = data.data() + (dataAt - size_t(e.serialOffset));
    const size_t pixels = size_t(ti.width) * size_t(ti.height);

    out.w = ti.width;
    out.h = ti.height;
    out.rgba.assign(pixels * 4, 0);
    switch (static_cast<TextureFormat>(ti.format)) {
    case TextureFormat::P8: {
        if (size_t(size) < pixels)
            return Error::fail(what + ": " + std::to_string(size) + " bytes for " + std::to_string(pixels) + " pixels");
        const ObjectRef pal = packages.resolve(package, paletteRef);
        Palette palette;
        if (!pal)
            return Error::fail(what + ": palette " + ti.paletteName + " not found");
        if (Error err = readPalette(*pal.package, pal.exportIndex, palette))
            return err;
        for (size_t i = 0; i < pixels; ++i) {
            const uint8_t c = src[i];
            uint8_t* p = out.rgba.data() + i * 4;
            std::copy_n(palette.rgba.data() + size_t(c) * 4, 3, p);
            // The palettes' alpha isn't used for P8 textures; masked ones
            // see through colour 0.
            p[3] = (ti.masked || (ti.polyFlags & polyflag::Masked)) && c == 0 ? 0 : 255;
        }
        return Error::success();
    }
    case TextureFormat::DXT1:
        decodeDxt1(src, size_t(size), ti.width, ti.height, out);
        return Error::success();
    case TextureFormat::RGBA8:
        if (size_t(size) < pixels * 4)
            return Error::fail(what + ": too little data for RGBA8");
        // Stored blue, green, red, alpha.
        for (size_t i = 0; i < pixels; ++i) {
            uint8_t* p = out.rgba.data() + i * 4;
            p[0] = src[i * 4 + 2];
            p[1] = src[i * 4 + 1];
            p[2] = src[i * 4];
            p[3] = src[i * 4 + 3];
        }
        return Error::success();
    case TextureFormat::RGB8:
        if (size_t(size) < pixels * 3)
            return Error::fail(what + ": too little data for RGB8");
        for (size_t i = 0; i < pixels; ++i) {
            uint8_t* p = out.rgba.data() + i * 4;
            p[0] = src[i * 3 + 2];
            p[1] = src[i * 3 + 1];
            p[2] = src[i * 3];
            p[3] = 255;
        }
        return Error::success();
    default:
        return Error::fail(what + ": format " + formatName(ti.format) + " isn't supported yet");
    }
}

} // namespace reluva::unreal
