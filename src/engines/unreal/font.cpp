// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/font.h"

#include "engines/unreal/properties.h"

namespace reluva::unreal {

// After its properties: the pages (a compact count; each its texture's
// object reference, then a compact count of characters, each its start U,
// start V, width and height as 32-bit ints), then how many characters a
// page holds (an int). Checked on The Fallen's: every one of Engine.u's
// fonts is one page of 256 characters and reads to its end.
Error readFont(const PackageSet& packages, const Package& package, int index, Font& out) {
    out = {};
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(package.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(package, index, r, props))
        return err;
    const int32_t pages = r.index();
    if (!r.ok() || pages < 0 || pages > 256)
        return Error::fail("font: " + std::to_string(pages) + " pages");
    for (int32_t p = 0; p < pages && r.ok(); ++p) {
        FontPage page;
        page.texture = packages.resolve(package, r.index());
        const int32_t count = r.index();
        if (!r.ok() || count < 0 || size_t(count) * 16 > r.remaining())
            return Error::fail("font: a page's characters don't fit");
        page.characters.resize(size_t(count));
        for (FontCharacter& c : page.characters) {
            c.u = r.i32();
            c.v = r.i32();
            c.width = r.i32();
            c.height = r.i32();
        }
        out.pages.push_back(std::move(page));
    }
    if (r.remaining() >= 4)
        out.charactersPerPage = std::max(1, r.i32());
    return r.ok() ? Error::success() : Error::fail("font: runs past the end");
}

const FontCharacter* Font::character(uint32_t c, const FontPage** page) const {
    const size_t p = c / uint32_t(charactersPerPage), i = c % uint32_t(charactersPerPage);
    if (p >= pages.size() || i >= pages[p].characters.size())
        return nullptr;
    if (page)
        *page = &pages[p];
    return &pages[p].characters[i];
}

} // namespace reluva::unreal
