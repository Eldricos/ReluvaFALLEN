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
// The fire and water simulations move on in steps of this many a second.
constexpr float kStepsPerSecond = 30;

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

// Steps at kStepsPerSecond: how many are due after `seconds` more (at most
// a few, so a long pause doesn't stall).
int dueSteps(float& pending, float seconds) {
    pending += std::max(0.0f, seconds);
    int n = 0;
    while (pending * kStepsPerSecond >= 1 && n < 4) {
        pending -= 1 / kStepsPerSecond;
        ++n;
    }
    if (pending * kStepsPerSecond >= 1)
        pending = 0;
    return n;
}

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
        : w_(w), h_(h), palette_(palette), sparks_(std::move(sparks)), random_(uint32_t(w * 7919 + h)) {
        rising_ = props.boolean("bRising");
        keep_ = props.byte("RenderHeat", 220);
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
        const int n = dueSteps(pending_, seconds);
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

    void tick() {
        ++step_;
        spread();
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
    }

    // The heat spreads and cools: rising, each pixel takes the heat below
    // it; else it blends with its neighbours. Rows past the bottom are cold.
    void spread() {
        for (int y = 0; y < h_; ++y) {
            const uint8_t* row = &heat_[size_t(y) * size_t(w_)];
            const uint8_t* up = &heat_[size_t((y + h_ - 1) % h_) * size_t(w_)];
            const uint8_t* down = &heat_[size_t((y + 1) % h_) * size_t(w_)];
            const bool hasDown = y + 1 < h_;
            uint8_t* out = &next_[size_t(y) * size_t(w_)];
            for (int x = 0; x < w_; ++x) {
                const int l = x ? x - 1 : w_ - 1, r = x + 1 < w_ ? x + 1 : 0;
                int sum;
                if (rising_)
                    sum = hasDown ? (down[l] + 2 * down[x] + down[r]) * 2 : 0; // out of 8
                else
                    sum = row[l] + row[r] + up[x] + down[x] + 4 * row[x];
                out[x] = uint8_t(sum * keep_ / (8 * 256));
            }
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
    size_t limit_ = 1024;
    int lathe_ = 0; // copies round the middle (1: a ring), 0 for none
    uint32_t step_ = 0;
    float pending_ = 0;
};

// ---------------------------------------------------------------------
// Water: a height field at half the texture's size, rippling: each step a
// cell becomes half its four neighbours' sum less what it was the step
// before, a little damped. Drops (the Drops properties, NumDrops of them:
// type, depth, X and Y in the field's cells, and four bytes) disturb it.
// A water or wave texture shows the slope through its palette; a wet
// texture shows its SourceTexture shifted by the slope.

enum Drop : uint8_t {
    FixedDepth, PhaseSpot, ShallowSpot, HalfAmpl, RandomMover, FixedRandomSpot, WhirlyThing, BigWhirly,
    HorizontalLine, VerticalLine, DiagonalLine1, DiagonalLine2, HorizontalOsc, VerticalOsc, DiagonalOsc1,
    DiagonalOsc2, RainDrops, AreaClamp, LeakyTap, DrippyTap,
};

class Water : public AnimatedTexture {
public:
    struct DropData {
        uint8_t type = 0, depth = 0, x = 0, y = 0, a = 0, b = 0, c = 0, d = 0;
        float px = 0, py = 0; // a moving drop's place
    };

    // `source`: a wet texture's source picture, already the texture's size;
    // empty for water and wave textures (drawn through `palette`).
    Water(int w, int h, const PropertyList& props, Palette palette, std::vector<DropData> drops, image::Image source,
          const char* kind)
        : w_(w), h_(h), fw_(std::max(1, w / 2)), fh_(std::max(1, h / 2)), palette_(palette), drops_(std::move(drops)),
          source_(std::move(source)), kind_(kind), random_(uint32_t(w * 31 + h)) {
        amplitude_ = std::max(1.0f, float(props.byte("WaveAmp", 128))) / 128;
        masked_ = props.boolean("bMasked");
        // A wave texture is lit as a bumpy surface: from BumpMapAngle
        // round (256 a turn), BumpMapLight strong, slopes facing the light
        // past what PhongSize leaves (the larger, the sooner) shining by
        // PhongRange.
        if (std::string_view(kind) == "wave") {
            const float angle = 2 * kPi * float(props.byte("BumpMapAngle")) / 256;
            lightX_ = std::cos(angle) * float(props.byte("BumpMapLight", 32)) / 128;
            lightY_ = std::sin(angle) * float(props.byte("BumpMapLight", 32)) / 128;
            phongFrom_ = float(255 - props.byte("PhongSize")) / 4;
            phong_ = float(props.byte("PhongRange")) / 64;
            bump_ = true;
        }
        height_.assign(size_t(fw_) * size_t(fh_), 0);
        before_ = next_ = height_;
        for (DropData& d : drops_) {
            d.px = d.x;
            d.py = d.y;
        }
        image_.w = w;
        image_.h = h;
        for (int i = 0; i < 60; ++i)
            tick();
        render();
    }

    const char* kind() const override { return kind_; }

    bool advance(float seconds) override {
        const int n = dueSteps(pending_, seconds);
        for (int i = 0; i < n; ++i)
            tick();
        if (n)
            render();
        return n > 0;
    }

private:
    float& cell(int x, int y) {
        x = ((x % fw_) + fw_) % fw_;
        y = ((y % fh_) + fh_) % fh_;
        return height_[size_t(y) * size_t(fw_) + size_t(x)];
    }

    void drop(DropData& d) {
        const float depth = d.depth;
        const int x = d.x, y = d.y;
        auto speed = [](uint8_t v) { return float(v ? v : 8); };
        const float wave = std::sin(turn(step_, speed(d.d), 0));
        switch (d.type) {
        case FixedDepth: cell(x, y) = depth; break;
        case PhaseSpot: cell(x, y) = depth * wave; break;
        case ShallowSpot: cell(x, y) = depth / 2 * wave; break;
        case HalfAmpl: cell(x, y) = depth * (0.5f + 0.5f * wave); break;
        case RandomMover:
            d.px += (random_.unit() - 0.5f) * 2;
            d.py += (random_.unit() - 0.5f) * 2;
            cell(int(d.px), int(d.py)) = depth;
            break;
        case FixedRandomSpot: cell(x, y) = depth * (random_.unit() * 2 - 1); break;
        case WhirlyThing:
        case BigWhirly: {
            const float r = float(1 + d.c / 16) * (d.type == BigWhirly ? 2.0f : 1.0f);
            const float angle = turn(step_, float(int8_t(d.b)), d.a);
            cell(int(float(x) + r * std::cos(angle)), int(float(y) + r * std::sin(angle))) = depth;
            break;
        }
        case HorizontalLine:
        case VerticalLine:
        case DiagonalLine1:
        case DiagonalLine2: {
            const int half = std::max(1, d.c / 4);
            const float v = depth * std::sin(turn(step_, speed(d.d) / 4, 0));
            for (int k = -half; k <= half; ++k) {
                const int dx = d.type == VerticalLine ? 0 : k;
                const int dy = d.type == HorizontalLine ? 0 : d.type == DiagonalLine2 ? -k : k;
                cell(x + dx, y + dy) = v;
            }
            break;
        }
        case HorizontalOsc:
        case VerticalOsc:
        case DiagonalOsc1:
        case DiagonalOsc2: {
            const float off = float(std::max(2, d.d / 4)) * std::sin(turn(step_, speed(d.c), 0));
            const float dx = d.type == VerticalOsc ? 0 : off;
            const float dy = d.type == HorizontalOsc ? 0 : d.type == DiagonalOsc2 ? -off : off;
            cell(int(float(x) + dx), int(float(y) + dy)) = depth;
            break;
        }
        case RainDrops:
            if (random_.below(4) == 0)
                cell(random_.below(fw_), random_.below(fh_)) = depth;
            break;
        case AreaClamp: {
            const int r = std::max(1, d.c / 8);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    cell(x + dx, y + dy) = 0;
            break;
        }
        case LeakyTap:
        case DrippyTap:
            if (step_ % uint32_t(d.type == LeakyTap ? 16 + d.d : 8 + d.d / 2) == 0)
                cell(x, y) = depth;
            break;
        default: break;
        }
    }

    void tick() {
        ++step_;
        for (DropData& d : drops_)
            drop(d);
        for (int y = 0; y < fh_; ++y) {
            const float* row = &height_[size_t(y) * size_t(fw_)];
            const float* up = &height_[size_t((y + fh_ - 1) % fh_) * size_t(fw_)];
            const float* down = &height_[size_t((y + 1) % fh_) * size_t(fw_)];
            const float* old = &before_[size_t(y) * size_t(fw_)];
            float* out = &next_[size_t(y) * size_t(fw_)];
            for (int x = 0; x < fw_; ++x) {
                const int l = x ? x - 1 : fw_ - 1, r = x + 1 < fw_ ? x + 1 : 0;
                out[x] = ((row[l] + row[r] + up[x] + down[x]) / 2 - old[x]) * 0.96f;
            }
        }
        before_.swap(height_);
        height_.swap(next_);
    }

    void render() {
        image_.rgba.resize(size_t(w_) * size_t(h_) * 4);
        // The slope across each cell, then each pixel from its cell's.
        slopeX_.resize(height_.size());
        slopeY_.resize(height_.size());
        for (int y = 0; y < fh_; ++y) {
            const float* row = &height_[size_t(y) * size_t(fw_)];
            const float* up = &height_[size_t((y + fh_ - 1) % fh_) * size_t(fw_)];
            const float* down = &height_[size_t((y + 1) % fh_) * size_t(fw_)];
            for (int x = 0; x < fw_; ++x) {
                const int l = x ? x - 1 : fw_ - 1, r = x + 1 < fw_ ? x + 1 : 0;
                slopeX_[size_t(y) * size_t(fw_) + size_t(x)] = (row[r] - row[l]) * amplitude_;
                slopeY_[size_t(y) * size_t(fw_) + size_t(x)] = (down[x] - up[x]) * amplitude_;
            }
        }
        for (int y = 0; y < h_; ++y) {
            const size_t cellRow = size_t(y * fh_ / h_) * size_t(fw_);
            uint8_t* p = image_.pixel(0, y);
            for (int x = 0; x < w_; ++x, p += 4) {
                const size_t c = cellRow + size_t(x * fw_ / w_);
                const float sx = slopeX_[c], sy = slopeY_[c];
                if (!source_.rgba.empty()) {
                    const int ox = std::clamp(int(sx / 16), -16, 16), oy = std::clamp(int(sy / 16), -16, 16);
                    int ux = x + ox, uy = y + oy;
                    ux = ux < 0 ? ux + w_ : ux >= w_ ? ux - w_ : ux;
                    uy = uy < 0 ? uy + h_ : uy >= h_ ? uy - h_ : uy;
                    std::copy_n(source_.pixel(ux, uy), 4, p);
                } else if (bump_) {
                    const float shade = (sx * lightX_ + sy * lightY_) / amplitude_;
                    const float shine = shade > phongFrom_ ? (shade - phongFrom_) * phong_ : 0;
                    const int v = std::clamp(128 + int(shade + shine), 0, 255);
                    std::copy_n(palette_.rgba.data() + size_t(v) * 4, 3, p);
                    p[3] = masked_ && v == 0 ? 0 : 255;
                } else {
                    const int v = std::clamp(128 + int((sx + sy) / 4), 0, 255);
                    std::copy_n(palette_.rgba.data() + size_t(v) * 4, 3, p);
                    p[3] = masked_ && v == 0 ? 0 : 255;
                }
            }
        }
    }

    int w_, h_, fw_, fh_;
    Palette palette_;
    std::vector<DropData> drops_;
    image::Image source_;
    const char* kind_;
    Random random_;
    std::vector<float> height_, before_, next_;
    std::vector<float> slopeX_, slopeY_;
    float amplitude_ = 1;
    bool masked_ = false;
    bool bump_ = false; // a wave texture: lit, below
    float lightX_ = 0, lightY_ = 0, phongFrom_ = 0, phong_ = 0;
    uint32_t step_ = 0;
    float pending_ = 0;
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
        : source_(std::move(source)), glass_(std::move(glass)) {
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
        const int n = dueSteps(pending_, seconds);
        time_ += float(n) / kStepsPerSecond;
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
    float time_ = 0, pending_ = 0;
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
