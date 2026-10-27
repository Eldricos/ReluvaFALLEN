// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/texture.h"

#include "engines/unreal/procedural.h"

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
           c == "wettexture" || c == "icetexture" || c == "scriptedtexture" ||
           c == "flitexture"; // The Fallen's: frames from an FLC animation (a C3DFLIC object)
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

// A stand-in for a procedural texture without pixels, `w` x `h`: the
// texture it distorts (scaled to its size), or a gradient through its
// palette, brightest at the bottom as flames rise.
Error proceduralStandIn(const PackageSet& packages, const Package& package, const PropertyList& props, int w, int h,
                        int depth, image::Image& out, std::string& what);

} // namespace

static Error decodeAt(const PackageSet& packages, const Package& package, int index, image::Image& out,
                      TextureInfo* info, int depth) {
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

    // The mipmaps, largest first; only the first is decoded. A texture
    // with bHasComp set also keeps a second list, in CompFormat (DXT1),
    // after the first; its first list's pixels may then be empty.
    struct Mip {
        size_t dataAt = 0;
        int32_t size = 0;
        int width = 0, height = 0;
    };
    auto readMips = [&](std::vector<Mip>& mips) -> Error {
        const int32_t count = r.index();
        if (!r.ok() || count < 0 || count > 16)
            return Error::fail(what + ": " + std::to_string(count) + " mipmaps");
        mips.resize(size_t(count));
        for (Mip& m : mips) {
            if (package.version() >= 63)
                r.i32(); // where the data ends, to skip it
            m.size = r.index();
            if (!r.ok() || m.size < 0 || size_t(m.size) > r.remaining())
                return Error::fail(what + ": mipmap data runs past the end");
            m.dataAt = r.pos();
            r.skip(size_t(m.size));
            m.width = r.i32();
            m.height = r.i32();
            r.u8(); // log2 of the width and height
            r.u8();
            if (!r.ok())
                return Error::fail(what + ": mipmaps run past the end");
        }
        return Error::success();
    };
    std::vector<Mip> mips, compMips;
    if (Error err = readMips(mips))
        return err;
    if (props.boolean("bHasComp"))
        if (Error err = readMips(compMips))
            return err;
    const Mip* mip = mips.empty() ? nullptr : &mips[0];
    if ((!mip || mip->size == 0) && !compMips.empty()) {
        mip = &compMips[0];
        ti.format = props.byte("CompFormat", uint8_t(TextureFormat::DXT1));
    }
    ti.mipCount = int(mips.size());
    if (!mip)
        return Error::fail(what + ": no mipmaps");
    ti.width = mip->width;
    ti.height = mip->height;
    if (ti.width <= 0 || ti.height <= 0 || ti.width > 8192 || ti.height > 8192)
        return Error::fail(what + ": bad size " + std::to_string(ti.width) + "x" + std::to_string(ti.height));
    if (info)
        *info = ti;
    if (mip->size == 0) {
        const std::string cls = package.className(index + 1);
        const std::string described = what + " (" + cls + ", " + formatName(ti.format) + " " +
                                      std::to_string(ti.width) + "x" + std::to_string(ti.height) + ")";
        if (!isTextureClass(cls) || fs::toLower(cls) == "texture")
            return Error::fail(described + ": no pixels stored" +
                               (props.find("bHasComp") ? "" : ", and no bHasComp"));
        std::string standIn;
        // Fire, water and FLC textures: their animation's first picture.
        // (Wet ones show their source texture, unrippled, as before.)
        if (fs::toLower(cls) != "wettexture" && isAnimatedTextureClass(cls)) {
            std::string why;
            if (auto anim = openAnimatedTexture(packages, package, index, &why)) {
                out = anim->image();
                if (info)
                    info->standIn = cls + " (animated), shown still: its " + anim->kind() + " as it starts";
                return Error::success();
            }
        }
        if (Error err = proceduralStandIn(packages, package, props, ti.width, ti.height, depth, out, standIn))
            return Error::fail(described + ": no pixels stored, and no stand-in: " + err.message);
        if (info)
            info->standIn = cls + " (procedural), shown as " + standIn;
        return Error::success();
    }
    const int32_t size = mip->size;
    const uint8_t* src = data.data() + (mip->dataAt - size_t(e.serialOffset));
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

namespace {

Error proceduralStandIn(const PackageSet& packages, const Package& package, const PropertyList& props, int w, int h,
                        int depth, image::Image& out, std::string& what) {
    out.w = w;
    out.h = h;
    out.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (const char* name : {"SourceTexture", "GlassTexture"}) {
        const int32_t ref = props.object(name);
        const ObjectRef src = packages.resolve(package, ref);
        image::Image img;
        if (!src || depth > 4 || decodeAt(packages, *src.package, src.exportIndex, img, nullptr, depth + 1))
            continue;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                std::copy_n(img.pixel(x * img.w / w, y * img.h / h), 4, out.pixel(x, y));
        what = "its " + std::string(name) + " " + package.path(ref);
        return Error::success();
    }
    const int32_t paletteRef = props.object("Palette");
    const ObjectRef pal = packages.resolve(package, paletteRef);
    Palette palette;
    if (!pal)
        return Error::fail("no source texture or palette");
    if (Error err = readPalette(*pal.package, pal.exportIndex, palette))
        return err;
    for (int y = 0; y < h; ++y) {
        const int c = h > 1 ? y * 255 / (h - 1) : 255;
        for (int x = 0; x < w; ++x) {
            uint8_t* p = out.pixel(x, y);
            std::copy_n(palette.rgba.data() + size_t(c) * 4, 3, p);
            p[3] = 255;
        }
    }
    what = "a gradient through its palette";
    return Error::success();
}

} // namespace

Error decodeTexture(const PackageSet& packages, const Package& package, int index, image::Image& out,
                    TextureInfo* info) {
    return decodeAt(packages, package, index, out, info, 0);
}

} // namespace reluva::unreal
