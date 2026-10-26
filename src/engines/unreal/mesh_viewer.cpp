// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Meshes page: one Mesh or LodMesh at a time, turning
// in front of the camera, with its animations.

#include "engines/unreal/viewers.h"

#include "engines/unreal/texture.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::unreal {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;

} // namespace

MeshViewer::MeshViewer(ViewerContext& ctx) : Page(ctx), browser_(ctx.packages, [&] {
    // Code packages first: that's where meshes usually are.
    std::vector<std::string> names = ctx.packages.names(".u");
    for (std::string& n : ctx.packages.mapNames())
        names.push_back(std::move(n));
    for (std::string& n : ctx.packages.names(".utx"))
        names.push_back(std::move(n));
    return names;
}(), isMeshClass) {}

MeshViewer::~MeshViewer() { deactivate(); }

void MeshViewer::deactivate() {
    unload();
    renderer_ = nullptr;
}

void MeshViewer::unload() {
    if (!renderer_)
        return;
    if (gpu_.mesh)
        renderer_->destroyMesh(gpu_.mesh);
    gpu_ = GpuMesh{};
    for (gfx::TextureId t : textures_)
        if (t)
            renderer_->destroyTexture(t);
    textures_.clear();
}

void MeshViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Meshes page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    if (!started_) {
        started_ = true;
        browser_.stepPackage(0);
    }
    show();
}

void MeshViewer::resetView() {
    yaw_ = 0;
    pitch_ = 0.3f;
    distance_ = gpu_.radius * 2.5f;
}

void MeshViewer::show() {
    unload();
    error_.clear();
    info_.clear();
    anim_ = -1;
    time_ = 0;
    const Package* pkg = browser_.package();
    if (!pkg) {
        error_ = "No meshes found";
        return;
    }
    const int index = browser_.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + browser_.where() + ")");
    info_.push_back(pkg->className(index + 1) + " " + name + "  " + browser_.where());
    Error err = readMesh(*pkg, index, mesh_);
    if (!err)
        err = uploadMesh(*renderer_, mesh_, gpu_, ctx_.meshTurn);
    if (err) {
        error_ = err.message;
        return;
    }
    // Its own textures.
    for (int32_t ref : mesh_.textures) {
        gfx::TextureId id = 0;
        const ObjectRef tex = ctx_.packages.resolve(*pkg, ref);
        image::Image img;
        if (tex && !decodeTexture(ctx_.packages, *tex.package, tex.exportIndex, img))
            if (renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id))
                id = 0;
        textures_.push_back(id);
    }
    if (!mesh_.anims.empty())
        anim_ = 0;
    char line[200];
    std::snprintf(line, sizeof line, "%d vertices x %d frames (%d-byte), %zu triangles, %zu textures, %zu animations",
                  mesh_.frameVerts, mesh_.animFrames, mesh_.vertexBytes, mesh_.tris.size(), mesh_.textures.size(),
                  mesh_.anims.size());
    info_.push_back(line);
    std::snprintf(line, sizeof line, "scale %g %g %g, origin %g %g %g, rotation origin %d %d %d", double(mesh_.scale[0]),
                  double(mesh_.scale[1]), double(mesh_.scale[2]), double(mesh_.origin[0]), double(mesh_.origin[1]),
                  double(mesh_.origin[2]), mesh_.rotOrigin.pitch, mesh_.rotOrigin.yaw, mesh_.rotOrigin.roll);
    info_.push_back(line);
    if (mesh_.importAxes) {
        const auto& a = *mesh_.importAxes;
        std::snprintf(line, sizeof line, "%s (rows %g %g %g / %g %g %g / %g %g %g); drawn %s (M changes)",
                      mesh_.importAxesGuessed ? "no float copy, usual turn" : "float copy turned",
                      double(a[0][0]), double(a[0][1]), double(a[0][2]), double(a[1][0]), double(a[1][1]),
                      double(a[1][2]), double(a[2][0]), double(a[2][1]), double(a[2][2]), meshTurnName(ctx_.meshTurn));
        info_.push_back(line);
    }
    if (mesh_.keyFrames)
        info_.push_back(std::to_string(mesh_.keyFrames) + " of its " + std::to_string(mesh_.animFrames) +
                        " frames of vertices found (keyframes not worked out yet): the first is shown");
    if (mesh_.trailingBytes)
        info_.push_back(std::to_string(mesh_.trailingBytes) + " bytes after the last field read");
    resetView();
}

void MeshViewer::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
    if (ev.type == EventType::KeyDown && !ev.repeat) {
        const int anims = int(mesh_.anims.size());
        if (ev.key == key::Left || ev.key == key::Right) {
            browser_.step(ev.key == key::Left ? -1 : 1);
            show();
        } else if (ev.key == key::PageUp || ev.key == key::PageDown) {
            browser_.stepPackage(ev.key == key::PageUp ? -1 : 1);
            show();
        } else if (ev.key == 'a') {
            playing_ = !playing_;
        } else if ((ev.key == '[' || ev.key == ']') && anims > 0) {
            anim_ = (anim_ + (ev.key == '[' ? anims - 1 : 1)) % anims;
            time_ = 0;
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        } else if (ev.key == 'm') {
            ctx_.meshTurn = MeshTurn((int(ctx_.meshTurn) + 1) % 3);
            const float yaw = yaw_, pitch = pitch_, distance = distance_;
            show();
            yaw_ = yaw;
            pitch_ = pitch;
            distance_ = distance;
        }
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ += float(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::clamp(distance_ * (ev.wheelY > 0 ? 0.9f : 1.1f), 4.0f, 20000.0f);
    }
}

void MeshViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;
    if (playing_)
        time_ += dt;
    // Orbiting the mesh's middle, in the renderer's axes (Y negated).
    const gfx::Vec3 target{gpu_.center[0], -gpu_.center[1], gpu_.center[2]};
    const gfx::Vec3 eye{target[0] + distance_ * std::cos(pitch_) * std::cos(yaw_),
                        target[1] + distance_ * std::cos(pitch_) * std::sin(yaw_), target[2] + distance_ * std::sin(pitch_)};
    const gfx::Vec3 forward{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float aspect = float(kFrameW) / float(kFrameH);
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(kPi / 3, aspect, 1, 65536),
                                             gfx::lookAlong(eye, forward, {0, 0, 1}));
    renderer_->beginFrame(viewProj, eye, {40, 44, 56});
    if (gpu_.mesh) {
        MeshLight light;
        light.ambient = {0.45f, 0.45f, 0.45f};
        light.directed = {0.6f, 0.6f, 0.6f};
        light.direction = {0.4f, -0.3f, 0.87f};
        const MeshAnimSeq* anim = anim_ >= 0 ? &mesh_.anims[size_t(anim_)] : nullptr;
        drawMesh(*renderer_, gpu_, meshPlacement({}, {}, 1, {}), poseAt(mesh_, anim, time_), textures_, light, 1);
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (showInfo_) {
        lines = info_;
        if (anim_ >= 0) {
            const MeshAnimSeq& a = mesh_.anims[size_t(anim_)];
            char line[160];
            std::snprintf(line, sizeof line, "animation %d/%zu: %s (%s), frames %d-%d at %g/s%s", anim_ + 1,
                          mesh_.anims.size(), a.name.c_str(), a.group.c_str(), a.startFrame,
                          a.startFrame + a.numFrames - 1, double(a.rate), playing_ ? "" : ", stopped");
            lines.push_back(line);
        }
    }
    if (!error_.empty())
        lines.push_back(error_);
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(overlay_, 4, 3 + int(i) * font::kLineHeight, lines[i],
                       !error_.empty() && i + 1 == lines.size() ? Color{255, 120, 120} : Color{230, 230, 230});
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::unreal
