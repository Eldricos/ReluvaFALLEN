// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/model_pose.h"

#include "engines/newworlds/cat_archive.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace reluva::newworlds {

Metamodels::Metamodels(const Archives& archives) : archives_(archives) {
    for (size_t a = 0; a < archives_.size(); ++a) {
        const auto& entries = archives_[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].tag == cattag::Mdl) {
                std::string folder, stem, ext;
                splitName(entries[i].name, folder, stem, ext);
                metamodels_.emplace(folder + "/" + stem, EntryRef{a, i});
            }
    }
}

bool Metamodels::find(const std::string& modelName, MdlModel& out) const {
    std::string folder, stem, ext;
    splitName(modelName, folder, stem, ext);
    std::string joined;
    for (char c : stem)
        if (c != '_')
            joined += c;
    const size_t u = stem.rfind('_');
    std::vector<uint8_t> data;
    for (const std::string& want : {stem, joined, u == std::string::npos ? stem : stem.substr(0, u)}) {
        auto found = metamodels_.find(folder + "/" + want);
        if (found == metamodels_.end())
            continue;
        CatArchive& cat = *archives_[found->second.archive];
        out = MdlModel{};
        if (!cat.read(cat.entries()[found->second.entry], data) && !parseMdl(data, out))
            return true;
    }
    out = MdlModel{};
    return false;
}

// The .arf frames that fit the model: one transform per animated part,
// grouped by name without the frame number (conbee00 ... conbee15: conbee;
// fcmwalk0 ... fcmwalk9: fcmwalk). A set plays the state it's named after
// (fcmstand: stand, fcmwalk: walk), or the model's only state other than
// "reference" (the construction vehicles' conbee, fconb, rcb: manipulate).
// Sets named like the model come first (fconb for fconbee), then those in
// its folder, then the rest (every side's crewmen use the fed/ sets). The
// actor archives repeat some sets (fcmwalk in each), so each frame counts once.
std::vector<Animation> Metamodels::animations(const std::string& modelName, const MdlModel& mdl) const {
    std::vector<Animation> out;
    if (mdl.animated.empty() || mdl.reference.size() != mdl.animated.size())
        return out;
    std::string modelFolder, stem, ext;
    splitName(modelName, modelFolder, stem, ext);
    struct Set {
        std::string folder;
        std::map<std::string, EntryRef> frames; // by frame name, sorted
    };
    std::map<std::string, Set> sets;
    for (size_t a = 0; a < archives_.size(); ++a) {
        const auto& entries = archives_[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].tag != cattag::Arf)
                continue;
            std::string folder, frame, fext;
            splitName(entries[i].name, folder, frame, fext);
            std::string name = frame;
            while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
                name.pop_back();
            Set& set = sets[name];
            set.folder = folder;
            set.frames.emplace(frame, EntryRef{a, i}); // the first copy wins
        }
    }
    std::vector<std::string> otherStates;
    for (const auto& st : mdl.states)
        if (!st.empty() && st != "reference")
            otherStates.push_back(st);
    std::vector<std::pair<int, Animation>> found;
    std::vector<uint8_t> data;
    for (auto& [name, set] : sets) {
        Animation anim{name, {}, {}};
        for (const auto& [frame, ref] : set.frames) {
            CatArchive& cat = *archives_[ref.archive];
            std::vector<PartTransform> t;
            if (cat.read(cat.entries()[ref.entry], data) || parseArf(data, t) || t.size() != mdl.animated.size()) {
                anim.frames.clear();
                break;
            }
            anim.frames.push_back(std::move(t));
        }
        if (anim.frames.empty())
            continue;
        for (const auto& st : otherStates)
            if (name.size() > st.size() && name.compare(name.size() - st.size(), st.size(), st) == 0)
                anim.state = st;
        if (anim.state.empty() && otherStates.size() == 1)
            anim.state = otherStates[0];
        const bool named = name.rfind(stem, 0) == 0 || (name.size() >= 4 && stem.rfind(name, 0) == 0);
        found.push_back({named ? 0 : set.folder == modelFolder ? 1 : 2, std::move(anim)});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& f : found)
        out.push_back(std::move(f.second));
    return out;
}

namespace {

// The animation channel (.mdl animated list) a geometry node is moved by:
// the node's name in the parts list, then that name's animated entry.
int channelOfNode(const MdlModel& mdl, uint32_t node) {
    for (const auto& p : mdl.parts)
        if (p.value == int32_t(node))
            for (const auto& a : mdl.animated)
                if (a.name == p.name)
                    return a.value;
    return -1;
}

// A node's turret or gun name, if it is one: "turret", "gun_2", ...; the
// suffix ("", "_2") goes to suffix.
bool nodeRole(const MdlModel& mdl, uint32_t node, const char* role, std::string& suffix) {
    const std::string r = role;
    for (const auto& p : mdl.parts) {
        if (p.value != int32_t(node) || p.name.rfind(r, 0) != 0)
            continue;
        const std::string rest = p.name.substr(r.size());
        if (rest.empty() || (rest[0] == '_' && rest.size() > 1 && std::isdigit(static_cast<unsigned char>(rest[1])))) {
            suffix = rest;
            return true;
        }
    }
    return false;
}

// A named attachment point's position (false if there's none).
bool pointAt(const GeoModel& geo, const MdlModel& mdl, const std::string& name, GeoModel::Vec3& out) {
    for (const auto& p : mdl.points)
        if (p.name == name && p.value >= 0 && size_t(p.value) < geo.attachments.size()) {
            out = geo.attachments[size_t(p.value)].position;
            return true;
        }
    return false;
}

void multiply(const float a[9], const float b[9], float out[9]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float sum = 0;
            for (int k = 0; k < 3; ++k)
                sum += a[i * 3 + k] * b[k * 3 + j];
            out[i * 3 + j] = sum;
        }
}

} // namespace

Pose framePose(const GeoModel& geo, const MdlModel& mdl, const std::vector<PartTransform>& frame) {
    Pose pose(geo.parts.size());
    for (size_t pi = 0; pi < geo.parts.size(); ++pi) {
        const int channel = channelOfNode(mdl, geo.parts[pi].node);
        if (channel < 0 || size_t(channel) >= frame.size() || size_t(channel) >= mdl.reference.size())
            continue;
        const PartTransform& f = frame[size_t(channel)];
        const PartTransform& r = mdl.reference[size_t(channel)];
        PartPose& p = pose[pi];
        p.moved = true;
        // frame rotation × reference rotation transposed
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float sum = 0;
                for (int k = 0; k < 3; ++k)
                    sum += f.rotation[i * 3 + k] * r.rotation[j * 3 + k];
                p.m[i * 3 + j] = sum;
            }
        p.from = r.position;
        p.to = f.position;
    }
    return pose;
}

bool hasTurret(const MdlModel& mdl) {
    std::string suffix;
    for (const auto& p : mdl.parts)
        if (p.value > 0 && nodeRole(mdl, uint32_t(p.value), "turret", suffix))
            return true;
    return false;
}

Pose turretPose(const GeoModel& geo, const MdlModel& mdl, float yaw, float pitch) {
    Pose pose(geo.parts.size());
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float rz[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
    // Up for a gun along −y: a turn about x by −pitch.
    const float cp = std::cos(-pitch), sp = std::sin(-pitch);
    const float rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};
    for (size_t pi = 0; pi < geo.parts.size(); ++pi) {
        const uint32_t node = geo.parts[pi].node;
        std::string suffix;
        PartPose& p = pose[pi];
        GeoModel::Vec3 axis{0, 0, 0}, elevate{0, 0, 0};
        if (nodeRole(mdl, node, "turret", suffix)) {
            if (!pointAt(geo, mdl, "turret_axis" + suffix, axis))
                continue;
            p.moved = true;
            std::copy(rz, rz + 9, p.m);
            p.from = p.to = axis;
        } else if (nodeRole(mdl, node, "gun", suffix)) {
            const bool turns = pointAt(geo, mdl, "turret_axis" + suffix, axis);
            const bool raises = pointAt(geo, mdl, "gun_elevate" + suffix, elevate);
            if (!turns && !raises)
                continue;
            if (!raises)
                elevate = axis;
            if (!turns)
                axis = elevate;
            // Raised about its elevation point, then turned with the turret:
            // v' = Rz (Rx (v − e) + e − a) + a.
            p.moved = true;
            static constexpr float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            multiply(turns ? rz : kIdentity, raises ? rx : kIdentity, p.m);
            p.from = elevate;
            const float dx = elevate.x - axis.x, dy = elevate.y - axis.y, dz = elevate.z - axis.z;
            p.to = turns ? GeoModel::Vec3{axis.x + c * dx - s * dy, axis.y + s * dx + c * dy, axis.z + dz} : elevate;
        }
    }
    return pose;
}

} // namespace reluva::newworlds
