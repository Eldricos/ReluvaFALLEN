// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/shader_cache.h"

#include "common/archive.h"
#include "common/gfx3d.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

using Skin = std::map<std::string, std::string>; // surface name (lower case) -> shader

// Angles in degrees as the games give them ("pitch yaw roll", pitch down
// positive) to the axes they turn a model to: forward, left, up.
std::array<Vec3, 3> anglesToAxes(const Vec3& angles);
// A placement: the axes as columns, then the origin.
gfx::Mat4 placement(const Vec3& origin, const std::array<Vec3, 3>& axes);
// Where a tag puts what's attached to it, in the tagged model's space.
gfx::Mat4 tagPlacement(const Md3Tag& tag);

// MD3 models loaded for the GPU, each once: one mesh holding every frame
// of every surface, drawn through a ShaderCache.
class ModelCache {
public:
    ModelCache(gfx::Renderer3D& renderer, ShaderCache& shaders, const SearchSet& files)
        : r_(renderer), shaders_(shaders), files_(files) {}
    ~ModelCache() { clear(); }
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    void clear();
    // The model at `path`, loaded on first use; -1 (with the reason in
    // `error`, if given) when it can't be.
    int load(const std::string& path, std::string* error = nullptr);
    // Models' levels of detail: an .md3's lower-detail versions beside it
    // (name_1.md3, name_2.md3), an .mdr's own. Each is loaded with the
    // model; level 0 is the model itself.
    int lodCount(int id) const { return 1 + int(models_[size_t(id)].lods.size()); }
    // The id to draw for `level` (past the last, the last).
    int lod(int id, int level) const {
        const auto& lods = models_[size_t(id)].lods;
        return level <= 0 || lods.empty() ? id : lods[size_t(std::min(level, int(lods.size()))) - 1];
    }
    // How far the model reaches from its origin in `frame`, as the
    // original measures it for choosing a level: the length of the
    // farthest corner of its box.
    float radius(int id, int frame) const;
    // The model detail setting (r_lodBias): how many levels further down
    // to draw than the distance says.
    void setLodBias(int bias) { lodBias_ = bias; }
    int lodBias() const { return lodBias_; }
    const Md3Model& model(int id) const { return models_[size_t(id)].md3; }
    // A skin file's contents, read on first use (empty if missing).
    const Skin& skin(const std::string& path);

    // Draws `frame` (wrapped to the model's frames) placed by `transform`
    // and lit by `light`; a skin, if given, picks surfaces' shaders, and a
    // shader named in `shader` replaces them all. `entity`: the colour
    // rgbGen and alphaGen entity ask for. `oldFrame` and `backLerp`: the
    // frame it's on its way from and how far back towards it (0: none).
    // `sortMin`..`sortMax`: only surfaces whose shader sorts in that range,
    // so translucent ones can be drawn after the rest of a scene.
    void draw(int id, int frame, const gfx::Mat4& transform, const PointLight& light, float time,
              const Skin* skin = nullptr, float sortMin = 0, float sortMax = 1e9f, const std::string& shader = {},
              const std::array<float, 4>* entity = nullptr, int oldFrame = 0, float backLerp = 0);

private:
    int lodBias_ = 0;
    struct Surface {
        uint32_t firstIndex = 0, indexCount = 0;
        int vertexCount = 0;
        int material = -1; // from the model's own shader name, once first needed
    };
    struct Loaded {
        Md3Model md3;
        gfx::MeshId mesh = 0;
        std::vector<Surface> surfaces;
        std::vector<int> lods; // the lower levels' ids, from level 1
    };
    // One level of a model, from `data`; -1 with the reason in `error`.
    int add(const std::string& path, const std::vector<uint8_t>& data, int level, std::string* error);

    gfx::Renderer3D& r_;
    ShaderCache& shaders_;
    const SearchSet& files_;
    std::vector<Loaded> models_;
    std::unordered_map<std::string, int> byPath_; // normalized; -1 for failures
    std::unordered_map<std::string, Skin> skins_;
};

} // namespace reluva::idtech3
