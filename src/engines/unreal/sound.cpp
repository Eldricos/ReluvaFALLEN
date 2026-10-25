// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/sound.h"

#include "engines/unreal/properties.h"

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

} // namespace reluva::unreal
