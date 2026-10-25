// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sdl3/sdl_system.h"
#include "common/config.h"
#include "common/detection.h"
#include "common/fs.h"
#include "engines/registry.h"
#include "launcher/launcher.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

using namespace reluva;

static std::filesystem::path dataDir() {
    std::filesystem::path dir = ".";
    if (char* pref = SDL_GetPrefPath("reluva", "reluva")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

static std::filesystem::path configPath() { return dataDir() / "reluva.ini"; }

// `reluva --hash <file>...` prints detection signatures for building tables.
static int printHashes(int argc, char** argv) {
    for (int i = 2; i < argc; ++i) {
        std::error_code ec;
        auto size = std::filesystem::file_size(argv[i], ec);
        auto md5 = fs::md5File(argv[i], kDetectionHashBytes);
        if (ec || !md5) {
            std::fprintf(stderr, "%s: cannot read\n", argv[i]);
            continue;
        }
        std::printf("{\"%s\", \"%s\", %llu},\n", std::filesystem::path(argv[i]).filename().string().c_str(),
                    md5->c_str(), static_cast<unsigned long long>(size));
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--hash") == 0)
        return printHashes(argc, argv);

    SdlSystem system;
    if (Error err = system.init()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reluva", err.message.c_str(), nullptr);
        SDL_Log("%s", err.message.c_str());
        return 1;
    }

    Config config;
    config.load(configPath());

    // `reluva <target>` starts a configured game (or "testbed") directly and
    // exits when it ends; otherwise the launcher runs.
    std::optional<GameTarget> direct;
    if (argc >= 2) {
        std::string name = argv[1];
        if (name == "testbed")
            direct = GameTarget{"testbed", "testbed", "testbed", "Testbed", {}, {}};
        else if (config.hasSection(name) && name != Config::kGlobalSection)
            direct = GameTarget{name, config.get(name, "engine"), config.get(name, "gameid"),
                                config.get(name, "description", name), config.get(name, "path"), {}};
        else {
            std::fprintf(stderr, "Unknown target '%s'\n", name.c_str());
            return 1;
        }
    }

    Launcher launcher(system, config);
    while (auto target = direct ? direct : launcher.run()) {
        const MetaEngine* meta = findEngine(target->engineId);
        if (!meta) {
            launcher.setStatus("Unknown engine '" + target->engineId + "' for " + target->target);
            continue;
        }

        // Each game's own files (settings, saves) go in a folder named after its target.
        target->savePath = dataDir() / "games" / target->target;
        SDL_Log("Starting %s (%s)", target->description.c_str(), meta->name());
        auto engine = meta->create(system, *target);
        Error err = engine->run();
        engine.reset();

        if (direct) {
            if (err)
                std::fprintf(stderr, "%s\n", err.message.c_str());
            return err ? 1 : 0;
        }
        if (system.quitRequested())
            break;
        launcher.setStatus(err ? target->description + ": " + err.message
                               : "Returned from " + target->description);
    }
    return 0;
}
