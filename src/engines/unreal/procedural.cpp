// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/procedural.h"

#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include "common/fs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace reluva::unreal {

namespace {

constexpr float kPi = 3.14159265358979f;

// A texture's properties and where its class's own data starts, after its
// mipmaps (one list, or two with bHasComp; see texture.md).
struct TextureData {
    std::vector<uint8_t> data;
    size_t base = 0;     // the export's offset in the file, as Reader positions count
    size_t native = 0;   // where the class's own data starts (a Reader position)
    PropertyList props;
    int width = 0, height = 0;
    size_t mip0 = 0, mip0Size = 0; // the first mipmap's bytes (an index into data)
};

Error readTextureData(const Package& package, int index, TextureData& out) {
    if (Error err = package.readExport(index, out.data))
        return err;
    out.base = size_t(package.exports()[size_t(index)].serialOffset);
    Reader r(out.data, out.base);
    if (Error err = beginObject(package, index, r, out.props))
        return err;
    const std::string what = package.path(index + 1);
    for (int list = 0; list < (out.props.boolean("bHasComp") ? 2 : 1); ++list) {
        const int32_t count = r.index();
        if (!r.ok() || count < 0 || count > 16)
            return Error::fail(what + ": " + std::to_string(count) + " mipmaps");
        for (int32_t m = 0; m < count; ++m) {
            if (package.version() >= 63)
                r.i32();
            const int32_t size = r.index();
            if (size < 0)
                return Error::fail(what + ": a mipmap of " + std::to_string(size) + " bytes");
            if (list == 0 && m == 0) {
                out.mip0 = r.pos() - out.base;
                out.mip0Size = size_t(size);
            }
            r.skip(size_t(size));
            const int32_t w = r.i32(), h = r.i32();
            r.u8();
            r.u8();
            if (list == 0 && m == 0) {
                out.width = w;
                out.height = h;
            }
        }
    }
    if (!r.ok())
        return Error::fail(what + ": mipmaps run past the end");
    if (out.width <= 0 || out.height <= 0 || out.width > 4096 || out.height > 4096)
        return Error::fail(what + ": bad size " + std::to_string(out.width) + "x" + std::to_string(out.height));
    out.native = r.pos();
    return Error::success();
}

// The texture's palette, grey if it has none.
Palette paletteOf(const PackageSet& packages, const Package& package, const PropertyList& props) {
    Palette p;
    const ObjectRef ref = packages.resolve(package, props.object("Palette"));
    if (!ref || readPalette(*ref.package, ref.exportIndex, p))
        for (int i = 0; i < 256; ++i) {
            for (int k = 0; k < 3; ++k)
                p.rgba[size_t(i) * 4 + size_t(k)] = uint8_t(i);
            p.count = 256;
        }
    return p;
}

// Palette indices to RGBA; colour 0 see-through when masked.
void toRgba(const std::vector<uint8_t>& indices, const Palette& palette, bool masked, image::Image& out) {
    out.rgba.resize(indices.size() * 4);
    for (size_t i = 0; i < indices.size(); ++i) {
        std::copy_n(palette.rgba.data() + size_t(indices[i]) * 4, 3, out.rgba.data() + i * 4);
        out.rgba[i * 4 + 3] = masked && indices[i] == 0 ? 0 : 255;
    }
}

// When a texture steps, as the engine's UTexture::Tick paces it (read for
// what it does): with no MaxFrameRate, once every frame drawn; else at
// most once a frame and no faster than MaxFrameRate, a frame's time added
// up until 1 / MaxFrameRate has gone by, then a step; the sum starts again
// unless it's past 1 / MinFrameRate (the rates kept to 0.01-100), when only
// that much is taken off, so a slow display catches up.
struct Pace {
    float fastest = 0, slowest = 0; // the shortest and longest time between steps; fastest 0: every frame
    float sum = 0;
    explicit Pace(const PropertyList& props) {
        const float most = props.number("MaxFrameRate");
        if (most != 0) {
            fastest = 1 / std::clamp(most, 0.01f, 100.0f);
            slowest = 1 / std::clamp(props.number("MinFrameRate"), 0.01f, 100.0f);
        }
    }
    int due(float seconds) {
        if (fastest == 0)
            return 1;
        sum += std::max(0.0f, seconds);
        if (sum < fastest)
            return 0;
        if (sum < slowest)
            sum = 0;
        else
            sum -= slowest;
        return 1;
    }
};

// Fire.dll's sine table: a turn in 256 steps, 0-255 (127 or 128 at 0).
const std::array<uint8_t, 256>& sineTable() {
    static const std::array<uint8_t, 256> table = [] {
        std::array<uint8_t, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = uint8_t(std::nearbyint(std::sin(float(i) / 256 * 2 * kPi) * 127.5f + 127.45f));
        return t;
    }();
    return table;
}

// Fire.dll's random numbers, one stream for all its textures: a table of
// 512 bytes (filled when it loads, here from MSVC's rand() as it starts),
// read and stirred four bytes at a time.
class FractalRandom {
public:
    FractalRandom() {
        uint32_t seed = 1;
        for (uint8_t& b : table_) {
            seed = seed * 214013u + 2531011u;
            b = uint8_t((seed >> 16) & 0x7fff);
        }
    }
    uint32_t next() {
        uint32_t r, v;
        std::memcpy(&r, &table_[(index_ + 0x80) & 0xfc], 4);
        index_ = (index_ + 4) & 0xfc;
        std::memcpy(&v, &table_[index_], 4);
        v ^= r;
        std::memcpy(&table_[index_], &v, 4);
        return r;
    }
    uint8_t byte() { return uint8_t(next()); }

private:
    std::array<uint8_t, 512> table_{};
    uint32_t index_ = 0;
};
FractalRandom& fractalRandom() {
    static FractalRandom random;
    return random;
}

// ---------------------------------------------------------------------
// Fire: a heat value per pixel, shown through the palette, as Fire.dll
// does it (read for what it does, its sparks checked against it running
// in an emulator; see docs/formats/unreal/texture.md). Each spark is 8
// bytes after the mipmaps (a compact count first): type, heat, X, Y and
// the bytes A-D. Each step every spark in turn draws itself (writing its
// pixels' heat outright) and the emitters add moving sparks to the same
// list; then the heat spreads and cools; then the Stars look again.

enum Spark : uint8_t {
    Burn, Sparkle, Pulse, Signal, Blaze, OzHasSpoken, Cone, BlazeRight, BlazeLeft, Cylinder, Cylinder3D,
    Lissajous, Jugglers, Emit, Fountain, Flocks, Eels, Organic, WanderOrganic, RandomCloud, CustomCloud,
    LocalCloud, Stars, LineLightning, RampLightning, SphereLightning, Wheel, Gametes, Sprinkler,
    LissajousX, LissajousY, // the editor's halves of a Lissajous
    // The moving sparks the emitters make, by what makes them.
    Dead = 31, BlazeSpark, EmitSpark, ConeSpark, OrganicSpark, CloudSpark, CustomSpark, EelSpark, FlockSpark,
    SprinklerSpark, CoolingSpark, FountainSpark, GameteSpark,
};

int8_t s8(uint8_t v) { return int8_t(v); }

class Fire : public AnimatedTexture {
public:
    struct SparkData {
        uint8_t type = 0, heat = 0, x = 0, y = 0, a = 0, b = 0, c = 0, d = 0;
    };

    Fire(int w, int h, const PropertyList& props, Palette palette, std::vector<SparkData> sparks)
        : w_(w), h_(h), palette_(palette), pace_(props) {
        ubits_ = 0;
        while ((1 << (ubits_ + 1)) <= w_)
            ++ubits_;
        um_ = uint8_t(w_ - 1), vm_ = uint8_t(h_ - 1);
        rising_ = props.boolean("bRising");
        keep_ = props.byte("RenderHeat", 220);
        for (int i = 0; i < 1024; ++i)
            cool_[size_t(i)] = uint8_t(std::clamp(float(i) * 0.25f + 1 - float(255 - keep_) * 0.0625f, 0.0f, 255.0f));
        masked_ = props.boolean("bMasked");
        // As loaded: sparks off the texture dropped; the list as long as
        // SparksLimit (only if it held more than that is the limit kept to
        // 4-8192 and the list cut).
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(),
                                    [&](const SparkData& s) { return s.x > um_ || s.y > vm_; }),
                     sparks.end());
        int32_t limit = props.integer("SparksLimit");
        if (size_t(std::max(0, limit)) < sparks.size()) {
            limit = std::clamp<int32_t>(limit, 4, 8192);
            if (sparks.size() > size_t(limit))
                sparks.resize(size_t(limit));
        }
        count_ = sparks.size();
        sparks.resize(std::max<size_t>(count_, size_t(limit)));
        sparks_ = std::move(sparks);
        limit_ = sparks_.size();
        heat_.assign(size_t(w) * size_t(h), 0);
        next_.assign(heat_.size(), 0);
        image_.w = w;
        image_.h = h;
        toRgba(heat_, palette_, masked_, image_);
    }

    const char* kind() const override { return "fire"; }

    bool advance(float seconds) override {
        const int n = pace_.due(seconds);
        for (int i = 0; i < n; ++i)
            tick();
        if (n)
            toRgba(heat_, palette_, masked_, image_);
        return n > 0;
    }

private:
    uint8_t r() { return fractalRandom().byte(); }
    void put(int x, int y, uint8_t v) {
        const size_t i = (size_t(y) << ubits_) + size_t(x);
        if (i < heat_.size())
            heat_[i] = v;
    }
    // One pixel's chance of a step along an axis: |v| / 128, its sign the
    // way; one random drawn whatever v is.
    void step(uint8_t& coord, uint8_t v, uint8_t mask) {
        const int sv = s8(v), roll = int(fractalRandom().next() & 127);
        if (sv < 0 ? roll < -sv : roll < sv)
            coord = uint8_t((coord + (sv < 0 ? -1 : 1)) & mask);
    }
    void move(SparkData& s, uint8_t dx, uint8_t dy) {
        step(s.x, dx, um_);
        step(s.y, dy, vm_);
    }
    SparkData* child() { return count_ < limit_ ? &sparks_[count_++] : nullptr; }
    uint8_t near31(uint8_t v, uint8_t mask) { return uint8_t(((fractalRandom().next() & 31) + v) & mask); }
    void wander15(SparkData& s) {
        if (fractalRandom().next() & 1)
            s.x = uint8_t(((fractalRandom().next() & 15) + s.x - 7) & um_);
        if (fractalRandom().next() & 1)
            s.y = uint8_t(((fractalRandom().next() & 15) + s.y - 7) & vm_);
    }
    void wander7(SparkData& s) {
        uint8_t a = r(), b = r();
        s.x = uint8_t((s.x + (a & 7) - (b & 7)) & um_);
        a = r(), b = r();
        s.y = uint8_t((s.y + (a & 7) - (b & 7)) & vm_);
    }

    // A flash: a jagged line from (x0, y0) of lengths lx, ly (bit 0 of
    // each: the negative way), its heat ramping from c1 to c2.
    void flash(uint8_t x0, uint8_t y0, int lx, int ly, int c1, int c2) {
        if (((ly & 1) && 2 * ly >= lx) || ((lx & 1) && 2 * ly < lx)) {
            x0 = uint8_t(x0 + ((lx & 1) ? -lx : lx));
            y0 = uint8_t(y0 + ((ly & 1) ? -ly : ly));
            lx ^= 1, ly ^= 1;
            std::swap(c1, c2);
        }
        const int n = std::max(lx, ly) | 1;
        std::vector<int> jitter(static_cast<size_t>(n));
        int total = 0;
        for (int& j : jitter)
            total += (j = r());
        const int64_t span = int64_t(c2 - c1) * (1 << 23);
        const int64_t colourStep = span / n; // rounded towards 0
        int64_t colour = int64_t(c1) << 23;
        const bool xMajor = lx >= ly;
        const int across = xMajor ? ((ly & 1) ? -ly : ly) : ((lx & 1) ? -lx : lx);
        const int perStep = (across * 64 - total) / n; // rounded towards 0
        int acc = (xMajor ? y0 : x0) * 64;
        const int length = xMajor ? lx : ly, along = ((xMajor ? lx : ly) & 1) ? -1 : 1;
        uint8_t major = xMajor ? x0 : y0;
        for (int i = 0; i < length; ++i) {
            acc += jitter[size_t(i)] + perStep;
            colour += colourStep;
            const int minor = acc >> 6;
            if (xMajor)
                put(major & um_, minor & vm_, uint8_t(colour >> 23));
            else
                put(minor & um_, major & vm_, uint8_t(colour >> 23));
            major = uint8_t(major + along);
        }
    }

    void tick() {
        ++phase_;
        for (size_t i = 0; i < count_; ++i)
            if (spark(sparks_[i])) {
                sparks_[i] = sparks_[--count_]; // the last moves here (and is next looked at next step)
            }
        spread();
        if (starsOn_) {
            bool any = false;
            for (size_t i = 0; i < count_; ++i) {
                SparkData& s = sparks_[i];
                if (s.type != Stars)
                    continue;
                any = true;
                const size_t at = (size_t(s.y) << ubits_) + s.x;
                if (at >= heat_.size())
                    continue;
                s.b = heat_[at];
                if (s.b < 0x26)
                    heat_[at] = s.a;
            }
            starsOn_ = any;
        }
    }

    // One spark's step; true when it dies.
    bool spark(SparkData& s) {
        const auto& sine = sineTable();
        auto sin32 = [&](uint8_t i) { return uint8_t(std::min(255, sine[i] + 32)); };
        const bool room = count_ < limit_;
        switch (s.type) {
        case Burn: put(s.x, s.y, r()); break;
        case Sparkle: {
            const int x = s.x + ((s.a * r()) >> 8);
            const int y = s.y + ((s.b * r()) >> 8);
            put(x & um_, y & vm_, s.heat);
            break;
        }
        case Pulse:
            put(s.x, s.y, s.heat);
            s.heat = uint8_t(s.heat + s.d);
            break;
        case Signal:
            if (s.heat > s.c)
                put(s.x, s.y, s.heat);
            s.heat = uint8_t(s.heat + s.d);
            if (s.heat < s.d)
                s.heat = r();
            break;
        case Cylinder:
        case Cylinder3D:
        case Jugglers: {
            const uint8_t v = uint8_t(std::min(255, sine[uint8_t(s.a + 64)] + s.heat));
            const uint8_t off = uint8_t((sine[s.a] * s.b) >> 8);
            if (s.type == Jugglers)
                put(s.x, uint8_t(s.y + off) & vm_, v);
            else if (s.type == Cylinder || uint8_t(s.a + 64) < 128)
                put(uint8_t(s.x + off) & um_, s.y, v);
            s.a = uint8_t(s.a + s.d);
            break;
        }
        case Lissajous:
        case LissajousX:
        case LissajousY: {
            const uint8_t x = s.type == LissajousY ? s.x : uint8_t(s.x + ((sine[s.a] * s.heat) >> 8)) & um_;
            const uint8_t y = s.type == LissajousX ? s.y : uint8_t(s.y + ((sine[s.b] * s.heat) >> 8)) & vm_;
            put(x, y, sin32(uint8_t((s.type == LissajousY ? s.b : s.a) + 64)));
            if (s.type != LissajousY)
                s.a = uint8_t(s.a + s.c);
            if (s.type != LissajousX)
                s.b = uint8_t(s.b + s.d);
            break;
        }
        case Blaze:
        case OzHasSpoken:
        case Cone:
        case BlazeRight:
        case BlazeLeft:
        case Emit:
        case Fountain:
        case Organic: {
            if (!room)
                break;
            const bool often = s.type == Blaze || s.type == OzHasSpoken || s.type == Organic;
            if (r() >= (often ? 0x80 : 0x40))
                break;
            SparkData& c = *child();
            if (s.type == Organic) {
                c.type = OrganicSpark;
                c.x = uint8_t((s.x + ((r() * s.c) >> 8)) & um_);
                c.y = uint8_t((s.y + ((r() * s.c) >> 8)) & vm_);
                c.a = uint8_t(fractalRandom().next() - 0x7f), c.b = 0x81, c.c = 0xff;
                break;
            }
            c.heat = s.heat, c.x = s.x, c.y = s.y;
            switch (s.type) {
            case Blaze:
                c.type = BlazeSpark, c.a = r(), c.b = r(), c.c = s.c, c.d = s.d;
                break;
            case OzHasSpoken:
                c.type = EmitSpark, c.a = uint8_t((fractalRandom().next() & 0x7f) - 0x3f), c.b = 0x81, c.d = 2;
                break;
            case Cone:
                c.type = ConeSpark, c.a = uint8_t((fractalRandom().next() & 0x7f) - 0x3f), c.b = 0, c.c = 0x32;
                break;
            case BlazeRight:
            case BlazeLeft:
                c.type = ConeSpark;
                c.a = uint8_t((fractalRandom().next() & 0x3f) + (s.type == BlazeRight ? 0x3f : 0x80));
                c.b = 0xe3, c.c = s.c;
                break;
            default: // Emit, Fountain
                c.type = s.type == Emit ? EmitSpark : FountainSpark, c.a = s.a, c.b = s.b, c.d = s.d;
                break;
            }
            break;
        }
        case WanderOrganic:
        case RandomCloud:
            if (SparkData* c = child()) {
                c->type = s.type == WanderOrganic ? OrganicSpark : CloudSpark;
                c->x = near31(s.x, um_), c->y = near31(s.y, vm_);
                if (s.type == WanderOrganic)
                    c->a = uint8_t(fractalRandom().next() - 0x7f), c->b = 0x81, c->c = 0xff;
                else
                    c->a = uint8_t((fractalRandom().next() & 31) - 15), c->b = 0x81, c->c = 0;
            }
            wander15(s);
            break;
        case Eels:
        case Gametes:
            if (r() < 0x14 && room) {
                SparkData& c = *child();
                c.type = s.type == Eels ? EelSpark : GameteSpark, c.heat = s.heat;
                c.x = near31(s.x, um_), c.y = near31(s.y, vm_);
                c.a = r();
                if (s.type == Eels)
                    c.b = r(), c.c = s.c;
                else
                    c.c = s.c, c.d = r();
            }
            wander15(s);
            break;
        case CustomCloud:
        case LocalCloud:
            if (SparkData* c = child()) {
                c->type = CustomSpark;
                if (s.type == CustomCloud) {
                    c->x = near31(s.x, um_), c->y = near31(s.y, vm_);
                } else {
                    c->x = uint8_t((s.x + ((r() * s.c) >> 8)) & um_);
                    c->y = uint8_t((s.y + ((r() * s.c) >> 8)) & vm_);
                }
                c->a = s.a, c->b = s.b, c->c = s.d;
            }
            if (s.type == CustomCloud)
                wander7(s);
            break;
        case Flocks:
            if (SparkData* c = child()) {
                c->type = FlockSpark, c->heat = s.heat;
                c->x = near31(s.x, um_), c->y = near31(s.y, vm_);
                c->a = 0, c->b = s.a, c->c = s.b, c->d = s.d;
                s.a = uint8_t(s.a + s.c);
            }
            wander7(s);
            break;
        case Wheel:
            if (SparkData* c = child())
                *c = {FlockSpark, s.heat, s.x, s.y, 0, s.a, s.b, s.d};
            s.a = uint8_t(s.a + s.c);
            break;
        case Sprinkler:
            if (SparkData* c = child())
                *c = {SprinklerSpark, s.heat, s.x, s.y, s.a, s.b, s.c, 2};
            s.a = uint8_t(s.a + s.d);
            break;
        case Stars: put(s.x, s.y, s.b); break;
        case LineLightning:
        case RampLightning:
            if (!s.heat)
                break;
            if (s.c > 0) {
                --s.c;
                flash(s.x, s.y, s.a, s.b, s.heat, s.type == LineLightning ? s.heat : s.heat >> 3);
            } else if (r() >= s.d) {
                s.c = uint8_t((r() + 1) & 5);
            }
            break;
        case SphereLightning:
            if (r() >= s.d) {
                const uint8_t angle = r();
                const int half = s.c >> 1;
                const int dx = ((sine[angle] * s.c) >> 8) - half, dy = ((sine[uint8_t(angle + 64)] * s.c) >> 8) - half;
                auto length = [](int v) { return (v > 0 ? (v & ~1) : ((-v) | 1)) & 255; };
                flash(s.x, s.y, length(dx), length(dy), s.heat, s.heat >> 2);
            }
            break;
        // The moving sparks.
        case BlazeSpark:
            s.heat = uint8_t(s.heat - 5);
            if (s.heat >= 0xfb)
                return true;
            put(s.x, s.y, s.heat);
            move(s, s.a, s.b);
            break;
        case EmitSpark:
        case FountainSpark:
            s.heat = uint8_t(s.heat - s.d);
            if (s.heat <= (s.type == EmitSpark ? s.d : 0x32))
                return true;
            put(s.x, s.y, s.heat);
            move(s, s.a, s.b);
            if (s.type == FountainSpark && (phase_ & 1) && s8(s.b) < 0x7c)
                s.b = uint8_t(s.b + 3); // falling back
            break;
        case CoolingSpark:
            s.heat = uint8_t(s.heat - s.c);
            if (s.heat >= 0xfa)
                return true;
            put(s.x, s.y, s.heat);
            move(s, s.a, s.b);
            break;
        case ConeSpark:
            if (--s.c == 0)
                return true;
            put(s.x, s.y, s.heat);
            move(s, s.a, s.b);
            if (s8(s.b) < 0x7a)
                s.b = uint8_t(s.b + 3);
            break;
        case OrganicSpark:
        case CloudSpark:
            s.c = uint8_t(s.c + (s.type == OrganicSpark ? 0xfd : 4));
            if (s.type == OrganicSpark ? s.c <= 0xbe : s.c >= 0xfa)
                return true;
            put(s.x, s.y, s.c);
            step(s.x, s.a, um_);
            s.y = uint8_t((s.y - 2) & vm_); // rising
            break;
        case CustomSpark:
            s.c = uint8_t(s.c + 4);
            if (s.c >= 0xfa)
                return true;
            put(s.x, s.y, s.c);
            move(s, s.a, s.b);
            break;
        case EelSpark:
        case FlockSpark:
        case SprinklerSpark:
        case GameteSpark: {
            if (--s.c == 0xff)
                return true;
            put(s.x, s.y, s.heat);
            if (s.type == EelSpark) {
                move(s, s.a, s.b);
            } else if (s.type == GameteSpark) {
                s.a = uint8_t(s.a + 7);
                int t = s.a & 0x7f;
                if (t > 0x3f)
                    t = 0x7f - t;
                const uint8_t angle = uint8_t(s.d + t);
                move(s, uint8_t(sine[angle] - 127), uint8_t(sine[uint8_t(angle + 64)] - 127));
            } else if (s.type == FlockSpark) {
                const uint8_t dy = uint8_t(sine[s.b] + 128), dx = uint8_t(sine[uint8_t(s.b + 64)] + 128);
                const uint16_t w = uint16_t(((s.b << 8) | s.a) + (s.d << 4));
                s.a = uint8_t(w), s.b = uint8_t(w >> 8);
                move(s, dx, dy);
            } else {
                const uint8_t dx = uint8_t(sine[uint8_t(s.a + 64)] + 0x80);
                s.a = uint8_t(s.a + s.d);
                move(s, dx, s.b);
            }
            break;
        }
        default: break;
        }
        return false;
    }

    // The heat spreads and cools, as Fire.dll does it: each pixel becomes
    // a quarter of four pixels' sum, plus 1, less (255 - RenderHeat) / 16
    // (cool_, a table of the 1024 sums in the original). Rising, the four
    // are below it: the one under and its right, the one two under and its
    // left; else itself, its right, and the two under it and to their
    // left. Counted through the picture as one row after another, so the
    // rows past the bottom are the top ones and a row's left neighbour is
    // the end of the row above.
    void spread() {
        const size_t n = heat_.size(), w = size_t(w_);
        for (size_t i = 0; i < n; ++i) {
            int sum;
            if (rising_)
                sum = heat_[(i + w) % n] + heat_[(i + w + 1) % n] + heat_[(i + 2 * w - 1) % n] + heat_[(i + 2 * w) % n];
            else
                sum = heat_[i] + heat_[(i + 1) % n] + heat_[(i + w - 1) % n] + heat_[(i + w) % n];
            next_[i] = cool_[size_t(sum)];
        }
        heat_.swap(next_);
    }

    int w_, h_;
    int ubits_ = 0;
    uint8_t um_ = 0, vm_ = 0;
    Palette palette_;
    std::vector<SparkData> sparks_; // the placed ones first, then the moving ones they made
    size_t count_ = 0, limit_ = 0;
    std::vector<uint8_t> heat_, next_;
    bool rising_ = false, masked_ = false, starsOn_ = true;
    int keep_ = 220;
    std::array<uint8_t, 1024> cool_{}; // the heat four pixels sum to, spread and cooled
    uint32_t phase_ = 0; // GlobalPhase
    Pace pace_;
};

// ---------------------------------------------------------------------
// Water: wave and wet textures (WaterTexture itself is abstract), as
// Fire.dll does them, worked out by running its routines on test fields
// and reading its tables' set-up code.
//
// The height field is a byte a cell (128 flat), half the texture wide and
// as tall, its rows staggered: row r's cell c sits over pixel column
// 2c + 1 + (r & 1). Each step updates every other row, alternately the
// odd and the even ones: a cell becomes half the sum of its four diagonal
// neighbours (the rows above and below, the two cells either side of it)
// less its own value, the rounding taking it a little back towards flat;
// the edges wrap. The rows not updated are drawn: each pixel from the
// slope across it, the difference between the cells to its right and to
// its left, and on the rows between those, from the two either side added.
// A wave texture looks that slope up in a table lighting a bumpy surface;
// a wet texture shifts its SourceTexture along the row by it.
//
// Drops (the Drops properties, NumDrops of them: type, depth, X, Y and the
// bytes A-D) set cells each step before the ripple, X and Y in cells of
// both rows of a pair (Y counts row pairs), wrapping. For several the byte
// named depth is a phase, and D a height or speed.

enum Drop : uint8_t {
    FixedDepth, PhaseSpot, ShallowSpot, HalfAmpl, RandomMover, FixedRandomSpot, WhirlyThing, BigWhirly,
    HorizontalLine, VerticalLine, DiagonalLine1, DiagonalLine2, HorizontalOsc, VerticalOsc, DiagonalOsc1,
    DiagonalOsc2, RainDrops, AreaClamp, LeakyTap, DrippyTap,
};

class Water : public AnimatedTexture {
public:
    struct DropData {
        uint8_t type = 0, depth = 0, x = 0, y = 0, a = 0, b = 0, c = 0, d = 0;
    };

    // `source`: a wet texture's source picture, already the texture's size;
    // empty for a wave texture (drawn through `palette`).
    Water(int w, int h, const PropertyList& props, Palette palette, std::vector<DropData> drops, image::Image source,
          const char* kind)
        : w_(w), h_(std::max(2, h & ~1)), fw_(std::max(1, w / 2)), palette_(palette), drops_(std::move(drops)),
          source_(std::move(source)), kind_(kind), pace_(props) {
        masked_ = props.boolean("bMasked");
        const int amp = props.byte("WaveAmp", 128);
        if (!source_.rgba.empty()) {
            // SetRefractionTable: a pixel's shift along its row, the slope
            // + 1 times WaveAmp / 512, rounded towards 0.
            for (int i = 0; i < 1024; ++i)
                table_[size_t(i)] = uint8_t(int8_t(std::clamp(int(float(i - 511) * (float(amp) / 512)), -128, 127)));
        } else {
            // SetWaveLight: the slope bends the surface's normal, theta
            // from pi/2 flat towards 0 or pi; lit from the angle
            // BumpMapLight * pi / 255 by (256 - PhongRange / 2) * cos, and
            // where the reflection 2 theta - light - BumpMapAngle * pi / 255
            // is within PhongSize / 512 of straight back, a highlight of up
            // to 2 * PhongRange.
            const float light = float(props.byte("BumpMapLight")) * (kPi / 255);
            const float view = float(props.byte("BumpMapAngle")) * (kPi / 255);
            const int range = props.byte("PhongRange");
            const float size = float(props.byte("PhongSize")) / 512;
            for (int i = 0; i < 1024; ++i) {
                const float s = float(amp) / 255 * float(512 - i) / 196;
                const float theta = 1.57075f * s / (1 + std::abs(s)) + kPi / 2;
                int v = int(float(256 - range / 2) * std::cos(theta - light));
                const float r = 2 * theta - light - view;
                if (r * r < size * size)
                    v += int((size - std::abs(r)) * float(2 * range) / size);
                table_[size_t(i)] = uint8_t(std::clamp(v, 0, 255));
            }
        }
        field_.assign(size_t(fw_) * size_t(h_), 128);
        image_.w = w;
        image_.h = h;
        render();
    }

    const char* kind() const override { return kind_; }

    bool advance(float seconds) override {
        const int n = pace_.due(seconds);
        for (int i = 0; i < n; ++i)
            tick();
        if (n)
            render();
        return n > 0;
    }

private:
    // Cell x of the row pair y: its two rows, wrapping.
    void set(int x, int y, uint8_t first, uint8_t second) {
        x = ((x % fw_) + fw_) % fw_;
        const int pairs = h_ / 2;
        y = ((y % pairs) + pairs) % pairs;
        field_[size_t(2 * y) * size_t(fw_) + size_t(x)] = first;
        field_[size_t(2 * y + 1) * size_t(fw_) + size_t(x)] = second;
    }
    void set(int x, int y, uint8_t v) { set(x, y, v, v); }
    uint8_t randomByte() { return fractalRandom().byte(); }

    void drop(DropData& d) {
        const auto& sine = sineTable();
        const int x = d.x, y = d.y, n = d.d / 2;
        switch (d.type) {
        case FixedDepth: set(x, y, d.d); break;
        case PhaseSpot: set(x, y, sine[d.depth += d.d]); break;
        case ShallowSpot: set(x, y, uint8_t(sine[d.depth += d.d] / 2 + 64)); break;
        case HalfAmpl: set(x, y, std::max<uint8_t>(sine[d.depth += d.d], 128)); break;
        case RandomMover:
            set(x, y, 185, 71);
            d.x = uint8_t(d.x + (randomByte() & 3) - (randomByte() & 3));
            d.y = uint8_t(d.y + (randomByte() & 3) - (randomByte() & 3));
            break;
        case FixedRandomSpot: {
            const uint8_t first = randomByte();
            set(x, y, first, randomByte());
            break;
        }
        case WhirlyThing:
        case BigWhirly: {
            // A 16-bit phase in B:A, moving by D:C a step; round a circle
            // of 8 cells (16 for BigWhirly) from (X, Y) + that.
            const uint16_t phase = uint16_t(((d.b << 8) | d.a) + ((d.d << 8) | d.c));
            d.a = uint8_t(phase), d.b = uint8_t(phase >> 8);
            const int shift = d.type == BigWhirly ? 3 : 4;
            set(x + (sine[d.b] >> shift), y + (sine[uint8_t(d.b + 64)] >> shift), d.depth);
            break;
        }
        case HorizontalLine:
        case VerticalLine:
        case DiagonalLine1:
        case DiagonalLine2:
        case HorizontalOsc:
        case VerticalOsc:
        case DiagonalOsc1:
        case DiagonalOsc2: {
            // D / 2 + 1 cells from (X, Y): the lines at the depth, the
            // oscillators rising and falling, their phase the depth byte
            // moving by C a step.
            const bool osc = d.type >= HorizontalOsc;
            const int kind = d.type - (osc ? HorizontalOsc : HorizontalLine);
            const uint8_t v = osc ? sine[d.depth += d.c] : d.depth;
            for (int k = 0; k <= n; ++k)
                set(x + (kind == 1 ? 0 : kind == 2 ? -k : k), y + (kind == 0 ? 0 : k), v);
            break;
        }
        case RainDrops:
            // One step in 16, a cell at random in the D x D square from (X, Y).
            if ((randomByte() & 15) == 0) {
                const int dy = randomByte() * d.d >> 8;
                const int dx = randomByte() * d.d >> 8;
                set(x + dx, y + dy, d.depth, uint8_t(~d.depth));
            }
            break;
        case AreaClamp:
            // The D / 2 square from (X, Y) held at the depth.
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    set(x + i, y + j, d.depth);
            break;
        case LeakyTap:
        case DrippyTap:
            // A counts up by D; each time it passes 255, a drip (and a
            // DrippyTap's next is a random while off).
            d.a = uint8_t(d.a + d.d);
            if (d.a <= d.d) {
                if (d.type == DrippyTap)
                    d.a = randomByte();
                set(x, y, d.depth, uint8_t(~d.depth));
            }
            break;
        default: break;
        }
    }

    uint8_t& at(int r, int c) {
        r = ((r % h_) + h_) % h_;
        c = ((c % fw_) + fw_) % fw_;
        return field_[size_t(r) * size_t(fw_) + size_t(c)];
    }

    void tick() {
        for (DropData& d : drops_)
            drop(d);
        ++parity_;
        // The rows of this step's parity: half their diagonal neighbours'
        // sum less their own value, rounded down, and up by one below flat.
        // (Rows and columns wrap; the rows above and below aren't this
        // step's, so they're read as they were.)
        const size_t fw = size_t(fw_);
        for (int r = parity_ & 1; r < h_; r += 2) {
            const int left = r & 1 ? 0 : -1; // the neighbours' first cell, from c
            uint8_t* row = &field_[size_t(r) * fw];
            const uint8_t* up = &field_[size_t((r + h_ - 1) % h_) * fw];
            const uint8_t* down = &field_[size_t((r + 1) % h_) * fw];
            for (int c = 0; c < fw_; ++c) {
                int a = c + left, b = a + 1;
                if (a < 0)
                    a += fw_;
                if (b >= fw_)
                    b -= fw_;
                const int sum = up[a] + up[b] + down[a] + down[b];
                const int i = sum - 2 * row[c] + 512; // 2-1532
                row[c] = uint8_t(std::clamp(i / 2 + (i < 768 ? 1 : 0) - 256, 0, 255));
            }
        }
    }

    // The slope at each pixel of pixel row y: field row y + 1 if it's one
    // drawn this step, else the two either side of it added; per cell, the
    // difference across it (even); between cells, the two either side's mean.
    void slopes(int y, std::vector<int>& out) {
        const int r = y + 1;
        const bool drawn = (r & 1) != (parity_ & 1);
        const int first = drawn ? r : r - 1; // the row whose cells' columns these are
        auto sum = [&](int c) { return drawn ? 2 * at(r, c) : at(r - 1, c) + at(r + 1, c); };
        // Each cell's difference across once (they wrap), then per pixel.
        sums_.resize(size_t(fw_));
        for (int c = 0; c < fw_; ++c)
            sums_[size_t(c)] = sum(c);
        across_.resize(size_t(fw_) + 1);
        for (int c = 0; c < fw_; ++c)
            across_[size_t(c)] = 2 * ((sums_[size_t((c + 1) % fw_)] - sums_[size_t((c + fw_ - 1) % fw_)]) >> 1);
        across_[size_t(fw_)] = across_[0];
        const int offset = 1 + (((first % h_) + h_) % h_ & 1);
        out.resize(size_t(w_));
        for (int x = 0; x < w_; ++x) {
            const int k = (((x - offset) % w_) + w_) % w_;
            const int cell = (k / 2) % fw_;
            out[size_t(x)] = k % 2 == 0 ? across_[size_t(cell)]
                                        : (across_[size_t(cell)] + across_[size_t(cell) + 1]) / 2;
        }
    }

    void render() {
        image_.rgba.resize(size_t(w_) * size_t(image_.h) * 4);
        std::vector<int> slope;
        for (int y = 0; y < image_.h; ++y) {
            slopes(y % h_, slope);
            uint8_t* p = image_.pixel(0, y);
            for (int x = 0; x < w_; ++x, p += 4) {
                const uint8_t v = table_[size_t(std::clamp(512 + slope[size_t(x)], 0, 1023))];
                if (!source_.rgba.empty()) {
                    const int ux = (((x + int8_t(v)) % w_) + w_) % w_;
                    std::copy_n(source_.pixel(ux, y), 4, p);
                } else {
                    std::copy_n(palette_.rgba.data() + size_t(v) * 4, 3, p);
                    p[3] = masked_ && v == 0 ? 0 : 255;
                }
            }
        }
    }

    int w_, h_, fw_;
    Palette palette_;
    std::vector<DropData> drops_;
    image::Image source_;
    const char* kind_;
    std::vector<uint8_t> field_;
    std::vector<int> sums_, across_; // slopes' working rows
    std::array<uint8_t, 1024> table_{}; // the slope + 512: a wave's palette index, a wet's shift (signed)
    uint8_t parity_ = 0;
    bool masked_ = false;
    Pace pace_;
};

// ---------------------------------------------------------------------
// Ice, as Fire.dll's UIceTexture does it (MoveIcePosition, BlitTexIce,
// BlitIceTex): each pixel shows the SourceTexture's row, taken from as far
// along it as the GlassTexture's palette index there (plus its own
// column, wrapping), the glass or (MoveIce) the source shifted by the
// position. The position: a clock moving 120 a second; a pan of
// 2 x (HorizPanSpeed - 128) pixels a second leftwards and 2 x
// (VertPanSpeed - 128) downwards; then by PanningStyle, in whole pixels
// with a = (Amplitude + 1) and f = (Frequency + 1) x clock x 0.0012
// radians: Linear the pan; Circular a sin f, a cos f; Gestation a sin f,
// a cos (f x 11/12); WavyX a sin f / 2 across only; WavyY a cos f / 2 down
// only. TimeMethod frame-rate synced steps 1/120 s each time the texture
// is due (its pace); real time moves it by each frame's time. It's drawn
// again only when the whole-pixel position changes.

// A referenced texture, decoded and scaled to w x h.
bool scaledPicture(const PackageSet& packages, const Package& package, int32_t ref, int w, int h, image::Image& out) {
    const ObjectRef obj = packages.resolve(package, ref);
    image::Image img;
    if (!obj || decodeTexture(packages, *obj.package, obj.exportIndex, img) || img.w <= 0 || img.h <= 0)
        return false;
    out.w = w;
    out.h = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::copy_n(img.pixel(x * img.w / w, y * img.h / h), 4, out.pixel(x, y));
    return true;
}

// A referenced texture's palette indices (its first mipmap), scaled to w x h.
bool scaledIndices(const PackageSet& packages, const Package& package, int32_t ref, int w, int h,
                   std::vector<uint8_t>& out) {
    const ObjectRef obj = packages.resolve(package, ref);
    TextureData t;
    if (!obj || readTextureData(*obj.package, obj.exportIndex, t) ||
        t.mip0Size < size_t(t.width) * size_t(t.height) || t.mip0 + t.mip0Size > t.data.size())
        return false;
    out.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out[size_t(y) * size_t(w) + size_t(x)] =
                t.data[t.mip0 + size_t(y * t.height / h) * size_t(t.width) + size_t(x * t.width / w)];
    return true;
}

class Ice : public AnimatedTexture {
public:
    enum Panning : uint8_t { Linear, Circular, Gestation, WavyX, WavyY };

    Ice(const PropertyList& props, image::Image source, std::vector<uint8_t> glass)
        : source_(std::move(source)), glass_(std::move(glass)), pace_(props) {
        panning_ = props.byte("PanningStyle");
        realTime_ = props.byte("TimeMethod") != 0;
        hSpeed_ = float(int(props.byte("HorizPanSpeed", 128)) - 128);
        vSpeed_ = float(int(props.byte("VertPanSpeed", 128)) - 128);
        frequency_ = float(props.byte("Frequency")) + 1;
        amplitude_ = float(props.byte("Amplitude")) + 1;
        moveIce_ = props.boolean("MoveIce");
        image_ = source_;
        render();
    }

    const char* kind() const override { return "ice"; }

    bool advance(float seconds) override {
        seconds = std::max(0.0f, seconds);
        const int n = pace_.due(seconds);
        if (realTime_) {
            if (n)
                move(seconds + held_), held_ = 0;
            else
                held_ += seconds;
        } else {
            for (int k = 0; k < n; ++k)
                move(1.0f / 120);
        }
        if (n && (int(std::lround(u_)) != drawnU_ || int(std::lround(v_)) != drawnV_)) {
            render();
            return true;
        }
        return false;
    }

private:
    void move(float dt) {
        clock_ += dt * 120;
        uPan_ -= 2 * dt * hSpeed_;
        vPan_ += 2 * dt * vSpeed_;
        const float f = frequency_ * clock_ * 0.0012f;
        auto whole = [](float x) { return float(int(std::lround(x))); };
        switch (panning_) {
        case Circular: u_ = uPan_ + whole(amplitude_ * std::sin(f)), v_ = vPan_ + whole(amplitude_ * std::cos(f)); break;
        case Gestation:
            u_ = uPan_ + whole(amplitude_ * std::sin(f));
            v_ = vPan_ + whole(amplitude_ * std::cos(frequency_ * clock_ * 0.0011f));
            break;
        case WavyX: u_ = uPan_ + whole(amplitude_ * std::sin(f) * 0.5f), v_ = vPan_; break;
        case WavyY: u_ = uPan_, v_ = vPan_ + whole(amplitude_ * std::cos(f) * 0.5f); break;
        default: u_ = uPan_, v_ = vPan_; break;
        }
    }

    static int wrap(int v, int n) { return ((v % n) + n) % n; }

    void render() {
        const int w = source_.w, h = source_.h;
        const int u = int(std::lround(u_)), v = int(std::lround(v_));
        drawnU_ = u;
        drawnV_ = v;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int sx, sy;
                if (moveIce_) { // the source shifted
                    sx = int(glass_[size_t(y) * size_t(w) + size_t(x)]) + x + u;
                    sy = y + v;
                } else { // the glass shifted
                    sx = int(glass_[size_t(wrap(y + v, h)) * size_t(w) + size_t(wrap(x + u, w))]) + x;
                    sy = y;
                }
                std::copy_n(source_.pixel(wrap(sx, w), wrap(sy, h)), 4, image_.pixel(x, y));
            }
    }

    image::Image source_;
    std::vector<uint8_t> glass_;
    uint8_t panning_ = Linear;
    bool realTime_ = false, moveIce_ = false;
    float hSpeed_ = 0, vSpeed_ = 0, frequency_ = 1, amplitude_ = 1;
    float clock_ = 0, uPan_ = 0, vPan_ = 0, u_ = 0, v_ = 0, held_ = 0;
    int drawnU_ = 0, drawnV_ = 0;
    Pace pace_;
};

// ---------------------------------------------------------------------
// FLC: The Fallen's FliTexture keeps an FLC animation (Autodesk
// Animator's format, without its file header) after its mipmaps:
// float length, float rate, u16 frames, width, height, an unknown u16,
// u32 bytes of frames, then the frames (each a u32 size, 0xF1FA, a u16
// chunk count, 8 reserved bytes, then the chunks; the size can include 2
// bytes of padding), the last turning the last frame back into the first.
// A frame shows for length / frames seconds, divided by the rate.

class Flc : public AnimatedTexture {
public:
    Flc(int w, int h, int fw, int fh, Palette palette, bool masked, std::vector<uint8_t> frames,
        std::vector<std::pair<size_t, size_t>> spans, bool ring, float frameTime)
        : w_(w), h_(h), fw_(fw), fh_(fh), palette_(palette), masked_(masked), data_(std::move(frames)),
          spans_(std::move(spans)), ring_(ring), frameTime_(frameTime), firstPalette_(palette) {
        pixels_.assign(size_t(fw) * size_t(fh), 0);
        image_.w = w;
        image_.h = h;
        decode(0);
        render();
    }

    const char* kind() const override { return "FLC"; }
    int frames() const override { return int(spans_.size()) - (ring_ ? 1 : 0); }
    void setBaseFrameSeconds(float s) { baseFrameSeconds_ = s; }
    float frameSeconds() const override { return baseFrameSeconds_; }

    // Frame n (from 1): decoded on from the frame showing, or from the
    // start if it's behind (each frame only changes the one before).
    bool showFrame(int n, bool seeThrough) override {
        const size_t index = size_t(std::clamp(n, 1, std::max(1, frames())) - 1);
        if (index == current_ && seeThrough == seeThrough_ && shown_)
            return false;
        if (index < current_ || !shown_) {
            std::fill(pixels_.begin(), pixels_.end(), 0);
            palette_ = firstPalette_;
            current_ = 0;
            decode(0);
        }
        while (current_ < index) {
            ++current_;
            decode(current_);
        }
        seeThrough_ = seeThrough;
        shown_ = true;
        render();
        return true;
    }

    bool advance(float seconds) override {
        if (spans_.size() < 2)
            return false;
        time_ += std::max(0.0f, seconds);
        bool changed = false;
        for (int n = 0; time_ >= frameTime_ && n < 8; ++n) {
            time_ -= frameTime_;
            current_ = current_ + 1 < spans_.size() ? current_ + 1 : ring_ ? 1 : 0;
            if (ring_ && current_ == 1 && spans_.size() == 2)
                current_ = 0;
            decode(current_);
            changed = true;
        }
        if (time_ >= frameTime_)
            time_ = 0;
        if (changed)
            render();
        return changed;
    }

private:
    void render() {
        image_.rgba.resize(size_t(w_) * size_t(h_) * 4);
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const uint8_t c = pixels_[size_t(y * fh_ / h_) * size_t(fw_) + size_t(x * fw_ / w_)];
                uint8_t* p = image_.pixel(x, y);
                std::copy_n(palette_.rgba.data() + size_t(c) * 4, 3, p);
                p[3] = (masked_ || seeThrough_) && c == 0 ? 0 : 255;
            }
    }

    // Applies frame `index`'s chunks to the picture and palette.
    void decode(size_t index) {
        const auto [start, size] = spans_[index];
        Reader frame(data_.data() + start, size);
        frame.skip(4);
        frame.u16(); // 0xF1FA
        const int chunks = frame.u16();
        frame.skip(8);
        for (int k = 0; k < chunks && frame.ok() && frame.remaining() >= 6; ++k) {
            const size_t at = size - frame.remaining();
            const uint32_t chunkSize = frame.u32();
            const uint16_t type = frame.u16();
            if (chunkSize < 6 || chunkSize > size - at)
                break;
            Reader c(data_.data() + start + at + 6, chunkSize - 6);
            chunk(type, c);
            frame.skip(chunkSize - 6);
        }
    }

    void chunk(uint16_t type, Reader& r) {
        switch (type) {
        case 4:  // colours, 8 bits each
        case 11: // colours, 6 bits each
        {
            const int packets = r.u16();
            int index = 0;
            for (int p = 0; p < packets && r.ok(); ++p) {
                index += r.u8();
                int count = r.u8();
                if (count == 0)
                    count = 256;
                for (int i = 0; i < count && r.ok(); ++i, ++index)
                    for (int k = 0; k < 3; ++k) {
                        const uint8_t v = r.u8();
                        if (index < 256)
                            palette_.rgba[size_t(index) * 4 + size_t(k)] = type == 11 ? uint8_t(v * 255 / 63) : v;
                    }
            }
            break;
        }
        case 7: { // changed lines, in 16-bit words
            int lines = r.u16();
            int y = 0;
            while (lines > 0 && y < fh_ && r.ok()) {
                const int16_t word = r.i16();
                if ((word & 0xc000) == 0xc000) { // lines to skip
                    y -= word;
                    continue;
                }
                if ((word & 0xc000) == 0x8000) { // the line's last pixel
                    put(fw_ - 1, y, uint8_t(word & 0xff));
                    continue;
                }
                int x = 0;
                for (int p = 0; p < word && r.ok(); ++p) {
                    x += r.u8();
                    const int8_t count = int8_t(r.u8());
                    if (count > 0) {
                        for (int i = 0; i < count * 2; ++i)
                            put(x++, y, r.u8());
                    } else {
                        const uint8_t a = r.u8(), b = r.u8();
                        for (int i = 0; i < -count; ++i) {
                            put(x++, y, a);
                            put(x++, y, b);
                        }
                    }
                }
                ++y;
                --lines;
            }
            break;
        }
        case 12: { // changed lines, in bytes
            int y = r.u16();
            const int lines = r.u16();
            for (int l = 0; l < lines && r.ok(); ++l, ++y) {
                const int packets = r.u8();
                int x = 0;
                for (int p = 0; p < packets && r.ok(); ++p) {
                    x += r.u8();
                    const int8_t count = int8_t(r.u8());
                    if (count > 0) {
                        for (int i = 0; i < count; ++i)
                            put(x++, y, r.u8());
                    } else {
                        const uint8_t v = r.u8();
                        for (int i = 0; i < -count; ++i)
                            put(x++, y, v);
                    }
                }
            }
            break;
        }
        case 13: // all colour 0
            std::fill(pixels_.begin(), pixels_.end(), 0);
            break;
        case 15: // the whole picture, run-length coded
            for (int y = 0; y < fh_ && r.ok(); ++y) {
                r.u8(); // a packet count, unused
                int x = 0;
                while (x < fw_ && r.ok()) {
                    const int8_t count = int8_t(r.u8());
                    if (count > 0) {
                        const uint8_t v = r.u8();
                        for (int i = 0; i < count; ++i)
                            put(x++, y, v);
                    } else {
                        for (int i = 0; i < -count; ++i)
                            put(x++, y, r.u8());
                    }
                }
            }
            break;
        case 16: // the whole picture, as is
            for (size_t i = 0; i < pixels_.size() && r.ok(); ++i)
                pixels_[i] = r.u8();
            break;
        default: // 18: a thumbnail
            break;
        }
    }

    void put(int x, int y, uint8_t v) {
        if (x >= 0 && x < fw_ && y >= 0 && y < fh_)
            pixels_[size_t(y) * size_t(fw_) + size_t(x)] = v;
    }

    int w_, h_, fw_, fh_;
    Palette palette_;
    bool masked_;
    std::vector<uint8_t> data_;
    std::vector<std::pair<size_t, size_t>> spans_; // each frame's start and size in data_
    bool ring_;
    float frameTime_;
    std::vector<uint8_t> pixels_;
    size_t current_ = 0;
    float time_ = 0;
    Palette firstPalette_;       // the palette before any frame's
    bool seeThrough_ = false, shown_ = false;
    float baseFrameSeconds_ = 1.0f / 30;
};

std::unique_ptr<AnimatedTexture> openFlc(const TextureData& t, Palette palette, const std::string& what,
                                         std::string& error) {
    Reader r(t.data, t.base);
    r.seek(t.native);
    const float length = r.f32(), rate = r.f32();
    const int frames = r.u16(), fw = r.u16(), fh = r.u16();
    r.u16();
    const uint32_t size = r.u32();
    if (!r.ok() || fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096 || size > r.remaining()) {
        error = what + ": no FLC animation after its mipmaps";
        return nullptr;
    }
    const size_t start = r.pos() - t.base;
    std::vector<uint8_t> data(t.data.begin() + ptrdiff_t(start), t.data.begin() + ptrdiff_t(start + size));
    std::vector<std::pair<size_t, size_t>> spans;
    size_t at = 0;
    while (at + 16 <= data.size()) {
        Reader f(data.data() + at, data.size() - at);
        const uint32_t frameSize = f.u32();
        if (f.u16() != 0xf1fa || frameSize < 16 || frameSize > data.size() - at)
            break;
        spans.push_back({at, frameSize});
        at += frameSize;
    }
    if (spans.empty()) {
        error = what + ": its FLC animation has no frames";
        return nullptr;
    }
    const bool ring = frames > 0 && spans.size() == size_t(frames) + 1;
    float frameTime = (length > 0 && frames > 0 ? length / float(frames) : 1.0f / 15) / (rate > 0 ? rate : 1);
    frameTime = std::clamp(frameTime, 1.0f / 60, 10.0f);
    auto out = std::make_unique<Flc>(t.width, t.height, fw, fh, palette, t.props.boolean("bMasked"), std::move(data),
                                     std::move(spans), ring, frameTime);
    out->setBaseFrameSeconds(length > 0 && frames > 0 ? length / float(frames) : 1.0f / 30);
    return out;
}

} // namespace

bool isAnimatedTextureClass(std::string_view className) {
    const std::string c = fs::toLower(className);
    return c == "firetexture" || c == "watertexture" || c == "wavetexture" || c == "wettexture" || c == "flitexture" ||
           c == "icetexture";
}

std::unique_ptr<AnimatedTexture> openAnimatedTexture(const PackageSet& packages, const Package& package,
                                                     int exportIndex, std::string* error) {
    std::string err;
    auto fail = [&](const std::string& why) -> std::unique_ptr<AnimatedTexture> {
        if (error)
            *error = why;
        return nullptr;
    };
    const std::string cls = fs::toLower(package.className(exportIndex + 1));
    const std::string what = package.path(exportIndex + 1);
    if (!isAnimatedTextureClass(cls))
        return fail(what + ": a " + cls + ", not animated");
    TextureData t;
    if (Error e = readTextureData(package, exportIndex, t))
        return fail(e.message);
    const Palette palette = paletteOf(packages, package, t.props);

    if (cls == "flitexture") {
        auto out = openFlc(t, palette, what, err);
        return out ? std::move(out) : fail(err);
    }
    if (cls == "icetexture") {
        image::Image source;
        std::vector<uint8_t> glass;
        if (!scaledPicture(packages, package, t.props.object("SourceTexture"), t.width, t.height, source))
            return fail(what + ": its SourceTexture doesn't read");
        if (!scaledIndices(packages, package, t.props.object("GlassTexture"), t.width, t.height, glass))
            return fail(what + ": its GlassTexture doesn't read");
        return std::make_unique<Ice>(t.props, std::move(source), std::move(glass));
    }
    if (cls == "firetexture") {
        Reader r(t.data, t.base);
        r.seek(t.native);
        const int32_t count = r.index();
        if (!r.ok() || count < 0 || size_t(count) * 8 > r.remaining())
            return fail(what + ": " + std::to_string(count) + " sparks don't fit");
        std::vector<Fire::SparkData> sparks(static_cast<size_t>(count));
        for (Fire::SparkData& s : sparks) {
            s.type = r.u8(), s.heat = r.u8(), s.x = r.u8(), s.y = r.u8();
            s.a = r.u8(), s.b = r.u8(), s.c = r.u8(), s.d = r.u8();
        }
        return std::make_unique<Fire>(t.width, t.height, t.props, palette, std::move(sparks));
    }
    // Water, wave and wet textures: their drops are properties.
    std::vector<Water::DropData> drops;
    const int32_t numDrops = std::clamp<int32_t>(t.props.integer("NumDrops"), 0, 256);
    for (int32_t i = 0; i < numDrops; ++i) {
        const Property* p = t.props.find("Drops", i);
        if (!p || p->data.size() < 8)
            continue;
        Water::DropData d;
        d.type = p->data[0], d.depth = p->data[1], d.x = p->data[2], d.y = p->data[3];
        d.a = p->data[4], d.b = p->data[5], d.c = p->data[6], d.d = p->data[7];
        drops.push_back(d);
    }
    image::Image source;
    if (cls == "wettexture") {
        if (!scaledPicture(packages, package, t.props.object("SourceTexture"), t.width, t.height, source))
            return fail(what + ": its SourceTexture doesn't read");
    }
    return std::make_unique<Water>(t.width, t.height, t.props, palette, std::move(drops), std::move(source),
                                   cls == "wettexture" ? "wet" : cls == "wavetexture" ? "wave" : "water");
}

} // namespace reluva::unreal
