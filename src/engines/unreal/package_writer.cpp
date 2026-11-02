// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/package_writer.h"

#include "common/fs.h"

#include <cstring>
#include <fstream>

namespace reluva::unreal {

void ByteWriter::u16(uint16_t v) {
    u8(uint8_t(v));
    u8(uint8_t(v >> 8));
}

void ByteWriter::u32(uint32_t v) {
    for (int k = 0; k < 4; ++k)
        u8(uint8_t(v >> (8 * k)));
}

void ByteWriter::f32(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    u32(u);
}

// A compact index: the first byte its sign (0x80), whether more follow
// (0x40) and six bits; each next byte whether more follow (0x80) and
// seven bits.
void ByteWriter::index(int32_t v) {
    uint32_t n = v < 0 ? uint32_t(-int64_t(v)) : uint32_t(v);
    uint8_t first = uint8_t(n & 0x3f) | (v < 0 ? 0x80 : 0);
    n >>= 6;
    if (n)
        first |= 0x40;
    u8(first);
    while (n) {
        uint8_t b = uint8_t(n & 0x7f);
        n >>= 7;
        if (n)
            b |= 0x80;
        u8(b);
    }
}

void ByteWriter::string(const std::string& s) {
    index(int32_t(s.size() + 1));
    bytes(s.data(), s.size());
    u8(0);
}

void ByteWriter::bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    data.insert(data.end(), b, b + n);
}

namespace {

// An import's path in the package that has it: its outers' names and its own.
std::string importPath(const Package& p, const std::vector<Import>& imports, int32_t ref) {
    std::string path;
    while (ref < 0 && size_t(-ref - 1) < imports.size()) {
        const Import& i = imports[size_t(-ref - 1)];
        const std::string n = size_t(i.objectName) < p.names().size() ? p.names()[size_t(i.objectName)].name : "";
        path = path.empty() ? n : n + "." + path;
        ref = i.package;
    }
    return path;
}

} // namespace

PackageWriter::PackageWriter(const Package& base) : base_(base) {
    names_ = base.names();
    for (size_t i = 0; i < names_.size(); ++i)
        nameIndex_.emplace(fs::toLower(names_[i].name), int32_t(i));
    imports_ = base.imports();
    for (size_t i = 0; i < imports_.size(); ++i) {
        const Import& im = imports_[i];
        const std::string key = fs::toLower(names_[size_t(im.classPackage)].name + "." +
                                            names_[size_t(im.className)].name + " " +
                                            importPath(base, imports_, -int32_t(i) - 1));
        importIndex_.emplace(key, -int32_t(i) - 1);
    }
    for (const Export& e : base.exports()) {
        Out o;
        o.e = e;
        exports_.push_back(std::move(o));
    }
}

int32_t PackageWriter::name(const std::string& text) {
    const std::string key = fs::toLower(text);
    if (auto it = nameIndex_.find(key); it != nameIndex_.end())
        return it->second;
    names_.push_back({text, 0x00070010});
    const int32_t n = int32_t(names_.size() - 1);
    nameIndex_.emplace(key, n);
    return n;
}

int32_t PackageWriter::import(const std::string& path, const std::string& classPackage, const std::string& className) {
    const std::string key = fs::toLower(classPackage + "." + className + " " + path);
    if (auto it = importIndex_.find(key); it != importIndex_.end())
        return it->second;
    // Its outer: the package (or a group) it's in, made too.
    int32_t outer = 0;
    std::string own = path;
    if (const size_t dot = path.rfind('.'); dot != std::string::npos) {
        const std::string up = path.substr(0, dot);
        outer = import(up, "Core", "Package");
        own = path.substr(dot + 1);
    }
    Import im;
    im.classPackage = name(classPackage);
    im.className = name(className);
    im.package = outer;
    im.objectName = name(own);
    imports_.push_back(im);
    const int32_t ref = -int32_t(imports_.size());
    importIndex_.emplace(key, ref);
    return ref;
}

int32_t PackageWriter::importIn(int32_t outer, const std::string& own, const std::string& classPackage,
                                const std::string& className) {
    for (size_t i = 0; i < imports_.size(); ++i) {
        const Import& im = imports_[i];
        if (im.package == outer && fs::toLower(names_[size_t(im.objectName)].name) == fs::toLower(own) &&
            fs::toLower(names_[size_t(im.className)].name) == fs::toLower(className))
            return -int32_t(i) - 1;
    }
    Import im;
    im.classPackage = name(classPackage);
    im.className = name(className);
    im.package = outer;
    im.objectName = name(own);
    imports_.push_back(im);
    return -int32_t(imports_.size());
}

int32_t PackageWriter::addExport(int32_t classRef, int32_t outerRef, const std::string& objectName, uint32_t flags) {
    Out o;
    o.e.classRef = classRef;
    o.e.package = outerRef;
    o.e.objectName = name(objectName);
    o.e.flags = flags;
    o.replaced = true;
    exports_.push_back(std::move(o));
    return int32_t(exports_.size());
}

void PackageWriter::setData(int32_t ref, std::vector<uint8_t> data) {
    Out& o = exports_[size_t(ref - 1)];
    o.data = std::move(data);
    o.replaced = true;
    o.dropped = false;
}

void PackageWriter::drop(int32_t ref) {
    Out& o = exports_[size_t(ref - 1)];
    o.data.clear();
    o.replaced = true;
    o.dropped = true;
}

// The header (tag, version, licensee, flags; the names', exports' and
// imports' counts and places; a GUID; one generation: the export and name
// counts), the names, the exports' data, the imports, the exports.
Error PackageWriter::write(const std::string& path) const {
    ByteWriter w;
    w.data.resize(0x40);
    const size_t nameOffset = w.data.size();
    for (const NameEntry& n : names_) {
        w.string(n.name);
        w.u32(n.flags);
    }
    std::vector<std::pair<int32_t, int32_t>> placed(exports_.size()); // size, offset
    for (size_t i = 0; i < exports_.size(); ++i) {
        const Out& o = exports_[i];
        std::vector<uint8_t> copy;
        const std::vector<uint8_t>* data = &o.data;
        if (!o.replaced && o.e.serialSize > 0) {
            if (Error err = base_.readExport(int(i), copy))
                return err;
            data = &copy;
        }
        placed[i] = {int32_t(data->size()), int32_t(w.data.size())};
        w.append(*data);
    }
    const size_t importOffset = w.data.size();
    for (const Import& im : imports_) {
        w.index(im.classPackage);
        w.index(im.className);
        w.i32(im.package);
        w.index(im.objectName);
    }
    const size_t exportOffset = w.data.size();
    for (size_t i = 0; i < exports_.size(); ++i) {
        const Export& e = exports_[i].e;
        w.index(e.classRef);
        w.index(e.superRef);
        w.i32(e.package);
        w.index(e.objectName);
        w.u32(e.flags);
        w.index(placed[i].first);
        if (placed[i].first > 0)
            w.index(placed[i].second);
    }
    ByteWriter h;
    h.u32(kPackageTag);
    h.u16(73);
    h.u16(0);
    h.u32(base_.flags());
    h.u32(uint32_t(names_.size()));
    h.u32(uint32_t(nameOffset));
    h.u32(uint32_t(exports_.size()));
    h.u32(uint32_t(exportOffset));
    h.u32(uint32_t(imports_.size()));
    h.u32(uint32_t(importOffset));
    // A GUID: made from the counts and sizes (any 16 bytes do).
    for (uint32_t k : {uint32_t(w.data.size()), uint32_t(names_.size() * 2654435761u),
                       uint32_t(exports_.size() * 40503u), uint32_t(imports_.size() * 69069u)})
        h.u32(k);
    h.u32(1);
    h.u32(uint32_t(exports_.size()));
    h.u32(uint32_t(names_.size()));
    std::memcpy(w.data.data(), h.data.data(), h.data.size());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.write(reinterpret_cast<const char*>(w.data.data()), std::streamsize(w.data.size())))
        return Error::fail(path + ": can't write");
    return Error::success();
}

} // namespace reluva::unreal
