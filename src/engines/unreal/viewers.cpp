// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's shared drawing, and its Packages, Textures and Sounds
// pages.

#include "engines/unreal/viewers.h"

#include "engines/unreal/properties.h"
#include "engines/unreal/sound.h"
#include "engines/unreal/texture.h"
#include "engines/unreal/procedural.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/image.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace reluva::unreal {

// --- Page ------------------------------------------------------------------------------

void Page::clear(uint8_t r, uint8_t g, uint8_t b) { fill(0, 0, ctx_.screen.w, ctx_.screen.h, r, g, b); }

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface& s = ctx_.screen;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t* p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string& s, Color c) { font::drawText(ctx_.screen, x, y, s, c); }

void Page::setTitle(const std::string& detail) {
    ctx_.sys.setWindowTitle(std::string("Reluva - ") + ctx_.game.name + " - " + name() + ": " + detail);
}

// --- PackageViewer ----------------------------------------------------------------------

PackageViewer::PackageViewer(ViewerContext& ctx) : Page(ctx), names_(ctx.packages.names()) {}

void PackageViewer::handleEvent(const Event& ev) {
    if (names_.empty())
        return;
    const Package* pkg = ctx_.packages.get(names_[size_t(index_)]);
    const int exports = pkg ? int(pkg->exports().size()) : 0;
    const int page = 30;
    int sel = selected_;
    if (ev.type == EventType::MouseWheel) {
        sel -= ev.wheelY * 3;
    } else if (ev.type != EventType::KeyDown) {
        return;
    } else if (ev.key == key::Left || ev.key == key::Right) {
        const int n = int(names_.size());
        index_ = (index_ + (ev.key == key::Left ? n - 1 : 1)) % n;
        selected_ = top_ = 0;
        showProps_ = false;
        draw();
        return;
    } else if (ev.key == key::Up) {
        --sel;
    } else if (ev.key == key::Down) {
        ++sel;
    } else if (ev.key == key::PageUp) {
        sel -= page;
    } else if (ev.key == key::PageDown) {
        sel += page;
    } else if (ev.key == key::Home) {
        sel = 0;
    } else if (ev.key == key::End) {
        sel = exports - 1;
    } else if (ev.key == key::Return || ev.key == key::KpEnter) {
        showProps_ = !showProps_;
    } else {
        return;
    }
    selected_ = std::clamp(sel, 0, std::max(0, exports - 1));
    draw();
}

void PackageViewer::draw() {
    clear(16, 18, 24);
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index_)];
    setTitle(name + " (" + std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + ")");
    const int lh = font::kLineHeight;
    const Package* pkg = ctx_.packages.get(name);
    text(4, 4, name + ctx_.packages.extension(name) + "  " + std::to_string(index_ + 1) + "/" +
                   std::to_string(names_.size()));
    if (!pkg) {
        text(4, 4 + lh, ctx_.packages.error(name), {255, 120, 120});
        return;
    }
    char line[200];
    std::snprintf(line, sizeof line, "version %d, licensee %d, flags %08x: %zu names, %zu imports, %zu exports",
                  pkg->version(), pkg->licensee(), pkg->flags(), pkg->names().size(), pkg->imports().size(),
                  pkg->exports().size());
    text(4, 4 + lh, line, {150, 200, 255});

    const int listTop = 4 + 3 * lh;
    const int rows = (ctx_.screen.h - listTop) / lh;
    const int listRows = showProps_ ? rows / 2 : rows;
    if (selected_ < top_)
        top_ = selected_;
    if (selected_ >= top_ + listRows)
        top_ = selected_ - listRows + 1;
    const auto& exports = pkg->exports();
    for (int i = 0; i < listRows && top_ + i < int(exports.size()); ++i) {
        const int e = top_ + i;
        const int y = listTop + i * lh;
        if (e == selected_)
            fill(0, y - 1, ctx_.screen.w, lh, 40, 50, 80);
        std::snprintf(line, sizeof line, "%5d %-16s %-40s %8d", e, pkg->className(e + 1).c_str(),
                      pkg->path(e + 1).c_str(), exports[size_t(e)].serialSize);
        text(4, y, line);
    }
    if (!showProps_ || exports.empty())
        return;
    // The selected export's properties under the list.
    int y = listTop + listRows * lh + lh / 2;
    std::vector<uint8_t> data;
    PropertyList props;
    Error err = pkg->readExport(selected_, data);
    if (!err) {
        Reader r(data, size_t(exports[size_t(selected_)].serialOffset));
        err = beginObject(*pkg, selected_, r, props);
        if (!err) {
            std::snprintf(line, sizeof line, "%zu properties, then %zu bytes of the class's own data",
                          props.items.size(), r.remaining());
            text(4, y, line, {150, 200, 255});
        }
    }
    if (err) {
        text(4, y, err.message, {255, 120, 120});
        return;
    }
    for (const Property& p : props.items) {
        y += lh;
        if (y + lh > ctx_.screen.h)
            break;
        std::string label = p.name;
        if (p.arrayIndex)
            label += "[" + std::to_string(p.arrayIndex) + "]";
        text(4, y, label + " (" + (p.structName.empty() ? typeName(p.type) : p.structName) + ") = " + describe(*pkg, p));
    }
}

namespace {

// The packages of one kind (".utx"), then the maps, then the code
// packages, which can carry their own textures and sounds too.
std::vector<std::string> browsedPackages(const PackageSet& packages, const char* extension) {
    std::vector<std::string> out = packages.names(extension);
    for (std::string& n : packages.mapNames())
        out.push_back(std::move(n));
    for (std::string& n : packages.names(".u"))
        out.push_back(std::move(n));
    return out;
}

} // namespace

// --- ObjectBrowser ----------------------------------------------------------------------

ObjectBrowser::ObjectBrowser(const PackageSet& packages, std::vector<std::string> names,
                             bool (*wanted)(std::string_view))
    : packages_(packages), wanted_(wanted), names_(std::move(names)) {}

bool ObjectBrowser::load(int package) {
    package_ = package;
    objects_.clear();
    object_ = 0;
    const Package* pkg = packages_.get(names_[size_t(package)]);
    if (!pkg)
        return false;
    for (size_t i = 0; i < pkg->exports().size(); ++i)
        if (wanted_(pkg->className(int32_t(i) + 1)))
            objects_.push_back(int(i));
    return !objects_.empty();
}

bool ObjectBrowser::stepPackage(int step) {
    const int n = int(names_.size());
    if (n == 0)
        return false;
    int p = package_ < 0 ? 0 : package_;
    if (package_ >= 0)
        p = (p + step % n + n) % n;
    const int dir = step < 0 ? -1 : 1;
    for (int tries = 0; tries < n; ++tries, p = (p + dir + n) % n)
        if (load(p))
            return true;
    return false;
}

bool ObjectBrowser::step(int step) {
    if (package_ < 0)
        return stepPackage(0);
    const int n = int(objects_.size());
    const int next = object_ + step;
    if (next >= 0 && next < n) {
        object_ = next;
        return true;
    }
    // Past either end: on into the next or previous package.
    if (!stepPackage(step < 0 ? -1 : 1))
        return false;
    if (step < 0)
        object_ = int(objects_.size()) - 1;
    return true;
}

const Package* ObjectBrowser::package() const {
    return package_ < 0 || objects_.empty() ? nullptr : packages_.get(names_[size_t(package_)]);
}

int ObjectBrowser::exportIndex() const { return objects_.empty() ? -1 : objects_[size_t(object_)]; }

std::string ObjectBrowser::where() const {
    if (package_ < 0)
        return {};
    return std::to_string(object_ + 1) + "/" + std::to_string(objects_.size()) + " in " +
           names_[size_t(package_)] + " (" + std::to_string(package_ + 1) + "/" + std::to_string(names_.size()) +
           ")";
}

// --- TextureViewer ----------------------------------------------------------------------

TextureViewer::TextureViewer(ViewerContext& ctx)
    : Page(ctx), browser_(ctx.packages, browsedPackages(ctx.packages, ".utx"), isTextureClass) {}

void TextureViewer::activate() {
    if (!started_) {
        started_ = true;
        browser_.stepPackage(0);
    }
    select();
}

void TextureViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    if (ev.key == key::Left || ev.key == key::Right)
        browser_.step(ev.key == key::Left ? -1 : 1);
    else if (ev.key == key::PageUp || ev.key == key::PageDown)
        browser_.stepPackage(ev.key == key::PageUp ? -1 : 1);
    else if (ev.key == key::Space && anim_)
        paused_ = !paused_;
    else
        return;
    select();
}

// Fire, water, wet and FLC textures play.
void TextureViewer::update(uint64_t nowMs) {
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;
    if (anim_ && !paused_ && anim_->advance(dt)) {
        img_ = anim_->image();
        draw();
    }
}

// Reads the texture the browser is on, and its animation if it has one.
void TextureViewer::select() {
    anim_.reset();
    img_ = {};
    info_ = {};
    error_.clear();
    if (const Package* pkg = browser_.package()) {
        const int index = browser_.exportIndex();
        if (Error err = decodeTexture(ctx_.packages, *pkg, index, img_, &info_)) {
            error_ = err.message;
        } else if (isAnimatedTextureClass(pkg->className(index + 1))) {
            std::string why;
            anim_ = openAnimatedTexture(ctx_.packages, *pkg, index, &why);
            if (anim_)
                img_ = anim_->image();
            else
                info_.standIn += " (doesn't play: " + why + ")";
        }
    }
    draw();
}

void TextureViewer::draw() {
    Surface& s = ctx_.screen;
    // Checkerboard, so transparency shows.
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            uint8_t* p = s.row(y) + x * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    const int lh = font::kLineHeight;
    fill(0, 0, s.w, 3 * lh, 16, 18, 24);
    const Package* pkg = browser_.package();
    if (!pkg) {
        text(4, 4, "No textures found");
        return;
    }
    const int index = browser_.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + browser_.where() + ")");
    text(4, 4, pkg->className(index + 1) + " " + name + "  " + browser_.where());

    if (!error_.empty()) {
        text(4, 4 + lh, error_, {255, 120, 120});
        return;
    }
    const image::Image& img = img_;
    const TextureInfo& info = info_;
    // Whole multiples up to 4x when small, nearest-neighbour shrinking when large.
    const int areaW = s.w, areaH = s.h - 3 * lh;
    float scale = std::min(float(areaW) / float(img.w), float(areaH) / float(img.h));
    scale = scale >= 1 ? std::min(4.0f, float(int(scale))) : scale;
    image::Image shown;
    shown.w = std::max(1, int(float(img.w) * scale));
    shown.h = std::max(1, int(float(img.h) * scale));
    shown.rgba.resize(size_t(shown.w) * size_t(shown.h) * 4);
    for (int y = 0; y < shown.h; ++y)
        for (int x = 0; x < shown.w; ++x) {
            const int sx = std::min(img.w - 1, int(float(x) / scale));
            const int sy = std::min(img.h - 1, int(float(y) / scale));
            std::copy_n(img.pixel(sx, sy), 4, shown.pixel(x, y));
        }
    image::blit(s, shown, (areaW - shown.w) / 2, 3 * lh + (areaH - shown.h) / 2);
    char line[200];
    std::snprintf(line, sizeof line, "%dx%d %s, %d mipmaps%s%s%s", info.width, info.height, formatName(info.format),
                  info.mipCount, info.masked ? ", masked" : "", info.paletteName.empty() ? "" : ", palette ",
                  info.paletteName.c_str());
    text(4, 4 + lh, line, {150, 200, 255});
    if (anim_)
        text(4, 4 + 2 * lh,
             std::string("Stored without pixels: playing its ") + anim_->kind() + (paused_ ? " (paused" : " (") +
                 ", Space pauses)",
             {255, 200, 120});
    else if (!info.standIn.empty())
        text(4, 4 + 2 * lh, "Stored without pixels: " + info.standIn, {255, 200, 120});
}

// --- SoundViewer ------------------------------------------------------------------------

namespace {
bool isSoundClass(std::string_view c) { return fs::toLower(c) == "sound"; }
} // namespace

SoundViewer::SoundViewer(ViewerContext& ctx) : Page(ctx), browser_(ctx.packages, browsedPackages(ctx.packages, ".uax"), isSoundClass) {}

SoundViewer::~SoundViewer() { deactivate(); }

void SoundViewer::activate() {
    if (!started_) {
        started_ = true;
        browser_.stepPackage(0);
    }
    status_.clear();
    draw();
}

void SoundViewer::deactivate() {
    if (ctx_.mixer && playing_ != audio::kNoSound)
        ctx_.mixer->stop(playing_);
    playing_ = audio::kNoSound;
}

void SoundViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || ev.repeat)
        return;
    if (ev.key == key::Left || ev.key == key::Right)
        browser_.step(ev.key == key::Left ? -1 : 1);
    else if (ev.key == key::PageUp || ev.key == key::PageDown)
        browser_.stepPackage(ev.key == key::PageUp ? -1 : 1);
    else if (ev.key == key::Space || ev.key == key::Return) {
        play();
    } else
        return;
    if (ev.key != key::Space && ev.key != key::Return)
        status_.clear();
    draw();
}

void SoundViewer::play() {
    deactivate();
    const Package* pkg = browser_.package();
    if (!pkg)
        return;
    auto sound = std::make_shared<audio::Sound>();
    std::string from;
    if (Error err = loadSound(ctx_.packages, *pkg, browser_.exportIndex(), *sound, &from)) {
        status_ = err.message;
    } else {
        char line[160];
        std::snprintf(line, sizeof line, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format),
                      sound->rate, sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        status_ = line;
        if (!from.empty())
            status_ += ", streamed from " + std::filesystem::path(from).filename().string();
        // The Fallen's streamed sounds are its music.
        const auto type = from.empty() ? audio::SoundType::Effects : audio::SoundType::Music;
        if (ctx_.mixer)
            playing_ = ctx_.mixer->play(sound, type);
        else
            status_ += " (no audio output)";
    }
}

void SoundViewer::draw() {
    clear(16, 18, 24);
    const Package* pkg = browser_.package();
    if (!pkg) {
        text(4, 4, "No sounds found");
        return;
    }
    const int lh = font::kLineHeight;
    const int index = browser_.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + browser_.where() + ")");
    text(4, 4, name + "  " + browser_.where());
    text(4, 4 + lh, std::to_string(pkg->exports()[size_t(index)].serialSize) + " bytes. Space plays it.",
         {150, 200, 255});
    if (!status_.empty())
        text(4, 4 + 3 * lh, status_);
}

} // namespace reluva::unreal
