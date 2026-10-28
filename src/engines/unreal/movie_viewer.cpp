// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Movies page: the game's AVI movies (The Fallen has one, its intro:
// Cinepak video and PCM sound). The sound plays through the mixer and the
// pictures follow it, so they stay together however the frames are paced.

#include "engines/unreal/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace reluva::unreal {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The game folder's "movies" (in any case), and the AVI files in it.
std::vector<std::filesystem::path> findMovies(const std::filesystem::path& game) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (game.empty() || !std::filesystem::is_directory(game, ec))
        return out;
    for (const auto& dir : std::filesystem::directory_iterator(game, ec)) {
        if (!dir.is_directory(ec) || lower(dir.path().filename().string()) != "movies")
            continue;
        for (const auto& f : std::filesystem::directory_iterator(dir.path(), ec))
            if (f.is_regular_file(ec) && lower(f.path().extension().string()) == ".avi")
                out.push_back(f.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

MovieViewer::MovieViewer(ViewerContext& ctx) : Page(ctx), files_(findMovies(ctx.gameFolder)) {}

MovieViewer::~MovieViewer() { stop(); }

void MovieViewer::activate() { draw(); }

void MovieViewer::deactivate() { stop(); }

bool MovieViewer::play(const std::string& name) {
    for (size_t i = 0; i < files_.size(); ++i)
        if (lower(files_[i].filename().string()) == lower(name)) {
            index_ = i;
            play(i);
            return playing();
        }
    return false;
}

void MovieViewer::play(size_t index) {
    stop();
    if (index >= files_.size())
        return;
    if (Error err = video::readAvi(files_[index], movie_)) {
        status_ = err.message;
        draw();
        return;
    }
    if (movie_.codec != "cvid" && movie_.codec != "CVID") {
        status_ = "its video is " + movie_.codec + ", not Cinepak";
        draw();
        return;
    }
    decoder_ = std::make_unique<video::CinepakDecoder>(movie_.width, movie_.height);
    shown_ = -1;
    startMs_ = 0;
    if (ctx_.mixer && movie_.audio.frames() > 0)
        sound_ = ctx_.mixer->play(std::make_shared<audio::Sound>(movie_.audio), audio::SoundType::Music);
    clear(0, 0, 0);
    setTitle(files_[index].filename().string());
}

void MovieViewer::stop() {
    if (ctx_.mixer && sound_ != audio::kNoSound)
        ctx_.mixer->stop(sound_);
    sound_ = audio::kNoSound;
    if (decoder_) {
        decoder_.reset();
        movie_ = video::Avi{};
        draw();
    }
}

void MovieViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const bool go = ev.key == key::Space || ev.key == key::Return || ev.key == key::KpEnter;
    if (playing()) {
        if (go || ev.key == key::Escape)
            stop();
        return;
    }
    if (files_.empty())
        return;
    if (ev.key == key::Left || ev.key == key::Right) {
        index_ = (index_ + (ev.key == key::Left ? files_.size() - 1 : 1)) % files_.size();
        status_.clear();
        draw();
    } else if (go) {
        play(index_);
    }
}

// The frame due now: by the sound's place while it plays, else by the clock
// since the movie started.
void MovieViewer::update(uint64_t nowMs) {
    if (!decoder_)
        return;
    if (startMs_ == 0)
        startMs_ = nowMs ? nowMs : 1;
    double seconds = double(nowMs - startMs_) / 1000.0;
    if (ctx_.mixer && sound_ != audio::kNoSound && ctx_.mixer->isPlaying(sound_) && movie_.audio.rate > 0)
        seconds = ctx_.mixer->position(sound_) / double(movie_.audio.rate);
    const double fps = movie_.framesPerSecond > 0 ? movie_.framesPerSecond : 15.0;
    const int due = int(seconds * fps);
    if (due >= int(movie_.frames.size())) {
        if (!ctx_.mixer || sound_ == audio::kNoSound || !ctx_.mixer->isPlaying(sound_))
            stop();
        return;
    }
    if (due <= shown_)
        return;
    // Frames build on those before, so every one up to it is decoded.
    while (shown_ < due) {
        ++shown_;
        if (Error err = decoder_->decode(movie_.frames[size_t(shown_)])) {
            status_ = "frame " + std::to_string(shown_) + ": " + err.message;
            stop();
            return;
        }
    }
    showPicture();
}

// The picture, scaled to fit the screen and centred on black.
void MovieViewer::showPicture() {
    const image::Image& pic = decoder_->picture();
    Surface& s = ctx_.screen;
    if (pic.w <= 0 || pic.h <= 0)
        return;
    const double scale = std::min(double(s.w) / pic.w, double(s.h) / pic.h);
    const int w = std::max(1, int(pic.w * scale)), h = std::max(1, int(pic.h * scale));
    const int x0 = (s.w - w) / 2, y0 = (s.h - h) / 2;
    if (w != s.w || h != s.h)
        clear(0, 0, 0);
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(pic.h - 1, int(y / scale));
        uint8_t* out = s.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, out += 4) {
            const uint8_t* in = pic.pixel(std::min(pic.w - 1, int(x / scale)), sy);
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = 255;
        }
    }
}

void MovieViewer::draw() {
    clear(16, 18, 24);
    if (files_.empty()) {
        text(4, 4, "No movies found");
        return;
    }
    const int lh = font::kLineHeight;
    const std::string name = files_[index_].filename().string();
    setTitle(name);
    text(4, 4, name + "  (" + std::to_string(index_ + 1) + " of " + std::to_string(files_.size()) + ")");
    text(4, 4 + lh, "Space or Enter plays it; Space, Enter or Esc stops it.", {150, 200, 255});
    if (!status_.empty())
        text(4, 4 + 3 * lh, status_);
}

} // namespace reluva::unreal
