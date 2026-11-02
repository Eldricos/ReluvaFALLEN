// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Writes an Unreal package (version 73, as The Fallen's), starting from
// another's tables: its names, imports and exports keep their numbers, so
// an export's data copied as it is still refers to the right objects. More
// names, imports and exports are added after them. See
// docs/formats/unreal/package.md.

#include "common/types.h"
#include "engines/unreal/package.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::unreal {

// Little-endian values and Unreal's compact indexes, into a buffer.
class ByteWriter {
public:
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void i32(int32_t v) { u32(uint32_t(v)); }
    void f32(float v);
    void index(int32_t v);
    // A string as version 64 and later keep them: a compact length,
    // counting the NUL, then the letters and the NUL.
    void string(const std::string& s);
    void bytes(const void* p, size_t n);
    void append(const std::vector<uint8_t>& v) { data.insert(data.end(), v.begin(), v.end()); }
    std::vector<uint8_t> data;
};

class PackageWriter {
public:
    // Starts from `base`'s tables, each export's data its own (copied when
    // written unless replaced).
    explicit PackageWriter(const Package& base);

    // A name's index, added if it's new.
    int32_t name(const std::string& text);
    // A reference to an import, added (with the packages and groups it's
    // in) if it's new: `path` "Package.Group.Object", `classPackage` and
    // `className` its class's ("Engine", "Texture").
    int32_t import(const std::string& path, const std::string& classPackage, const std::string& className);
    // An import of `name` in another import (a state in its class).
    int32_t importIn(int32_t outer, const std::string& name, const std::string& classPackage,
                     const std::string& className);
    // A new export; returns its reference (index + 1).
    int32_t addExport(int32_t classRef, int32_t outerRef, const std::string& objectName, uint32_t flags);
    void setFlags(int32_t ref, uint32_t flags) { exports_[size_t(ref - 1)].e.flags = flags; }
    // An export's data, in place of what the base has.
    void setData(int32_t ref, std::vector<uint8_t> data);
    // Left out: written with no data (an object the level no longer has).
    void drop(int32_t ref);
    size_t exportCount() const { return exports_.size(); }

    Error write(const std::string& path) const;

private:
    const Package& base_;
    std::vector<NameEntry> names_;
    std::map<std::string, int32_t> nameIndex_; // lower case
    std::vector<Import> imports_;
    std::map<std::string, int32_t> importIndex_; // "classpackage.classname path", lower case
    struct Out {
        Export e;
        bool replaced = false, dropped = false;
        std::vector<uint8_t> data;
    };
    std::vector<Out> exports_;
};

} // namespace reluva::unreal
