// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/sound.h"

#include "engines/unreal/properties.h"

#include "common/audio/decode.h"
#include "common/fs.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::unreal {

Error readEmbeddedFile(const Package& package, int index, EmbeddedFile& out) {
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    const Export& e = package.exports()[size_t(index)];
    const std::string what = package.path(index + 1);
    Reader r(data, size_t(e.serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    // The format's name, then the file as a byte array (from version 63
    // with where it ends first, for skipping it).
    out.format = package.name(r.index());
    if (package.version() >= 63)
        r.i32();
    const int32_t size = r.index();
    if (!r.ok() || size < 0 || size_t(size) > r.remaining())
        return Error::fail(what + ": data runs past the end");
    out.data.resize(size_t(size));
    r.bytes(out.data.data(), out.data.size());
    return Error::success();
}

bool EmbeddedFile::isPlaceholder() const {
    return data.size() < 16 && !(data.size() >= 4 && std::memcmp(data.data(), "RIFF", 4) == 0);
}

Error decodeEmbeddedSound(const EmbeddedFile& file, audio::Sound& out) {
    if (file.isPlaceholder())
        return Error::fail("a placeholder with no sound in it (" + std::to_string(file.data.size()) + " bytes)");
    if (Error err = audio::decodeSound(file.data, out)) {
        std::string head;
        for (size_t i = 0; i < file.data.size() && i < 8; ++i) {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%02x", file.data[i]);
            head += hex;
            head += i == 7 || i + 1 == file.data.size() ? "" : " ";
        }
        std::string text;
        for (size_t i = 0; i < file.data.size() && i < 4; ++i)
            text += std::isprint(file.data[i]) ? char(file.data[i]) : '.';
        return Error::fail("format " + file.format + ", " + std::to_string(file.data.size()) + " bytes starting " +
                           head + " (\"" + text + "\"): " + err.message);
    }
    return Error::success();
}

std::optional<std::filesystem::path> streamedFile(const PackageSet& packages, const Package& package, int index) {
    if (packages.gameDir().empty() || index < 0 || size_t(index) >= package.exports().size())
        return std::nullopt;
    return fs::findFile(packages.gameDir(), "mp3/" + package.name(package.exports()[size_t(index)].objectName) + ".mp3");
}

Error loadSound(const PackageSet& packages, const Package& package, int index, audio::Sound& out, std::string* from) {
    EmbeddedFile file;
    if (Error err = readEmbeddedFile(package, index, file))
        return err;
    if (from)
        from->clear();
    if (!file.isPlaceholder())
        return decodeEmbeddedSound(file, out);
    const auto mp3 = streamedFile(packages, package, index);
    if (!mp3)
        return Error::fail("a placeholder with no sound in it, and no mp3/" +
                           package.name(package.exports()[size_t(index)].objectName) + ".mp3");
    std::ifstream in(*mp3, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (Error err = audio::decodeSound(data, out))
        return Error::fail(mp3->string() + ": " + err.message);
    if (from)
        *from = mp3->string();
    return Error::success();
}

} // namespace reluva::unreal
