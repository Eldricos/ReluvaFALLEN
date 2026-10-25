// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Testbed: a built-in "game" that exercises the System interface (palette
// graphics, input, timing, audio) so the backend can be checked without any
// game data. Esc returns to the launcher, Space toggles a test tone.

#include "engines/engine.h"

#include <atomic>
#include <cmath>

namespace reluva {

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr double kPi = 3.14159265358979323846;

class TestbedEngine : public Engine {
public:
    explicit TestbedEngine(System& system) : sys_(system) {}

    Error run() override {
        if (Error err = sys_.initGraphics(kWidth, kHeight, PixelFormat::CLUT8))
            return err;
        sys_.setWindowTitle("Reluva - Testbed (Esc: back, Space: tone)");
        screen_.create(kWidth, kHeight, PixelFormat::CLUT8);

        const int rate = sys_.audioRate();
        // Audio failing is not fatal for the testbed; it just stays silent.
        sys_.startAudio([this, rate](int16_t* out, int frames) {
            const bool on = toneOn_.load();
            for (int i = 0; i < frames; ++i) {
                int16_t s = 0;
                if (on) {
                    s = static_cast<int16_t>(std::sin(phase_) * 6000);
                    phase_ += 2 * kPi * 440.0 / rate;
                    if (phase_ > 2 * kPi)
                        phase_ -= 2 * kPi;
                }
                out[i * 2] = s;
                out[i * 2 + 1] = s;
            }
        });

        const uint64_t start = sys_.getMillis();
        bool running = true;
        while (running) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                switch (ev.type) {
                case EventType::Quit:
                case EventType::ReturnToLauncher:
                    running = false;
                    break;
                case EventType::KeyDown:
                    if (ev.key == key::Escape)
                        running = false;
                    else if (ev.key == key::Space && !ev.repeat)
                        toneOn_ = !toneOn_;
                    break;
                case EventType::MouseMove:
                    mouseX_ = ev.mouseX;
                    mouseY_ = ev.mouseY;
                    break;
                default:
                    break;
                }
            }

            const uint64_t t = sys_.getMillis() - start;
            drawFrame(t);
            sys_.updateScreen(screen_);
            sys_.delayMillis(1); // vsync paces the loop; this just yields
        }

        sys_.stopAudio();
        return Error::success();
    }

private:
    void drawFrame(uint64_t t) {
        // Static plasma pattern in indices 0-254; animation comes purely from
        // palette cycling, the way many games of the era did it.
        if (plasma_.pixels.empty()) {
            plasma_.create(kWidth, kHeight, PixelFormat::CLUT8);
            for (int y = 0; y < kHeight; ++y) {
                uint8_t* row = plasma_.row(y);
                for (int x = 0; x < kWidth; ++x) {
                    double v = std::sin(x / 16.0) + std::sin(y / 8.0) +
                               std::sin((x + y) / 16.0) + std::sin(std::hypot(x - 160, y - 100) / 8.0);
                    row[x] = static_cast<uint8_t>((v + 4) / 8 * 254);
                }
            }
        }
        screen_.pixels = plasma_.pixels;

        Color pal[256];
        const double shift = t / 1000.0;
        for (int i = 0; i < 255; ++i) {
            double a = i / 254.0 * 2 * kPi + shift;
            pal[i].r = static_cast<uint8_t>(127 + 127 * std::sin(a));
            pal[i].g = static_cast<uint8_t>(127 + 127 * std::sin(a + 2 * kPi / 3));
            pal[i].b = static_cast<uint8_t>(127 + 127 * std::sin(a + 4 * kPi / 3));
        }
        pal[255] = {255, 255, 255};
        sys_.setPalette(pal, 0, 256);

        // Crosshair at the mouse in colour 255, to verify coordinate mapping.
        for (int d = -4; d <= 4; ++d) {
            putPixel(mouseX_ + d, mouseY_, 255);
            putPixel(mouseX_, mouseY_ + d, 255);
        }
    }

    void putPixel(int x, int y, uint8_t c) {
        if (x >= 0 && y >= 0 && x < kWidth && y < kHeight)
            screen_.row(y)[x] = c;
    }

    System& sys_;
    Surface screen_;
    Surface plasma_;
    int mouseX_ = kWidth / 2, mouseY_ = kHeight / 2;
    std::atomic<bool> toneOn_{false};
    double phase_ = 0; // audio thread only
};

class TestbedMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "testbed"; }
    const char* name() const override { return "Testbed"; }

    // The testbed needs no files; the launcher starts it directly.
    std::vector<DetectedGame> detect(const std::filesystem::path&) const override { return {}; }

    std::unique_ptr<Engine> create(System& system, const GameTarget&) const override {
        return std::make_unique<TestbedEngine>(system);
    }
};

} // namespace

const MetaEngine& testbedMetaEngine() {
    static const TestbedMetaEngine instance;
    return instance;
}

} // namespace reluva
