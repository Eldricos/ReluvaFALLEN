// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/colony.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace reluva::newworlds {

namespace {

struct SiteEntry {
    const char* mission;
    char side;
    int a, b;
    const char* source;
};

// From the play pages (and briefing.cpp's sites, the same places). A mine
// the colony reported exhausted early lies beside it; where two did, the
// middle of them.
constexpr SiteEntry kSites[] = {
    {"F1", 'F', 407, 209, "the start's Tricorder readout"},
    {"J2", 'F', 220, 412, "a mine (the other reading, 196, 436, lies on no deposit)"},
    {"F12", 'F', 212, 196, "a mine"},
    {"J9", 'F', 312, 392, "between two early mines"},
    {"C9", 'F', 260, 300, "a mine"},
    {"K1", 'K', 244, 92, "a mine"},
    {"K2", 'K', 300, 284, "between two early mines"},
    {"J1", 'K', 52, 148, "a mine"},
    {"J2", 'K', 92, 404, "a mine"},
    {"J3", 'K', 204, 172, "a mine"},
    {"K7", 'K', 160, 440, "between two early mines"},
    {"J4", 'K', 340, 180, "the first mine"},
    {"K12", 'K', 84, 252, "a mine"},
    {"K14", 'K', 100, 292, "a mine"},
    {"J9", 'K', 308, 368, "between two early mines"},
    {"C10", 'K', 292, 364, "the first mine"},
    {"R2", 'R', 260, 356, "between two early mines"},
    {"J1", 'R', 111, 442, "the start's Tricorder readout"},
    {"J2", 'R', 166, 184, "the Tricorder readout it kept returning to"},
    {"J3", 'R', 103, 311, "between three early mines"},
    {"J4", 'R', 276, 380, "a mine"},
    {"R9", 'R', 308, 428, "the first mine"},
    {"J6", 'R', 92, 196, "a mine"},
    {"J7", 'R', 200, 324, "between two early mines"},
    {"R12", 'R', 260, 264, "between two early mines"},
    {"R14", 'R', 260, 272, "between two early mines"},
};

struct ModelEntry {
    const char* building;
    const char* fed;
    const char* kln;
    const char* rom;
};

// Models by the scripts' building names. Upper case: matched in recordings
// (play-f1.md and play-k1.md's tables); lower case: by the game's own names
// for its models, the metamodels in misc.cat (fpgs "fedpowgen", ruh_a
// "romsickbay"; colony.md). The Romulan upper-case ones were matched by the
// selection panel's portrait as well as the model's shape (colony.md). The
// Klingons' accommodation block (kab) is the panel's Arsenal. Upgrades stand
// on the building they upgrade.
constexpr ModelEntry kModels[] = {
    {"colony-hub", "FCH", "KCH", "RCH"},
    {"colony-hub-upgrade", "FCH_B", "KCH_B", "RCH_B"},
    {"vehicle-yard", "FVPY_A", "KVY", "RVY"},
    {"construction-yard", "FCY", "KCY", "RCY_A"},
    {"resource-processor", "FRPS_A", "KRPS", "RRPP_A"},
    {"power-generator", "fpgs", "KPGS", "RPS1"},
    {"advanced-generator", "fpgs2", "KPGS2", "RPS2"},
    {"mining-station", "fms", "KMS", "RMS"},
    {"accommodation", "ACCOM", "KAB", "rab"},
    {"arsenal", "", "KAB", ""},
    {"sickbay", "FSB", "KSB_A", "RUH_A"},
    {"science-station", "fss_a", "KSS_A", "RSB_A"},
    {"science-station-upgrade", "fss_b", "kss_b", "RSB_B"},
    {"security-center", "FSC", "ksc", "risb"},
    {"hydroponics-farm", "FHF", "", ""},
    {"livestock-farm", "", "KLF", ""},
    {"clone-farm", "", "", "rcbf"},
    {"storage-facility", "", "KSF", "RSF"},
    {"landing-pad", "landpad", "", ""},
    {"transporter-pad", "", "", "RBCS"},
    {"photon-turret", "fpdt", "", ""},
    {"disruptor-turret", "", "KSG", ""},
    {"static-disruptor", "", "kdws", "RDDS"},
};

constexpr std::pair<const char*, const char*> kUpgrades[] = {
    {"colony-hub-upgrade", "colony-hub"},
    {"science-station-upgrade", "science-station"},
};

// The ground counts as flat for a building if it rises less than this
// across a tile, and near the hub's height if within this of it.
constexpr float kFlat = 1024, kNear = 2048;
// How many tiles from the site the hub may move, and how far around it
// counts as its room.
constexpr int kSearch = 6, kRoom = 3;
// How far the starting pad reaches beyond the hub, in tiles.
constexpr int kPad = 2;
// Pads are level in the recordings: flatter ground than a building needs.
constexpr float kPadFlat = 256;

} // namespace

ColonySite colonySite(std::string_view shortName, char side) {
    for (const SiteEntry& s : kSites)
        if (shortName == s.mission && side == s.side)
            return {s.a, s.b, s.source};
    return {};
}

std::string buildingModel(std::string_view building, char side, bool* matched) {
    for (const ModelEntry& m : kModels) {
        if (building != m.building)
            continue;
        const std::string name = side == 'K' ? m.kln : side == 'R' ? m.rom : m.fed;
        if (name.empty())
            break;
        if (matched)
            *matched = std::isupper(static_cast<unsigned char>(name[0])) != 0;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        const char* folder = side == 'K' ? "kln" : side == 'R' ? "rom" : "fed";
        return "geo/" + std::string(folder) + "/" + lower + ".geo";
    }
    if (matched)
        *matched = false;
    return {};
}

namespace {

constexpr int kTile = 4;            // map positions per tile
constexpr float kTileUnits = 8192;  // model units per tile
constexpr float kGap = 1024;        // kept clear around a building

int ceilDiv(float v, float d) { return std::max(1, int(std::ceil(v / d))); }

} // namespace

bool ColonyLayout::tileFree(int tx, int ty, bool mine) const {
    for (const Rect& r : taken_)
        if (tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h)
            return false;
    return groundFits(tx, ty, mine, kFlat);
}

bool ColonyLayout::groundFits(int tx, int ty, bool mine, float flatness) const {
    if (tx < 0 || ty < 0 || tx >= ground_.width * 2 || ty >= ground_.height * 2)
        return false;
    if ((ground_.deposit(tx / 2, ty / 2) != 0) != mine)
        return false;
    // Flat: its middle and four points near its corners within flatness.
    const int a = ty * kTile + 2, b = tx * kTile + 2;
    float lo = ground_.heightAt(a, b), hi = lo;
    if (std::isnan(lo))
        return false;
    for (const auto& [da, db] : {std::pair{-2, -2}, std::pair{-2, 1}, std::pair{1, -2}, std::pair{1, 1}}) {
        const float h = ground_.heightAt(a + da, b + db);
        if (std::isnan(h))
            return false;
        lo = std::min(lo, h), hi = std::max(hi, h);
    }
    return hi - lo < flatness && (!heightSet_ || std::fabs(lo - hubHeight_) < kNear);
}

bool ColonyLayout::fits(const Rect& r, bool mine) const {
    // A mining station needs its middle on the deposit, not all of it.
    if (mine)
        return tileFree(r.x + r.w / 2, r.y + r.h / 2, true) && [&] {
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x)
                    if (!tileFree(x, y, true) && !tileFree(x, y, false))
                        return false;
            return true;
        }();
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            if (!tileFree(x, y, false))
                return false;
    return true;
}

ColonyLayout::Rect ColonyLayout::footprint(const std::string& model, int tx, int ty) const {
    const auto [w, h] = ground_.size ? ground_.size(model) : std::pair{kTileUnits, kTileUnits};
    return {tx, ty, ceilDiv(w + kGap, kTileUnits), ceilDiv(h + kGap, kTileUnits)};
}

void ColonyLayout::place(std::string_view name, const std::string& model, const Rect& r) {
    taken_.push_back(r);
    buildings_.push_back({std::string(name), model, r.y * kTile + r.h * kTile / 2, r.x * kTile + r.w * kTile / 2});
}

bool ColonyLayout::start(int a, int b) {
    buildings_.clear();
    taken_.clear();
    pad_.clear();
    heightSet_ = false;
    const std::string model = buildingModel("colony-hub", side_);
    if (model.empty() || ground_.width <= 0)
        return false;
    // Of the places near the site, the one with the most free tiles
    // around it (within kRoom), less three for each tile it is from the
    // site.
    const int sx = b / kTile, sy = a / kTile;
    int best = -1;
    Rect bestRect{};
    for (int r = 0; r <= kSearch; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                const Rect rect = footprint(model, sx + dx, sy + dy);
                heightSet_ = false;
                if (!fits(rect, false))
                    continue;
                // Room at this place's height.
                hubHeight_ = ground_.heightAt(rect.y * kTile + 2, rect.x * kTile + 2);
                heightSet_ = true;
                int room = 0;
                for (int y = rect.y - kRoom; y < rect.y + rect.h + kRoom; ++y)
                    for (int x = rect.x - kRoom; x < rect.x + rect.w + kRoom; ++x)
                        room += tileFree(x, y, false);
                const int score = room + 3 * (kSearch - r);
                if (score > best)
                    best = score, bestRect = rect;
            }
    if (best < 0)
        return false;
    hubHeight_ = ground_.heightAt(bestRect.y * kTile + 2, bestRect.x * kTile + 2);
    heightSet_ = true;
    place("colony-hub", model, bestRect);
    // The starting colony's concrete: the ground it can use round the hub.
    for (int y = bestRect.y - kPad; y < bestRect.y + bestRect.h + kPad; ++y)
        for (int x = bestRect.x - kPad; x < bestRect.x + bestRect.w + kPad; ++x)
            if (groundFits(x, y, false, kPadFlat))
                pad_.push_back({x, y});
    return true;
}

bool ColonyLayout::add(std::string_view building) {
    if (buildings_.empty())
        return false;
    const std::string model = buildingModel(building, side_);
    if (model.empty())
        return false;
    for (const auto& [upgrade, base] : kUpgrades) {
        if (building != upgrade)
            continue;
        // On the building it upgrades, once.
        for (const ColonyBuilding& c : buildings_)
            if (c.name == building)
                return false;
        for (size_t i = 0; i < buildings_.size(); ++i)
            if (buildings_[i].name == base) {
                const int a = buildings_[i].a, b = buildings_[i].b;
                buildings_.push_back({std::string(building), model, a, b});
                return true;
            }
        return false;
    }
    const bool mine = building == "mining-station";
    // The free place nearest the hub's middle (by the building's middle).
    const Rect hub = taken_.front();
    const float hx = float(hub.x) + float(hub.w) / 2, hy = float(hub.y) + float(hub.h) / 2;
    const int range = 2 * std::max(ground_.width, ground_.height);
    for (int r = 1; r < range; ++r) {
        std::vector<Rect> ring;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == r)
                    ring.push_back(footprint(model, int(std::floor(hx)) + dx, int(std::floor(hy)) + dy));
        auto distance = [&](const Rect& p) {
            const float x = float(p.x) + float(p.w) / 2 - hx, y = float(p.y) + float(p.h) / 2 - hy;
            return x * x + y * y;
        };
        std::stable_sort(ring.begin(), ring.end(), [&](const Rect& p, const Rect& q) { return distance(p) < distance(q); });
        for (const Rect& rect : ring)
            if (fits(rect, mine)) {
                place(building, model, rect);
                return true;
            }
    }
    return false;
}

bool ColonyLayout::spotRect(std::string_view building, int a, int b, std::string& model, Rect& r) const {
    model = buildingModel(building, side_);
    for (const auto& [upgrade, base] : kUpgrades)
        if (building == upgrade)
            return false;
    if (model.empty() || buildings_.empty())
        return false;
    // The footprint's corner such that its middle is nearest the point.
    r = footprint(model, 0, 0);
    r.x = int(std::lround(float(b) / kTile - float(r.w) / 2));
    r.y = int(std::lround(float(a) / kTile - float(r.h) / 2));
    return true;
}

bool ColonyLayout::spotAt(std::string_view building, int a, int b, int& ma, int& mb, bool& fit) const {
    std::string model;
    Rect r{};
    if (!spotRect(building, a, b, model, r))
        return false;
    ma = r.y * kTile + r.h * kTile / 2, mb = r.x * kTile + r.w * kTile / 2;
    fit = fits(r, building == "mining-station");
    return true;
}

bool ColonyLayout::addAt(std::string_view building, int a, int b) {
    std::string model;
    Rect r{};
    if (!spotRect(building, a, b, model, r) || !fits(r, building == "mining-station"))
        return false;
    place(building, model, r);
    return true;
}

} // namespace reluva::newworlds
