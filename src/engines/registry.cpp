// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/registry.h"

namespace reluva {

// Each engine exposes one accessor. Add new engines here, guarded by the
// RELUVA_ENGINE_* define CMake sets when the engine is enabled.
const MetaEngine& testbedMetaEngine();
#ifdef RELUVA_ENGINE_NEWWORLDS
const MetaEngine& newWorldsMetaEngine();
#endif
#ifdef RELUVA_ENGINE_IDTECH3
const MetaEngine& idTech3MetaEngine();
#endif

const std::vector<const MetaEngine*>& allEngines() {
    static const std::vector<const MetaEngine*> engines = {
        &testbedMetaEngine(),
#ifdef RELUVA_ENGINE_NEWWORLDS
        &newWorldsMetaEngine(),
#endif
#ifdef RELUVA_ENGINE_IDTECH3
        &idTech3MetaEngine(),
#endif
    };
    return engines;
}

const MetaEngine* findEngine(std::string_view id) {
    for (const MetaEngine* e : allEngines())
        if (id == e->id())
            return e;
    return nullptr;
}

std::vector<DetectedGame> detectGames(const std::filesystem::path& dir) {
    std::vector<DetectedGame> all;
    for (const MetaEngine* e : allEngines()) {
        auto found = e->detect(dir);
        all.insert(all.end(), found.begin(), found.end());
    }
    return all;
}

} // namespace reluva
