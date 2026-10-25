// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/gfx3d.h"
#include "common/types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace reluva {

// Everything an engine may ask of the platform. Engines must never include
// SDL (or any other backend) headers; they go through this interface so the
// backend can be swapped or extended (GPU rendering, consoles, web...).
class System {
public:
    virtual ~System() = default;

    // --- Graphics -----------------------------------------------------------
    // Set up a game framebuffer of the given size. It is scaled to the window.
    virtual Error initGraphics(int width, int height, PixelFormat format) = 0;
    virtual void setPalette(const Color* colors, int start, int count) = 0;
    // Copy the surface to the screen and present it.
    virtual void updateScreen(const Surface& surface) = 0;
    virtual void setWindowTitle(const std::string& title) = 0;

    // --- 3D graphics (optional) ----------------------------------------------
    // Switch to GPU rendering of width x height frames, scaled to the window
    // like the framebuffer. `out` stays valid, and its textures and meshes
    // alive, until the next initGraphics() or init3D() or the engine ends;
    // updateScreen() does nothing meanwhile. Fails (out = nullptr) when the
    // backend or the machine can't render 3D.
    virtual Error init3D(int width, int height, gfx::Renderer3D*& out) {
        (void)width;
        (void)height;
        out = nullptr;
        return Error::fail("3D rendering is not available");
    }

    // --- Input --------------------------------------------------------------
    // Returns false when no more events are pending.
    virtual bool pollEvent(Event& event) = 0;
    // Whether typed text arrives as TextInput events (off by default).
    virtual void setTextInput(bool on) { (void)on; }
    // Whether the system's mouse pointer shows over the game (for games
    // that draw their own).
    virtual void setMouseCursorVisible(bool visible) { (void)visible; }
    // Whether the mouse is held by the window, hidden, for looking around:
    // MouseMove events then carry only how far it moved (moveX, moveY).
    virtual void setRelativeMouse(bool on) { (void)on; }
    // The window for a game's video mode: the whole screen, or a window of
    // width x height (when it fits on the screen; the frame is letterboxed
    // in whatever size the window is).
    virtual void setVideoMode(int width, int height, bool fullscreen) { (void)width, (void)height, (void)fullscreen; }

    // --- Time ---------------------------------------------------------------
    virtual uint64_t getMillis() const = 0;
    virtual void delayMillis(uint32_t ms) = 0;

    // --- Audio --------------------------------------------------------------
    // The callback runs on the audio thread and must fill `frames` stereo
    // frames of interleaved signed 16-bit samples at audioRate().
    using AudioCallback = std::function<void(int16_t* out, int frames)>;
    virtual Error startAudio(AudioCallback callback) = 0;
    virtual void stopAudio() = 0;
    virtual int audioRate() const = 0;
};

} // namespace reluva
