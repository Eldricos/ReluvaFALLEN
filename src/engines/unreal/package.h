// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Unreal Engine 1 packages (.u, .unr, .utx, .uax, .umx): every file the
// engine loads is one. A package holds a table of names, the objects it
// takes from other packages (imports) and the objects it defines
// (exports), each export's data stored at its own offset. See
// docs/formats/unreal/package.md.

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::unreal {

constexpr uint32_t kPackageTag = 0x9E2A83C1;

// Object flags that change how an export's data starts.
namespace objflag {
constexpr uint32_t HasStack = 0x02000000; // a state frame comes before the properties
} // namespace objflag

// Reads little-endian values from a buffer, bounds-checked: reading past
// the end sets a failure flag and returns zeros, so callers check ok()
// once after a run of reads. Positions are absolute: `base` is the file
// offset of data[0] (an export's data read on its own still sees the
// offsets the file stores).
class Reader {
public:
    Reader(const uint8_t* data, size_t size, size_t base = 0) : data_(data), size_(size), base_(base) {}
    explicit Reader(const std::vector<uint8_t>& data, size_t base = 0) : Reader(data.data(), data.size(), base) {}

    bool ok() const { return ok_; }
    void fail() { ok_ = false; }
    size_t pos() const { return base_ + pos_; }
    size_t end() const { return base_ + size_; }
    size_t remaining() const { return size_ - pos_; }
    void seek(size_t absolute);
    void skip(size_t n);

    uint8_t u8();
    uint16_t u16();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32();
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64();
    float f32();
    // The engine's compact integer: 1 to 5 bytes, see package.md.
    int32_t index();
    bool bytes(void* out, size_t n);
    // A string as packages of `version` store them: before 64 NUL
    // terminated, from 64 a compact length (counting the NUL) first.
    std::string string(int version);

private:
    const uint8_t* data_;
    size_t size_, base_;
    size_t pos_ = 0;
    bool ok_ = true;
};

struct NameEntry {
    std::string name;
    uint32_t flags = 0;
};

// An object another package defines. Names are indices into the name table.
struct Import {
    int32_t classPackage = 0, className = 0;
    int32_t package = 0; // object reference: the import's outer (its group or package)
    int32_t objectName = 0;
};

// An object this package defines.
struct Export {
    int32_t classRef = 0;  // object reference; 0: the object is a class
    int32_t superRef = 0;  // object reference: what a class or struct extends
    int32_t package = 0;   // object reference: the outer (a group), 0 for the package itself
    int32_t objectName = 0;
    uint32_t flags = 0;
    int32_t serialSize = 0, serialOffset = 0;
};

// Object references, as packages store them: > 0 the export at ref - 1,
// < 0 the import at -ref - 1, 0 none.
inline bool isExport(int32_t ref) { return ref > 0; }
inline bool isImport(int32_t ref) { return ref < 0; }
inline int exportIndex(int32_t ref) { return ref - 1; }
inline int importIndex(int32_t ref) { return -ref - 1; }

class Package {
public:
    // Reads the header and tables from a file; export data is read from the
    // file when asked for. `name` defaults to the file's stem.
    Error open(const std::filesystem::path& file);
    // The same from a whole file in memory (kept).
    Error openMemory(std::vector<uint8_t> data, std::string name);

    const std::string& name() const { return name_; }
    const std::filesystem::path& file() const { return file_; }
    int version() const { return version_; }
    int licensee() const { return licensee_; }
    uint32_t flags() const { return flags_; }
    uint64_t fileSize() const { return fileSize_; }

    const std::vector<NameEntry>& names() const { return names_; }
    const std::vector<Import>& imports() const { return imports_; }
    const std::vector<Export>& exports() const { return exports_; }

    // A name table entry; "" when out of range.
    const std::string& name(int32_t index) const;
    // An object's own name and its class's name ("Class" for classes).
    std::string objectName(int32_t ref) const;
    std::string className(int32_t ref) const;
    // Its outers' names and its own, joined by dots ("Group.Name"); for an
    // import, starting with the package it comes from.
    std::string path(int32_t ref) const;
    // The package an import comes from: its outermost outer's name.
    std::string importPackage(int index) const;

    // The first export of that name (and class, unless empty), ignoring
    // case; -1 if none.
    int findExport(std::string_view objectName, std::string_view className = {}) const;
    // Every export whose class has that name, ignoring case.
    std::vector<int> exportsOfClass(std::string_view className) const;

    // An export's data, and a reader over it at its file offset.
    Error readExport(int index, std::vector<uint8_t>& out) const;

private:
    Error parse(const std::vector<uint8_t>& data);

    std::string name_;
    std::filesystem::path file_;
    std::vector<uint8_t> memory_; // the whole file, for packages opened from memory
    int version_ = 0, licensee_ = 0;
    uint32_t flags_ = 0;
    uint64_t fileSize_ = 0;
    std::vector<NameEntry> names_;
    std::vector<Import> imports_;
    std::vector<Export> exports_;
};

} // namespace reluva::unreal
