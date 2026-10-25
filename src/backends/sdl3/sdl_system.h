// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace reluva {

class SdlGpuRenderer;

class SdlSystem : public System {
public:
    SdlSystem();
    ~SdlSystem() override;
    SdlSystem(const SdlSystem&) = delete;
    SdlSystem& operator=(const SdlSystem&) = delete;

    Error init();

    // Used by the launcher, which draws with ImGui directly on SDL.
    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    // Drop the game framebuffer and return the window to launcher mode.
    void resetForLauncher();
    // True once the user closed the window during a game.
    bool quitRequested() const { return quitRequested_; }

    // System
    Error initGraphics(int width, int height, PixelFormat format) override;
    void setPalette(const Color* colors, int start, int count) override;
    void updateScreen(const Surface& surface) override;
    void setWindowTitle(const std::string& title) override;
    Error init3D(int width, int height, gfx::Renderer3D*& out) override;
    bool pollEvent(Event& event) override;
    void setTextInput(bool on) override;
    void setMouseCursorVisible(bool visible) override;
    void setVideoMode(int width, int height, bool fullscreen) override;
    void setRelativeMouse(bool on) override;
    uint64_t getMillis() const override;
    void delayMillis(uint32_t ms) override;
    Error startAudio(AudioCallback callback) override;
    void stopAudio() override;
    int audioRate() const override { return kAudioRate; }

private:
    static constexpr int kAudioRate = 44100;
    static void SDLCALL audioThunk(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void release3D();

    SDL_Window* window_ = nullptr;
    bool relativeMouse_ = false; // held by setRelativeMouse, held again when the window changes
    SDL_Renderer* renderer_ = nullptr;
    SDL_GPUDevice* gpu_ = nullptr; // set when renderer_ runs on SDL's GPU API, which 3D needs
    std::unique_ptr<SdlGpuRenderer> renderer3d_;
    SDL_Texture* gameTexture_ = nullptr;
    int gameW_ = 0, gameH_ = 0;
    PixelFormat gameFormat_ = PixelFormat::CLUT8;
    uint32_t palette_[256] = {};          // RGBA32, byte order R,G,B,A
    std::vector<uint32_t> convertBuffer_; // CLUT8 -> RGBA32 staging
    bool quitRequested_ = false;

    SDL_AudioStream* audioStream_ = nullptr;
    AudioCallback audioCallback_;
    std::vector<int16_t> audioBuffer_; // audio thread only
};

} // namespace reluva
