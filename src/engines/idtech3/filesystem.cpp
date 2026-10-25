// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/filesystem.h"

#include "common/fs.h"
#include "common/zip.h"

#include <algorithm>
#include <memory>

namespace reluva::idtech3 {

Error mountGameDir(SearchSet& set, const std::filesystem::path& install, std::string_view gameDir, int layer,
                   std::vector<std::string>& warnings) {
    const auto dir = fs::findDirectory(install, gameDir);
    if (!dir)
        return Error::fail("No " + std::string(gameDir) + " folder in " + install.string());

    std::vector<std::filesystem::path> paks;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(*dir, ec), end; !ec && it != end; it.increment(ec))
        if (fs::toLower(it->path().extension().string()) == ".pk3" && it->is_regular_file(ec))
            paks.push_back(it->path());
    // Compared in upper case: that puts '_' after the letters, unlike lower
    // case, and decides which pak overrides which.
    auto upper = [](const std::filesystem::path& p) {
        std::string s = p.filename().string();
        for (char& c : s)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        return s;
    };
    std::sort(paks.begin(), paks.end(), [&](const auto& a, const auto& b) { return upper(a) < upper(b); });
    if (paks.size() >= kPrioritiesPerGameDir)
        return Error::fail("Too many .pk3 files in " + dir->string());

    const int base = layer * kPrioritiesPerGameDir;
    int priority = base;
    for (const auto& p : paks) {
        auto zip = std::make_unique<ZipArchive>();
        if (Error err = zip->open(p)) {
            warnings.push_back(err.message);
            continue;
        }
        set.add(std::move(zip), ++priority);
    }

    // The loose files, above the folder's own archives. They include the
    // .pk3 files themselves, which is harmless.
    auto loose = std::make_unique<DirectoryArchive>();
    if (Error err = loose->open(*dir))
        return err;
    set.add(std::move(loose), base + kPrioritiesPerGameDir - 1);
    return Error::success();
}

} // namespace reluva::idtech3
