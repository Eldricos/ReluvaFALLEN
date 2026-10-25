// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Unreal Engine 1: games built on Epic's first Unreal engine, starting
// with Star Trek: Deep Space Nine - The Fallen.
//
// Sources: the package formats are documented in docs/formats/unreal/,
// from public community write-ups of the format and the game's own data.
// See CONTRIBUTING.md: never use leaked or unlicensed code, and code from
// other reimplementations is not copied.
//
// For now the engine is a data viewer (see viewers.h for the keys): it
// finds the game's packages and shows its maps, textures and sounds.

#include "common/audio/mixer.h"
#include "engines/engine.h"
#include "engines/unreal/games.h"
#include "engines/unreal/package_set.h"
#include "engines/unreal/viewers.h"

#include <memory>

namespace reluva {

namespace {

using namespace unreal;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class UnrealEngine : public Engine {
public:
    UnrealEngine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = packages_.open(target_.path, game_.iniName, &warnings))
            return err;

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);

        audio::Mixer mixer(sys_.audioRate());
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System& sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{sys_};

        ViewerContext ctx{sys_, screen_, packages_, game_, warnings, audioOut ? &mixer : nullptr};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<MapViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<SoundViewer>(ctx));
        pages.push_back(std::make_unique<PackageViewer>(ctx));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("No packages found in " + target_.path.string());
        pages[page]->activate();
        auto switchTo = [&](size_t next) -> Error {
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
                    return err;
            page = next;
            pages[page]->activate();
            return Error::success();
        };

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::Escape)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::Tab) {
                    size_t next = page;
                    do
                        next = (next + 1) % pages.size();
                    while (pages[next]->count() == 0);
                    if (Error err = switchTo(next))
                        return err;
                    continue;
                }
                pages[page]->handleEvent(ev);
            }
            pages[page]->update(sys_.getMillis());
            sys_.updateScreen(screen_);
            // 3D pages are paced by presenting (vsync); the rest needn't spin.
            if (!pages[page]->uses3D())
                sys_.delayMillis(10);
        }
    }

private:
    System& sys_;
    GameTarget target_;
    const GameInfo& game_;
    PackageSet packages_;
    Surface screen_;
};

class UnrealMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "unreal"; }
    const char* name() const override { return "Unreal Engine 1"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        return unreal::detect(id(), dir);
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = findGame(target.gameId);
        if (!game)
            game = &games().back(); // the unidentified game
        return std::make_unique<UnrealEngine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& unrealMetaEngine() {
    static const UnrealMetaEngine instance;
    return instance;
}

} // namespace reluva
