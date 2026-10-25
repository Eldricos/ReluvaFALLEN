// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/geo.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::newworlds {

using Archives = std::vector<std::unique_ptr<CatArchive>>;

// Where an item lives: archive index and entry index.
struct EntryRef {
    size_t archive, entry;
};

// A full-size texture: an .lbp, or level 0 (.mp0) of a mipmap set.
struct TextureFile {
    std::string stem, folder; // "k_bop_a", "kln" for lbp/txr/kln/k_bop_a.mp0
    EntryRef ref;
};

// "lbp/txr/kln/k_bop_a.mp0" -> folder "kln", stem "k_bop_a", extension "mp0".
void splitName(const std::string& name, std::string& folder, std::string& stem, std::string& ext);

// Every full-size texture in the archives, sorted by stem.
std::vector<TextureFile> listTextureFiles(const Archives& archives);

// A model's textures, in the order its faces number them; `needed` is how
// many the faces number (the highest number + 1). Slots the rule can't fill
// (shared textures with other names) are null. See
// docs/formats/newworlds/geo.md#textures for the rule.
std::vector<const TextureFile*> findModelTextures(const std::string& modelName,
                                                  const std::vector<TextureFile>& textures, size_t needed);

// Whether a texture number is a building's door: one or two faces that
// don't face up, each at least 500 units across and showing (nearly) the
// whole texture. Nearly every building has one, as
// its last number or near it (fch, accom, landpad, kab, kief, ksc, ...).
bool isDoorSlot(const GeoModel& geo, uint32_t slot);

// Gives a model's door numbers its own door texture (mch_door), else its
// side's closed door (door3 in lbp/txr/fed, kcdoor in lbp/txr/kln), replacing
// what the name rule put there (fch's second number is its door, not fch_g).
void addDoorTextures(const GeoModel& geo, const std::string& modelName, const std::vector<TextureFile>& textures,
                     std::vector<const TextureFile*>& slots);

// Whether a texture number is a building's apron: one or two up-facing
// faces. The Federation's are pad, whose alpha layer holds four panels of
// hazard stripes, one per quarter; the faces show one quarter or the whole
// (the portraits in fedgui.cat show the striped aprons).
bool isPadSlot(const GeoModel& geo, uint32_t slot);

// Gives a model's unfilled apron numbers its side's pad texture (only the
// Federation has one).
void addPadTextures(const GeoModel& geo, const std::string& modelName, const std::vector<TextureFile>& textures,
                    std::vector<const TextureFile*>& slots);

} // namespace reluva::newworlds
