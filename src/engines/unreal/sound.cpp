// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/sound.h"

#include "engines/unreal/properties.h"

#include "common/audio/decode.h"

#include <cctype>
#include <cstdio>
#include <cstring>

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

} // namespace reluva::unreal
