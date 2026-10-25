// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/geo.h"
#include "engines/newworlds/model_textures.h"
#include "engines/newworlds/rasterizer.h"
#include "engines/newworlds/texture.h"

#include <string>
#include <vector>

namespace reluva::newworlds {

// A model with the textures the name rule gives it (model_textures.h),
// doors' and aprons' included.
struct TexturedModel {
    GeoModel geo;
    std::vector<Texture> textures; // in the order its faces number them; empty where none was found
};

// Reads a model by its full name ("geo/fed/fch.geo") from the first archive
// holding it. False if none does or it doesn't decode.
bool loadTexturedModel(const Archives& archives, const std::vector<TextureFile>& textureFiles, const std::string& name,
                       TexturedModel& out);

// How a model view is lit: its light's colour, and how opaque the model
// is (below 1, see-through, faces blended in drawing order).
struct ModelViewStyle {
    Color light{255, 255, 255};
    float opacity = 1;
};

// The briefings' model views per side, from recordings: the Federation's
// see-through and lavender, the Klingons' solid and dark blue (the Taubat
// temple's grey under a blue light), the Romulans' see-through and
// blue-violet; others plain.
ModelViewStyle briefingModelStyle(char side);

// Draws a model over what's in the target, framed by its size, turned by
// yaw (radians) and seen from pitch above, lit from above: as the
// briefings' model views show one. False if it has no vertices.
bool drawModelView(Surface& target, Rasterizer& raster, const TexturedModel& m, float yaw, float pitch = 0.35f,
                   const ModelViewStyle& style = {});

} // namespace reluva::newworlds
