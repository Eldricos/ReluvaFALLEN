// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Unreal Engine 1 data viewer: pages showing what a game's packages
// hold, while the renderer and the game's script are written. Tab switches
// pages, Esc returns to the launcher.
//
// Maps:     a level's BSP drawn by the GPU. Left/Right pick a map; drag to
//           look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift to
//           go faster, R back to the player start, I to show or hide the
//           info, T for textures or plain shading, L for the level's
//           lightmaps or shading by facing, [ and ] to dim or brighten
//           the lightmaps, - and = to change the display's contrast (its
//           gamma; to compare with the original), O to move the movers
//           (doors, lifts) on to their next keyframe, K to show or hide
//           the sky.
// Textures: Left/Right step through a package's textures, PageUp/PageDown
//           pick the package (.utx, then maps' own, then .u).
// Sounds:   Left/Right step, PageUp/PageDown pick the package, Space plays.
// Packages: every package on the search path. Left/Right pick one,
//           Up/Down move through its exports (PageUp/PageDown, Home/End),
//           Enter shows the selected export's properties.

#include "engines/unreal/actors.h"
#include "engines/unreal/games.h"
#include "engines/unreal/model.h"
#include "engines/unreal/package_set.h"

#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
#include "common/system.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::unreal {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const PackageSet& packages;
    const GameInfo& game;
    const std::vector<std::string>& warnings;
    audio::Mixer* mixer = nullptr; // none if the system has no audio
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0; // pages with nothing to show are skipped
    virtual void activate() = 0;      // redraw and retitle after switching here
    virtual void deactivate() {}
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    virtual bool uses3D() const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

class PackageViewer : public Page {
public:
    explicit PackageViewer(ViewerContext& ctx);
    const char* name() const override { return "Packages"; }
    size_t count() const override { return names_.size(); }
    void activate() override { draw(); }
    void handleEvent(const Event& ev) override;

private:
    void draw();

    std::vector<std::string> names_;
    int index_ = 0;
    int selected_ = 0, top_ = 0;
    bool showProps_ = false;
};

// Steps through the exports of some classes, package by package.
class ObjectBrowser {
public:
    // Over the named packages, in order.
    ObjectBrowser(const PackageSet& packages, std::vector<std::string> names, bool (*wanted)(std::string_view));
    size_t packageCount() const { return names_.size(); }
    // Moves within the package (by objects) or between packages (skipping
    // those with none); false if there's nothing.
    bool step(int objects);
    bool stepPackage(int packages);
    const Package* package() const;
    int exportIndex() const;
    // "3/40 in Package (2/12)".
    std::string where() const;

private:
    bool load(int package);

    const PackageSet& packages_;
    bool (*wanted_)(std::string_view);
    std::vector<std::string> names_;
    int package_ = -1;
    std::vector<int> objects_;
    int object_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();
    ObjectBrowser browser_;
    bool started_ = false;
};

class SoundViewer : public Page {
public:
    explicit SoundViewer(ViewerContext& ctx);
    ~SoundViewer() override;
    const char* name() const override { return "Sounds"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();
    void play();
    ObjectBrowser browser_;
    bool started_ = false;
    std::string status_;
    audio::SoundHandle playing_ = audio::kNoSound;
};

class MapViewer : public Page {
public:
    explicit MapViewer(ViewerContext& ctx);
    ~MapViewer() override;
    const char* name() const override { return "Maps"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    void load(int index);
    void unload();
    void toStart();
    void drawOverlay(uint64_t nowMs);

    struct Batch {
        uint32_t firstIndex = 0, indexCount = 0;
        gfx::TextureId texture = 0;
        gfx::TextureId lightmap = 0; // 0: none (unlit, blended, or the level has no lighting)
        uint32_t polyFlags = 0;
    };

    // A mover: its polygons (in its brush's own coordinates, in
    // moverMesh_) and where it is between its keyframes.
    struct MoverDraw {
        Mover mover;
        std::vector<Batch> batches;
        int key = 0, target = 0; // moving from key to target
        float t = 0;             // 0-1 of the way
    };
    gfx::Mat4 moverTransform(const MoverDraw& m) const;
    void drawBatches(const std::vector<Batch>& batches, gfx::MeshId mesh, const gfx::Mat4& model, bool levelLit);

    std::vector<std::string> names_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr; // valid while this page is shown
    gfx::MeshId mesh_ = 0;
    std::vector<Batch> batches_;
    gfx::MeshId moverMesh_ = 0;
    std::vector<MoverDraw> movers_;
    std::optional<Vec3f> sky_; // the sky zone's viewpoint, Unreal's coordinates
    bool showSky_ = true;
    std::vector<gfx::TextureId> textures_;
    Surface overlay_;
    std::string error_;
    std::vector<std::string> info_;
    bool textured_ = true;
    bool lightmaps_ = true;  // drawn with the lightmaps (else shaded by facing)
    float lightScale_ = 1;   // the lightmaps' brightness, [ and ]
    // How the finished frame is shown: colours times kExposure, then
    // raised to displayGamma_ (- and =). Fitted to the original's
    // screenshots: see lighting.md.
    float displayGamma_ = 1.3f;
    bool hasLightmaps_ = false;

    // Fly camera, in the renderer's world: Unreal's axes with Y negated
    // (so right-handed), Z up; yaw 0 looks along +X.
    std::array<float, 3> start_{}, pos_{};
    float startYaw_ = 0, yaw_ = 0, pitch_ = 0;
    bool held_[7] = {};
    bool dragging_ = false, showInfo_ = true;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0, fpsStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
};

} // namespace reluva::unreal
