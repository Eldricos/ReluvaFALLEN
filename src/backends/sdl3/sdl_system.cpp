// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sdl3/sdl_system.h"
#include "backends/sdl3/sdl_gpu.h"

#include <cstring>

namespace reluva {

namespace {

uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t bytes[4] = {r, g, b, a};
    uint32_t v;
    std::memcpy(&v, bytes, 4);
    return v;
}

uint32_t translateKey(SDL_Keycode k) {
    if (k >= 'A' && k <= 'Z')
        return k - 'A' + 'a';
    if (k < 128)
        return k; // SDL keycodes match ASCII for printable keys
    switch (k) {
    case SDLK_UP: return key::Up;
    case SDLK_DOWN: return key::Down;
    case SDLK_LEFT: return key::Left;
    case SDLK_RIGHT: return key::Right;
    case SDLK_INSERT: return key::Insert;
    case SDLK_HOME: return key::Home;
    case SDLK_END: return key::End;
    case SDLK_PAGEUP: return key::PageUp;
    case SDLK_PAGEDOWN: return key::PageDown;
    case SDLK_LSHIFT: return key::LShift;
    case SDLK_RSHIFT: return key::RShift;
    case SDLK_LCTRL: return key::LCtrl;
    case SDLK_RCTRL: return key::RCtrl;
    case SDLK_LALT: return key::LAlt;
    case SDLK_RALT: return key::RAlt;
    default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12)
        return key::F1 + (k - SDLK_F1);
    // The keypad's keys keep their own codes with NumLock off too (SDL3
    // reports KP_8, not Up), so engines can use them as keys of their own.
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
        return key::Kp0 + 1 + (k - SDLK_KP_1);
    if (k == SDLK_KP_0)
        return key::Kp0;
    if (k == SDLK_KP_PERIOD)
        return key::KpPeriod;
    if (k == SDLK_KP_ENTER)
        return key::KpEnter;
    return key::Unknown;
}

uint16_t translateMods(SDL_Keymod m) {
    uint16_t out = 0;
    if (m & SDL_KMOD_SHIFT) out |= keymod::Shift;
    if (m & SDL_KMOD_CTRL) out |= keymod::Ctrl;
    if (m & SDL_KMOD_ALT) out |= keymod::Alt;
    return out;
}

MouseButton translateButton(uint8_t b) {
    switch (b) {
    case SDL_BUTTON_LEFT: return MouseButton::Left;
    case SDL_BUTTON_RIGHT: return MouseButton::Right;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    default: return MouseButton::None;
    }
}

} // namespace

SdlSystem::SdlSystem() = default;

SdlSystem::~SdlSystem() {
    stopAudio();
    release3D();
    if (gameTexture_)
        SDL_DestroyTexture(gameTexture_);
    if (renderer_)
        SDL_DestroyRenderer(renderer_);
    if (gpu_)
        SDL_DestroyGPUDevice(gpu_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
}

Error SdlSystem::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        return Error::fail(std::string("SDL_Init failed: ") + SDL_GetError());
    // No sound card shouldn't stop anyone playing; startAudio() reports it.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        SDL_Log("Audio unavailable: %s", SDL_GetError());

    window_ = SDL_CreateWindow("Reluva", 1280, 800,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_)
        return Error::fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    // The renderer runs on a GPU device of our own when it can, so engines
    // can draw 3D on the same device (init3D). Asking SDL for a particular
    // renderer (SDL_RENDER_DRIVER) other than "gpu" skips this.
    const char* driver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    if (!driver || std::strcmp(driver, "gpu") == 0) {
        gpu_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
        if (gpu_) {
            renderer_ = SDL_CreateGPURenderer(gpu_, window_);
            if (!renderer_) {
                SDL_DestroyGPUDevice(gpu_);
                gpu_ = nullptr;
            }
        }
        if (!gpu_)
            SDL_Log("No GPU device, so no 3D: %s", SDL_GetError());
    }
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
        return Error::fail(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    SDL_SetRenderVSync(renderer_, 1);

    SDL_Log("Video driver: %s, renderer: %s%s%s%s%s", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(renderer_),
            gpu_ ? ", GPU driver: " : "", gpu_ ? SDL_GetGPUDeviceDriver(gpu_) : "", gpu_ ? ", device: " : "",
            gpu_ ? SDL_GetStringProperty(SDL_GetGPUDeviceProperties(gpu_), SDL_PROP_GPU_DEVICE_NAME_STRING, "?") : "");
    return Error::success();
}

void SdlSystem::release3D() { renderer3d_.reset(); }

Error SdlSystem::init3D(int width, int height, gfx::Renderer3D*& out) {
    out = nullptr;
    release3D();
    if (!gpu_)
        return Error::fail("3D rendering needs a GPU with Vulkan or Metal support");
    if (gameTexture_) {
        SDL_DestroyTexture(gameTexture_);
        gameTexture_ = nullptr;
    }
    gameW_ = gameH_ = 0;
    auto r = std::make_unique<SdlGpuRenderer>(gpu_, renderer_);
    if (Error err = r->init(width, height))
        return err;
    renderer3d_ = std::move(r);
    SDL_SetRenderLogicalPresentation(renderer_, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    out = renderer3d_.get();
    return Error::success();
}

void SdlSystem::resetForLauncher() {
    stopAudio();
    SDL_StopTextInput(window_);
    SDL_ShowCursor();
    relativeMouse_ = false;
    SDL_SetWindowRelativeMouseMode(window_, false);
    SDL_SetWindowFullscreen(window_, false);
    release3D();
    if (gameTexture_) {
        SDL_DestroyTexture(gameTexture_);
        gameTexture_ = nullptr;
    }
    gameW_ = gameH_ = 0;
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetWindowTitle(window_, "Reluva");
}

Error SdlSystem::initGraphics(int width, int height, PixelFormat format) {
    release3D();
    if (gameTexture_)
        SDL_DestroyTexture(gameTexture_);
    gameTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!gameTexture_)
        return Error::fail(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    SDL_SetTextureScaleMode(gameTexture_, SDL_SCALEMODE_NEAREST);

    gameW_ = width;
    gameH_ = height;
    gameFormat_ = format;
    convertBuffer_.assign(static_cast<size_t>(width) * height, 0);
    for (int i = 0; i < 256; ++i)
        palette_[i] = packRgba(0, 0, 0, 255);

    // Letterbox the game framebuffer into the window at any size, and map
    // mouse coordinates back into framebuffer pixels.
    SDL_SetRenderLogicalPresentation(renderer_, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return Error::success();
}

void SdlSystem::setPalette(const Color* colors, int start, int count) {
    for (int i = 0; i < count && start + i < 256; ++i)
        palette_[start + i] = packRgba(colors[i].r, colors[i].g, colors[i].b, 255);
}

void SdlSystem::updateScreen(const Surface& surface) {
    if (!gameTexture_ || surface.w != gameW_ || surface.h != gameH_ || surface.format != gameFormat_)
        return;

    if (surface.format == PixelFormat::CLUT8) {
        for (int y = 0; y < surface.h; ++y) {
            const uint8_t* src = surface.row(y);
            uint32_t* dst = convertBuffer_.data() + static_cast<size_t>(y) * gameW_;
            for (int x = 0; x < surface.w; ++x)
                dst[x] = palette_[src[x]];
        }
        SDL_UpdateTexture(gameTexture_, nullptr, convertBuffer_.data(), gameW_ * 4);
    } else {
        SDL_UpdateTexture(gameTexture_, nullptr, surface.pixels.data(), surface.pitch);
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, gameTexture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void SdlSystem::setWindowTitle(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

bool SdlSystem::pollEvent(Event& out) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        SDL_ConvertEventToRenderCoordinates(renderer_, &ev);
        out = Event{};
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            quitRequested_ = true;
            out.type = EventType::Quit;
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            // Ctrl+F5 is the global "return to launcher" hotkey.
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F5 && (ev.key.mod & SDL_KMOD_CTRL)) {
                out.type = EventType::ReturnToLauncher;
                return true;
            }
            // Alt+Enter toggles fullscreen.
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                bool fs = SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN;
                SDL_SetWindowFullscreen(window_, !fs);
                continue;
            }
            out.type = ev.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
            out.key = translateKey(ev.key.key);
            out.mods = translateMods(ev.key.mod);
            out.repeat = ev.key.repeat;
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            out.type = EventType::MouseMove;
            out.mouseX = static_cast<int>(ev.motion.x);
            out.mouseY = static_cast<int>(ev.motion.y);
            out.moveX = ev.motion.xrel;
            out.moveY = ev.motion.yrel;
            out.mods = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseDown : EventType::MouseUp;
            out.button = translateButton(ev.button.button);
            out.mouseX = static_cast<int>(ev.button.x);
            out.mouseY = static_cast<int>(ev.button.y);
            out.clicks = ev.button.clicks;
            out.mods = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            out.type = EventType::MouseWheel;
            out.wheelY = ev.wheel.integer_y;
            return true;
        case SDL_EVENT_TEXT_INPUT:
            if (!ev.text.text)
                break;
            out.type = EventType::TextInput;
            SDL_strlcpy(out.text, ev.text.text, sizeof out.text);
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            // The window changed under a held mouse: hold it again, as some
            // systems let it go.
            if (relativeMouse_) {
                SDL_SetWindowRelativeMouseMode(window_, false);
                SDL_SetWindowRelativeMouseMode(window_, true);
            }
            break;
        default:
            break; // ignore everything else
        }
    }
    return false;
}

void SdlSystem::setTextInput(bool on) {
    if (on)
        SDL_StartTextInput(window_);
    else
        SDL_StopTextInput(window_);
}

void SdlSystem::setMouseCursorVisible(bool visible) {
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void SdlSystem::setVideoMode(int width, int height, bool fullscreen) {
    if (!window_)
        return;
    // Fullscreen at the desktop's own mode (null), which needs no mode switch.
    const bool wasFullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    if (fullscreen != wasFullscreen) {
        SDL_SetWindowFullscreenMode(window_, nullptr);
        SDL_SetWindowFullscreen(window_, fullscreen);
    }
    SDL_Rect usable{};
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (!fullscreen && (w != width || h != height) &&
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable) && width <= usable.w &&
        height <= usable.h && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED))
        SDL_SetWindowSize(window_, width, height);
    // The changes happen in their own time on some systems: wait, so the
    // mouse is held by the window as it ends up.
    SDL_SyncWindow(window_);
    if (relativeMouse_)
        SDL_SetWindowRelativeMouseMode(window_, true);
}

void SdlSystem::setRelativeMouse(bool on) {
    relativeMouse_ = on;
    if (window_)
        SDL_SetWindowRelativeMouseMode(window_, on);
}

uint64_t SdlSystem::getMillis() const { return SDL_GetTicks(); }

void SdlSystem::delayMillis(uint32_t ms) { SDL_Delay(ms); }

void SDLCALL SdlSystem::audioThunk(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<SdlSystem*>(userdata);
    const int frames = additional / (2 * static_cast<int>(sizeof(int16_t)));
    if (frames <= 0)
        return;
    self->audioBuffer_.resize(static_cast<size_t>(frames) * 2);
    self->audioCallback_(self->audioBuffer_.data(), frames);
    SDL_PutAudioStreamData(stream, self->audioBuffer_.data(), frames * 4);
}

Error SdlSystem::startAudio(AudioCallback callback) {
    stopAudio();
    audioCallback_ = std::move(callback);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kAudioRate};
    audioStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioThunk, this);
    if (!audioStream_)
        return Error::fail(std::string("Cannot open audio: ") + SDL_GetError());
    SDL_ResumeAudioStreamDevice(audioStream_);
    return Error::success();
}

void SdlSystem::stopAudio() {
    if (audioStream_) {
        SDL_DestroyAudioStream(audioStream_); // stops the callback before returning
        audioStream_ = nullptr;
    }
    audioCallback_ = nullptr;
}

} // namespace reluva
