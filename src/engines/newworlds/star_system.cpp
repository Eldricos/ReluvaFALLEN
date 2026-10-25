// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/star_system.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace reluva::newworlds {

namespace {

std::string lettersOnly(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (std::isalpha(static_cast<unsigned char>(c)))
            out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Faces at least this big belong to the starfield round the system.
constexpr float kBackdropFace = 1500;

} // namespace

std::string starSystemName(const StringTable& table, const std::vector<std::string>& systems) {
    std::map<std::string, int> counts;
    for (const StringTable::String& s : table.strings()) {
        const std::string text = lettersOnly(s.text);
        for (const std::string& name : systems) {
            const std::string want = lettersOnly(name);
            for (size_t at = text.find(want); !want.empty() && at != std::string::npos; at = text.find(want, at + 1))
                ++counts[name];
        }
    }
    std::string best;
    int most = 0;
    for (const std::string& name : systems) // ties: the first listed
        if (counts[name] > most)
            best = name, most = counts[name];
    return best;
}

StarSystemLayout readStarSystem(const GeoModel& model) {
    StarSystemLayout out;
    out.faceSlot.assign(model.faces.size(), -1);
    out.faceBody.assign(model.faces.size(), -1);
    struct Info {
        GeoModel::Vec3 centre{0, 0, 0}, lo{0, 0, 0}, hi{0, 0, 0};
        float size = 0;
        int slot = 0;
        int cluster = -1;
    };
    std::vector<Info> info(model.faces.size());
    for (size_t i = 0; i < model.faces.size(); ++i) {
        const GeoModel::Face& f = model.faces[i];
        if (f.corners.empty() || f.part >= model.parts.size())
            continue;
        const auto& verts = model.parts[f.part].vertices;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}, sum[3] = {0, 0, 0};
        for (const GeoModel::Corner& c : f.corners) {
            if (c.vertex >= verts.size())
                continue;
            const float p[3] = {verts[c.vertex].x, verts[c.vertex].y, verts[c.vertex].z};
            for (int k = 0; k < 3; ++k)
                lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]), sum[k] += p[k];
        }
        const float n = float(f.corners.size());
        info[i] = {{sum[0] / n, sum[1] / n, sum[2] / n},
                   {lo[0], lo[1], lo[2]},
                   {hi[0], hi[1], hi[2]},
                   std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}),
                   int(f.corners.back().extra[4])};
    }
    // The starfield: the big faces' texture numbers other than 0, in order;
    // its big faces numbered 0 (the cylinder's ends) take the first.
    for (size_t i = 0; i < info.size(); ++i)
        if (info[i].size >= kBackdropFace && info[i].slot != 0)
            out.backdropSlots.push_back(info[i].slot);
    std::sort(out.backdropSlots.begin(), out.backdropSlots.end());
    out.backdropSlots.erase(std::unique(out.backdropSlots.begin(), out.backdropSlots.end()), out.backdropSlots.end());
    for (size_t i = 0; i < info.size(); ++i)
        if (info[i].size >= kBackdropFace)
            out.faceSlot[i] = info[i].slot != 0 ? info[i].slot
                              : out.backdropSlots.empty() ? -1
                                                          : out.backdropSlots.front();
    // The rest in bodies: faces near each other.
    struct Cluster {
        GeoModel::Vec3 centre;
        float size = 0;
        std::map<int, int> slots; // texture number: faces
        std::vector<size_t> faces;
    };
    std::vector<Cluster> clusters;
    for (size_t i = 0; i < info.size(); ++i) {
        if (info[i].size >= kBackdropFace || model.faces[i].corners.empty())
            continue;
        const GeoModel::Vec3& c = info[i].centre;
        Cluster* home = nullptr;
        for (Cluster& k : clusters)
            if (std::hypot(c.x - k.centre.x, c.y - k.centre.y, c.z - k.centre.z) < k.size + 400) {
                home = &k;
                break;
            }
        if (!home) {
            clusters.push_back({c, 0, {}, {}});
            home = &clusters.back();
        }
        home->size = std::max(home->size, info[i].size);
        ++home->slots[info[i].slot];
        home->faces.push_back(i);
        info[i].cluster = int(home - clusters.data());
    }
    for (const Cluster& k : clusters) {
        StarSystemLayout::Body b;
        // Its middle: the middle of all its faces.
        GeoModel::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const size_t i : k.faces) {
            lo = {std::min(lo.x, info[i].lo.x), std::min(lo.y, info[i].lo.y), std::min(lo.z, info[i].lo.z)};
            hi = {std::max(hi.x, info[i].hi.x), std::max(hi.y, info[i].hi.y), std::max(hi.z, info[i].hi.z)};
        }
        b.centre = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
        b.size = k.size;
        int most = 0;
        for (const auto& [slot, n] : k.slots) {
            if (slot == 0)
                continue;
            if (n == 1)
                b.ringSlot = slot;
            else if (n > most)
                b.slot = slot, most = n;
        }
        b.star = b.slot < 0;
        if (b.star)
            b.slot = 0;
        // Each face its body's: a back face (numbered 0) the quad it backs,
        // the one in the same plane and of the same size.
        for (const size_t i : k.faces) {
            out.faceBody[i] = int(out.bodies.size());
            if (b.star || info[i].slot != 0) {
                out.faceSlot[i] = info[i].slot;
                continue;
            }
            float bestErr = 1e30f;
            for (const size_t j : k.faces) {
                if (info[j].slot == 0)
                    continue;
                const float err = std::fabs(info[j].centre.z - info[i].centre.z) + std::fabs(info[j].size - info[i].size);
                if (err < bestErr)
                    bestErr = err, out.faceSlot[i] = info[j].slot;
            }
        }
        out.bodies.push_back(b);
    }
    return out;
}

std::vector<std::string> starSystemTextures(const StarSystemLayout& layout, std::string_view planetWorld,
                                            std::string_view system) {
    int slots = 0;
    for (const int s : layout.faceSlot)
        slots = std::max(slots, s + 1);
    std::vector<std::string> out(static_cast<size_t>(slots));
    for (size_t i = 0; i < layout.backdropSlots.size(); ++i)
        if (size_t(layout.backdropSlots[i]) < out.size())
            out[size_t(layout.backdropSlots[i])] = "stars" + std::to_string(std::min<size_t>(i, 3) + 1);
    if (!out.empty())
        out[0] = system == "bezouk" || system == "sakuntul" ? "sun3" : "sun"; // yellow, but red in R1, R12, R14
    // Planets in texture-number order: the mission's world first.
    std::vector<const StarSystemLayout::Body*> planets;
    for (const StarSystemLayout::Body& b : layout.bodies)
        if (!b.star && size_t(b.slot) < out.size())
            planets.push_back(&b);
    std::sort(planets.begin(), planets.end(), [](const auto* a, const auto* b) { return a->slot < b->slot; });
    static constexpr std::pair<const char*, const char*> kRinged[] = {
        {"red", "red_r"}, {"alien3", "alien3_r"}, {"alien7", "alien7_r"}};
    // Durac's recording shows red and magenta planets beside its own.
    static constexpr const char* kOthers[] = {"red",    "alien2",  "desert", "alien4", "alien6", "volcanic", "alpine2",
                                              "alien1", "alien5",  "volcani1", "alpine", "ice",  "alien3",   "alien7"};
    auto used = [&](std::string_view name) { return std::find(out.begin(), out.end(), name) != out.end(); };
    size_t ringed = 0;
    for (const auto* p : planets) {
        std::string& tex = out[size_t(p->slot)];
        if (!tex.empty())
            continue; // several bodies share a number
        if (p->ringSlot >= 0 && size_t(p->ringSlot) < out.size()) {
            const auto& [planet, ring] = kRinged[ringed++ % std::size(kRinged)];
            tex = planet;
            out[size_t(p->ringSlot)] = ring;
        } else if (p == planets.front() && !planetWorld.empty()) {
            tex = std::string(planetWorld);
        } else {
            for (const char* name : kOthers)
                if (!used(name)) {
                    tex = name;
                    break;
                }
        }
    }
    return out;
}

std::string planetPicture(std::string_view archiveName) {
    static constexpr std::pair<const char*, const char*> kWorlds[] = {
        {"icewrld", "ice"}, {"deswrld", "desert"}, {"alpwrld", "alpine"},
        {"redwrld", "red"}, {"vlcwrld", "volcanic"}, {"alnwrld", "alien4"}};
    std::string lower;
    for (const char c : archiveName)
        lower += char(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& [world, picture] : kWorlds)
        if (lower.find(world) != std::string::npos)
            return picture;
    return {};
}

int missionBody(const StarSystemLayout& layout) {
    int best = -1, star = -1;
    for (size_t i = 0; i < layout.bodies.size(); ++i) {
        const auto& b = layout.bodies[i];
        if (b.star)
            star = star < 0 ? int(i) : star;
        else if (best < 0 || b.slot < layout.bodies[size_t(best)].slot)
            best = int(i);
    }
    return best >= 0 ? best : star;
}

} // namespace reluva::newworlds
