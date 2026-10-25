// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/package_set.h"

#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

namespace reluva::unreal {

namespace {

std::string trim(std::string_view s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

// The folder a search path names, relative to the System folder, found
// ignoring case ("..\\Textures" -> <game>/Textures).
std::optional<std::filesystem::path> resolveFolder(const std::filesystem::path& systemDir, std::string folder) {
    std::replace(folder.begin(), folder.end(), '\\', '/');
    std::filesystem::path current = systemDir;
    size_t start = 0;
    while (start <= folder.size()) {
        size_t slash = folder.find('/', start);
        if (slash == std::string::npos)
            slash = folder.size();
        const std::string part = folder.substr(start, slash - start);
        if (part == "..")
            current = current.parent_path();
        else if (!part.empty() && part != ".") {
            auto found = fs::findDirectory(current, part);
            if (!found)
                return std::nullopt;
            current = *found;
        }
        start = slash + 1;
    }
    return current;
}

} // namespace

std::vector<std::string> defaultSearchPaths() {
    return {"*.u", "../Maps/*.unr", "../Textures/*.utx", "../Sounds/*.uax", "../Music/*.umx"};
}

std::vector<std::string> parseSearchPaths(std::string_view ini) {
    std::vector<std::string> out;
    bool inSection = false;
    size_t start = 0;
    while (start < ini.size()) {
        size_t nl = ini.find('\n', start);
        if (nl == std::string_view::npos)
            nl = ini.size();
        const std::string line = trim(ini.substr(start, nl - start));
        start = nl + 1;
        if (line.empty() || line[0] == ';')
            continue;
        if (line.front() == '[') {
            inSection = fs::toLower(line) == "[core.system]";
            continue;
        }
        const size_t eq = line.find('=');
        if (!inSection || eq == std::string::npos)
            continue;
        if (fs::toLower(trim(std::string_view(line).substr(0, eq))) == "paths")
            out.push_back(trim(std::string_view(line).substr(eq + 1)));
    }
    return out;
}

Error PackageSet::open(const std::filesystem::path& gameDir, std::string_view iniName,
                       std::vector<std::string>* warnings) {
    entries_.clear();
    paths_.clear();
    mapExtensions_ = {".unr"};
    ini_.clear();
    const auto systemDir = fs::findDirectory(gameDir, "System");
    if (!systemDir)
        return Error::fail("No System folder in " + gameDir.string());

    auto readIni = [&](const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto paths = parseSearchPaths(text);
        if (!paths.empty()) {
            paths_ = std::move(paths);
            ini_ = file;
        }
    };
    // The game's own settings, else the defaults it makes them from.
    for (std::string_view name : {iniName, std::string_view("Default.ini")})
        if (paths_.empty() && !name.empty())
            if (auto file = fs::findFile(*systemDir, name))
                readIni(*file);
    if (paths_.empty()) {
        std::error_code ec;
        std::vector<std::filesystem::path> inis;
        for (std::filesystem::directory_iterator it(*systemDir, ec), end; !ec && it != end; it.increment(ec))
            if (fs::toLower(it->path().extension().string()) == ".ini")
                inis.push_back(it->path());
        std::sort(inis.begin(), inis.end());
        for (const auto& f : inis) {
            readIni(f);
            if (!paths_.empty())
                break;
        }
    }
    if (paths_.empty())
        paths_ = defaultSearchPaths();

    for (const std::string& p : paths_) {
        std::string spec = p;
        std::replace(spec.begin(), spec.end(), '\\', '/');
        const size_t slash = spec.rfind('/');
        const std::string folder = slash == std::string::npos ? "" : spec.substr(0, slash);
        const std::string pattern = fs::toLower(slash == std::string::npos ? spec : spec.substr(slash + 1));
        // Only "*.ext" patterns are used in practice.
        if (pattern.rfind("*.", 0) != 0) {
            if (warnings)
                warnings->push_back("Search path " + p + " isn't of the form folder/*.ext; skipped");
            continue;
        }
        const std::string ext = pattern.substr(1);
        const std::string lastFolder = fs::toLower(std::filesystem::path(folder).filename().string());
        if (lastFolder == "maps" && std::find(mapExtensions_.begin(), mapExtensions_.end(), ext) == mapExtensions_.end())
            mapExtensions_.push_back(ext);
        const auto dir = resolveFolder(*systemDir, folder);
        if (!dir) {
            if (warnings)
                warnings->push_back("Search path " + p + ": no such folder");
            continue;
        }
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(*dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && fs::toLower(it->path().extension().string()) == ext)
                files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files)
            addFile(f);
    }
    if (entries_.empty())
        return Error::fail("No packages found from " + systemDir->string());
    return Error::success();
}

void PackageSet::addFile(const std::filesystem::path& file) {
    const std::string name = file.stem().string();
    Entry e;
    e.name = name;
    e.extension = fs::toLower(file.extension().string());
    e.file = file;
    entries_.try_emplace(fs::toLower(name), std::move(e));
}

void PackageSet::addMemory(std::string name, std::string extension, std::vector<uint8_t> data) {
    Entry e;
    e.name = name;
    e.extension = fs::toLower(extension);
    e.memory = std::move(data);
    entries_.try_emplace(fs::toLower(name), std::move(e));
}

const Package* PackageSet::get(std::string_view name) const {
    auto it = entries_.find(fs::toLower(name));
    if (it == entries_.end())
        return nullptr;
    const Entry& e = it->second;
    if (!e.tried) {
        e.tried = true;
        auto pkg = std::make_unique<Package>();
        Error err = e.file.empty() ? pkg->openMemory(e.memory, e.name) : pkg->open(e.file);
        if (err)
            e.error = err.message;
        else
            e.package = std::move(pkg);
    }
    return e.package.get();
}

std::string PackageSet::error(std::string_view name) const {
    auto it = entries_.find(fs::toLower(name));
    if (it == entries_.end())
        return "No package " + std::string(name);
    return it->second.error;
}

ObjectRef PackageSet::resolve(const Package& from, int32_t ref) const {
    if (isExport(ref))
        return size_t(exportIndex(ref)) < from.exports().size() ? ObjectRef{&from, exportIndex(ref)} : ObjectRef{};
    if (!isImport(ref) || size_t(importIndex(ref)) >= from.imports().size())
        return {};
    const Import& imp = from.imports()[size_t(importIndex(ref))];
    const Package* target = get(from.importPackage(importIndex(ref)));
    if (!target)
        return {};
    // The import's groups, innermost first, to tell apart objects of the
    // same name in different groups.
    std::vector<std::string> groups;
    for (int32_t outer = imp.package, depth = 0; isImport(outer) && size_t(importIndex(outer)) < from.imports().size() && depth < 64; ++depth) {
        const Import& o = from.imports()[size_t(importIndex(outer))];
        if (o.package == 0)
            break; // the package itself
        groups.push_back(fs::toLower(from.name(o.objectName)));
        outer = o.package;
    }
    const std::string wanted = fs::toLower(from.name(imp.objectName));
    const std::string wantedClass = fs::toLower(from.name(imp.className));
    int fallback = -1;
    for (size_t i = 0; i < target->exports().size(); ++i) {
        const Export& e = target->exports()[i];
        if (fs::toLower(target->name(e.objectName)) != wanted ||
            fs::toLower(target->className(int32_t(i) + 1)) != wantedClass)
            continue;
        std::vector<std::string> outers;
        for (int32_t outer = e.package, depth = 0; isExport(outer) && size_t(exportIndex(outer)) < target->exports().size() && depth < 64; ++depth) {
            const Export& o = target->exports()[size_t(exportIndex(outer))];
            outers.push_back(fs::toLower(target->name(o.objectName)));
            outer = o.package;
        }
        if (outers == groups)
            return {target, int(i)};
        if (fallback < 0)
            fallback = int(i);
    }
    return fallback >= 0 ? ObjectRef{target, fallback} : ObjectRef{};
}

std::vector<std::string> PackageSet::names(std::string_view extension) const {
    std::vector<std::string> out;
    const std::string ext = fs::toLower(extension);
    for (const auto& [key, e] : entries_)
        if (ext.empty() || e.extension == ext)
            out.push_back(e.name);
    return out;
}

std::string PackageSet::extension(std::string_view name) const {
    auto it = entries_.find(fs::toLower(name));
    return it == entries_.end() ? std::string() : it->second.extension;
}

bool PackageSet::isMap(std::string_view name) const {
    const std::string ext = extension(name);
    return !ext.empty() && std::find(mapExtensions_.begin(), mapExtensions_.end(), ext) != mapExtensions_.end();
}

std::vector<std::string> PackageSet::mapNames() const {
    std::vector<std::string> out;
    for (const auto& [key, e] : entries_)
        if (isMap(key))
            out.push_back(e.name);
    return out;
}

} // namespace reluva::unreal
