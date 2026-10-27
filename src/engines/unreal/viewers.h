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
//           the sky, N to play or stop the meshes' animations.
//           Enter plays the level: its scripts run live with the player
//           in it, seen from the game's own camera. W/S move, A/D step
//           aside, Left/Right turn, Up/Down look, drag to turn and look,
//           Space jumps, Ctrl or F fires; V lets the camera loose (to
//           fly round the level as it plays) and back; Enter stops.
// Textures: Left/Right step through a package's textures, PageUp/PageDown
//           pick the package (.utx, then maps' own, then .u).
// Meshes:   every Mesh and LodMesh. Left/Right step, PageUp/PageDown pick
//           the package; drag to turn, the wheel zooms, A plays or stops
//           the animation, [ and ] pick it, R resets the view, I shows or
//           hides the info.
// Sounds:   Left/Right step, PageUp/PageDown pick the package, Space plays.
// Packages: every package on the search path. Left/Right pick one,
//           Up/Down move through its exports (PageUp/PageDown, Home/End),
//           Enter shows the selected export's properties.

#include "engines/unreal/actors.h"
#include "engines/unreal/games.h"
#include "engines/unreal/level_play.h"
#include "engines/unreal/lighting.h"
#include "engines/unreal/mesh_render.h"
#include "engines/unreal/model.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/procedural.h"
#include "engines/unreal/texture.h"

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
    // How imported-model meshes are turned, on both 3D pages (M changes it).
    MeshTurn meshTurn = MeshTurn::Reverse;
    int meshYawQuarters = kImportYawQuarters; // and about Z (Y changes it)
    // The Fallen's shared animation sets, read the first time a page needs them.
    std::optional<std::vector<AnimationSet>> animationSets{};
    const std::vector<AnimationSet>& sets() {
        if (!animationSets)
            animationSets = findAnimationSets(packages);
        return *animationSets;
    }
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
    void update(uint64_t nowMs) override;

private:
    void select();
    void draw();
    ObjectBrowser browser_;
    bool started_ = false;
    image::Image img_;
    TextureInfo info_;
    std::string error_;
    std::unique_ptr<AnimatedTexture> anim_; // the texture's animation, if it has one
    bool paused_ = false;
    uint64_t lastMs_ = 0;
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

class MeshViewer : public Page {
public:
    explicit MeshViewer(ViewerContext& ctx);
    ~MeshViewer() override;
    const char* name() const override { return "Meshes"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    void show();
    void unload();
    void resetView();

    ObjectBrowser browser_;
    bool started_ = false;
    gfx::Renderer3D* renderer_ = nullptr;
    Mesh mesh_;
    GpuMesh gpu_;
    std::vector<gfx::TextureId> textures_;
    std::string error_;
    std::vector<std::string> info_;
    Surface overlay_;
    int anim_ = -1; // into mesh_.anims
    // The animations to step through: the mesh's own, then (built on bones)
    // those of the shared sets its list names, borrowed when picked.
    std::vector<std::string> choices_;
    int choice_ = -1;
    size_t ownAnims_ = 0; // of choices_, the mesh's own
    size_t ownAnimCount_ = 0;
    int32_t ownFrames_ = 0;
    void pick(int choice);
    bool playing_ = true, showInfo_ = true, dragging_ = false;
    float time_ = 0, yaw_ = 0, pitch_ = 0.3f, distance_ = 200;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0;
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
    // Shows the map of that name (once active); false if there's none.
    bool selectMap(const std::string& name);
    bool playing() const { return play_ != nullptr; }
    // Playing: the camera let loose, in front of the live actor of that
    // name looking at it; false if there's none.
    bool watchActor(const std::string& name);

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
    static gfx::Mat4 moverPlacement(const Mover& m, const Vec3f& location, const Rotator& rotation);
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
    // Actors drawn as meshes, each mesh uploaded once.
    struct MeshDraw {
        size_t mesh = 0; // into meshes_ / gpuMeshes_
        gfx::Mat4 placement{};
        std::vector<gfx::TextureId> textures; // by texture index
        MeshLight light;
        int anim = -1; // into the mesh's animations
        float startFraction = 0;
        std::string label; // the actor's and its mesh's names
        gfx::Vec3 at{};    // where it stands, the renderer's axes
        float drawScale = 1;
        bool skeletal = false; // its mesh is posed from bones
    };
    // Actors drawn as sprites.
    struct SpriteDraw {
        gfx::TextureId texture = 0;
        gfx::Vec3 at{};             // its centre, the renderer's axes
        float width = 0, height = 0; // world units
        std::array<float, 3> color{1, 1, 1};
        uint8_t style = kStyleNormal;
        bool vertical = false;
    };
    void drawSprites(const std::vector<SpriteDraw>& sprites, const gfx::Vec3& forward, const gfx::Vec3& right);
    gfx::MeshId spriteQuad_ = 0;
    std::vector<SpriteDraw> sprites_;
    void jumpToMeshActor(bool skeletalOnly);
    static std::string idleFor(const std::string& anim);
    int animationSetsMs_ = 0; // how long reading the shared animation sets took
    int jump_ = -1; // the mesh actor last jumped to (G / H)
    std::vector<Mesh> meshes_;
    std::vector<GpuMesh> gpuMeshes_;
    std::vector<MeshDraw> meshActors_;
    bool animate_ = false;
    float animTime_ = 0;
    std::vector<gfx::TextureId> textures_;
    // Textures uploaded, each once: its id (0: unreadable) and size.
    std::pair<gfx::TextureId, std::array<int, 2>> textureOf(const ObjectRef& texture, bool* unreadable = nullptr);
    std::map<std::pair<const Package*, int>, std::pair<gfx::TextureId, std::array<int, 2>>> textureCache_;
    LevelLights lights_; // the level's, for lighting what moves
    MeshLight meshLightAt(const Vec3f& location) const;

    // Playing (Enter): the level live, drawn as its scripts have it.
    void startPlay();
    void stopPlay();
    void updatePlay(float dt);
    void drawLiveActors(const gfx::Vec3& forward, const gfx::Vec3& right);
    void drawPlayHud();
    void drawSubtitles(int bottom);
    void drawGameHud(const gfx::Vec3& forward, const gfx::Vec3& right);
    void drawLiveActor(VmObject* actor, std::vector<SpriteDraw>& sprites, bool evenHidden);
    std::pair<gfx::TextureId, std::array<int, 2>> hudTexture(const ObjectRef& texture, bool seeThrough);
    std::map<std::pair<const Package*, int>, std::pair<gfx::TextureId, std::array<int, 2>>> seeThroughTextures_;
    // A mesh for live actors (by the mesh object), with the animation
    // they play borrowed from the shared sets if it's not its own: an
    // index into meshes_ / gpuMeshes_, -1 if it doesn't read.
    int liveMesh(const ObjectRef& mesh, const std::string& anim);
    std::unique_ptr<LevelPlay> play_;
    std::string playError_;
    std::vector<VmObject*> moverActors_; // each of movers_'s live actor
    std::map<std::pair<const Package*, int>, int> liveMeshes_;
    // Meshes posed at draw time for The Fallen's layered animations: each
    // mesh's poser (none if it has no skeleton) and its parts, and the
    // frames posed for the last frame drawn (kept until the next).
    struct Poser {
        std::unique_ptr<LayeredPoser> poser;
        std::map<std::pair<std::string, bool>, std::vector<bool>> parts;
    };
    std::map<std::pair<const Package*, int>, Poser> posers_;
    std::vector<GpuMesh> posedMeshes_;
    bool drawLayered(const LevelPlay::Look& look, int mesh, const gfx::Mat4& placement,
                     const std::vector<gfx::TextureId>& skins, const MeshLight& light);
    float playClock_ = 0, fov_ = 90;
    LevelPlay::View view_;
    float mouseDx_ = 0, mouseDy_ = 0;
    bool freeCam_ = false; // playing with the camera flown (V), the player standing
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
    bool runScripts_ = true; // the level's scripts started, for how it looks when play begins (P)
    // Lights whose brightness changes, and the lightmaps they reach,
    // redrawn as they do.
    void updateLightmaps(float dt);
    std::vector<gfx::TextureId> lightmapPages_;
    std::vector<LightSource> changingLights_;
    std::vector<float> lightLevels_; // each one's brightness as last drawn
    std::vector<AnimatedLightmap> animatedLightmaps_;
    float lightTime_ = 0;
    // Fire, water, wet, ice and FLC textures, playing.
    void updateAnimatedTextures(float dt);
    std::vector<std::pair<gfx::TextureId, std::unique_ptr<AnimatedTexture>>> animatedTextures_;

    // Fly camera, in the renderer's world: Unreal's axes with Y negated
    // (so right-handed), Z up; yaw 0 looks along +X.
    std::array<float, 3> start_{}, pos_{};
    float startYaw_ = 0, yaw_ = 0, pitch_ = 0;
    bool held_[12] = {}; // by map_viewer.cpp's Held
    bool dragging_ = false, showInfo_ = true;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0, fpsStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
};

} // namespace reluva::unreal
