// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/model_textures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>

namespace reluva::newworlds {

void splitName(const std::string& name, std::string& folder, std::string& stem, std::string& ext) {
    const size_t slash = name.rfind('/');
    const size_t dot = name.rfind('.');
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    stem = name.substr(start, (dot == std::string::npos || dot < start ? name.size() : dot) - start);
    ext = dot == std::string::npos || dot < start ? std::string() : name.substr(dot + 1);
    folder.clear();
    if (slash != std::string::npos && slash > 0) {
        const size_t prev = name.rfind('/', slash - 1);
        const size_t from = prev == std::string::npos ? 0 : prev + 1;
        folder = name.substr(from, slash - from);
    }
}

std::vector<TextureFile> listTextureFiles(const Archives& archives) {
    std::vector<TextureFile> out;
    for (size_t a = 0; a < archives.size(); ++a) {
        const auto& entries = archives[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].tag != cattag::Texture)
                continue;
            TextureFile t;
            std::string ext;
            splitName(entries[i].name, t.folder, t.stem, ext);
            if (ext == "lbp" || ext == "mp0") {
                t.ref = {a, i};
                out.push_back(std::move(t));
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const TextureFile& x, const TextureFile& y) { return x.stem < y.stem; });
    return out;
}

namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// "landpd" in "landpad": the letters in order, starting the same way.
bool abbreviates(const std::string& shortName, const std::string& full) {
    if (shortName.size() < 3 || full.size() <= shortName.size() || full.compare(0, 2, shortName, 0, 2) != 0)
        return false;
    size_t i = 0;
    for (char c : full)
        if (i < shortName.size() && c == shortName[i])
            ++i;
    return i == shortName.size();
}

} // namespace

// A model's own textures are named after it: k_bop uses k_bop_a, k_bop_b and
// k_bop_g. A variant such as k_bop_l with no textures of its own uses those
// of the name without the suffix (except the flat _s outlines), and a
// shortened name (landpd_d) those of a texture it abbreviates (landpad);
// failing that, names differing by a number or a shortening (fapc2: fapc).
// Textures in the model's own folder (kln for geo/kln/k_bop.geo) are
// preferred, and _s textures belong to the _s models only.
//
// The faces number the model's own textures from 0 in name order, the _g
// texture after its base (k_bop: 0 k_bop_a, 1 k_bop_b, 2 k_bop_g; frps_a:
// 0 frps, 1 frps_g). Later numbers are shared textures with other names
// (doors: see addDoorTextures).
std::vector<const TextureFile*> findModelTextures(const std::string& modelName,
                                                  const std::vector<TextureFile>& textures, size_t needed) {
    std::string folder, base, ext;
    splitName(modelName, folder, base, ext);
    const bool outline = endsWith(base, "_s");
    const bool lowDetail = endsWith(base, "_l");
    std::vector<const TextureFile*> found;
    auto collect = [&](auto&& matches) {
        for (int sameFolder = 1; sameFolder >= 0 && found.empty(); --sameFolder)
            for (const TextureFile& t : textures)
                if ((!sameFolder || t.folder == folder) && (outline || !endsWith(t.stem, "_s")) && matches(t))
                    found.push_back(&t);
    };
    while (true) {
        collect([&](const TextureFile& t) {
            return t.stem == base || (t.stem.size() > base.size() + 1 && t.stem.compare(0, base.size(), base) == 0 &&
                                      t.stem[base.size()] == '_' &&
                                      t.stem.find('_', base.size() + 1) == std::string::npos);
        });
        // Otherwise drop a short suffix (_l, _d, _ad, ...) and try again; but
        // not _s, the flat outlines, which don't use the full model's textures.
        const size_t us = base.rfind('_');
        if (!found.empty() || us == std::string::npos || base.size() - us > 3 || base.compare(us, 3, "_s") == 0)
            break;
        base.resize(us);
    }
    if (found.empty() && !outline) // a shortened name: landpd -> landpad, its textures too
        collect([&](const TextureFile& t) {
            const size_t us = t.stem.find('_');
            return t.folder == folder && abbreviates(base, t.stem.substr(0, us));
        });
    // Or names that differ by a number or a shortening: fapc2 uses fapc,
    // rsv rsv2, r_bop r_bop5-7, fconbee fconb and fconb_a.
    auto stripDigits = [](std::string n) {
        while (!n.empty() && std::isdigit(static_cast<unsigned char>(n.back())))
            n.pop_back();
        return n;
    };
    if (found.empty() && !outline)
        collect([&](const TextureFile& t) {
            if (t.folder != folder)
                return false;
            // The texture's name without a _suffix (for r_bop5: the whole name).
            std::string name = t.stem;
            if (const size_t us = name.rfind('_'); us != std::string::npos && us + 3 >= name.size() && us > 0)
                name.resize(us);
            const std::string a = stripDigits(base), b = stripDigits(name);
            return !b.empty() && (a == b || (b.size() >= 4 && a.size() > b.size() && a.compare(0, b.size(), b) == 0));
        });

    // Weapon effects name their textures otherwise: beam segments
    // (mb_mid, mb_midh, mb_midt, mb_midht) use the side's beam (m_beam;
    // the Federation's ph_mid* phaser), a muzzle flash (mmuzzle, tmuzzle)
    // the folder's muzzle texture starting with the same letter (mbmuzzle,
    // t_muzzle), a shield its ripple (fripple), and kdisrupt disrupt: a
    // texture named inside the model's.
    if (found.empty() && !outline) {
        const size_t mid = base.find("_mid");
        std::string want;
        if (mid != std::string::npos && base.compare(0, 2, "ph") == 0)
            want = "phaser";
        else if (mid != std::string::npos && mid >= 2 && base[mid - 1] == 'b')
            want = base.substr(0, mid - 1) + "_beam";
        collect([&](const TextureFile& t) {
            if (t.folder != folder)
                return false;
            if (!want.empty())
                return t.stem == want;
            if (base.find("muzzle") != std::string::npos)
                return t.stem.find("muzzl") != std::string::npos && t.stem[0] == base[0];
            if (base.find("shield") != std::string::npos) // fshield: fripple, the shield's ripple
                return t.stem.find("ripple") != std::string::npos && t.stem[0] == base[0];
            return t.stem.size() >= 5 && base.size() > t.stem.size() && base.find(t.stem) != std::string::npos;
        });
    }
    // A low-detail model found its family's textures under another name
    // (fapc2_l: fapc, fapc_g, fapc_l): its own is the _l one.
    if (lowDetail && !endsWith(base, "_l") &&
        std::any_of(found.begin(), found.end(), [](const TextureFile* t) { return endsWith(t->stem, "_l"); }))
        found.erase(std::remove_if(found.begin(), found.end(), [](const TextureFile* t) { return !endsWith(t->stem, "_l"); }),
                    found.end());
    // One of each name (some are stored both as .lbp and as a mipmap set),
    // in name order (the list is sorted by stem).
    // A texture named <model>_door (mch_door) is the model's door, which
    // addDoorTextures places; it doesn't take a number in the name order.
    std::vector<const TextureFile*> own;
    for (const TextureFile* t : found)
        if (!endsWith(t->stem, "_door") &&
            std::none_of(own.begin(), own.end(), [&](const TextureFile* x) { return x->stem == t->stem; }))
            own.push_back(t);
    std::vector<const TextureFile*> out(std::max(needed, own.size()), nullptr);
    std::copy(own.begin(), own.end(), out.begin());
    return out;
}

bool isDoorSlot(const GeoModel& geo, uint32_t slot) {
    unsigned faces = 0;
    for (const auto& f : geo.faces) {
        if (f.corners.back().extra[4] != slot)
            continue;
        if (++faces > 2)
            return false;
        // Upright, and showing (nearly) the whole texture.
        const auto& vs = geo.parts[f.part].vertices;
        float nx = 0, ny = 0, nz = 0, u0 = 1, u1 = 0, v0 = 1, v1 = 0;
        for (size_t i = 0; i < f.corners.size(); ++i) {
            const auto& p = vs[f.corners[i].vertex];
            const auto& q = vs[f.corners[(i + 1) % f.corners.size()].vertex];
            nx += (p.y - q.y) * (p.z + q.z);
            ny += (p.z - q.z) * (p.x + q.x);
            nz += (p.x - q.x) * (p.y + q.y);
            u0 = std::min(u0, f.corners[i].u), u1 = std::max(u1, f.corners[i].u);
            v0 = std::min(v0, f.corners[i].v), v1 = std::max(v1, f.corners[i].v);
        }
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        // Doors can lean (fch's 20 degrees, kab's 40) but don't face up, and
        // are at least 500 units across (the 250-unit faces showing a whole
        // texture in fsc and fss_a are something else: lights, signs).
        if (len <= 0 || std::fabs(nz) / len > 0.7f || len / 2 < 250000 || u1 - u0 < 0.9f || v1 - v0 < 0.6f)
            return false;
    }
    return faces > 0;
}

void addDoorTextures(const GeoModel& geo, const std::string& modelName, const std::vector<TextureFile>& textures,
                     std::vector<const TextureFile*>& slots) {
    std::string folder, base, ext;
    splitName(modelName, folder, base, ext);
    // The model's own door (mch_door), else its side's single closed door
    // (the Federation's and Klingons'; the other door textures are 4 x 4
    // sheets of opening frames, and the Romulans and natives have only
    // those, so their doors stay unknown).
    const TextureFile* door = nullptr;
    for (const TextureFile& t : textures)
        if (t.folder == folder && (t.stem == "door3" || t.stem == "kcdoor"))
            door = &t;
    for (const TextureFile& t : textures)
        if (t.folder == folder && t.stem == base + "_door")
            door = &t;
    if (!door)
        return;
    for (size_t i = 1; i < slots.size(); ++i) // slot 0 is always the model's main texture
        if (isDoorSlot(geo, uint32_t(i)))
            slots[i] = door;
}

bool isPadSlot(const GeoModel& geo, uint32_t slot) {
    unsigned faces = 0;
    for (const auto& f : geo.faces) {
        if (f.corners.back().extra[4] != slot)
            continue;
        if (++faces > 2)
            return false;
        const auto& vs = geo.parts[f.part].vertices;
        float nx = 0, ny = 0, nz = 0;
        for (size_t i = 0; i < f.corners.size(); ++i) {
            const auto& p = vs[f.corners[i].vertex];
            const auto& q = vs[f.corners[(i + 1) % f.corners.size()].vertex];
            nx += (p.y - q.y) * (p.z + q.z);
            ny += (p.z - q.z) * (p.x + q.x);
            nz += (p.x - q.x) * (p.y + q.y);
        }
        // Front faces are clockwise seen from the front, so an up-facing
        // face's Newell normal points down.
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len <= 0 || -nz / len < 0.9f)
            return false;
    }
    return faces > 0;
}

void addPadTextures(const GeoModel& geo, const std::string& modelName, const std::vector<TextureFile>& textures,
                    std::vector<const TextureFile*>& slots) {
    std::string folder, base, ext;
    splitName(modelName, folder, base, ext);
    const TextureFile* pad = nullptr;
    for (const TextureFile& t : textures)
        if (t.folder == folder && t.stem == "pad")
            pad = &t;
    if (!pad)
        return;
    for (size_t i = 1; i < slots.size(); ++i)
        if (!slots[i] && isPadSlot(geo, uint32_t(i)))
            slots[i] = pad;
}

} // namespace reluva::newworlds
