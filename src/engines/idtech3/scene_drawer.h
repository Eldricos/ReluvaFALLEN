// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/eliteforce/scene.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/shader_cache.h"
#include "engines/idtech3/vm_services.h"
#include "engines/idtech3/world_renderer.h"

#include "common/gfx3d.h"

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// refdef_t's flag for views without the level (the HUD's heads, the
// menus' models).
constexpr int32_t kNoWorldModel = 1;

// refEntity_t: Quake III's 140 bytes, then 20 of Elite Force's own (see
// eliteforce-syscalls.md). Its type (0: a model), render flags, model,
// lighting origin, axes (forward, left, up), origin, frame, old frame
// and how far back towards it, custom skin.
namespace refent {
constexpr size_t Type = 0, Flags = 4, Model = 8, LightingOrigin = 12, Axes = 28, Origin = 68, Frame = 80,
                 OldFrame = 96, BackLerp = 100, SkinNum = 104, CustomSkin = 108;
constexpr int32_t TypeModel = 0;
// Elite Force's effect types, read from what its client game adds when
// firing (see eliteforce-syscalls.md): a sprite facing the camera (1:
// radius at 136, rotation in degrees at 132; its axes are left as they
// were cleared), a quad in the entity's own axes (2: left and up, radius
// at 136; the first axis faces out of what it lies on), a line from the
// origin to the old origin (width at 132, the texture repeated along it as often as 140
// says), and one tapering from the width at 132 to the one at 136.
// More, from the other weapons: a cubic Bezier curve from the origin
// through control points at 136 and 148 to the old origin (12), a ring
// or cone round the first axis (13: radii at 132 and 136, height at 140,
// 144 the texture's repeats round it), and electricity from the origin to
// the old origin (14: width 132, jitter 136, texture scale 140).
constexpr int32_t TypeSprite = 1, TypeQuad = 2, TypeLine = 9, TypeTaperedLine = 11, TypeCurve = 12,
                  TypeCylinder = 13, TypeBolt = 14;
// A mirror's or portal's camera (game-run -cgame on a map with a mirror:
// origin and old origin the same, at the mirror; axes as cleared).
constexpr int32_t TypePortal = 8;
constexpr size_t OldOrigin = 84, CustomShader = 112, Rgba = 116, ShaderTime = 128, Rotation = 132, Radius = 136,
                 Width = 132, EndWidth = 136, Repeat = 140, Control1 = 136, Control2 = 148, Jitter = 136,
                 CylinderRepeat = 144;
// Render flags (Quake III's): drawn only in mirrors and portals (the
// player's own body); drawn in front of everything else, its depth kept
// to the nearest 0.3 of the range (the player's weapon and its flash);
// lit from the lighting origin.
constexpr int32_t ThirdPerson = 2, FirstPerson = 4, DepthHack = 8, UseLightingOrigin = 0x80;
constexpr float kDepthHackRange = 0.3f;
} // namespace refent

// Reading the structures the programs hand over, kept as bytes.
int32_t readInt(const std::vector<uint8_t>& bytes, size_t at);
float readFloat(const std::vector<uint8_t>& bytes, size_t at);
gfx::Vec3 readVec(const std::vector<uint8_t>& bytes, size_t at);

// Draws what a game program (the client game, the menus) hands over:
// its 2D pictures, and each RenderScene's view of its entities, polygons
// and lights, the level with it when the view has it.
class SceneDrawer {
public:
    SceneDrawer(gfx::Renderer3D& renderer, ShaderCache& shaders, ModelCache& models)
        : r_(renderer), shaders_(shaders), models_(models) {}

    // The program's handles, by which it names models, skins and shaders.
    struct Handles {
        const HandleTable* models = nullptr;
        const HandleTable* skins = nullptr;
        const HandleTable* shaders = nullptr;
    };
    void setHandles(const Handles& handles) { handles_ = handles; }
    // Forgets what the handles were loaded as (a new program, or the
    // caches cleared).
    void reset();

    // Pictures [first, last) over the frame, in the program's screen
    // (screenW x screenH, scaled to the frame); `time` in seconds.
    void drawPictures(const std::vector<eliteforce::Picture>& pictures, size_t first, size_t last, int screenW,
                      int screenH, float time);

    // `view` of `scene`, its rectangle (refdef_t's x, y, width, height in
    // the program's screen) scaled to the frame. With the level (`world`,
    // `grid`) unless the view says it has none; then lit as the original
    // lights models without one. After beginFrame(); `firstView` says
    // whether depths are already clear. `time` in seconds is the shaders'
    // clock; dynamic lights only if `dynamicLights`.
    void drawView(const eliteforce::Scene& scene, const eliteforce::View& view, int screenW, int screenH,
                  float time, bool firstView, WorldRenderer* world, const LightGrid* grid, bool dynamicLights);

    // What the last view drew, for the info line.
    const gfx::Vec3& eye() const { return eye_; }
    size_t placedCount() const { return placed_.size(); }
    size_t effectCount() const { return effects_.size(); }
    size_t lightCount() const { return lights_.size(); }
    int otherEntities() const { return otherEntities_; }
    int portalViews() const { return portalViews_; }
    const std::set<std::string>& unloadable() const { return unloadable_; }

private:
    void placeEntities(const eliteforce::Scene& scene, const eliteforce::View& view, const LightGrid& grid,
                       float time);
    void addEffect(const std::vector<uint8_t>& entity, const gfx::Vec3& left, const gfx::Vec3& up, float time);
    void drawEffects(float time);
    static std::array<float, 4> entityColor(const std::vector<uint8_t>& entity);
    static float shaderTime(const std::vector<uint8_t>& entity, float time);
    int modelId(int32_t handle);
    // The level of detail to draw `model` at, where it is in the view.
    int lodLevel(int model, int frame, const gfx::Vec3& origin) const;
    int material(std::vector<int>& cache, int32_t handle, Lighting lighting);
    void addDynamicLights(PointLight& light, const gfx::Vec3& at) const;
    static const std::string& name(const HandleTable* table, int32_t handle);

    gfx::Renderer3D& r_;
    ShaderCache& shaders_;
    ModelCache& models_;
    Handles handles_;
    std::map<int32_t, int> modelIds_; // the program's model handle -> models_ id (-1: can't load)
    std::vector<int> pictureMaterials_, effectMaterials_; // by shader handle; -2 not made yet
    // This view's model entities, ready to draw.
    struct Placed {
        int model = -1, frame = 0, oldFrame = 0;
        float backLerp = 0;
        gfx::Mat4 transform{};
        PointLight light;
        const Skin* skin = nullptr;
        std::string shader; // in place of the model's own, if set
        std::array<float, 4> color{1, 1, 1, 1};
        float shaderTime = 0;   // seconds: when its shader animations start
        bool depthHack = false; // kept in front (the player's weapon)
        bool thirdPerson = false, firstPerson = false; // only through mirrors; not through them
    };
    std::vector<Placed> placed_;
    std::vector<std::pair<int, gfx::Mat4>> brushes_; // the level's models (*N) placed this view
    // Mirrors' and portals' cameras this view.
    struct PortalEntity {
        gfx::Vec3 origin{}, oldOrigin{};
        std::array<gfx::Vec3, 3> axes{};
        int32_t frame = 0, oldFrame = 0, skinNum = 0;
    };
    std::vector<PortalEntity> portalEntities_;
    std::vector<uint8_t> mainRefdef_;
    int portalViews_ = 0; // drawn so far, for the info line
    void drawLayers(float time, WorldRenderer* world, bool throughPortal);
    void drawPortalView(WorldRenderer& world, const std::array<uint8_t, 32>& areaMask, const gfx::Vec3& left,
                        float fovY, float aspect, const gfx::Mat4& mainViewProj, float time);

    // This view's effects (sprites, beams, marks): in one mesh, each with
    // the program's shader handle.
    struct Effect {
        int32_t shader = 0;
        uint32_t firstIndex = 0, indexCount = 0;
        std::array<float, 4> color{1, 1, 1, 1};
        float shaderTime = 0;
        bool depthHack = false;
    };
    std::vector<Effect> effects_;
    std::vector<gfx::Vertex> effectVertices_;
    std::vector<uint32_t> effectIndices_;
    std::minstd_rand effectRandom_;         // electricity's jitter
    std::vector<gfx::DynamicLight> lights_; // this view's
    gfx::Vec3 eye_{}, forward_{1, 0, 0};
    float focal_ = 1; // the view's 1 / tan(half its height's angle)
    int otherEntities_ = 0;            // entities not drawn (kinds not done yet)
    std::set<std::string> unloadable_; // models this view couldn't draw
};

} // namespace reluva::idtech3
