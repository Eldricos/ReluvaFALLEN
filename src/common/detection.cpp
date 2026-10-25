// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/detection.h"
#include "common/fs.h"

#include <map>
#include <optional>

namespace reluva {

std::vector<DetectedGame> detectFromTable(const std::string& engineId,
                                          const std::vector<GameVariant>& table,
                                          const std::filesystem::path& dir) {
    // Many variants share files, so cache per-file results.
    std::map<std::string, std::optional<std::filesystem::path>> found;
    std::map<std::string, std::optional<std::string>> hashes;

    auto locate = [&](const std::string& name) -> const std::optional<std::filesystem::path>& {
        auto it = found.find(name);
        if (it == found.end())
            it = found.emplace(name, fs::findFile(dir, name)).first;
        return it->second;
    };

    std::vector<DetectedGame> results;
    for (const GameVariant& variant : table) {
        bool matches = !variant.files.empty();
        for (const FileSignature& sig : variant.files) {
            const auto& path = locate(sig.name);
            if (!path) {
                matches = false;
                break;
            }
            if (sig.size >= 0) {
                std::error_code ec;
                auto size = std::filesystem::file_size(*path, ec);
                if (ec || static_cast<int64_t>(size) != sig.size) {
                    matches = false;
                    break;
                }
            }
            if (!sig.md5.empty()) {
                auto key = path->string();
                auto it = hashes.find(key);
                if (it == hashes.end())
                    it = hashes.emplace(key, fs::md5File(*path, kDetectionHashBytes)).first;
                if (!it->second || *it->second != sig.md5) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches)
            results.push_back({engineId, variant.gameId, variant.description, variant.language,
                               variant.platform});
    }
    return results;
}

} // namespace reluva
