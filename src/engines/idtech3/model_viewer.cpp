// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: every .md3 and .mdr in the game drawn on the GPU
// with its shaders, players put together from their three parts.

#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kAnimationFps = 15;

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

ModelViewer::ModelViewer(ViewerContext& ctx) : Page(ctx) {
    std::set<std::string> names;
    for (const std::string& n : ctx.files.list())
        names.insert(normalizeArchiveName(n));
    std::set<std::string> players;
    // A player part in either format (Elite Force's bodies are .mdr).
    auto has = [&](const std::string& dir, const char* part) {
        return names.count(dir + "/" + part + ".md3") || names.count(dir + "/" + part + ".mdr");
    };
    for (const std::string& n : names) {
        if (!endsWith(n, ".md3") && !endsWith(n, ".mdr"))
            continue;
        // Lower-detail copies of a model.
        if (endsWith(n, "_1.md3") || endsWith(n, "_2.md3"))
            continue;
        const std::string dir = folderOf(n);
        bool part = false;
        for (const char* p : {"lower", "upper", "head"})
            part = part || endsWith(n, (std::string("/") + p + ".md3").c_str()) ||
                   endsWith(n, (std::string("/") + p + ".mdr").c_str());
        if (part && has(dir, "lower") && has(dir, "upper") && has(dir, "head")) {
            if (players.insert(dir).second)
                entries_.push_back({dir, true});
            continue;
        }
        entries_.push_back({n, false});
    }
}

ModelViewer::~ModelViewer() { deactivate(); }

void ModelViewer::deactivate() {
    models_.reset();
    shaders_.reset();
    renderer_ = nullptr;
}

void ModelViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    applyVideoSettings(videoSettings(ctx_.cvars), *shaders_, nullptr, models_.get());
    show(index_);
}

void ModelViewer::show(int index) {
    index_ = index;
    const Entry& e = entries_[size_t(index)];
    setTitle(e.name + " (" + std::to_string(index + 1) + "/" + std::to_string(entries_.size()) + ")");
    models_->clear();
    shaders_->clear();
    info_.clear();
    error_.clear();
    model_ = lower_ = upper_ = head_ = -1;
    skins_[0] = skins_[1] = skins_[2] = nullptr;
    frame_ = 0;
    startMs_ = ctx_.sys.getMillis();
    anims_.clear();
    animTime_ = 0;

    std::vector<const Md3Model*> parts;
    if (e.player) {
        // Each part as .mdr if there is one (Elite Force's), else .md3.
        auto part = [&](const char* name) {
            const std::string mdr = e.name + "/" + name + ".mdr";
            return models_->load(ctx_.files.has(mdr) ? mdr : e.name + "/" + name + ".md3", &error_);
        };
        lower_ = part("lower");
        upper_ = part("upper");
        head_ = part("head");
        if (lower_ < 0 || upper_ < 0 || head_ < 0) {
            info_.push_back(e.name);
            return;
        }
        // Its animations: played in pairs, legs and torso, as the game does.
        std::vector<uint8_t> cfg;
        if (Error err = ctx_.files.read(e.name + "/animation.cfg", cfg); !err) {
            anims_ = parseAnimationConfig(std::string(cfg.begin(), cfg.end()));
            legsAnim_ = torsoAnim_ = -1;
            for (size_t i = 0; i < anims_.size(); ++i) {
                if (anims_[i].name == "LEGS_IDLE" || (legsAnim_ < 0 && anims_[i].part == PlayerAnimation::Part::Legs))
                    legsAnim_ = int(i);
                if (anims_[i].name == "TORSO_STAND" ||
                    (torsoAnim_ < 0 && anims_[i].part == PlayerAnimation::Part::Torso))
                    torsoAnim_ = int(i);
            }
            if (legsAnim_ < 0 || torsoAnim_ < 0)
                anims_.clear();
        }
        skins_[0] = &models_->skin(e.name + "/lower_default.skin");
        skins_[1] = &models_->skin(e.name + "/upper_default.skin");
        skins_[2] = &models_->skin(e.name + "/head_default.skin");
        parts = {&models_->model(lower_), &models_->model(upper_), &models_->model(head_)};
    } else {
        model_ = models_->load(e.name, &error_);
        if (model_ < 0) {
            info_.push_back(e.name);
            return;
        }
        parts = {&models_->model(model_)};
    }

    // Frame the first frame's bounds (a player stands about 60 units
    // above its legs' origin).
    const Md3Frame& f = parts[0]->frames.front();
    for (int i = 0; i < 3; ++i)
        center_[size_t(i)] = (f.mins[size_t(i)] + f.maxs[size_t(i)]) / 2;
    radius_ = std::max(8.0f, f.radius);
    if (e.player) {
        center_[2] += 16;
        radius_ = std::max(radius_, 40.0f);
    }
    resetView();

    info_.push_back(e.name);
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const Md3Model* m = parts[pi];
        size_t verts = 0, tris = 0;
        std::set<std::string> shaderNames;
        for (const Md3Surface& s : m->surfaces) {
            verts += size_t(s.vertexCount);
            tris += s.indices.size() / 3;
            for (const std::string& sh : s.shaders)
                shaderNames.insert(sh);
        }
        std::string tags;
        for (int t = 0; t < m->tagCount; ++t)
            tags += (t ? " " : "") + m->tags[size_t(t)].name;
        char line[256];
        std::snprintf(line, sizeof line, "%s: %zu frames, %zu surfaces, %zu vertices, %zu triangles%s%s",
                      m->name.empty() ? "model" : m->name.c_str(), m->frames.size(), m->surfaces.size(), verts, tris,
                      tags.empty() ? "" : ", tags: ", tags.c_str());
        info_.push_back(line);
        // Each surface's shader (from the skin if it names one) and whether
        // it, or the image it needs, is there.
        for (const Md3Surface& s : m->surfaces) {
            std::string shader = s.shaders.empty() ? std::string() : s.shaders.front();
            if (const Skin* skin = skins_[pi])
                if (auto it = skin->find(fs::toLower(s.name)); it != skin->end())
                    shader = it->second;
            bool found = !shader.empty();
            if (const ShaderDef* def = ctx_.shaders.find(shader)) {
                for (const ShaderStage& st : def->stages)
                    for (const std::string& img : st.maps)
                        if (!img.empty() && img[0] != '$' && findImage(ctx_.files, img).empty())
                            found = false;
            } else if (found) {
                found = !findImage(ctx_.files, shader).empty();
            }
            info_.push_back("  " + s.name + ": " + (shader.empty() ? std::string("(no shader)") : shader) +
                            (found ? "" : "  [MISSING]"));
        }
    }
}

void ModelViewer::resetView() {
    yaw_ = kPi * 0.85f;
    pitch_ = 0.25f;
    distance_ = radius_ * 2.6f;
}

void ModelViewer::handleEvent(const Event& ev) {
    if (!renderer_ || entries_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int n = static_cast<int>(entries_.size());
        int i = index_;
        if (ev.key == key::Left)
            i = (i + n - 1) % n;
        else if (ev.key == key::Right)
            i = (i + 1) % n;
        else if (ev.key == key::PageUp)
            i = std::max(0, i - 10);
        else if (ev.key == key::PageDown)
            i = std::min(n - 1, i + 10);
        else if (ev.key == key::Home)
            i = 0;
        else if (ev.key == key::End)
            i = n - 1;
        else if (ev.key == 'a')
            animate_ = !animate_;
        else if (ev.key == 't')
            torsoOnTag_ = !torsoOnTag_;
        else if ((ev.key == '[' || ev.key == ']') && !anims_.empty())
            nextAnimation(ev.key == ']' ? 1 : -1);
        else if (ev.key == ',')
            --frame_;
        else if (ev.key == '.')
            ++frame_;
        else if (ev.key == 'r')
            resetView();
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
        else if (ev.key == '+' || ev.key == '=')
            distance_ = std::max(radius_ * 0.3f, distance_ * 0.9f);
        else if (ev.key == '-')
            distance_ /= 0.9f;
        if (i != index_)
            show(i);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::max(radius_ * 0.3f, distance_ * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::nextAnimation(int step) {
    // Through every animation: one for the whole body plays on both
    // halves; a torso or legs one on its half, the other going back to
    // standing or idling if it was playing one for the whole body.
    const int n = int(anims_.size());
    const int current = step > 0 ? std::max(legsAnim_, torsoAnim_) : std::min(legsAnim_, torsoAnim_);
    const int i = ((current + step) % n + n) % n;
    auto find = [&](const char* name, PlayerAnimation::Part part) {
        for (size_t k = 0; k < anims_.size(); ++k)
            if (anims_[k].name == name)
                return int(k);
        for (size_t k = 0; k < anims_.size(); ++k)
            if (anims_[k].part == part)
                return int(k);
        return 0;
    };
    switch (anims_[size_t(i)].part) {
    case PlayerAnimation::Part::Both: legsAnim_ = torsoAnim_ = i; break;
    case PlayerAnimation::Part::Torso:
        torsoAnim_ = i;
        if (anims_[size_t(legsAnim_)].part != PlayerAnimation::Part::Legs)
            legsAnim_ = find("LEGS_IDLE", PlayerAnimation::Part::Legs);
        break;
    case PlayerAnimation::Part::Legs:
        legsAnim_ = i;
        if (anims_[size_t(torsoAnim_)].part != PlayerAnimation::Part::Torso)
            torsoAnim_ = find("TORSO_STAND", PlayerAnimation::Part::Torso);
        break;
    }
    animTime_ = 0;
    animate_ = true;
}

void ModelViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    const float time = float(nowMs - startMs_) / 1000.0f;
    if (animate_ && lastMs_ != 0)
        animTime_ += std::min(float(nowMs - lastMs_), 100.0f) / 1000.0f;
    lastMs_ = nowMs;
    const int frame = animate_ ? int(time * kAnimationFps) : frame_;

    const gfx::Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 eye{center_[0] + offset[0] * distance_, center_[1] + offset[1] * distance_,
                        center_[2] + offset[2] * distance_};
    const float aspect = float(kFrameW) / float(kFrameH);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.9f, aspect, 1, 8192),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(viewProj, eye, {70, 74, 82});

    // No map, so no light grid: a fixed light, as the original uses then.
    const PointLight light = LightGrid{}.sample({0, 0, 0});
    std::string frames;
    if (model_ >= 0) {
        models_->draw(model_, frame, gfx::identity(), light, time);
        const int n = int(models_->model(model_).frames.size());
        frames = "frame " + std::to_string(((frame % n) + n) % n + 1) + "/" + std::to_string(n);
    } else if (lower_ >= 0) {
        // Legs at the origin, the torso on the legs' tag_torso, the head
        // on the torso's tag_head.
        const Md3Model& legs = models_->model(lower_);
        const Md3Model& torso = models_->model(upper_);
        const int nl = int(legs.frames.size()), nt = int(torso.frames.size());
        // Each half's frame, the next and how far towards it: from its
        // animation (animation.cfg) if the player has them, else frame by
        // frame through the whole model.
        int fl = ((frame % nl) + nl) % nl, ft = ((frame % nt) + nt) % nt, nextL = fl, nextT = ft;
        float lerpL = 0, lerpT = 0;
        if (!anims_.empty()) {
            anims_[size_t(legsAnim_)].frameAt(animTime_, fl, nextL, lerpL);
            anims_[size_t(torsoAnim_)].frameAt(animTime_, ft, nextT, lerpT);
            fl = std::clamp(fl, 0, nl - 1), nextL = std::clamp(nextL, 0, nl - 1);
            ft = std::clamp(ft, 0, nt - 1), nextT = std::clamp(nextT, 0, nt - 1);
        }
        // A tag between two frames.
        auto tagBetween = [](const Md3Model& m, int a, int b, float f, const char* name) -> std::optional<Md3Tag> {
            const Md3Tag* ta = m.tag(a, name);
            const Md3Tag* tb = m.tag(b, name);
            if (!ta || !tb)
                return std::nullopt;
            Md3Tag t = *ta;
            for (size_t k = 0; k < 3; ++k) {
                t.origin[k] += (tb->origin[k] - ta->origin[k]) * f;
                for (size_t j = 0; j < 3; ++j)
                    t.axis[k][j] += (tb->axis[k][j] - ta->axis[k][j]) * f;
            }
            return t;
        };
        const gfx::Mat4 base = gfx::identity();
        models_->draw(lower_, nextL, base, light, time, skins_[0], 0, 1e9f, {}, nullptr, fl, 1 - lerpL);
        // (With T, the torso where its own file has it instead, to compare:
        // skeletal models' halves may already fit together.)
        gfx::Mat4 torsoAt = base;
        if (const auto t = tagBetween(legs, fl, nextL, lerpL, "tag_torso"); t && torsoOnTag_)
            torsoAt = gfx::multiply(base, tagPlacement(*t));
        models_->draw(upper_, nextT, torsoAt, light, time, skins_[1], 0, 1e9f, {}, nullptr, ft, 1 - lerpT);
        gfx::Mat4 headAt = torsoAt;
        if (const auto t = tagBetween(torso, ft, nextT, lerpT, "tag_head"))
            headAt = gfx::multiply(torsoAt, tagPlacement(*t));
        models_->draw(head_, 0, headAt, light, time, skins_[2]);
        frames = "legs frame " + std::to_string(fl + 1) + "/" + std::to_string(nl) + ", torso " +
                 std::to_string(ft + 1) + "/" + std::to_string(nt) +
                 (anims_.empty() ? std::string()
                                 : "; " + anims_[size_t(legsAnim_)].name + " with " +
                                       anims_[size_t(torsoAnim_)].name + " ([ ]: animation)") +
                 (torsoOnTag_ ? "; torso on the legs' tag_torso (T: as in its file)"
                              : "; torso as in its file (T: on the legs' tag_torso)");
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (showInfo_ || !error_.empty()) {
        std::vector<std::string> lines = info_;
        if (!error_.empty())
            lines.push_back(error_);
        if (!frames.empty())
            lines.push_back(frames + (animate_ ? " (A: stop)" : " (A: animate, ,/.: step)") + ", " +
                            std::to_string(shaders_->missingImages()) + " images missing");
        const int lh = font::kLineHeight;
        size_t widest = 0;
        for (const std::string& l : lines)
            widest = std::max(widest, font::length(l));
        const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                overlay_.row(y)[x * 4 + 3] = 150;
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                           !error_.empty() && lines[i] == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::idtech3
