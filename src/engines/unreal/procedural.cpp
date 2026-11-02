// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/procedural.h"

#include "engines/unreal/properties.h"
#include "engines/unreal/texture.h"

#include "common/fs.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::unreal {

namespace {

constexpr float kPi = 3.14159265358979f;

// A small repeatable random number generator (xorshift).
struct Random {
    uint32_t state;
    explicit Random(uint32_t seed) : state(seed ? seed : 0x12345678u) {}
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int below(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
    int between(int lo, int hi) { return lo + below(hi - lo + 1); } // inclusive
    float unit() { return float(next() & 0xffffff) / float(0x1000000); }
};

// A texture's properties and where its class's own data starts, after its
// mipmaps (one list, or two with bHasComp; see texture.md).
struct TextureData {
    std::vector<uint8_t> data;
    size_t base = 0;     // the export's offset in the file, as Reader positions count
    size_t native = 0;   // where the class's own data starts (a Reader position)
    PropertyList props;
    int width = 0, height = 0;
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

// Where something is in a cycle of `speed` 256ths of a turn a step,
// starting `phase` 256ths in: radians.
float turn(uint32_t step, float speed, uint8_t phase) {
    return 2 * kPi * (float(phase) / 256 + float(step) * speed / 256);
}

// ---------------------------------------------------------------------
// Fire: a heat value per pixel, shown through the palette. Each step the
// heat spreads (upwards when bRising) and cools by RenderHeat / 256, then
// the sparks add theirs. Each spark is 8 bytes after the mipmaps (a
// compact count first): type, heat, X, Y and four bytes whose use depends
// on the type (ESparkType in Fire.u); what each type does is Reluva's
// reading of its name and its bytes in The Fallen's textures.

enum Spark : uint8_t {
    Burn, Sparkle, Pulse, Signal, Blaze, OzHasSpoken, Cone, BlazeRight, BlazeLeft, Cylinder, Cylinder3D,
    Lissajous, Jugglers, Emit, Fountain, Flocks, Eels, Organic, WanderOrganic, RandomCloud, CustomCloud,
    LocalCloud, Stars, LineLightning, RampLightning, SphereLightning, Wheel, Gametes, Sprinkler,
};

class Fire : public AnimatedTexture {
public:
    struct SparkData {
        uint8_t type = 0, heat = 0, x = 0, y = 0, a = 0, b = 0, c = 0, d = 0;
        float px = 0, py = 0; // where a wandering spark has got to
    };

    Fire(int w, int h, const PropertyList& props, Palette palette, std::vector<SparkData> sparks)
        : w_(w), h_(h), palette_(palette), sparks_(std::move(sparks)), random_(uint32_t(w * 7919 + h)), pace_(props) {
        rising_ = props.boolean("bRising");
        keep_ = props.byte("RenderHeat", 220);
        for (int i = 0; i < 1024; ++i)
            cool_[size_t(i)] = uint8_t(std::clamp(float(i) * 0.25f + 1 - float(255 - keep_) * 0.0625f, 0.0f, 255.0f));
        masked_ = props.boolean("bMasked");
        limit_ = size_t(std::clamp<int32_t>(props.integer("SparksLimit", 1024), 16, 4096));
        // DrawMode (dmode in Fire.u): DRAW_Normal, DRAW_Lathe, then
        // DRAW_Lathe_2 to _4, the sparks repeated 2 to 4 times round.
        const uint8_t mode = props.byte("DrawMode");
        lathe_ = mode == 1 ? 1 : mode >= 2 && mode <= 4 ? mode : 0;
        heat_.assign(size_t(w) * size_t(h), 0);
        next_.assign(heat_.size(), 0);
        for (SparkData& s : sparks_) {
            s.px = s.x;
            s.py = s.y;
        }
        image_.w = w;
        image_.h = h;
        // Under way, as it is by the time anyone sees it.
        for (int i = 0; i < 60; ++i)
            tick();
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
    struct Particle {
        float x, y, vx, vy, gravity;
        float heat, fade; // heat lost a step
    };

    uint8_t& at(int x, int y) {
        x = ((x % w_) + w_) % w_;
        y = ((y % h_) + h_) % h_;
        return heat_[size_t(y) * size_t(w_) + size_t(x)];
    }
    void put(int x, int y, float heat) {
        const auto h = uint8_t(std::clamp(heat, 0.0f, 255.0f));
        uint8_t& v = at(x, y);
        v = std::max(v, h);
        if (!lathe_)
            return;
        // Turned round the middle: as many copies as the lathe has, or
        // with DRAW_Lathe one a pixel all the way round.
        const float dx = float(x) + 0.5f - float(w_) / 2, dy = float(y) + 0.5f - float(h_) / 2;
        const int copies = lathe_ > 1 ? lathe_ : std::clamp(int(std::ceil(2 * kPi * std::hypot(dx, dy))), 1, 1024);
        for (int k = 1; k < copies; ++k) {
            const float a = 2 * kPi * float(k) / float(copies), c = std::cos(a), sn = std::sin(a);
            uint8_t& u = at(int(std::floor(float(w_) / 2 + dx * c - dy * sn)), int(std::floor(float(h_) / 2 + dx * sn + dy * c)));
            u = std::max(u, h);
        }
    }
    void emit(float x, float y, float vx, float vy, float heat, int life, float gravity = 0) {
        if (particles_.size() < limit_)
            particles_.push_back({x, y, vx, vy, gravity, heat, heat / float(std::max(life, 1))});
    }
    // A jagged line from one point to another.
    void lightning(float x0, float y0, float x1, float y1, float heat, bool ramp) {
        const int n = std::max(1, int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))));
        float jx = 0, jy = 0;
        for (int i = 0; i <= n; ++i) {
            const float t = float(i) / float(n);
            jx = std::clamp(jx + float(random_.between(-1, 1)), -4.0f, 4.0f);
            jy = std::clamp(jy + float(random_.between(-1, 1)), -4.0f, 4.0f);
            put(int(x0 + (x1 - x0) * t + jx), int(y0 + (y1 - y0) * t + jy), ramp ? heat * (1 - t) : heat);
        }
    }

    // A step: the sparks drawn, then the heat spread (Fire.dll's order).
    void tick() {
        ++step_;
        for (SparkData& s : sparks_)
            spark(s);
        for (Particle& p : particles_) {
            p.x += p.vx;
            p.y += p.vy;
            p.vy += p.gravity;
            p.heat -= p.fade;
            put(int(std::floor(p.x)), int(std::floor(p.y)), p.heat);
        }
        particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.heat <= 0; }),
                         particles_.end());
        spread();
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

    void spark(SparkData& s) {
        const float heat = s.heat;
        const int x = s.x, y = s.y;
        // The usual speed byte; 0 counts as a moderate 16.
        auto speed = [](uint8_t v) { return float(v ? v : 16); };
        switch (s.type) {
        case Burn:
            put(x, y, float(random_.below(int(heat) + 1)));
            break;
        case Sparkle:
            if (random_.below(256) <= std::max<int>(s.a, 32)) {
                const int r = 1 + s.b / 32;
                put(x + random_.between(-r, r), y + random_.between(-r, r), heat);
            }
            break;
        case Pulse:
            put(x, y, heat * (0.5f + 0.5f * std::sin(turn(step_, speed(s.d), s.a))));
            break;
        case Signal:
            if (std::sin(turn(step_, speed(s.d), s.a)) > 0)
                put(x, y, heat);
            break;
        case Blaze: {
            const float angle = random_.unit() * 2 * kPi, v = 0.4f + random_.unit();
            emit(float(x), float(y), std::cos(angle) * v, std::sin(angle) * v, heat, 8 + random_.below(24));
            break;
        }
        case OzHasSpoken:
            emit(float(x), float(y), (random_.unit() - 0.5f) * 0.6f, -1 - random_.unit(), heat, 16 + random_.below(16));
            break;
        case Cone:
            emit(float(x), float(y), (random_.unit() - 0.5f) * 1.4f, -1 - random_.unit() * 0.5f, heat, 12 + random_.below(16));
            break;
        case BlazeRight:
        case BlazeLeft:
            emit(float(x), float(y), (s.type == BlazeRight ? 1.0f : -1.0f) * (0.6f + random_.unit()),
                 (random_.unit() - 0.5f) * 0.6f, heat, 12 + random_.below(16));
            break;
        case Cylinder:
        case Cylinder3D: {
            const float angle = turn(step_, speed(s.c) / 4, s.a), r = float(std::max<uint8_t>(s.b, 8)) / 8;
            const float shade = s.type == Cylinder3D ? 0.6f + 0.4f * std::cos(angle) : 1.0f;
            put(int(float(x) + r * std::sin(angle)), y, heat * shade);
            break;
        }
        case Lissajous:
            put(int(float(x) + 8 * std::sin(turn(step_, speed(s.c) / 4, s.a))),
                int(float(y) + 8 * std::sin(turn(step_, speed(s.d) / 4, s.b))), heat);
            break;
        case Jugglers: {
            const float r = float(std::max<uint8_t>(s.b, 16)) / 8;
            put(x, int(float(y) - r * std::fabs(std::sin(turn(step_, speed(s.d) * 2, s.a)))), heat);
            break;
        }
        case Emit:
        case Fountain:
            emit(float(x), float(y), float(int8_t(s.a)) / 32, float(int8_t(s.b)) / 32, heat, 24,
                 s.type == Fountain ? 0.08f : 0.0f);
            break;
        case Sprinkler: {
            const float angle = turn(step_, speed(s.c) / 4, s.a);
            emit(float(x), float(y), std::cos(angle), std::sin(angle), heat, 24);
            break;
        }
        case Flocks:
        case Eels:
        case Organic:
        case WanderOrganic:
        case Gametes: {
            // Wandering: free, or kept near where it started.
            s.px += (random_.unit() - 0.5f) * 1.5f;
            s.py += (random_.unit() - 0.5f) * 1.5f;
            const float r = float(4 + s.b / 8);
            if (s.type != WanderOrganic) {
                s.px = std::clamp(s.px, float(x) - r, float(x) + r);
                s.py = std::clamp(s.py, float(y) - r, float(y) + r);
            }
            put(int(s.px), int(s.py), heat);
            if (s.type == Gametes) // in pairs
                put(int(s.px) + 1, int(s.py), heat);
            break;
        }
        case RandomCloud:
        case CustomCloud:
        case LocalCloud: {
            if (random_.below(256) > std::max<int>(s.a, 64))
                break;
            const int r = std::max(1, (s.type == RandomCloud ? s.a : s.c) / 2);
            put(x + random_.between(-r, r), y + random_.between(-r, r), heat);
            break;
        }
        case Stars:
            if (random_.below(8) == 0)
                put(x, y, heat);
            break;
        case LineLightning:
        case RampLightning:
            if (random_.below(4) == 0)
                lightning(float(x), float(y), float(s.a), float(s.b), heat, s.type == RampLightning);
            break;
        case SphereLightning:
            if (random_.below(4) == 0) {
                const float angle = random_.unit() * 2 * kPi, len = float(4 + s.b / 4);
                lightning(float(x), float(y), float(x) + std::cos(angle) * len, float(y) + std::sin(angle) * len, heat,
                          true);
            }
            break;
        case Wheel: {
            const float angle = turn(step_, speed(s.c) / 4, s.a), r = float(s.b) / 4;
            put(int(float(x) + r * std::cos(angle)), int(float(y) + r * std::sin(angle)), heat);
            break;
        }
        default:
            put(x, y, heat);
            break;
        }
    }

    int w_, h_;
    Palette palette_;
    std::vector<SparkData> sparks_;
    std::vector<Particle> particles_;
    std::vector<uint8_t> heat_, next_;
    Random random_;
    bool rising_ = false, masked_ = false;
    int keep_ = 220;
    std::array<uint8_t, 1024> cool_{}; // the heat four pixels sum to, spread and cooled
    size_t limit_ = 1024;
    int lathe_ = 0; // copies round the middle (1: a ring), 0 for none
    uint32_t step_ = 0;
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

// Fire.dll's sine table: a turn in 256 steps, 0-255 (127 or 128 at 0).
const std::array<uint8_t, 256>& waterSine() {
    static const std::array<uint8_t, 256> table = [] {
        std::array<uint8_t, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = uint8_t(std::nearbyint(std::sin(float(i) / 256 * 2 * kPi) * 127.5f + 127.45f));
        return t;
    }();
    return table;
}

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
          source_(std::move(source)), kind_(kind), random_(uint32_t(w * 31 + h)), pace_(props) {
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
    uint8_t randomByte() { return uint8_t(random_.below(256)); }

    void drop(DropData& d) {
        const auto& sine = waterSine();
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
        for (int r = parity_ & 1; r < h_; r += 2) {
            const int left = r & 1 ? 0 : -1; // the neighbours' first cell, from c
            for (int c = 0; c < fw_; ++c) {
                const int sum = at(r - 1, c + left) + at(r - 1, c + left + 1) + at(r + 1, c + left) +
                                at(r + 1, c + left + 1);
                const int i = sum - 2 * at(r, c) + 512; // 2-1532
                at(r, c) = uint8_t(std::clamp(i / 2 + (i < 768 ? 1 : 0) - 256, 0, 255));
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
        auto across = [&](int c) { return 2 * ((sum(c + 1) - sum(c - 1)) >> 1); };
        const int offset = 1 + (((first % h_) + h_) % h_ & 1);
        out.resize(size_t(w_));
        for (int x = 0; x < w_; ++x) {
            const int k = (((x - offset) % w_) + w_) % w_;
            out[size_t(x)] = k % 2 == 0 ? across(k / 2) : (across(k / 2) + across(k / 2 + 1)) / 2;
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
    Random random_;
    std::vector<uint8_t> field_;
    std::array<uint8_t, 1024> table_{}; // the slope + 512: a wave's palette index, a wet's shift (signed)
    uint8_t parity_ = 0;
    bool masked_ = false;
    Pace pace_;
};

// ---------------------------------------------------------------------
// Ice: a SourceTexture seen through a GlassTexture, one sliding over the
// other. Each pixel shows the source shifted by the slope of the glass's
// brightness there (a quarter of the difference either side, up to 16
// pixels), like light bent through uneven glass. The glass slides (or,
// with MoveIce, the source does) as PanningStyle says (IceTexture's
// PanningType in Fire.u): Linear at HorizPanSpeed and VertPanSpeed (128
// still; a step either side a quarter of a pixel a second); Circular round
// a circle of Amplitude / 4 pixels, Frequency / 64 turns a second;
// Gestation wobbling to and fro on both axes; WavyX and WavyY linear with
// a wobble across. None of The Fallen's textures is an IceTexture, so this
// is untried on real data.

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

class Ice : public AnimatedTexture {
public:
    enum Panning : uint8_t { Linear, Circular, Gestation, WavyX, WavyY };

    Ice(const PropertyList& props, image::Image source, image::Image glass)
        : source_(std::move(source)), glass_(std::move(glass)), pace_(props) {
        panning_ = props.byte("PanningStyle");
        hSpeed_ = float(int(props.byte("HorizPanSpeed", 128)) - 128) / 4;
        vSpeed_ = float(int(props.byte("VertPanSpeed", 128)) - 128) / 4;
        frequency_ = float(props.byte("Frequency")) / 64;
        amplitude_ = float(props.byte("Amplitude")) / 4;
        moveIce_ = props.boolean("MoveIce");
        // The glass's brightness, for its slopes.
        brightness_.resize(size_t(glass_.w) * size_t(glass_.h));
        for (size_t i = 0; i < brightness_.size(); ++i) {
            const uint8_t* p = glass_.rgba.data() + i * 4;
            brightness_[i] = (int(p[0]) * 3 + int(p[1]) * 6 + int(p[2])) / 10;
        }
        image_ = source_;
        render();
    }

    const char* kind() const override { return "ice"; }

    bool advance(float seconds) override {
        // Its glass slides in real time; drawn when its pace says.
        time_ += std::max(0.0f, seconds);
        const int n = pace_.due(seconds);
        if (n)
            render();
        return n > 0;
    }

private:
    // How far the moving one has slid, in pixels.
    std::array<float, 2> offset() const {
        const float angle = 2 * kPi * frequency_ * time_;
        switch (panning_) {
        case Circular: return {amplitude_ * std::cos(angle), amplitude_ * std::sin(angle)};
        case Gestation: return {amplitude_ * std::sin(angle), amplitude_ * std::sin(angle * 1.3f)};
        case WavyX: return {hSpeed_ * time_, vSpeed_ * time_ + amplitude_ * std::sin(angle)};
        case WavyY: return {hSpeed_ * time_ + amplitude_ * std::sin(angle), vSpeed_ * time_};
        default: return {hSpeed_ * time_, vSpeed_ * time_};
        }
    }

    static int wrap(int v, int n) { return ((v % n) + n) % n; }

    void render() {
        const int w = source_.w, h = source_.h;
        const auto [ou, ov] = offset();
        const int gx = int(std::floor(ou)), gy = int(std::floor(ov));
        // The glass's offset, or the source's with MoveIce.
        const int glassX = moveIce_ ? 0 : gx, glassY = moveIce_ ? 0 : gy;
        const int srcX = moveIce_ ? gx : 0, srcY = moveIce_ ? gy : 0;
        auto glassAt = [&](int x, int y) {
            return brightness_[size_t(wrap(y + glassY, h)) * size_t(w) + size_t(wrap(x + glassX, w))];
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int dx = std::clamp((glassAt(x + 1, y) - glassAt(x - 1, y)) / 4, -16, 16);
                const int dy = std::clamp((glassAt(x, y + 1) - glassAt(x, y - 1)) / 4, -16, 16);
                std::copy_n(source_.pixel(wrap(x + dx + srcX, w), wrap(y + dy + srcY, h)), 4, image_.pixel(x, y));
            }
    }

    image::Image source_, glass_;
    std::vector<int> brightness_;
    uint8_t panning_ = Linear;
    float hSpeed_ = 0, vSpeed_ = 0, frequency_ = 0, amplitude_ = 0;
    bool moveIce_ = false;
    float time_ = 0;
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
        image::Image source, glass;
        if (!scaledPicture(packages, package, t.props.object("SourceTexture"), t.width, t.height, source))
            return fail(what + ": its SourceTexture doesn't read");
        if (!scaledPicture(packages, package, t.props.object("GlassTexture"), t.width, t.height, glass))
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
