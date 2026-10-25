// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 engine: games built on the Quake III Arena engine, starting
// with Star Trek: Voyager - Elite Force.
//
// Sources: the file formats are documented in docs/formats/idtech3/, from
// the game's own data and from the Quake III Arena source code id Software
// released under the GPL (version 2 or later, so usable in this GPLv3
// project). See CONTRIBUTING.md: never use leaked or unlicensed code.
//
// For now the engine is a data viewer (see viewers.h for the keys): it
// mounts the game's search path and shows its files, textures and maps.
// Tab switches pages; Esc goes back to the launcher (on the Menu page,
// Ctrl+Tab and Ctrl+Esc, as the menus take Tab and Esc).

#include "common/audio/mixer.h"
#include "engines/engine.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/viewers.h"

#include <memory>

namespace reluva {

namespace {

using namespace idtech3;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class IdTech3Engine : public Engine {
public:
    IdTech3Engine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = mountGameDir(files_, target_.path, game_.baseDir, 0, warnings))
            return err;
        // Damaged archives or scripts don't stop the viewer; the Files
        // page lists what went wrong.
        shaders_.load(files_, &warnings);

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);

        // One mixer for every page's sounds; without audio output, none.
        audio::Mixer mixer(sys_.audioRate());
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System& sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{sys_};

        ViewerContext ctx{sys_, screen_, files_, shaders_, game_, warnings, target_.savePath,
                          audioOut ? &mixer : nullptr, {}, {}, {}, {}, {}};
        loadConfig(ctx);
        struct SaveConfig { // keeps settings changed while running, however it ends
            ViewerContext& ctx;
            ~SaveConfig() { saveConfig(ctx); }
        } saveOnExit{ctx};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<MenuViewer>(ctx));
        pages.push_back(std::make_unique<PlayViewer>(ctx));
        pages.push_back(std::make_unique<WorldViewer>(ctx));
        pages.push_back(std::make_unique<ModelViewer>(ctx));
        pages.push_back(std::make_unique<MapViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<FileViewer>(ctx));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            ++page;
        if (page == pages.size())
            return Error::fail("No files found in " + target_.path.string() + "/" + game_.baseDir);
        pages[page]->activate();
        // Leaving a 3D page: its renderer goes, the framebuffer comes back.
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
                const bool pageKey = pages[page]->takesKey(ev);
                if (ev.type == EventType::KeyDown && ev.key == key::Escape && !pageKey)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::Tab && !pageKey) {
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
            // A page asking for another (the menus starting a level).
            if (!ctx.requestPage.empty()) {
                const std::string wanted = std::move(ctx.requestPage);
                ctx.requestPage.clear();
                for (size_t i = 0; i < pages.size(); ++i)
                    if (wanted == pages[i]->name() && pages[i]->count() > 0 && i != page) {
                        if (Error err = switchTo(i))
                            return err;
                        break;
                    }
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
    SearchSet files_;
    ShaderLibrary shaders_;
    Surface screen_;
};

class IdTech3MetaEngine : public MetaEngine {
public:
    const char* id() const override { return "idtech3"; }
    const char* name() const override { return "id Tech 3"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        return found.empty() ? detectFromTable(id(), fallbackTable(), dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<IdTech3Engine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& idTech3MetaEngine() {
    static const IdTech3MetaEngine instance;
    return instance;
}

} // namespace reluva
