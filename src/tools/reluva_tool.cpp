// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool: developer utilities for examining game data files.
//
//   reluva-tool hexdump <file> [offset] [length]   offset may be hex (0x..) or
//                                                  negative (from the end)
//   reluva-tool cat-probe <file> [-q]              check a .cat file against the documented
//                                                  layout and work out unknown entry kinds
//   reluva-tool cat-list <file>                    list a .cat file's contents
//   reluva-tool cat-extract <file> <dir> [filter]  extract files whose name contains filter
//   reluva-tool stb-dump <file.stb | file.cat>     print a string table, or list an archive's
//   reluva-tool tex-survey <file.cat>...           group every texture by header layout
//   reluva-tool tex-keys <file.cat>...             compare each colour key with the image corners
//   reluva-tool chunks <file>                      try to read any file as a (u16 tag, u32 size) chunk tree
//   reluva-tool words <file> [offset] [count]      show 4-byte values as hex, integer and float
//   reluva-tool table <file> [per row] [offset]    show 4-byte values compactly, N per row
//   reluva-tool geo-info <file.geo> [out.obj]      decode a model; optionally export it as OBJ
//   reluva-tool geo-survey <file.cat>...           decode every model, group failures by reason
//   reluva-tool geo-textures [-a] <file.cat>...    show which textures each model gets (by default
//                                                  only models with too few, with similar names)
//   reluva-tool mdl-info <file.mdl | file.cat>     show a metamodel's names and states, or survey every
//                                                  one in an archive
//   reluva-tool wav-info <file.wav | file.mp3 | file.cat>     show a sound's format and length, or survey an archive's
//   reluva-tool wav-decode <in.wav | in.mp3> <out.wav>      decode a sound to 16-bit PCM
//   reluva-tool fif-info <file.fif | file.cat>     show a font's glyph table, or list an archive's fonts
//   reluva-tool level-info <level.dat> [file.cat...] list the missions, with the archives holding their maps
//   reluva-tool gmf-fit [-a] <planet.cat> [map]    check the model numbering by how tiles' edges meet
//   reluva-tool geo-slots <file.geo>...             per texture number: faces, area, facing, texture coordinates
//   reluva-tool ground-textures <planet.cat>      show the textures the map viewer gives each ground
//                                                  tile model's slots, with each slot's face count
//   reluva-tool gmf-dump <planet.cat> <out dir>    write each map's tiles, grid and objects as CSV
//   reluva-tool geo-sheet [-l|-t] <out.ppm> <filter> <file.cat>...  draw models to a contact sheet, two
//                                                  views each (-l: larger, four; -t: eight round it)
//   reluva-tool gmf-info <file.gmf> [record]       walk a map's records, summarise them, and dump
//                                                  one record (or where the walk stops)
//   reluva-tool geo-faces <file.geo>               walk a model's faces, showing each header and
//                                                  corner, and dump the bytes where the walk stops
//   reluva-tool lbp-info <file> [out]              decode a texture, show its key and colours,
//                                                  write <out>.bmp and <out>_alpha.bmp
//   reluva-tool bsp-check <game folder>            load every maps/*.bsp of an id Tech 3 game folder
//                                                  (e.g. .../BaseEF), report failures and corrections
//   reluva-tool pk3-list <game folder> [filter]    list the search path's files, with sizes and the
//                                                  archive each comes from
//   reluva-tool image-check <game folder> [map]    list the images maps' shaders use that can't be
//                                                  found (only maps whose name contains map)
//   reluva-tool model-check <game folder> [filter] [-v]  for every .md3 surface, the shader or image it
//                                                  names and whether it's found; where missing files
//                                                  of the same name are; -v lists every miss
//   reluva-tool qvm-info <game folder> <vm/x.qvm> [-s]  a QVM's sizes, functions, and the system calls its
//                                                  code makes (found in the code, not run); -s its strings
//   reluva-tool qvm-run <game folder> <vm/x.qvm> [command [arg...]] [-limit N]
//                                                  run vmMain(command, args) answering every system
//                                                  call with 0, and log each call and its arguments
//   reluva-tool qvm-refs <game folder> <vm/x.qvm> <address> <size>
//                                                  the constants in a QVM's code that point into a
//                                                  structure at address, by offset: which fields it uses
//   reluva-tool qvm-dis <game folder> <vm/x.qvm> <first> [count]   list instructions, with the text,
//   reluva-tool qvm-dis <game folder> <vm/x.qvm> -ref <address> [size]  floats and system calls they
//   reluva-tool qvm-dis <game folder> <vm/x.qvm> -str <text>  use; -ref: the code around each use of an
//                                                  address; -str: the functions using strings with the
//                                                  text, and the functions they call; -syscall: the code
//                                                  before every call to one system call; -blockcopies:
//                                                  the sizes of the structures the code copies whole;
//                                                  -fields: the offsets used through a global pointer;
//                                                  -syscalls first last: each system call in the range
//                                                  in short (calls, arguments, nearby texts);
//                                                  -func N: the function holding instruction N and
//                                                  the functions it calls
//   reluva-tool ui-run <game folder> [-frames N] [-menu M] [-size WxH] [-glwords N] [-trace] [-v]
//                                                  run Elite Force's menus (vm/ui.qvm) with the engine's
//                                                  system calls: Init, SetActiveMenu, Refresh; report
//                                                  what they print, register and draw
//   reluva-tool game-run <game folder> <map> [-frames N] [-noclient] [-walk [forward|back|left|right]] [-set name value] [-addbot 'name skill']... [-cgame [-events] [-entities N] [-cgameframes N] [-fire] [-altfire] [-weapon N|all] [-give] [-watch model]] [-buttons] [-trace]
//                                                  start a level with Elite Force's game logic
//                                                  (vm/qagame.qvm): Init with the map's entities, then
//                                                  frames, with a player (client 0) joining unless
//                                                  -noclient (-walk: it runs forward); report what it
//                                                  prints, sets and links; -cgame: then start the
//                                                  client game (vm/cgame.qvm) for the player
//                                                  (-entities N: dump N of its scene's entities
//                                                  word by word; -cgameframes N frames, -fire,
//                                                  -altfire held with -weapon N (all: each in turn),
//                                                  -give: every weapon first; and the effects
//                                                  seen reported); -buttons: hold each button bit
//                                                  in turn and report what it changes
//   reluva-tool file-show <game folder> <path>    print a file from the search path (a text file:
//                                                  a config, a script)
//   reluva-tool shader-show <game folder> <shader | model.md3 | model.mdr>  a shader's text as its script has it; for
//                                                  a model, its surfaces' shaders and theirs
//   reluva-tool shader-check <game folder>         count the shader directives the engine doesn't know
//   reluva-tool portal-check <game folder>         each map's portal and mirror surfaces and portal entities
//                                                  or draw yet, with example shaders

#include "common/audio/decode.h"
#include "common/audio/wav.h"
#ifdef RELUVA_ENGINE_NEWWORLDS
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/fif.h"
#include "engines/newworlds/string_table.h"
#include "engines/newworlds/texture.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/gmf.h"
#include "engines/newworlds/levels.h"
#include "engines/newworlds/ground_textures.h"
#include "engines/newworlds/mdl.h"
#include "engines/newworlds/model_textures.h"
#include "engines/newworlds/model_render.h"
#endif
#include "common/font.h"
#include "common/fs.h"
#include "common/zip.h"
#ifdef RELUVA_ENGINE_IDTECH3
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/shader.h"
#endif

#include <map>
#include <memory>
#include <set>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

bool readFile(const char* path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

uint16_t rd16(const Bytes& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0; }
uint32_t rd32(const Bytes& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 |
                                   uint32_t(d[o + 3]) << 24
                             : 0;
}

void hexdump(const Bytes& d, size_t off, size_t len) {
    size_t end = std::min(d.size(), off + len);
    for (size_t row = off; row < end; row += 16) {
        std::printf("%08zx: ", row);
        for (size_t i = 0; i < 16; ++i) {
            if (row + i < end)
                std::printf("%02x", d[row + i]);
            else
                std::printf("  ");
            if (i % 2)
                std::printf(" ");
        }
        std::printf(" ");
        for (size_t i = 0; i < 16 && row + i < end; ++i) {
            uint8_t c = d[row + i];
            std::putchar(c >= 32 && c < 127 ? c : '.');
        }
        std::printf("\n");
    }
}
#ifdef RELUVA_ENGINE_NEWWORLDS

// Unix time -> "YYYY-MM-DD HH:MM" (UTC), without platform time functions.
std::string formatTime(uint32_t t) {
    int64_t days = t / 86400, secs = t % 86400;
    int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = yoe + era * 400 + (month <= 2);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", int(year), int(month), int(day),
                  int(secs / 3600), int(secs / 60 % 60));
    return buf;
}
#endif // RELUVA_ENGINE_NEWWORLDS

long long parseNumber(const char* s) { return std::strtoll(s, nullptr, 0); }

int cmdHexdump(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool hexdump <file> [offset] [length]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    long long off = argc > 3 ? parseNumber(argv[3]) : 0;
    if (off < 0)
        off += static_cast<long long>(d.size());
    size_t len = argc > 4 ? size_t(parseNumber(argv[4])) : 256;
    hexdump(d, size_t(std::max(0LL, off)), len);
    return 0;
}
#ifdef RELUVA_ENGINE_NEWWORLDS

// Current understanding (see docs/formats/newworlds/cat.md): the file is a
// tree of chunks, each a u16 tag + u32 size + payload.
//   0x00 tag 1, size = rest of file
//   0x06 tag 5, size = table of contents: u32 count, then one entry per item
//        file entries are 334 bytes, folder entries 264
//   then the items themselves, each a chunk; folders (tag 0x29) contain the
//   chunks of the entries that follow them
// An entry's offset + the table size gives the absolute position of its chunk.
constexpr size_t kPathLen = 260;
constexpr size_t kFileEntrySize = 334;
constexpr size_t kFolderEntrySize = 264;
constexpr uint16_t kFolderTag = 0x29;

std::string readName(const Bytes& d, size_t p) {
    std::string n;
    for (size_t k = p; k < p + kPathLen && k < d.size() && d[k]; ++k) {
        if (d[k] < 32 || d[k] >= 127)
            return "<binary>";
        n += char(d[k]);
    }
    return n;
}

std::string extensionOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || name.find('\\', dot) != std::string::npos ? "" : name.substr(dot);
}

// Rules for walking the table. Start with what is known; learn the rest.
struct TagRule {
    uint16_t tag;
    size_t entrySize;
    bool container;
    bool learned;
};

struct Walker {
    const Bytes& d;
    uint32_t tocSize;
    uint8_t archiveByte;
    std::vector<TagRule> rules;

    TagRule rule(uint16_t tag) const {
        for (const TagRule& r : rules)
            if (r.tag == tag)
                return r;
        return tag == kFolderTag ? TagRule{tag, kFolderEntrySize, true, false} : TagRule{tag, kFileEntrySize, false, false};
    }
    void learn(const TagRule& r) {
        for (TagRule& x : rules)
            if (x.tag == r.tag) {
                x = r;
                return;
            }
        rules.push_back(r);
    }
    size_t chunkOf(size_t entryPos) const { return size_t(rd32(d, entryPos)) + tocSize; }
    bool validEntry(size_t q, int prevIndex, size_t expectedChunk) const {
        return q + 8 <= d.size() && int(rd16(d, q + 4)) > prevIndex && d[q + 6] == 0 && d[q + 7] == archiveByte &&
               chunkOf(q) == expectedChunk;
    }
};

int cmdCatProbe(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool cat-probe <file.cat> [-q]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const bool quiet = argc > 3 && std::strcmp(argv[3], "-q") == 0;

    const uint32_t tocSize = rd32(d, 8);
    const uint32_t count = rd32(d, 0x0c);
    const size_t tocEnd = 0x0c + size_t(tocSize);
    if (!quiet) {
        std::printf("file size %zu; root chunk tag %u size %u (%s)\n", d.size(), rd16(d, 0), rd32(d, 2),
                    rd32(d, 2) + 6 == d.size() ? "matches" : "MISMATCH");
        std::printf("table chunk tag %u size %u, ends at 0x%zx; %u entries\n\n", rd16(d, 6), tocSize, tocEnd, count);
        std::printf("%4s %6s %9s %4s %8s  %-16s %s\n", "idx", "at", "chunk at", "tag", "size", "modified (UTC)",
                    "name (indented by folder)");
    }

    Walker w{d, tocSize, d.size() > 0x17 ? d[0x17] : uint8_t(0), {}};
    std::vector<size_t> folderEnds;
    struct TagStat {
        uint16_t tag;
        unsigned count = 0, sizeMismatches = 0;
        std::vector<std::string> extensions;
    };
    std::vector<TagStat> stats;
    auto statFor = [&](uint16_t tag) -> TagStat& {
        for (TagStat& s : stats)
            if (s.tag == tag)
                return s;
        stats.push_back(TagStat{tag, 0, 0, {}});
        return stats.back();
    };

    size_t pos = 0x10, expectedChunk = tocEnd;
    size_t prevPos = 0, prevChunk = 0;
    uint32_t prevSize = 0;
    uint16_t prevTag = 0;
    int prevIndex = -1;
    unsigned entries = 0, indexGaps = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!w.validEntry(pos, prevIndex, expectedChunk)) {
            // The previous entry's rule was wrong. Try the other kind (file /
            // container) and any entry size, and keep the one that lets the
            // chain continue.
            // Take the nearest entry that fits either explanation: a farther
            // match can be a later entry that skips a container's children.
            bool recovered = false;
            const TagRule old = w.rule(prevTag);
            size_t bestQ = 0, bestWant = 0;
            bool bestContainer = false;
            for (int attempt = 0; attempt < 2 && i > 0; ++attempt) {
                const bool container = attempt == 0 ? old.container : !old.container;
                const size_t want = container ? prevChunk + 6 : prevChunk + 6 + prevSize;
                for (size_t q = prevPos + 8; q < prevPos + 4096 && q + 8 <= d.size() && (!bestQ || q < bestQ); ++q) {
                    if (w.validEntry(q, prevIndex, want)) {
                        bestQ = q;
                        bestWant = want;
                        bestContainer = container;
                        break;
                    }
                }
            }
            if (bestQ) {
                TagRule r{prevTag, bestQ - prevPos, bestContainer, true};
                w.learn(r);
                if (!quiet)
                    std::printf("  -> learned: tag 0x%x entries are %zu bytes, %s\n", prevTag, r.entrySize,
                                bestContainer ? "container" : "file");
                if (bestContainer && !old.container) // re-open what we treated as a file
                    folderEnds.push_back(prevChunk + 6 + prevSize);
                if (!bestContainer && old.container && !folderEnds.empty())
                    folderEnds.pop_back();
                pos = bestQ;
                expectedChunk = bestWant;
                recovered = true;
            }
            if (!recovered) {
                std::printf("entry #%u: no valid entry follows the one at 0x%zx (tag 0x%x)\n", i, prevPos, prevTag);
                hexdump(d, pos > 0x20 ? pos - 0x20 : 0, 0x80);
                std::printf("RESULT: FAIL at entry #%u after a tag 0x%x entry\n", i, prevTag);
                return 1;
            }
        }

        const int index = rd16(d, pos + 4);
        if (prevIndex >= 0 && index != prevIndex + 1)
            indexGaps += unsigned(index - prevIndex - 1);
        const size_t chunk = w.chunkOf(pos);
        const uint16_t tag = rd16(d, chunk);
        const uint32_t size = rd32(d, chunk + 2);
        const TagRule r = w.rule(tag);
        while (!folderEnds.empty() && chunk >= folderEnds.back())
            folderEnds.pop_back();
        const std::string indent(folderEnds.size() * 2, ' ');
        TagStat& st = statFor(tag);
        ++st.count;

        if (r.container) {
            if (!quiet)
                std::printf("%4d %6zx %9zx %4x %8u  %-16s %s<container>\n", index, pos, chunk, tag, size, "",
                            indent.c_str());
            folderEnds.push_back(chunk + 6 + size);
            expectedChunk = chunk + 6;
        } else {
            const size_t p = pos + 8;
            const std::string name = readName(d, p);
            const uint32_t metaSize = rd32(d, p + 0x116);
            const bool sizeOk = metaSize == size;
            st.sizeMismatches += !sizeOk;
            std::string ext = extensionOf(name);
            bool seen = false;
            for (const std::string& e : st.extensions)
                seen |= e == ext;
            if (!seen && !ext.empty())
                st.extensions.push_back(ext);
            if (!quiet)
                std::printf("%4d %6zx %9zx %4x %8u%s %-16s %s%s\n", index, pos, chunk, tag, size, sizeOk ? " " : "*",
                            formatTime(rd32(d, p + 0x11e)).c_str(), indent.c_str(), name.c_str());
            expectedChunk = chunk + 6 + size;
        }
        prevPos = pos;
        prevChunk = chunk;
        prevSize = size;
        prevTag = tag;
        prevIndex = index;
        pos += r.entrySize;
        ++entries;
    }

    const bool tableOk = pos == tocEnd;
    const long long trailing = (long long)d.size() - (long long)expectedChunk;
    if (!quiet) {
        std::printf("\n%u entries, %u index gaps; table walk ended at 0x%zx, table chunk ends at 0x%zx\n", entries,
                    indexGaps, pos, tocEnd);
        std::printf("last chunk ends at %zu, file size %zu\n", expectedChunk, d.size());
        std::printf("* = stored size differs from the size in the entry's copied metadata\n\n");
    }
    std::printf("tags:");
    for (const TagStat& s : stats) {
        const TagRule r = w.rule(s.tag);
        std::printf(" 0x%x x%u", s.tag, s.count);
        if (r.container)
            std::printf("(container");
        else
            std::printf("(");
        for (size_t k = 0; k < s.extensions.size(); ++k)
            std::printf("%s%s", k || r.container ? " " : "", s.extensions[k].c_str());
        if (s.sizeMismatches)
            std::printf(" *%u", s.sizeMismatches);
        std::printf(")");
    }
    std::printf("\n");
    for (const TagRule& r : w.rules)
        if (r.learned)
            std::printf("learned: tag 0x%x entry %zu bytes, %s\n", r.tag, r.entrySize, r.container ? "container" : "file");
    const bool ok = tableOk && trailing >= 0;
    std::printf("RESULT: %s (%u entries, table %s, %lld trailing bytes)\n", ok ? "OK" : "FAIL", entries,
                tableOk ? "exact" : "MISMATCH", trailing);
    return ok ? 0 : 1;
}


int cmdCatList(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool cat-list <file.cat>\n");
        return 2;
    }
    reluva::newworlds::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const auto& entries = cat.entries();
    for (const auto& e : entries) {
        int depth = 0;
        for (int p = e.parent; p >= 0; p = entries[size_t(p)].parent)
            ++depth;
        std::printf("%4x %9u  %-16s %*s%s\n", e.tag, e.size, e.container ? "" : formatTime(e.modified).c_str(),
                    depth * 2, "", e.container ? "<container>" : e.name.c_str());
    }
    return 0;
}

int cmdCatExtract(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool cat-extract <file.cat> <output dir> [name filter]\n");
        return 2;
    }
    reluva::newworlds::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path outDir = argv[3];
    const std::string filter = argc > 4 ? argv[4] : "";
    std::vector<uint8_t> data;
    unsigned written = 0;
    for (const auto& e : cat.entries()) {
        if (e.container || e.name.empty() || e.name.find(filter) == std::string::npos)
            continue;
        // Names come from the archive: refuse anything that could escape outDir.
        const std::filesystem::path rel(e.name);
        bool safe = !rel.is_absolute() && !rel.has_root_name();
        for (const auto& part : rel)
            safe &= part != "..";
        if (!safe) {
            std::fprintf(stderr, "skipping unsafe name %s\n", e.name.c_str());
            continue;
        }
        if (reluva::Error err = cat.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        const std::filesystem::path out = outDir / rel;
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("extracted %u files to %s\n", written, outDir.string().c_str());
    return 0;
}


int cmdStbDump(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stb-dump <file.stb | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        // Every table in the archive (one in misc.cat has the texture tag).
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        unsigned count = 0, good = 0;
        Bytes d;
        for (const auto& e : cat.entries()) {
            const bool stb = e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".stb") == 0;
            if (e.container || (e.tag != nw::cattag::Strings && !stb))
                continue;
            ++count;
            nw::StringTable table;
            reluva::Error err = cat.read(e, d);
            if (!err)
                err = table.parse(d);
            if (err) {
                std::printf("%-28s %s\n", e.name.c_str(), err.message.c_str());
                continue;
            }
            ++good;
            uint32_t maxId = 0;
            for (const auto& s : table.strings())
                maxId = std::max(maxId, s.id);
            const std::string first = table.strings().empty() ? "" : table.strings()[0].text.substr(0, 40);
            std::printf("%-28s %3zu strings, ids up to %3u  %s\n", e.name.c_str(), table.strings().size(), maxId,
                        first.c_str());
        }
        std::printf("%u string tables, %u read\n", count, good);
        return good == count ? 0 : 1;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    nw::StringTable table;
    reluva::Error err = table.parse(d);
    for (const auto& s : table.strings())
        std::printf("%6u  %s\n", s.id, s.text.c_str());
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("-- %zu strings, layout matches exactly\n", table.strings().size());
    return 0;
}

// 8-bit paletted BMP, rows top-down. swapRB picks which palette byte is red.
bool writeBmp(const std::string& path, int w, int h, const uint8_t* palette, int paletteCount, const uint8_t* pixels,
              bool swapRB) {
    const int stride = (w + 3) & ~3;
    const uint32_t paletteBytes = 256 * 4, dataOffset = 14 + 40 + paletteBytes;
    const uint32_t fileSize = dataOffset + uint32_t(stride * h);
    Bytes out;
    auto p16 = [&](uint32_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
    auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    out.push_back('B');
    out.push_back('M');
    p32(fileSize); p32(0); p32(dataOffset);
    p32(40); p32(uint32_t(w)); p32(uint32_t(-h)); p16(1); p16(8); p32(0); p32(uint32_t(stride * h));
    p32(2835); p32(2835); p32(256); p32(0);
    for (int i = 0; i < 256; ++i) {
        uint8_t c0 = 0, c1 = 0, c2 = 0;
        if (i < paletteCount) {
            c0 = palette[i * 4];
            c1 = palette[i * 4 + 1];
            c2 = palette[i * 4 + 2];
        }
        // BMP palettes are stored blue, green, red, 0.
        uint8_t r = swapRB ? c2 : c0, b = swapRB ? c0 : c2;
        out.push_back(b);
        out.push_back(c1);
        out.push_back(r);
        out.push_back(0);
    }
    for (int y = 0; y < h; ++y) {
        out.insert(out.end(), pixels + size_t(y) * size_t(w), pixels + size_t(y + 1) * size_t(w));
        out.resize(out.size() + size_t(stride - w), 0);
    }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

int cmdLbpInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool lbp-info <file> [output prefix]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    reluva::newworlds::Texture tex;
    if (reluva::Error err = reluva::newworlds::decodeTexture(d, tex)) {
        std::printf("doesn't decode: %s\nfirst bytes:\n", err.message.c_str());
        hexdump(d, 0, 64);
        return 1;
    }
    const uint32_t paletteCount = rd32(d, 4);
    const size_t header = 8 + size_t(paletteCount) * 4;
    const size_t pixels = size_t(tex.width) * size_t(tex.height);
    const size_t indices = header + (tex.colourKeyed ? 16 : 12);
    auto colour = [&](unsigned i) { // palette entries are blue, green, red, 0
        char buf[32];
        const uint8_t* e = &d[8 + size_t(i) * 4];
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x", e[2], e[1], e[0]);
        return std::string(buf);
    };

    std::printf("format %u, %d x %d, palette %u entries\n", tex.format, tex.width, tex.height, paletteCount);
    if (tex.colourKeyed)
        std::printf("colour key: index %u = %s\n", tex.keyIndex,
                    tex.keyIndex < paletteCount ? colour(tex.keyIndex).c_str() : "(outside the palette)");
    std::printf("header after the palette:\n");
    hexdump(d, header, tex.colourKeyed ? 16 : 12);

    unsigned hist[256] = {};
    for (size_t i = 0; i < pixels; ++i)
        ++hist[d[indices + i]];
    std::vector<unsigned> order;
    for (unsigned i = 0; i < 256; ++i)
        if (hist[i])
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](unsigned x, unsigned y) { return hist[x] > hist[y]; });
    std::printf("most used indices:\n");
    for (size_t k = 0; k < order.size() && k < 6; ++k)
        std::printf("  index %3u %s  %6u pixels%s\n", order[k], order[k] < paletteCount ? colour(order[k]).c_str() : "?",
                    hist[order[k]], tex.colourKeyed && order[k] == tex.keyIndex ? "  <- key" : "");

    std::printf("palette entries that look like key colours (magenta or bright green):\n ");
    for (unsigned i = 0; i < paletteCount; ++i) {
        const uint8_t* e = &d[8 + size_t(i) * 4];
        const uint8_t r = e[2], g = e[1], b = e[0];
        if ((r > 200 && b > 200 && g < 60) || (g > 150 && r < 40 && b < 40))
            std::printf(" %u=%s(%u px)", i, colour(i).c_str(), hist[i]);
    }
    std::printf("\n");

    if (argc > 3) {
        const std::string prefix = argv[3];
        Bytes alpha(pixels);
        for (size_t i = 0; i < pixels; ++i)
            alpha[i] = tex.alpha.empty() ? tex.rgba[i * 4 + 3] : tex.alpha[i];
        uint8_t grey[256 * 4];
        for (int i = 0; i < 256; ++i)
            grey[i * 4] = grey[i * 4 + 1] = grey[i * 4 + 2] = uint8_t(i), grey[i * 4 + 3] = 0;
        const bool ok = writeBmp(prefix + ".bmp", tex.width, tex.height, d.data() + 8, int(paletteCount),
                                 d.data() + indices, true) &&
                        writeBmp(prefix + "_alpha.bmp", tex.width, tex.height, grey, 256, alpha.data(), false);
        std::printf(ok ? "wrote %s.bmp and %s_alpha.bmp\n" : "failed writing %s.bmp\n", prefix.c_str(), prefix.c_str());
        return ok ? 0 : 1;
    }
    return 0;
}

// Decode every texture (tag 0x16) in the given archives and group them by
// what their header holds, to find the layouts the decoder doesn't handle.
int cmdTexSurvey(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tex-survey <file.cat>...\n");
        return 2;
    }
    struct Group {
        unsigned count = 0;
        std::map<std::string, unsigned> extensions;
        std::vector<std::string> examples;
        Bytes firstBytes;
    };
    std::map<std::string, Group> groups;
    unsigned total = 0, decoded = 0;
    std::vector<uint8_t> data;
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Texture)
                continue;
            if (cat.read(e, data))
                continue;
            ++total;
            reluva::newworlds::Texture tex;
            const bool ok = !reluva::newworlds::decodeTexture(data, tex);
            decoded += ok;

            // Describe the header: format, palette count, the three u32s
            // after the palette, and data size relative to width x height.
            char key[160];
            const uint32_t format = data.size() >= 4 ? rd32(data, 0) : 0;
            const uint32_t pal = data.size() >= 8 ? rd32(data, 4) : 0;
            const size_t hdr = 8 + size_t(pal) * 4;
            if (pal > 256 || hdr + 12 > data.size()) {
                std::snprintf(key, sizeof key, "%s format %u, u32@4 %u: palette doesn't fit", ok ? "OK  " : "FAIL",
                              format, pal);
            } else {
                const uint32_t f0 = rd32(data, hdr), w = rd32(data, hdr + 4), h = rd32(data, hdr + 8);
                if (f0 == 1 && hdr + 16 <= data.size()) {
                    // Colour-keyed: u32 1, key index, width, height.
                    const uint32_t kw = rd32(data, hdr + 8), kh = rd32(data, hdr + 12);
                    const size_t layers = format == 6 ? 2 : 1;
                    const bool fits = size_t(kw) * kh * layers == data.size() - hdr - 16;
                    std::snprintf(key, sizeof key, "%s format %u, palette %u, colour-keyed, size %s",
                                  ok ? "OK  " : "FAIL", format, pal, fits ? "fits" : "doesn't fit");
                    goto grouped;
                }
                const size_t rest = data.size() - hdr - 12;
                char ratio[48];
                if (w && h && w <= 4096 && h <= 4096 && rest % (size_t(w) * h) == 0)
                    std::snprintf(ratio, sizeof ratio, "data = %zu x w*h", rest / (size_t(w) * h));
                else if (w && h && w <= 4096 && h <= 4096)
                    std::snprintf(ratio, sizeof ratio, "data = %.3f x w*h", double(rest) / (double(w) * h));
                else
                    std::snprintf(ratio, sizeof ratio, "w/h not plausible");
                std::snprintf(key, sizeof key, "%s format %u, palette %u, u32 after palette %u, %s", ok ? "OK  " : "FAIL",
                              format, pal, f0, ratio);
            }
        grouped:
            Group& g = groups[key];
            ++g.count;
            ++g.extensions[extensionOf(e.name)];
            if (g.examples.size() < 3)
                g.examples.push_back(std::string(argv[a]) + ": " + e.name + " (" + std::to_string(data.size()) + " bytes)");
            if (g.firstBytes.empty())
                g.firstBytes.assign(data.begin(), data.begin() + std::min<size_t>(data.size(), 48));
        }
    }
    std::printf("%u textures, %u decode with the current rules\n\n", total, decoded);
    for (const auto& [key, g] : groups) {
        std::printf("%5u  %s\n       extensions:", g.count, key.c_str());
        for (const auto& [ext, n] : g.extensions)
            std::printf(" %s x%u", ext.empty() ? "(none)" : ext.c_str(), n);
        std::printf("\n");
        for (const std::string& ex : g.examples)
            std::printf("       e.g. %s\n", ex.c_str());
        if (key.rfind("FAIL", 0) == 0)
            hexdump(g.firstBytes, 0, g.firstBytes.size());
        std::printf("\n");
    }
    return 0;
}


// For every colour-keyed texture, compare the key index with the corner
// pixels, which in sprites and interface images are nearly always background.
int cmdTexKeys(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tex-keys <file.cat>...\n");
        return 2;
    }
    unsigned total = 0, keyIsCorner = 0, cornerLooksKey = 0;
    std::vector<uint8_t> d;
    std::printf("%-44s %3s %4s %-8s %6s  %-14s %s\n", "texture", "fmt", "key", "colour", "px", "corners", "corner colour");
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Texture || cat.read(e, d))
                continue;
            reluva::newworlds::Texture tex;
            if (reluva::newworlds::decodeTexture(d, tex) || !tex.colourKeyed)
                continue;
            ++total;
            const uint32_t pal = rd32(d, 4);
            const size_t idx = 8 + size_t(pal) * 4 + 16;
            const size_t w = size_t(tex.width), h = size_t(tex.height);
            const uint8_t corners[4] = {d[idx], d[idx + w - 1], d[idx + (h - 1) * w], d[idx + h * w - 1]};
            auto colour = [&](unsigned i) {
                char buf[16];
                if (i >= pal)
                    return std::string("?");
                const uint8_t* c = &d[8 + size_t(i) * 4];
                std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c[2], c[1], c[0]);
                return std::string(buf);
            };
            unsigned keyPixels = 0;
            for (size_t i = 0; i < w * h; ++i)
                keyPixels += d[idx + i] == tex.keyIndex;
            const bool allSame = corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3];
            const bool match = allSame && corners[0] == tex.keyIndex;
            keyIsCorner += match;
            const uint8_t* cc = corners[0] < pal ? &d[8 + size_t(corners[0]) * 4] : nullptr;
            const bool looksKey = cc && ((cc[2] > 200 && cc[0] > 200 && cc[1] < 60) || (cc[1] > 150 && cc[2] < 40 && cc[0] < 40));
            cornerLooksKey += looksKey;
            char cornerText[24];
            std::snprintf(cornerText, sizeof cornerText, "%u %u %u %u", corners[0], corners[1], corners[2], corners[3]);
            std::string name = e.name.size() > 44 ? "..." + e.name.substr(e.name.size() - 41) : e.name;
            std::printf("%-44s %3u %4u %-8s %6u  %-14s %s%s%s\n", name.c_str(), tex.format, tex.keyIndex,
                        colour(tex.keyIndex).c_str(), keyPixels, cornerText, colour(corners[0]).c_str(),
                        match ? "  key = corners" : "", looksKey ? "  (key-like colour)" : "");
        }
    }
    std::printf("\n%u colour-keyed textures: key index = all four corners in %u; corner colour is magenta/green in %u\n",
                total, keyIsCorner, cornerLooksKey);
    return 0;
}


#endif // RELUVA_ENGINE_NEWWORLDS

// A payload counts as a container if it splits exactly into chunks whose
// tags look plausible. Small tag limit keeps random data from qualifying.
bool tilesIntoChunks(const Bytes& d, size_t begin, size_t end) {
    if (end - begin < 6)
        return false;
    size_t p = begin;
    while (p < end) {
        if (p + 6 > end)
            return false;
        const uint16_t tag = rd16(d, p);
        const uint32_t size = rd32(d, p + 2);
        if (tag > 0x400 || size > end - p - 6)
            return false;
        p += 6 + size;
    }
    return p == end;
}

std::string describeWords(const Bytes& d, size_t p, size_t end, int count) {
    std::string out;
    char buf[64];
    for (int i = 0; i < count && p + 4 <= end; ++i, p += 4) {
        const uint32_t v = rd32(d, p);
        float f;
        std::memcpy(&f, &v, 4);
        const bool plausibleFloat = (v & 0x7f800000) != 0 && (v & 0x7f800000) != 0x7f800000 && f > -1e6f &&
                                    f < 1e6f && (f > 1e-4f || f < -1e-4f);
        if (plausibleFloat)
            std::snprintf(buf, sizeof buf, " %gf", double(f));
        else
            std::snprintf(buf, sizeof buf, " %u", v);
        out += buf;
    }
    return out;
}

void printChunks(const Bytes& d, size_t begin, size_t end, int depth) {
    size_t p = begin;
    while (p < end) {
        const uint16_t tag = rd16(d, p);
        const uint32_t size = rd32(d, p + 2);
        const size_t payload = p + 6, payloadEnd = payload + size;
        const bool container = tilesIntoChunks(d, payload, payloadEnd);
        std::printf("%*s%06zx tag 0x%04x size %u%s", depth * 2, "", p, tag, size, container ? "  {" : "");
        if (!container)
            std::printf("  :%s%s", describeWords(d, payload, payloadEnd, 6).c_str(), size > 24 ? " ..." : "");
        std::printf("\n");
        if (container)
            printChunks(d, payload, payloadEnd, depth + 1);
        p = payloadEnd;
    }
}

int cmdChunks(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool chunks <file>\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    std::printf("%s: %zu bytes\n", argv[2], d.size());
    if (!tilesIntoChunks(d, 0, d.size())) {
        std::printf("doesn't divide into chunks from the start; first words:%s\n",
                    describeWords(d, 0, d.size(), 12).c_str());
        // Maybe there's a header: report the first offset from which it does.
        for (size_t start = 1; start < std::min<size_t>(d.size(), 64); ++start)
            if (tilesIntoChunks(d, start, d.size())) {
                std::printf("but it does from offset %zu:\n", start);
                printChunks(d, start, d.size(), 1);
                return 0;
            }
        return 1;
    }
    printChunks(d, 0, d.size(), 0);
    return 0;
}

int cmdWords(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool words <file> [offset] [count]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    size_t off = argc > 3 ? size_t(parseNumber(argv[3])) : 0;
    const size_t count = argc > 4 ? size_t(parseNumber(argv[4])) : 64;
    std::printf("%8s  %8s  %11s  %14s  %s\n", "offset", "hex", "u32", "float", "u16 pair");
    for (size_t i = 0; i < count && off + 4 <= d.size(); ++i, off += 4) {
        const uint32_t v = rd32(d, off);
        float f;
        std::memcpy(&f, &v, 4);
        std::printf("%8zx  %08x  %11u  %14g  %u, %u\n", off, v, v, double(f), v & 0xffff, v >> 16);
    }
    return 0;
}


int cmdTable(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool table <file> [values per row] [offset]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const size_t perRow = argc > 3 ? std::max<size_t>(1, size_t(parseNumber(argv[3]))) : 8;
    size_t off = argc > 4 ? size_t(parseNumber(argv[4])) : 0;
    for (; off + 4 <= d.size(); off += perRow * 4) {
        std::printf("%6zx:", off);
        for (size_t i = 0; i < perRow && off + i * 4 + 4 <= d.size(); ++i) {
            const uint32_t v = rd32(d, off + i * 4);
            float f;
            std::memcpy(&f, &v, 4);
            const bool isFloat = (v & 0x7f800000) != 0 && (v & 0x7f800000) != 0x7f800000 && f > -1e7f && f < 1e7f &&
                                 (f > 1e-6f || f < -1e-6f);
            if (isFloat)
                std::printf(" %10.4gf", double(f));
            else
                std::printf(" %11u", v);
        }
        std::printf("\n");
    }
    if (d.size() % 4)
        std::printf("(%zu trailing bytes)\n", d.size() % 4);
    return 0;
}


#ifdef RELUVA_ENGINE_NEWWORLDS
bool writeObj(const reluva::newworlds::GeoModel& m, const std::string& path) {
    std::ofstream f(path);
    f << "# exported by reluva-tool geo-info\n";
    // OBJ numbers vertices across the whole file, so offset each part's.
    std::vector<size_t> base;
    size_t next = 1;
    for (const auto& part : m.parts) {
        base.push_back(next);
        next += part.vertices.size();
        for (const auto& v : part.vertices)
            f << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
    }
    size_t vt = 1;
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        f << "g part" << pi << '\n';
        uint32_t slot = ~0u;
        for (const auto& face : m.faces) {
            if (face.part != pi)
                continue;
            // The face's texture number, as a material name.
            if (face.corners.back().extra[4] != slot) {
                slot = face.corners.back().extra[4];
                f << "usemtl slot" << slot << '\n';
            }
            for (const auto& c : face.corners)
                f << "vt " << c.u << ' ' << 1.0f - c.v << '\n';
            f << 'f';
            for (const auto& c : face.corners)
                f << ' ' << base[pi] + c.vertex << '/' << vt++;
            f << '\n';
        }
    }
    return bool(f);
}

int cmdGeoInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool geo-info <file.geo> [out.obj]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    reluva::newworlds::GeoModel m;
    if (reluva::Error err = reluva::newworlds::parseGeo(d, m)) {
        std::printf("doesn't decode: %s\n", err.message.c_str());
        return 1;
    }
    size_t corners = 0;
    for (const auto& face : m.faces)
        corners += face.corners.size();
    std::printf("%zu faces (%zu corners), %zu part(s)\n", m.faces.size(), corners, m.parts.size());
    for (size_t pi = 0; pi < m.parts.size(); ++pi) {
        const auto& part = m.parts[pi];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& v : part.vertices) {
            const float c[3] = {v.x, v.y, v.z};
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], c[i]);
                hi[i] = std::max(hi[i], c[i]);
            }
        }
        size_t faces = 0;
        for (const auto& face : m.faces)
            faces += face.part == pi;
        std::string lists;
        for (const auto& list : part.normalLists)
            lists += (lists.empty() ? "" : "+") + std::to_string(list.size());
        std::printf("  part %zu (%u): %zu faces, %zu vertices, %zu edges, normal lists of %s; x %g..%g, y %g..%g, "
                    "z %g..%g\n",
                    pi, part.node, faces, part.vertices.size(), part.edges.size(),
                    lists.empty() ? "none" : lists.c_str(), double(lo[0]),
                    double(hi[0]), double(lo[1]), double(hi[1]), double(lo[2]), double(hi[2]));
    }
    std::printf("centre %g, %g; radius %g\n", double(m.centreX), double(m.centreY), double(m.radius));
    for (size_t i = 0; i < m.attachments.size(); ++i) {
        const auto& a = m.attachments[i];
        std::printf("  attachment %zu at (%g, %g, %g); frame", i, double(a.position.x), double(a.position.y),
                    double(a.position.z));
        for (float f : a.frame)
            std::printf(" %.3g", double(f));
        std::printf("\n");
    }
    // How often each value of the faces' second header word occurs (face 0's
    // header is the file header, so it's left out).
    std::map<uint32_t, unsigned> seconds;
    for (size_t i = 1; i < m.faces.size(); ++i)
        ++seconds[m.faces[i].groupB];
    std::printf("faces' second header value (value x count):");
    for (const auto& [value, n] : seconds)
        std::printf(" %u x%u", value, n);
    std::printf("\n");
    // The last value of each face's last corner: a texture number?
    std::map<uint32_t, unsigned> tails;
    for (const auto& face : m.faces)
        ++tails[face.corners.back().extra[4]];
    std::printf("last value of each face's last corner (value x count):");
    for (const auto& [value, n] : tails)
        std::printf(" %u x%u", value, n);
    std::printf("\n");

    // Test: is the second value an index into the face's part's first normal
    // list, giving the face's normal? Compare with the normal worked out from
    // the corners (Newell's method), either way round.
    unsigned same = 0, opposite = 0, other = 0, outOfRange = 0;
    for (size_t i = 1; i < m.faces.size(); ++i) {
        const auto& face = m.faces[i];
        const auto& part = m.parts[face.part];
        if (part.normalLists.empty() || face.groupB >= part.normalLists[0].size()) {
            ++outOfRange;
            continue;
        }
        double n[3] = {0, 0, 0};
        for (size_t k = 0; k < face.corners.size(); ++k) {
            const auto& a = part.vertices[face.corners[k].vertex];
            const auto& b = part.vertices[face.corners[(k + 1) % face.corners.size()].vertex];
            n[0] += double(a.y - b.y) * double(a.z + b.z);
            n[1] += double(a.z - b.z) * double(a.x + b.x);
            n[2] += double(a.x - b.x) * double(a.y + b.y);
        }
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const auto& s = part.normalLists[0][face.groupB];
        const double d = len > 0 ? (n[0] * s.x + n[1] * s.y + n[2] * s.z) / len : 0;
        if (d > 0.99)
            ++same;
        else if (d < -0.99)
            ++opposite;
        else
            ++other;
    }
    std::printf("second value as an index into the part's first normal list: %u match the face's normal, "
                "%u the opposite, %u neither, %u out of range\n",
                same, opposite, other, outOfRange);
    if (argc > 3) {
        if (!writeObj(m, argv[3])) {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("wrote %s\n", argv[3]);
    }
    return 0;
}

int cmdGeoFaces(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool geo-faces <file.geo>\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    // Same walk as parseGeo, but any fourth header word is accepted and shown.
    const uint32_t faceCount = rd32(d, 4);
    std::printf("%zu bytes, %u faces\n", d.size(), faceCount);
    size_t p = 0;
    for (uint32_t i = 0; i < faceCount; ++i) {
        const uint32_t n = rd32(d, p + 8);
        if (n < 3 || n > 256 || p + 16 + size_t(n) * 32 > d.size()) {
            std::printf("face %u at 0x%zx: bad header (%u, %u, %u, %u); bytes around it:\n", i, p, rd32(d, p),
                        rd32(d, p + 4), n, rd32(d, p + 12));
            hexdump(d, p >= 0x60 ? p - 0x60 : 0, 0x160);
            return 1;
        }
        std::printf("face %u at 0x%zx: (%u, %u, %u, %u)\n", i, p, rd32(d, p), rd32(d, p + 4), n, rd32(d, p + 12));
        p += 16;
        for (uint32_t k = 0; k < n; ++k, p += 32) {
            float u, v;
            const uint32_t ub = rd32(d, p + 4), vb = rd32(d, p + 8);
            std::memcpy(&u, &ub, 4);
            std::memcpy(&v, &vb, 4);
            std::printf("    v %-4u uv %9.4f %9.4f  rest %u %u %u %u %u\n", rd32(d, p), double(u / 65536),
                        double(v / 65536), rd32(d, p + 12), rd32(d, p + 16), rd32(d, p + 20), rd32(d, p + 24),
                        rd32(d, p + 28));
        }
    }
    std::printf("faces end at 0x%zx; next bytes:\n", p);
    hexdump(d, p, 0x100);
    return 0;
}

int cmdMdlInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool mdl-info <file.mdl | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto list = [](const char* what, const std::vector<nw::MdlModel::Name>& names) {
        if (names.empty())
            return;
        std::printf("  %s:", what);
        for (const auto& n : names)
            std::printf(" %s=%d", n.name.c_str(), n.value);
        std::printf("\n");
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        std::map<std::string, unsigned> states;
        unsigned count = 0, failed = 0;
        std::vector<uint8_t> data;
        for (const auto& e : cat.entries()) {
            if (e.tag != nw::cattag::Mdl)
                continue;
            ++count;
            nw::MdlModel m;
            reluva::Error err = cat.read(e, data);
            if (!err)
                err = nw::parseMdl(data, m);
            if (err) {
                std::printf("%s: %s\n", e.name.c_str(), err.message.c_str());
                ++failed;
                continue;
            }
            std::string key;
            for (const auto& s : m.states)
                key += (key.empty() ? "" : ", ") + (s.empty() ? std::string("(unnamed)") : s);
            ++states[key.empty() ? "(none listed)" : key];
        }
        std::printf("%u metamodels, %u decode\nstates:\n", count, count - failed);
        for (const auto& [k, n] : states)
            std::printf("  %4u  %s\n", n, k.c_str());
        return failed ? 1 : 0;
    }
    std::vector<uint8_t> data;
    if (!readFile(path.c_str(), data)) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    nw::MdlModel m;
    if (reluva::Error err = nw::parseMdl(data, m)) {
        std::printf("%s\n", err.message.c_str());
        return 1;
    }
    std::printf("\"%s\"\n", m.title.c_str());
    list("parts", m.parts);
    list("points", m.points);
    list("places", m.places);
    list("animated", m.animated);
    std::printf("  states:");
    for (const auto& s : m.states)
        std::printf(" \"%s\"", s.c_str());
    std::printf("\n");
    for (size_t i = 0; i < m.reference.size(); ++i) {
        const auto& t = m.reference[i];
        std::string name = "#" + std::to_string(i);
        for (const auto& a : m.animated)
            if (a.value == int32_t(i))
                name = a.name;
        std::printf("  reference pose %zu %-16s at (%g, %g, %g)\n", i, name.c_str(), double(t.position.x),
                    double(t.position.y), double(t.position.z));
    }
    return 0;
}

int cmdWavInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool wav-info <file.wav | file.mp3 | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto describe = [](const std::string& name, const std::vector<uint8_t>& data, const std::string& note = "") {
        reluva::audio::Sound s;
        if (reluva::Error err = reluva::audio::decodeSound(data, s)) {
            std::printf("%-28s %s\n", name.c_str(), err.message.c_str());
            return false;
        }
        std::printf("%-28s %s, %d channel%s, %d Hz, %zu frames (%.2f s)%s\n", name.c_str(),
                    reluva::audio::formatName(s.format), s.channels, s.channels == 1 ? "" : "s", s.rate, s.frames(),
                    double(s.frames()) / s.rate, note.c_str());
        return true;
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        // Some sounds are stored many times over, under the same or other
        // names: note each copy's first.
        unsigned count = 0, good = 0;
        std::map<std::string, unsigned> first; // bytes -> entry number
        std::vector<uint8_t> data;
        for (size_t i = 0; i < cat.entries().size(); ++i) {
            const auto& e = cat.entries()[i];
            if (e.tag != nw::cattag::Wave)
                continue;
            ++count;
            if (cat.read(e, data)) {
                std::printf("%-28s cannot be read\n", e.name.c_str());
                continue;
            }
            const auto [it, added] = first.emplace(std::string(data.begin(), data.end()), unsigned(i));
            const std::string note =
                added ? "" : " copy of " + cat.entries()[it->second].name + " (entry " + std::to_string(it->second) + ")";
            good += describe(e.name, data, note);
        }
        std::printf("%u sounds (%zu different), %u decode\n", count, first.size(), good);
        return good == count ? 0 : 1;
    }
    std::vector<uint8_t> data;
    if (!readFile(path.c_str(), data)) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    return describe(path, data) ? 0 : 1;
}

// Per texture number of each model: how many faces use it, their area, how
// they face (share of area facing up, down or sideways) and the range of
// their texture coordinates, to tell what kind of texture each must be.
int cmdGeoSlots(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool geo-slots <file.geo>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    for (int a = 2; a < argc; ++a) {
        Bytes d;
        nw::GeoModel geo;
        if (!readFile(argv[a], d) || nw::parseGeo(d, geo)) {
            std::printf("%s: doesn't read\n", argv[a]);
            continue;
        }
        struct Slot { unsigned faces = 0; double area = 0, up = 0, down = 0, tilt = 0; float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f; };
        std::map<uint32_t, Slot> slots;
        for (const auto& f : geo.faces) {
            Slot& sl = slots[f.corners.back().extra[4]];
            ++sl.faces;
            const auto& vs = geo.parts[f.part].vertices;
            double nx = 0, ny = 0, nz = 0;
            for (size_t i = 0; i < f.corners.size(); ++i) {
                const auto& p = vs[f.corners[i].vertex];
                const auto& q = vs[f.corners[(i + 1) % f.corners.size()].vertex];
                nx += (p.y - q.y) * (p.z + q.z);
                ny += (p.z - q.z) * (p.x + q.x);
                nz += (p.x - q.x) * (p.y + q.y);
                sl.u0 = std::min(sl.u0, f.corners[i].u), sl.u1 = std::max(sl.u1, f.corners[i].u);
                sl.v0 = std::min(sl.v0, f.corners[i].v), sl.v1 = std::max(sl.v1, f.corners[i].v);
            }
            const double area = std::sqrt(nx * nx + ny * ny + nz * nz) / 2;
            sl.area += area;
            if (area > 0) {
                const double c = nz / (2 * area); // faces are clockwise from the front, so -c is up
                sl.tilt += std::fabs(c) * area;  // |cos| of the angle from vertical axis, area-weighted
                if (-c > 0.7)
                    sl.up += area;
                else if (c > 0.7)
                    sl.down += area;
            }
        }
        std::printf("%s:\n", argv[a]);
        for (const auto& [n, sl] : slots)
            std::printf("  %u: %3u faces, area %9.0f, up %3.0f%% down %3.0f%%, u %5.2f..%5.2f v %5.2f..%5.2f, tilt %.2f\n", n,
                        sl.faces, sl.area, sl.area ? 100 * sl.up / sl.area : 0, sl.area ? 100 * sl.down / sl.area : 0,
                        sl.u0, sl.u1, sl.v0, sl.v1, sl.area ? sl.tilt / sl.area : 0);
    }
    return 0;
}

// For each model number a planet's maps use, how well each ground model would
// fit there: the share of the number's tile sides that meet their neighbours
// (all 5 heights within 80), the neighbours taking their models from
// tileModelOrder. The model the numbering gives should fit best.
int cmdGmfFit(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool gmf-fit [-a] <planet.cat> [map]\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    bool archiveOrder = false, table = false, layersMode = false, heightMode = false, jointMode = false,
         summaryMode = false, rotationScan = false;
    const char* orderFile = nullptr;
    while (argc >= 3 && argv[2][0] == '-' && argv[2][1] != 0 && argv[2][2] == 0) {
        if (argv[2][1] == 'o' && argc >= 4) {
            orderFile = argv[3];
            argv += 2, argc -= 2;
            continue;
        }
        archiveOrder |= argv[2][1] == 'a';
        table |= argv[2][1] == 't';
        layersMode |= argv[2][1] == 'l';
        heightMode |= argv[2][1] == 'h';
        jointMode |= argv[2][1] == 'j';
        summaryMode |= argv[2][1] == 'm';
        rotationScan |= argv[2][1] == 'r';
        ++argv, --argc;
    }
    if (argc < 3)
        return 2;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> geoNames;
    std::vector<const nw::CatEntry*> geoEntries, maps;
    for (const auto& e : cat.entries()) {
        if (e.tag == nw::cattag::Geometry)
            geoNames.push_back(e.name), geoEntries.push_back(&e);
        if (e.tag == nw::cattag::Gmf && (argc < 4 || e.name.find(argv[3]) != std::string::npos))
            maps.push_back(&e);
    }
    // -a: number the models in plain archive order instead; -o file: the
    // numbering from a file of "number model-stem" lines.
    std::vector<size_t> order = nw::tileModelOrder(geoNames);
    if (archiveOrder)
        for (size_t i = 0; i < order.size() || i < geoNames.size(); ++i)
            order.resize(geoNames.size()), order[i] = i;
    if (orderFile) {
        order.assign(order.size(), nw::kNoModel);
        std::ifstream in(orderFile);
        unsigned n;
        std::string stem;
        while (in >> n >> stem) {
            if (n >= order.size())
                order.resize(n + 1, nw::kNoModel);
            for (size_t g = 0; g < geoNames.size(); ++g)
                if (geoNames[g].find("/lod/") == std::string::npos &&
                    geoNames[g].size() > stem.size() + 5 &&
                    geoNames[g].compare(geoNames[g].size() - stem.size() - 4, stem.size() + 4, stem + ".geo") == 0 &&
                    geoNames[g][geoNames[g].size() - stem.size() - 5] == '/')
                    order[n] = g;
        }
    }
    // Each ground model's sides at each rotation (lod/ and scenery models
    // aren't candidates).
    std::vector<std::array<nw::TileSides, 4>> sides(geoNames.size());
    std::vector<bool> candidate(geoNames.size());
    Bytes d;
    for (size_t g = 0; g < geoNames.size(); ++g) {
        nw::GeoModel geo;
        if (cat.read(*geoEntries[g], d) || nw::parseGeo(d, geo))
            continue;
        candidate[g] = true;
        for (int r = 0; r < 4; ++r)
            sides[g][size_t(r)] = nw::tileSides(geo, r);
    }
    // -s: each model's sides instead, as heights in levels of 1000 (east,
    // north, west, south; 5 points each, low coordinate first).
    if (argc >= 4 && std::strcmp(argv[3], "-s") == 0) {
        for (size_t g = 0; g < geoNames.size(); ++g) {
            if (!candidate[g] || geoNames[g].find("/lod/") != std::string::npos)
                continue;
            std::printf("%2zu %-24s", g, geoNames[g].c_str());
            const auto& t = sides[g][0];
            for (int sd = 0; sd < 4; ++sd) {
                std::printf(" ");
                for (int k = 0; k < 5; ++k)
                    std::printf(t.present[sd] ? "%2d" : " .", int(std::lround(t.z[sd][k] / 1000)));
            }
            std::printf("\n");
        }
        return 0;
    }
    // Each model's surface height at the centre of each cell of its 8 x 8
    // grid, in the maps' terrain-layer units (1 at 0, 3 at 3264, 5 at 7360:
    // 1 + z / 1840), for each rotation, cells in map orientation (row 0 south,
    // column 0 west).
    std::vector<std::array<std::array<int, 64>, 4>> levels(geoNames.size());
    for (size_t g = 0; g < geoNames.size(); ++g) {
        nw::GeoModel geo;
        if (!candidate[g] || cat.read(*geoEntries[g], d) || nw::parseGeo(d, geo))
            continue;
        auto heightAt = [&](float x, float y) {
            float best = -1e9f;
            for (const auto& f : geo.faces) {
                const auto& vs = geo.parts[f.part].vertices;
                for (size_t i = 1; i + 1 < f.corners.size(); ++i) {
                    const auto& a = vs[f.corners[0].vertex];
                    const auto& b = vs[f.corners[i].vertex];
                    const auto& c = vs[f.corners[i + 1].vertex];
                    const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                    if (std::fabs(det) < 1e-3f)
                        continue;
                    const float l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
                    const float l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
                    const float l3 = 1 - l1 - l2;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f)
                        continue;
                    best = std::max(best, l1 * a.z + l2 * b.z + l3 * c.z);
                }
            }
            return best;
        };
        for (int r = 0; r < 4; ++r)
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; ++col) {
                    // The cell's centre in map orientation, turned back into the model's frame.
                    float x = -nw::kGroundTileSize / 2 + (float(col) + 0.5f) * nw::kGroundTileSize / 8;
                    float y = -nw::kGroundTileSize / 2 + (float(row) + 0.5f) * nw::kGroundTileSize / 8;
                    for (int k = 0; k < r; ++k) {
                        const float t = x;
                        x = y;
                        y = -t;
                    }
                    const float z = heightAt(x, y);
                    // In tenths of a level, so slopes compare within a level.
                    levels[g][size_t(r)][size_t(row * 8 + col)] = z < -1e8f ? -100 : int(std::lround(10 + z / 184));
                }
    }
    auto layerCount = [](const nw::GmfMap::Tile& t, int cell) {
        int n = 0;
        for (int l = 0; l < nw::GmfMap::kLayers; ++l)
            n += t.layers[size_t(l * 64 + cell)] != 0;
        return n;
    };
    auto shortName = [&](size_t g) {
        if (g == nw::kNoModel)
            return std::string("-");
        const std::string& n = geoNames[g];
        return n.substr(n.rfind('/') + 1);
    };
    auto meets = [](const nw::TileSides& a, int sa, const nw::TileSides& b, int sb) {
        for (int k = 0; k < 5; ++k)
            if (std::fabs(a.z[sa][k] - b.z[sb][k]) >= 80)
                return false;
        return true;
    };
    if (jointMode) {
        // -j: search for the numbering that best fits the planet's maps:
        // each number may take only models whose heights fit its tiles'
        // terrain layers (within 10 points of the best: the layers only roughly
        // follow slopes), and among those, swaps
        // between numbers are kept while more tile edges meet.
        struct Pair { uint32_t na, nb; int ra, rb, side; };
        std::vector<Pair> pairs;
        std::map<uint32_t, std::vector<std::pair<unsigned, unsigned>>> heightTally; // number -> per model (met, seen)
        std::map<uint32_t, unsigned> uses;
        for (const nw::CatEntry* me : maps) {
            nw::GmfMap map;
            if (cat.read(*me, d) || nw::parseGmf(d, map))
                continue;
            for (int ty = 0; ty < 2 * map.height; ++ty)
                for (int tx = 0; tx < 2 * map.width; ++tx)
                    for (int sd = 0; sd < 2; ++sd) {
                        const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (sd == 0), ty + (sd == 1));
                        if (a < 0 || b < 0)
                            continue;
                        const auto& ta = map.tiles[size_t(a)];
                        const auto& tb = map.tiles[size_t(b)];
                        pairs.push_back({ta.model, tb.model, ta.rotation, tb.rotation, sd});
                    }
            for (const auto& t : map.tiles) {
                ++uses[t.model];
                auto& tally = heightTally[t.model];
                tally.resize(geoNames.size());
                for (size_t g = 0; g < geoNames.size(); ++g) {
                    if (!candidate[g])
                        continue;
                    for (int cell = 0; cell < 64; ++cell) {
                        ++tally[g].second;
                        tally[g].first += std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                    }
                }
            }
        }
        auto isMain = [&](size_t g) { return candidate[g] && geoNames[g].find("/lod/") == std::string::npos; };
        std::map<uint32_t, std::vector<bool>> allowed;
        std::map<uint32_t, std::vector<double>> heightScore;
        for (auto& [n, tally] : heightTally) {
            double best = 0;
            auto& hs = heightScore[n];
            hs.assign(geoNames.size(), 0);
            for (size_t g = 0; g < geoNames.size(); ++g)
                if (isMain(g) && tally[g].second)
                    hs[g] = double(tally[g].first) / tally[g].second, best = std::max(best, hs[g]);
            auto& al = allowed[n];
            al.assign(geoNames.size(), false);
            for (size_t g = 0; g < geoNames.size(); ++g)
                al[g] = isMain(g) && hs[g] >= best - 0.10;
        }
        std::map<uint32_t, size_t> assign;
        for (const auto& [n, c] : uses)
            assign[n] = n < order.size() ? order[n] : nw::kNoModel;
        const std::map<uint32_t, size_t> start = assign;
        auto pairMet = [&](const Pair& p) -> int {
            const size_t ga = assign[p.na], gb = assign[p.nb];
            if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                return 0;
            const auto& sa = sides[ga][size_t(p.ra)];
            const auto& sb = sides[gb][size_t(p.rb)];
            if (!sa.present[p.side] || !sb.present[p.side + 2])
                return 0;
            return meets(sa, p.side, sb, p.side + 2) ? 1 : 0;
        };
        // The pairs touching each number, for scoring a change quickly.
        std::map<uint32_t, std::vector<size_t>> touching;
        for (size_t i = 0; i < pairs.size(); ++i) {
            touching[pairs[i].na].push_back(i);
            if (pairs[i].nb != pairs[i].na)
                touching[pairs[i].nb].push_back(i);
        }
        auto localScore = [&](uint32_t a, uint32_t b) {
            int total = 0;
            for (size_t i : touching[a])
                total += pairMet(pairs[i]);
            for (size_t i : touching[b])
                if (pairs[i].na != a && pairs[i].nb != a)
                    total += pairMet(pairs[i]);
            return total;
        };
        auto fits = [&](uint32_t n, size_t g) { return g == nw::kNoModel ? false : allowed[n][g]; };
        // Numbers whose model doesn't fit their heights take any fitting model first.
        auto inUse = [&](size_t c) {
            for (const auto& [m, gm] : assign)
                if (gm == c)
                    return true;
            return false;
        };
        for (auto& [n, g] : assign)
            if (!fits(n, g))
                for (size_t c = 0; c < geoNames.size(); ++c)
                    if (allowed[n][c] && !inUse(c)) {
                        g = c;
                        break;
                    }
        for (int pass = 0; pass < 20; ++pass) {
            bool improved = false;
            for (auto& [n, g] : assign) {
                for (size_t c = 0; c < geoNames.size(); ++c) {
                    if (c == g || !fits(n, c))
                        continue;
                    // The number using c now, if any, takes n's model (if it fits there).
                    uint32_t other = n;
                    for (const auto& [m, gm] : assign)
                        if (gm == c && m != n)
                            other = m;
                    if (other != n && !fits(other, g))
                        continue;
                    const int before = localScore(n, other);
                    const size_t old = g;
                    g = c;
                    if (other != n)
                        assign[other] = old;
                    const int after = localScore(n, other);
                    if (after > before) {
                        improved = true;
                    } else {
                        g = old;
                        if (other != n)
                            assign[other] = c;
                    }
                }
            }
            if (!improved)
                break;
        }
        int met = 0, seen = 0;
        for (const auto& p : pairs) {
            met += pairMet(p);
            const size_t ga = assign[p.na], gb = assign[p.nb];
            seen += ga != nw::kNoModel && gb != nw::kNoModel && candidate[ga] && candidate[gb];
        }
        std::printf("%zu maps, %zu tile pairs, %.1f%% of edges meet\n", maps.size(), pairs.size(),
                    seen ? 100.0 * met / seen : 0.0);
        for (const auto& [n, g] : assign) {
            const size_t s0 = start.at(n);
            std::printf("  %3u x%-5u %-14s height %5.1f%%%s\n", n, uses[n], shortName(g).c_str(),
                        g == nw::kNoModel ? 0.0 : 100 * heightScore[n][g],
                        g == s0 ? "" : ("   (table: " + shortName(s0) + ")").c_str());
        }
        return 0;
    }
    if (summaryMode) {
        // -m: only the share of tile edges that meet with this numbering, and
        // of tile cells whose terrain layers match the model's height.
        unsigned met = 0, seen = 0, cellsOk = 0, cells = 0;
        for (const nw::CatEntry* me : maps) {
            nw::GmfMap map;
            if (cat.read(*me, d) || nw::parseGmf(d, map))
                continue;
            auto modelOf = [&](uint32_t number) { return number < order.size() ? order[number] : nw::kNoModel; };
            for (int ty = 0; ty < 2 * map.height; ++ty)
                for (int tx = 0; tx < 2 * map.width; ++tx)
                    for (int sd = 0; sd < 2; ++sd) {
                        const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (sd == 0), ty + (sd == 1));
                        if (a < 0 || b < 0)
                            continue;
                        const auto& ta = map.tiles[size_t(a)];
                        const auto& tb = map.tiles[size_t(b)];
                        const size_t ga = modelOf(ta.model), gb = modelOf(tb.model);
                        ++seen;
                        if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                            continue;
                        const auto& sa = sides[ga][size_t(ta.rotation)];
                        const auto& sb = sides[gb][size_t(tb.rotation)];
                        met += sa.present[sd] && sb.present[sd + 2] && meets(sa, sd, sb, sd + 2);
                    }
            for (const auto& t : map.tiles) {
                const size_t g = modelOf(t.model);
                for (int cell = 0; cell < 64; ++cell) {
                    ++cells;
                    cellsOk += g != nw::kNoModel && candidate[g] &&
                               std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                }
            }
        }
        std::printf("%zu maps: %.1f%% of tile edges meet, %.1f%% of cells match the terrain layers\n", maps.size(),
                    seen ? 100.0 * met / seen : 0.0, cells ? 100.0 * cellsOk / cells : 0.0);
        return 0;
    }
    for (const nw::CatEntry* me : maps) {
        nw::GmfMap map;
        if (cat.read(*me, d) || nw::parseGmf(d, map)) {
            std::printf("%s: doesn't read\n", me->name.c_str());
            continue;
        }
        // Neighbouring tile pairs: (tile, side 0 east or 1 north, neighbour).
        struct Pair { int a, side, b; };
        std::vector<Pair> pairs;
        for (int ty = 0; ty < 2 * map.height; ++ty)
            for (int tx = 0; tx < 2 * map.width; ++tx)
                for (int s = 0; s < 2; ++s) {
                    const int a = nw::tileAt(map, tx, ty), b = nw::tileAt(map, tx + (s == 0), ty + (s == 1));
                    if (a >= 0 && b >= 0)
                        pairs.push_back({a, s, b});
                }
        std::map<uint32_t, unsigned> uses;
        for (const auto& t : map.tiles)
            ++uses[t.model];
        if (rotationScan) {
            // -r: per number, each tile's best-fitting models and rotations by
            // heights; tallies of (model, stored rotation - fitted rotation).
            for (const auto& [number, count] : uses) {
                std::map<std::string, unsigned> tally;
                for (const auto& t : map.tiles) {
                    if (t.model != number)
                        continue;
                    // The best-fitting models and rotations for this tile (ties all count).
                    int bestOk = -1;
                    std::vector<std::string> best;
                    for (size_t g = 0; g < geoNames.size(); ++g) {
                        if (!candidate[g] || geoNames[g].find("/lod/") != std::string::npos)
                            continue;
                        for (int r = 0; r < 4; ++r) {
                            int ok = 0;
                            for (int cell = 0; cell < 64; ++cell)
                                ok += std::abs(levels[g][size_t(r)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                            const std::string key = shortName(g) + " d" + std::to_string((t.rotation - r + 4) % 4);
                            if (ok > bestOk)
                                bestOk = ok, best = {key};
                            else if (ok == bestOk)
                                best.push_back(key);
                        }
                    }
                    for (const auto& k : best)
                        ++tally[k];
                }
                std::vector<std::pair<unsigned, std::string>> v;
                for (auto& [k, c] : tally)
                    v.push_back({c, k});
                std::sort(v.rbegin(), v.rend());
                std::printf("%3u x%-3u", number, count);
                for (size_t i = 0; i < v.size() && i < 6; ++i)
                    std::printf("  %s:%u", v[i].second.c_str(), v[i].first);
                std::printf("\n");
            }
            continue;
        }
        if (layersMode) {
            // -l: per number, a rotation-0 tile's count of non-zero terrain
            // layers per cell (row 7 = north first), beside the assigned
            // model's heights per cell (vertex mean, in levels of 1000).
            for (const auto& [number, count] : uses) {
                const nw::GmfMap::Tile* sample = nullptr;
                for (const auto& t : map.tiles)
                    if (t.model == number && (!sample || t.rotation == 0))
                        sample = sample && sample->rotation == 0 ? sample : &t;
                const size_t g = number < order.size() ? order[number] : nw::kNoModel;
                std::printf("%u (x%u, rotation %d) %s\n", number, count, sample->rotation,
                            g == nw::kNoModel ? "-" : geoNames[g].c_str());
                nw::GeoModel geo;
                const bool haveGeo = g != nw::kNoModel && !cat.read(*geoEntries[g], d) && !nw::parseGeo(d, geo);
                for (int row = 7; row >= 0; --row) {
                    std::printf("   ");
                    for (int col = 0; col < 8; ++col) {
                        int n = 0;
                        for (int l = 0; l < nw::GmfMap::kLayers; ++l)
                            n += sample->layers[size_t(l * 64 + row * 8 + col)] != 0;
                        std::printf("%d", n);
                    }
                    std::printf("   ");
                    for (int col = 0; col < 8 && haveGeo; ++col) {
                        const float cell = nw::kGroundTileSize / 8, x0 = -nw::kGroundTileSize / 2 + col * cell,
                                    y0 = -nw::kGroundTileSize / 2 + row * cell;
                        double z = 0;
                        int k = 0;
                        for (const auto& part : geo.parts)
                            for (const auto& v : part.vertices)
                                if (v.x >= x0 - 1 && v.x <= x0 + cell + 1 && v.y >= y0 - 1 && v.y <= y0 + cell + 1)
                                    z += v.z, ++k;
                        if (k)
                            std::printf("%2d", int(std::lround(z / k / 1000)));
                        else
                            std::printf(" .");
                    }
                    std::printf("\n");
                }
            }
            continue;
        }
        if (!table)
            std::printf("%s (%zu tiles):\n", me->name.c_str(), map.tiles.size());
        auto modelOf = [&](uint32_t number) { return number < order.size() ? order[number] : nw::kNoModel; };
        for (const auto& [number, count] : uses) {
            const size_t given = modelOf(number);
            // Score every candidate in this number's place.
            std::vector<std::pair<double, size_t>> scores;
            unsigned sidesSeen = 0;
            for (size_t g = 0; g < geoNames.size() && heightMode; ++g) {
                if (!candidate[g])
                    continue;
                // -h: the share of the number's tiles' cells whose terrain
                // layer count is the model's height there.
                unsigned met = 0, seen = 0;
                for (const auto& t : map.tiles) {
                    if (t.model != number)
                        continue;
                    for (int cell = 0; cell < 64; ++cell) {
                        ++seen;
                        met += std::abs(levels[g][size_t(t.rotation)][size_t(cell)] - 10 * layerCount(t, cell)) < 10;
                    }
                }
                sidesSeen = std::max(sidesSeen, seen);
                scores.push_back({seen ? double(met) / seen : 0, g});
            }
            for (size_t g = 0; g < geoNames.size() && !heightMode; ++g) {
                if (!candidate[g])
                    continue;
                unsigned met = 0, seen = 0;
                for (const Pair& p : pairs) {
                    const auto& ta = map.tiles[size_t(p.a)];
                    const auto& tb = map.tiles[size_t(p.b)];
                    const bool ina = ta.model == number, inb = tb.model == number;
                    if (ina == inb)
                        continue; // only sides against other numbers
                    const size_t ga = ina ? g : modelOf(ta.model), gb = inb ? g : modelOf(tb.model);
                    if (ga == nw::kNoModel || gb == nw::kNoModel || !candidate[ga] || !candidate[gb])
                        continue;
                    const auto& sa = sides[ga][size_t(ta.rotation)];
                    const auto& sb = sides[gb][size_t(tb.rotation)];
                    const int oa = p.side, ob = p.side + 2;
                    if (!sa.present[oa] || !sb.present[ob])
                        continue;
                    ++seen;
                    met += meets(sa, oa, sb, ob);
                }
                sidesSeen = std::max(sidesSeen, seen);
                scores.push_back({seen ? double(met) / seen : 0, g});
            }
            std::sort(scores.begin(), scores.end(), [](auto& x, auto& y) { return x.first > y.first; });
            if (table) {
                // -t: every candidate, for analysis: map number uses sides model score
                for (const auto& [sc, g] : scores)
                    std::printf("T %s %u %u %u %s %.4f\n", me->name.c_str(), number, count, sidesSeen,
                                shortName(g).c_str(), sc);
                continue;
            }
            double givenScore = -1;
            for (const auto& [sc, g] : scores)
                if (g == given)
                    givenScore = sc;
            const bool best = !scores.empty() && (scores[0].second == given || scores[0].first <= givenScore);
            std::printf("  %3u x%-4u %-10s %5.1f%%   best %-10s %5.1f%%, %-10s %5.1f%%  (%u sides)%s\n", number, count,
                        shortName(given).c_str(), givenScore < 0 ? 0.0 : 100 * givenScore,
                        scores.size() > 0 ? shortName(scores[0].second).c_str() : "", scores.size() > 0 ? 100 * scores[0].first : 0.0,
                        scores.size() > 1 ? shortName(scores[1].second).c_str() : "", scores.size() > 1 ? 100 * scores[1].first : 0.0,
                        sidesSeen, best ? "" : "  <-");
        }
    }
    return 0;
}

int cmdLevelInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool level-info <level.dat> [file.cat...]\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    std::vector<nw::Level> levels;
    if (reluva::Error err = nw::parseLevelList(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()),
                                               levels)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    // Which of the given archives holds each level's map.
    std::vector<std::pair<std::string, std::string>> maps; // map name, archive
    for (int a = 3; a < argc; ++a) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s: %s\n", argv[a], err.message.c_str());
            continue;
        }
        for (const auto& e : cat.entries())
            if (e.tag == nw::cattag::Gmf)
                maps.emplace_back(e.name, std::filesystem::path(argv[a]).filename().string());
    }
    for (const auto& l : levels) {
        std::string where;
        for (const auto& [map, archive] : maps)
            if (nw::levelForMap(levels, map) == &l)
                where += (where.empty() ? "" : ", ") + map + " in " + archive;
        std::printf("%-5s %d  %-28s %s%s\n", l.shortName.c_str(), l.maxPlayers, l.title.c_str(),
                    l.commentedOut ? "(commented out) " : "", where.c_str());
    }
    std::printf("%zu levels\n", levels.size());
    return 0;
}

int cmdFifInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool fif-info <file.fif | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto summary = [](const nw::GameFont& f) {
        size_t drawn = 0;
        int widest = 0;
        for (const auto& g : f.glyphs) {
            drawn += g.height > 0;
            widest = std::max(widest, g.width);
        }
        char line[160];
        std::snprintf(line, sizeof line, "codes %u-%zu, %zu drawn, line height %d, size %d, widest %d", f.first,
                      f.first + f.glyphs.size() - 1, drawn, f.lineHeight, f.size, widest);
        return std::string(line);
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        unsigned listed = 0;
        for (const auto& e : cat->entries())
            listed += e.tag == nw::cattag::Fif;
        nw::Archives archives;
        archives.push_back(std::move(cat));
        const auto fonts = nw::loadGameFonts(archives);
        for (const auto& f : fonts)
            std::printf("%-16s %dx%d image; %s\n", f.name.c_str(), f.imageWidth, f.imageHeight, summary(f).c_str());
        std::printf("%u font entries, %zu different fonts read\n", listed, fonts.size());
        return 0;
    }
    Bytes d;
    if (!readFile(path.c_str(), d))
        return 1;
    nw::GameFont f;
    if (reluva::Error err = nw::parseFif(d, f)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("%s\n", summary(f).c_str());
    for (size_t i = 0; i < f.glyphs.size(); ++i) {
        const auto& g = f.glyphs[i];
        const uint32_t code = f.first + uint32_t(i);
        std::printf("  %3u %c  x %3d y %3d  %2d x %2d%s\n", code, code < 127 ? char(code) : ' ', g.x, g.y, g.width,
                    g.height, g.height ? "" : "  (none)");
    }
    return 0;
}

int cmdWavDecode(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool wav-decode <in.wav | in.mp3> <out.wav>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    reluva::audio::Sound s;
    if (reluva::Error err = reluva::audio::decodeSound(data, s)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::vector<uint8_t> out = reluva::audio::encodePcmWav(s);
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    std::printf("%zu frames at %d Hz written to %s\n", s.frames(), s.rate, argv[3]);
    return 0;
}

// Every map in a planet archive as CSV files for study: <map>_tiles.csv
// (each tile position's model number and name, flags), <map>_cells.csv
// (each block's grid entry) and <map>_objects.csv (placed objects).
int cmdGmfDump(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool gmf-dump <planet.cat> <output dir>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> geoNames;
    for (const auto& e : cat.entries())
        if (e.tag == nw::cattag::Geometry)
            geoNames.push_back(e.name);
    const std::vector<size_t> order = nw::tileModelOrder(geoNames);
    const std::filesystem::path out = argv[3];
    std::filesystem::create_directories(out);
    std::vector<uint8_t> data;
    int maps = 0;
    for (const auto& e : cat.entries()) {
        std::string folder, stem, ext;
        nw::splitName(e.name, folder, stem, ext);
        if (ext != "gmf")
            continue;
        nw::GmfMap map;
        if (cat.read(e, data) || nw::parseGmf(data, map)) {
            std::fprintf(stderr, "%s doesn't decode\n", e.name.c_str());
            continue;
        }
        ++maps;
        std::ofstream tiles(out / (stem + "_tiles.csv")), cells(out / (stem + "_cells.csv")),
            objects(out / (stem + "_objects.csv"));
        tiles << "tx,ty,record,model,name,flags\n";
        for (int ty = 0; ty < map.height * 2; ++ty)
            for (int tx = 0; tx < map.width * 2; ++tx) {
                const int t = nw::tileAt(map, tx, ty);
                if (t < 0)
                    continue;
                const auto& tile = map.tiles[size_t(t)];
                std::string name = "?";
                if (tile.model < order.size() && order[tile.model] != nw::kNoModel) {
                    std::string f, st, ex;
                    nw::splitName(geoNames[order[tile.model]], f, st, ex);
                    name = st;
                }
                tiles << tx << ',' << ty << ',' << t << ',' << tile.model << ',' << name << ',' << tile.flags << '\n';
            }
        cells << "bx,by,block,value\n";
        for (int by = 0; by < map.height; ++by)
            for (int bx = 0; bx < map.width; ++bx) {
                const auto& c = map.cells[size_t(by) * size_t(map.width) + size_t(bx)];
                cells << bx << ',' << by << ',' << c.block << ',' << c.value << '\n';
            }
        objects << "bx,by,x,y,type,turn,variant,extra\n";
        for (const auto& o : map.scenery)
            objects << o.bx << ',' << o.by << ',' << int(o.x) << ',' << int(o.y) << ',' << int(o.type) << ','
                    << int(o.turn) << ',' << int(o.variant) << ',' << int(o.extra) << '\n';
    }
    std::printf("%d maps written to %s\n", maps, argv[3]);
    return 0;
}

// Renders models to a contact sheet (a PPM image), textured as the model
// viewer shows them, two views each with its name: every main model (not
// lod/ or a variant) whose name holds the filter, from the
// archives given. Textures are found across all of them.
int cmdGeoSheet(int argc, char** argv) {
    // -l: larger cells, four views each, a model a row; -t: eight views
    // round it at the briefings' height.
    const bool turntable = argc >= 3 && std::strcmp(argv[2], "-t") == 0;
    const bool large = turntable || (argc >= 3 && std::strcmp(argv[2], "-l") == 0);
    if (large)
        --argc, ++argv;
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool geo-sheet [-l|-t] <out.ppm> <name filter> <file.cat>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::Archives archives;
    for (int i = 4; i < argc; ++i) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(argv[i])) {
            std::fprintf(stderr, "%s: %s\n", argv[i], err.message.c_str());
            return 1;
        }
        archives.push_back(std::move(cat));
    }
    const std::vector<nw::TextureFile> textures = nw::listTextureFiles(archives);
    const std::string filter = argv[3];
    std::vector<std::string> names;
    for (const auto& a : archives)
        for (const auto& e : a->entries()) {
            std::string folder, stem, ext;
            nw::splitName(e.name, folder, stem, ext);
            // Variants: damaged (_d, _ad), low detail (_l), shadow (_s) and
            // placement outline (_p).
            const bool variant = stem.size() > 2 && (stem.ends_with("_d") || stem.ends_with("_l") || stem.ends_with("_s") ||
                                                     stem.ends_with("_p") ||
                                                     (stem.ends_with("d") && stem[stem.size() - 3] == '_'));
            if (e.tag == nw::cattag::Geometry && e.name.find("/lod/") == std::string::npos && !variant &&
                e.name.find(filter) != std::string::npos)
                names.push_back(e.name);
        }
    if (names.empty()) {
        std::fprintf(stderr, "no models match %s\n", filter.c_str());
        return 1;
    }
    const int kCellW = large ? 320 : 200, kCellH = large ? 240 : 150, kPerRow = large ? 1 : 4, kViews = turntable ? 8 : large ? 4 : 2;
    const int rows = int((names.size() + size_t(kPerRow) - 1) / size_t(kPerRow));
    reluva::Surface sheet;
    sheet.create(kPerRow * kViews * kCellW, rows * kCellH, reluva::PixelFormat::RGBA32);
    reluva::Surface cell;
    cell.create(kCellW, kCellH, reluva::PixelFormat::RGBA32);
    nw::Rasterizer raster;
    for (size_t i = 0; i < names.size(); ++i) {
        nw::TexturedModel m;
        const bool ok = nw::loadTexturedModel(archives, textures, names[i], m);
        for (int view = 0; view < kViews; ++view) {
            for (size_t p = 0; p < cell.pixels.size(); p += 4)
                cell.pixels[p] = 0, cell.pixels[p + 1] = 0, cell.pixels[p + 2] = 24, cell.pixels[p + 3] = 255;
            static constexpr float kYaw[4] = {0.6f, 2.7f, 1.6f, 4.2f}, kPitch[4] = {0.35f, 0.9f, 0.15f, 0.35f};
            if (ok)
                nw::drawModelView(cell, raster, m, turntable ? float(view) * 0.7854f : kYaw[view],
                                  turntable ? 0.35f : kPitch[view]);
            if (view == 0) {
                std::string folder, stem, ext;
                nw::splitName(names[i], folder, stem, ext);
                reluva::font::drawText(cell, 3, 3, ok ? stem : stem + " ?", {255, 230, 80});
            }
            const int x0 = int(i % size_t(kPerRow)) * kViews * kCellW + view * kCellW, y0 = int(i / size_t(kPerRow)) * kCellH;
            for (int y = 0; y < kCellH; ++y)
                std::memcpy(sheet.row(y0 + y) + x0 * 4, cell.row(y), size_t(kCellW) * 4);
        }
    }
    std::ofstream out(argv[2], std::ios::binary);
    out << "P6\n" << sheet.w << ' ' << sheet.h << "\n255\n";
    for (int y = 0; y < sheet.h; ++y)
        for (int x = 0; x < sheet.w; ++x)
            out.write(reinterpret_cast<const char*>(sheet.row(y) + x * 4), 3);
    std::printf("%zu models drawn to %s\n", names.size(), argv[2]);
    return out ? 0 : 1;
}

int cmdGroundTextures(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool ground-textures <planet.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::CatArchive cat;
    if (reluva::Error err = cat.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    for (const auto& e : cat.entries()) {
        std::string folder, stem, ext;
        nw::splitName(e.name, folder, stem, ext);
        static const char* const kScenery[] = {"tree", "tree_s", "rock", "frn_spec", "bush"};
        if (e.tag != nw::cattag::Geometry || e.name.find("/map/") == std::string::npos ||
            e.name.find("/lod/") != std::string::npos ||
            std::any_of(std::begin(kScenery), std::end(kScenery), [&](const char* n) { return stem == n; }))
            continue;
        std::vector<std::string> planet;
        for (const auto& t : cat.entries()) {
            std::string tf, ts, te;
            nw::splitName(t.name, tf, ts, te);
            if (t.tag == nw::cattag::Texture && tf == folder && (te == "mp0" || te == "lbp"))
                planet.push_back(ts);
        }
        nw::GeoModel m;
        if (cat.read(e, data) || nw::parseGeo(data, m)) {
            std::printf("%-12s doesn't decode\n", stem.c_str());
            continue;
        }
        const auto picks = nw::chooseGroundTextures(stem, m, planet);
        std::printf("%-12s", stem.c_str());
        for (size_t s = 0; s < picks.size(); ++s) {
            size_t faces = 0;
            for (const auto& f : m.faces)
                faces += f.corners.back().extra[4] == s;
            std::printf("  %zu: %s (%zu)", s, picks[s].empty() ? "?" : picks[s].c_str(), faces);
        }
        std::printf("\n");
    }
    return 0;
}

int cmdGeoTextures(int argc, char** argv) {
    int first = 2;
    const bool all = argc > 2 && std::strcmp(argv[2], "-a") == 0;
    if (all)
        ++first;
    if (argc <= first) {
        std::fprintf(stderr, "usage: reluva-tool geo-textures [-a] <file.cat>...\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    nw::Archives archives;
    for (int a = first; a < argc; ++a) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        archives.push_back(std::move(cat));
    }
    const std::vector<nw::TextureFile> textures = nw::listTextureFiles(archives);
    unsigned models = 0, without = 0, short_ = 0;
    std::vector<uint8_t> data;
    for (const auto& cat : archives)
        for (const auto& e : cat->entries()) {
            if (e.tag != nw::cattag::Geometry)
                continue;
            ++models;
            // How many textures the faces number: the highest number + 1.
            uint32_t needed = 0;
            nw::GeoModel m;
            if (!cat->read(e, data) && !nw::parseGeo(data, m))
                for (const auto& face : m.faces)
                    needed = std::max(needed, face.corners.back().extra[4] + 1);
            auto slots = nw::findModelTextures(e.name, textures, needed);
            nw::addDoorTextures(m, e.name, textures, slots);
            nw::addPadTextures(m, e.name, textures, slots);
            const size_t found = size_t(std::count_if(slots.begin(), slots.end(), [](auto* t) { return t; }));
            const bool tooFew = found < needed;
            if (found == 0)
                ++without;
            else if (tooFew)
                ++short_;
            if (!all && !tooFew)
                continue;
            std::printf("%s: needs %u, found", e.name.c_str(), needed);
            for (const auto* t : slots)
                std::printf(" %s", t ? t->stem.c_str() : "?");
            if (slots.empty())
                std::printf(" none");
            if (tooFew) {
                // Textures in the same folder whose names start like the model's.
                std::string folder, stem, ext;
                nw::splitName(e.name, folder, stem, ext);
                const size_t prefix = std::min<size_t>(3, stem.size());
                std::printf("; similar:");
                unsigned shown = 0;
                for (const auto& t : textures)
                    if (t.folder == folder && t.stem.compare(0, prefix, stem, 0, prefix) == 0 && shown++ < 10)
                        std::printf(" %s", t.stem.c_str());
                if (!shown)
                    std::printf(" -");
            }
            std::printf("\n");
        }
    std::printf("%u models: %u without textures, %u with fewer than their faces number\n", models, without,
                short_);
    return 0;
}

// Map (.gmf) layout as far as known: u16 counts of type-21, type-20 and
// type-19 records, u16 grid width and height; those records (u16 type, u16
// length, u16 ?, then `length` bytes) in that order; then data of unknown
// layout. See docs/formats/newworlds/gmf.md.
int cmdGmfInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool gmf-info <file.gmf> [record number]\n");
        return 2;
    }
    Bytes d;
    if (!readFile(argv[2], d))
        return 1;
    const long want = argc > 3 ? std::strtol(argv[3], nullptr, 0) : -1;
    const unsigned counts[3] = {rd16(d, 0), rd16(d, 2), rd16(d, 4)};
    const uint16_t types[3] = {21, 20, 19};
    std::printf("%zu bytes; %u type-21, %u type-20 and %u type-19 records; grid %u x %u\n", d.size(), counts[0],
                counts[1], counts[2], rd16(d, 6), rd16(d, 8));
    {
        reluva::newworlds::GmfMap map;
        const reluva::Error err = reluva::newworlds::parseGmf(d, map);
        std::printf("engine's reader: %s\n", err ? err.message.c_str() : "reads it");
        if (!err) {
            std::map<int, size_t> types;
            for (const auto& o : map.scenery)
                ++types[o.type];
            std::printf("placed objects: %zu;", map.scenery.size());
            for (const auto& [type, n] : types)
                std::printf(" type %d x%zu", type, n);
            std::printf("\n");
        }
    }

    struct Record {
        size_t offset;
        uint16_t type, length, third;
    };
    std::vector<Record> records;
    size_t p = 10;
    for (int k = 0; k < 3; ++k) {
        std::map<uint16_t, unsigned> lengths, thirds;
        const size_t start = p;
        for (unsigned i = 0; i < counts[k]; ++i) {
            const Record r{p, rd16(d, p), rd16(d, p + 2), rd16(d, p + 4)};
            if (p + 6 > d.size() || r.type != types[k] || p + 6 + r.length > d.size()) {
                std::printf("type-%u record %u at 0x%zx doesn't fit (type %u, length %u):\n", types[k], i, p, r.type,
                            r.length);
                hexdump(d, p, 0x40);
                return 1;
            }
            records.push_back(r);
            ++lengths[r.length];
            ++thirds[r.third];
            p += 6 + r.length;
        }
        std::printf("type %u: 0x%zx..0x%zx; lengths", types[k], start, p);
        for (const auto& [v, n] : lengths)
            std::printf(" %u x%u", v, n);
        std::printf("; third values");
        for (const auto& [v, n] : thirds)
            std::printf(" %u x%u", v, n);
        std::printf("\n");
    }
    std::printf("after the records: 0x%zx, %zu bytes (%.2f per grid cell)\n", p, d.size() - p,
                double(d.size() - p) / std::max(1, rd16(d, 6) * rd16(d, 8)));

    if (want >= 0 && size_t(want) < records.size()) {
        const Record& r = records[size_t(want)];
        std::printf("record %ld at 0x%zx: type %u, length %u, third %u\n", want, r.offset, r.type, r.length, r.third);
        hexdump(d, r.offset, 6 + size_t(r.length));
        return 0;
    }
    // Type 19: four u32 indices after 6 bytes, presumably into the tiles
    // (the type-21/20 records). Check they're in range.
    {
        const size_t first = counts[0] + counts[1];
        uint32_t maxIndex = 0;
        for (size_t i = first; i < records.size(); ++i)
            for (int k = 0; k < 4; ++k)
                maxIndex = std::max(maxIndex, rd32(d, records[i].offset + 6 + 6 + 4 * size_t(k)));
        std::printf("type-19 indices: highest %u (%u tiles)\n", maxIndex, counts[0]);
        // Type 20: u32 own number, u32, u32, u32, u16, then 8 x 8 byte grids.
        unsigned numbered = 0;
        for (size_t i = counts[0]; i < first; ++i)
            numbered += rd32(d, records[i].offset + 6) == i - counts[0];
        std::printf("type-20 records whose first value is their own number: %u of %u\n", numbered, counts[1]);
    }

    // Each tile's type-20 header values: own number, 0, model?, flags?, u16.
    std::printf("tiles (number: type-20 values 2-5):");
    for (size_t i = counts[0]; i < size_t(counts[0]) + counts[1]; ++i) {
        const size_t o = records[i].offset + 6;
        std::printf("%s %zu: %u %u 0x%x %u", (i - counts[0]) % 6 ? ";" : "\n ", i - counts[0], rd32(d, o + 4),
                    rd32(d, o + 8), rd32(d, o + 12), rd16(d, o + 16));
    }
    std::printf("\n");

    // After the records: a u32 count, then that many u32 entries (lists ending
    // ffffffff), then one 10-byte entry per grid cell (u16 block, u16 ?, u16 ?,
    // u32 where its list starts), then 1024 more bytes.
    {
        const uint32_t n = rd32(d, p);
        size_t separators = 0;
        const size_t end = p + 4 + size_t(n) * 4;
        if (end <= d.size()) {
            for (size_t o = p + 4; o < end; o += 4)
                separators += rd32(d, o) == 0xffffffffu;
            std::printf("after the records: u32 %u; %u u32s from there hold %zu ffffffff separators and end at 0x%zx, "
                        "leaving %zu bytes (%.2f per grid cell)\n",
                        n, n, separators, end, d.size() - end,
                        double(d.size() - end) / std::max(1, rd16(d, 6) * rd16(d, 8)));
            const unsigned w = rd16(d, 6), h = rd16(d, 8);
            if (d.size() - end >= size_t(w) * h * 10) {
                std::map<uint16_t, unsigned> seconds;
                std::printf("blocks on the grid (rows of %u; 0-9 then a-z, '+' for higher):\n", w);
                for (unsigned y = 0; y < h; ++y) {
                    std::printf("  ");
                    for (unsigned x = 0; x < w; ++x) {
                        const size_t e = end + (size_t(y) * w + x) * 10;
                        const unsigned b = rd16(d, e);
                        ++seconds[rd16(d, e + 2)];
                        std::printf("%c", b < 10 ? char('0' + b) : b < 36 ? char('a' + b - 10) : '+');
                    }
                    std::printf("\n");
                }
                std::printf("second values of the grid entries:");
                for (const auto& [v, n] : seconds)
                    std::printf(" %u x%u", v, n);
                std::printf("\nlast 1024 bytes start:\n");
                hexdump(d, end + size_t(w) * h * 10, 0x40);
            } else {
                std::printf("start of what's left:\n");
                hexdump(d, end, 0x80);
            }
        } else {
            std::printf("after the records: u32 %u (too many for the file to be that many u32s)\n", n);
        }
    }

    // A sample of each kind, and the start of what follows.
    for (int k = 0; k < 3; ++k) {
        size_t first = 0;
        for (int j = 0; j < k; ++j)
            first += counts[j];
        if (counts[k] == 0)
            continue;
        const size_t pick = first + counts[k] / 2;
        const Record& r = records[pick];
        std::printf("record %zu (a type-%u one from the middle) at 0x%zx:\n", pick, r.type, r.offset);
        if (r.type == 20) { // header values, then the 8 x 8 grids
            std::printf("  values %u %u %u %u %u; grids:\n", rd32(d, r.offset + 6), rd32(d, r.offset + 10),
                        rd32(d, r.offset + 14), rd32(d, r.offset + 18), rd16(d, r.offset + 22));
            const size_t grids = r.offset + 6 + 18;
            for (unsigned row = 0; row < 8; ++row) {
                std::printf("   ");
                for (size_t g = 0; g * 64 + 64 <= size_t(r.length) - 18; ++g) {
                    for (unsigned c = 0; c < 8; ++c)
                        std::printf(" %02x", d[grids + g * 64 + row * 8 + c]);
                    std::printf("  ");
                }
                std::printf("\n");
            }
            const size_t rest = grids + ((size_t(r.length) - 18) / 64) * 64;
            if (rest < r.offset + 6 + r.length) {
                std::printf("  after the grids:\n");
                hexdump(d, rest, r.offset + 6 + r.length - rest);
            }
            continue;
        }
        hexdump(d, r.offset, std::min<size_t>(6 + size_t(r.length), 0x80));
    }
    if (counts[2] > 1) {
        std::printf("first type-19 records, one per line:\n");
        const size_t first = counts[0] + counts[1];
        for (size_t i = first; i < first + std::min<size_t>(counts[2], 12); ++i)
            hexdump(d, records[i].offset, 6 + size_t(records[i].length));
    }
    std::printf("after the records:\n");
    hexdump(d, p, 0x80);
    return 0;
}

int cmdGeoSurvey(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool geo-survey <file.cat>...\n");
        return 2;
    }
    std::map<std::string, std::vector<std::string>> failures;
    unsigned total = 0, ok = 0;
    std::vector<uint8_t> d;
    for (int a = 2; a < argc; ++a) {
        reluva::newworlds::CatArchive cat;
        if (reluva::Error err = cat.open(argv[a])) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto& e : cat.entries()) {
            if (e.tag != reluva::newworlds::cattag::Geometry || cat.read(e, d))
                continue;
            ++total;
            reluva::newworlds::GeoModel m;
            reluva::Error err = reluva::newworlds::parseGeo(d, m);
            if (!err) {
                ++ok;
                continue;
            }
            // Group by message with numbers (decimal or 0x hex) removed.
            std::string key;
            const std::string& msg = err.message;
            for (size_t i = 0; i < msg.size();) {
                if (msg[i] >= '0' && msg[i] <= '9') {
                    if (msg.compare(i, 2, "0x") == 0)
                        i += 2;
                    while (i < msg.size() && std::isxdigit(static_cast<unsigned char>(msg[i])))
                        ++i;
                    key += '#';
                } else {
                    key += msg[i++];
                }
            }
            failures[key].push_back(std::string(argv[a]) + ": " + e.name + " (" + std::to_string(d.size()) +
                                    " bytes): " + err.message);
        }
    }
    std::printf("%u models, %u decode\n\n", total, ok);
    for (const auto& [key, list] : failures) {
        std::printf("%5zu  %s\n", list.size(), key.c_str());
        for (size_t i = 0; i < list.size() && i < 3; ++i)
            std::printf("       e.g. %s\n", list[i].c_str());
    }
    return 0;
}

#endif // RELUVA_ENGINE_NEWWORLDS

#ifdef RELUVA_ENGINE_IDTECH3
// Mounts an id Tech 3 game folder (".../BaseEF") the way the engine does.
bool mountIdTech3(const char* folder, reluva::SearchSet& set) {
    const std::filesystem::path dir = std::filesystem::path(folder).lexically_normal();
    const std::filesystem::path path = dir.has_filename() ? dir : dir.parent_path();
    std::vector<std::string> warnings;
    if (reluva::Error err =
            reluva::idtech3::mountGameDir(set, path.parent_path(), path.filename().string(), 0, warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    for (const std::string& w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    return true;
}

int cmdBspCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool bsp-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    int maps = 0, failed = 0, corrected = 0;
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        ++maps;
        reluva::idtech3::Bsp bsp;
        reluva::Error err = set.read(name, data);
        if (!err)
            err = reluva::idtech3::parseBsp(data, bsp);
        if (err) {
            ++failed;
            std::printf("FAIL %s: %s\n", name.c_str(), err.message.c_str());
            continue;
        }
        const auto& f = bsp.fixes;
        if (f.total())
            ++corrected;
        std::printf("ok   %s: %zu surfaces, %zu lightmaps", name.c_str(), bsp.surfaces.size(), bsp.lightmapCount());
        if (f.total())
            std::printf("; corrected: %d lightmap, %d fog, %d flare/bad, %d type", f.lightmaps, f.fogs, f.flares,
                        f.types);
        std::printf("\n");
    }
    std::printf("%d maps: %d failed, %d loaded with corrections\n", maps, failed, corrected);
    return failed ? 1 : 0;
}

int cmdImageCheck(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool image-check <game folder> [map name filter]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);
    for (const std::string& w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    const std::string filter = argc == 4 ? reluva::normalizeArchiveName(argv[3]) : std::string();

    std::map<std::string, int> missingEverywhere; // image -> maps missing it
    int maps = 0;
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0 ||
            key.find(filter) == std::string::npos)
            continue;
        q3::Bsp bsp;
        if (set.read(name, data) || q3::parseBsp(data, bsp))
            continue;
        ++maps;
        // Every image each shader names: its stages' maps, or for a shader
        // without a script, the image of its own name.
        std::map<std::string, std::string> missing; // image -> a shader wanting it
        for (const q3::BspShader& s : bsp.shaders) {
            std::vector<std::string> images;
            const q3::ShaderDef* def = shaders.find(s.name);
            if (!def)
                images.push_back(s.name);
            else
                for (const q3::ShaderStage& st : def->stages)
                    for (const std::string& m : st.maps)
                        if (!m.empty() && m[0] != '$')
                            images.push_back(m);
            for (const std::string& img : images)
                if (q3::findImage(set, img).empty())
                    missing.emplace(reluva::normalizeArchiveName(img), std::string(def ? "script " : "") + s.name);
        }
        std::printf("%s: %zu shaders, %zu missing images\n", name.c_str(), bsp.shaders.size(), missing.size());
        for (const auto& [img, shader] : missing) {
            std::printf("    %-56s (shader %s)\n", img.c_str(), shader.c_str());
            ++missingEverywhere[img];
        }
    }
    std::printf("%d maps, %zu distinct missing images\n", maps, missingEverywhere.size());
    return 0;
}

int cmdModelCheck(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool model-check <game folder> [name filter] [-v]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string filter;
    bool verbose = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else
            filter = reluva::normalizeArchiveName(argv[i]);
    }
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);

    // Every image by its file name without folder or extension, to say
    // where a missing one might be.
    auto stem = [](const std::string& path) {
        std::string s = reluva::normalizeArchiveName(path);
        if (const size_t slash = s.find_last_of('/'); slash != std::string::npos)
            s = s.substr(slash + 1);
        if (const size_t dot = s.find_last_of('.'); dot != std::string::npos)
            s.resize(dot);
        return s;
    };
    auto folder = [](const std::string& path) {
        const std::string s = reluva::normalizeArchiveName(path);
        const size_t slash = s.find_last_of('/');
        return slash == std::string::npos ? std::string() : s.substr(0, slash);
    };
    Bytes data;
    std::multimap<std::string, std::string> imagesByStem;
    std::vector<std::string> models;
    std::set<std::string> mapShadersBare, mapModels; // what the maps use: shader names without extension, models
    std::vector<std::string> maps;
    for (const std::string& n : set.list()) {
        const std::string key = reluva::normalizeArchiveName(n);
        if (key.size() > 4) {
            const std::string ext = key.substr(key.size() - 4);
            if (ext == ".tga" || ext == ".jpg" || ext == ".png")
                imagesByStem.emplace(stem(key), key);
            else if ((ext == ".md3" || ext == ".mdr") && key.find(filter) != std::string::npos)
                models.push_back(n);
            else if (ext == ".bsp" && key.rfind("maps/", 0) == 0)
                maps.push_back(n);
        }
    }
    // A shader name as maps store it: without an extension.
    auto bare = [](const std::string& name) {
        std::string s = reluva::normalizeArchiveName(name);
        const size_t dot = s.find_last_of('.'), slash = s.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            s.resize(dot);
        return s;
    };
    for (const std::string& n : maps) {
        q3::Bsp bsp;
        if (set.read(n, data) || q3::parseBsp(data, bsp))
            continue;
        for (const q3::BspShader& s : bsp.shaders)
            mapShadersBare.insert(bare(s.name));
        std::vector<q3::Entity> entities;
        if (!q3::parseEntities(bsp.entities, entities))
            for (const q3::Entity& e : entities)
                for (const char* k : {"model", "model2"})
                    if (const std::string m = e.get(k); !m.empty() && m[0] != '*')
                        mapModels.insert(reluva::normalizeArchiveName(m));
    }


    // Kinds of result, with counts and a few examples.
    std::map<std::string, std::pair<int, std::vector<std::string>>> kinds;
    auto count = [&](const std::string& kind, const std::string& example) {
        auto& [n, ex] = kinds[kind];
        ++n;
        if (ex.size() < 3)
            ex.push_back(example);
    };
    int surfaces = 0, unreadable = 0;
    for (const std::string& path : models) {
        q3::Md3Model m;
        if (set.read(path, data) || q3::parseModel(data, m)) {
            ++unreadable;
            continue;
        }
        const std::string base = reluva::normalizeArchiveName(path).substr(0, path.size() - 4);
        // A skin next to the model, as players have, picks shaders first.
        std::string skins;
        std::map<std::string, std::string> skin;
        // Lower-detail copies (head_1.md3) use the main model's skins.
        std::string main = base;
        if (main.size() > 2 && main[main.size() - 2] == '_' && (main.back() == '1' || main.back() == '2'))
            main.resize(main.size() - 2);
        for (const std::string& n : {base + ".skin", base + "_default.skin", main + ".skin", main + "_default.skin"})
            if (set.has(n)) {
                skins += " " + n;
                if (skin.empty() && !set.read(n, data))
                    skin = q3::parseSkin(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
            }
        for (const q3::Md3Surface& s : m.surfaces) {
            ++surfaces;
            std::string name = s.shaders.empty() ? std::string() : s.shaders.front();
            std::string via;
            if (auto it = skin.find(reluva::fs::toLower(s.name)); it != skin.end()) {
                name = it->second;
                via = "skin: ";
            }
            const std::string where = path + " surface " + s.name + ": \"" + name + "\"";
            if (name.empty()) {
                count("no shader name" + std::string(skins.empty() ? "" : " (model has skins)"), where + skins);
                continue;
            }
            std::vector<std::string> wanted; // images it needs
            if (const q3::ShaderDef* def = shaders.find(name)) {
                for (const q3::ShaderStage& st : def->stages)
                    for (const std::string& img : st.maps)
                        if (!img.empty() && img[0] != '$')
                            wanted.push_back(img);
                if (wanted.empty()) {
                    count(via + "found: script without images", where);
                    continue;
                }
            } else {
                wanted.push_back(name);
            }
            bool allFound = true;
            for (const std::string& img : wanted) {
                if (!q3::findImage(set, img).empty())
                    continue;
                allFound = false;
                // Where files of that name are, if anywhere.
                std::string kind = via + (shaders.find(name) ? "missing image of a script" : "missing image");
                if (img.find('/') == std::string::npos && img.find('\\') == std::string::npos)
                    kind += ", name without a folder";
                const auto range = imagesByStem.equal_range(stem(img));
                std::string found;
                bool sameFolder = false;
                for (auto it = range.first; it != range.second; ++it) {
                    found += " " + it->second;
                    if (folder(it->second) == folder(path))
                        sameFolder = true;
                }
                kind += found.empty() ? ", no file of that name anywhere"
                                      : sameFolder ? ", a file of that name in the model's folder"
                                                   : ", a file of that name elsewhere";
                // Does a map show it: built into a map (whose surfaces keep
                // the model's shader names), or placed by an entity?
                const bool inMap = mapShadersBare.count(bare(name)) != 0;
                const bool placed = mapModels.count(reluva::normalizeArchiveName(path)) != 0;
                kind += inMap ? " [built into a map]" : placed ? " [placed by a map entity]" : " [not used by any map]";
                count(kind, where + (img != name ? " needs " + img : std::string()) +
                                (found.empty() ? std::string() : " ->" + found));
                if (verbose)
                    std::printf("MISSING %s%s ->%s\n", where.c_str(), img != name ? (" needs " + img).c_str() : "",
                                found.empty() ? " (none)" : found.c_str());
            }
            if (allFound)
                count(via + "found", where);
        }
    }
    std::printf("%zu models, %d surfaces, %d models unreadable\n", models.size(), surfaces, unreadable);
    for (const auto& [kind, info] : kinds) {
        std::printf("%6d  %s\n", info.first, kind.c_str());
        if (kind.find("found") == std::string::npos || kind.find("missing") != std::string::npos)
            for (const std::string& e : info.second)
                std::printf("          e.g. %s\n", e.c_str());
    }
    return 0;
}

// Loads a QVM from a game folder's search path.
bool loadQvm(const char* folder, const char* name, reluva::idtech3::Qvm& vm) {
    reluva::SearchSet set;
    if (!mountIdTech3(folder, set))
        return false;
    Bytes data;
    reluva::Error err = set.read(name, data);
    if (!err)
        err = vm.load(data);
    if (err) {
        std::fprintf(stderr, "%s: %s\n", name, err.message.c_str());
        return false;
    }
    return true;
}

int cmdQvmInfo(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool qvm-info <game folder> <vm/x.qvm> [-s]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const auto& code = vm.code();
    int functions = 0;
    // System calls in the code: a constant negative target just before a
    // CALL. Per number: how many call sites, and in how many functions.
    std::map<int, std::pair<int, std::set<int>>> calls;
    int indirect = 0, function = -1;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].op == q3::qvmop::Enter) {
            ++functions;
            function = int(i);
        }
        if (code[i].op != q3::qvmop::Call)
            continue;
        if (i > 0 && code[i - 1].op == q3::qvmop::Const) {
            const int32_t target = code[i - 1].operand;
            if (target < 0) {
                auto& [n, fns] = calls[-1 - target];
                ++n;
                fns.insert(function);
            }
        } else {
            ++indirect;
        }
    }
    std::printf("%s: %zu instructions in %d functions, %u bytes of initialised data and literals, %zu bytes of memory\n",
                argv[3], code.size(), functions, unsigned(vm.litEnd()), vm.memory().size());
    std::printf("%d calls through function pointers\n", indirect);
    size_t tableBytes = 0;
    for (const auto& [first, end] : vm.protectedRanges())
        tableBytes += end - first;
    std::printf("%zu switch jump tables (%zu bytes), kept from writes while running\n", vm.protectedRanges().size(),
                tableBytes);
    std::printf("system calls (number: call sites, functions):\n");
    for (const auto& [number, info] : calls)
        std::printf("  %4d: %4d sites in %3zu functions\n", number, info.first, info.second.size());
    if (argc == 5 && std::strcmp(argv[4], "-s") == 0) {
        // Printable runs in the initialised data, which hold the strings.
        const auto& mem = vm.memory();
        std::string run;
        size_t start = 0;
        for (size_t i = 0; i < mem.size() && i < vm.litEnd(); ++i) {
            const char c = char(mem[i]);
            if (c >= 32 && c < 127) {
                if (run.empty())
                    start = i;
                run += c;
                continue;
            }
            if (c == 0 && run.size() >= 3)
                std::printf("  %8zu  %s\n", start, run.c_str());
            run.clear();
        }
    }
    return 0;
}

int cmdQvmRun(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool qvm-run <game folder> <vm/x.qvm> [command [arg...]] [-limit N]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    int32_t command = 0;
    std::vector<int32_t> args;
    uint64_t limit = 50'000'000;
    bool haveCommand = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-limit") == 0 && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 0);
        else if (!haveCommand) {
            command = int32_t(std::strtol(argv[i], nullptr, 0));
            haveCommand = true;
        } else
            args.push_back(int32_t(std::strtol(argv[i], nullptr, 0)));
    }
    vm.setInstructionLimit(limit);
    // Log each system call; an argument pointing at text shows the text.
    int logged = 0;
    vm.setSystemCall([&](q3::Qvm& v, int number, const int32_t* a) -> int32_t {
        ++logged;
        std::printf("syscall %4d (", number);
        int last = 5;
        while (last >= 0 && a[last] == 0)
            --last;
        for (int i = 0; i <= last; ++i) {
            std::printf("%s%d", i ? ", " : "", a[i]);
            if (a[i] > 0 && v.valid(a[i], 1)) {
                const std::string s = v.string(a[i], 120);
                bool printable = s.size() >= 2;
                for (char c : s)
                    printable = printable && ((c >= 32 && c < 127) || c == '\n' || c == '\t');
                if (printable) {
                    std::string shown;
                    for (char c : s)
                        shown += c == '\n' ? std::string("\\n") : std::string(1, c);
                    std::printf(" \"%s\"", shown.c_str());
                }
            }
        }
        std::printf(")\n");
        return 0;
    });
    int32_t result = 0;
    reluva::Error err = vm.call(command, args, result);
    std::printf("vmMain(%d) %s %d after %llu instructions and %d system calls\n", command,
                err ? "failed:" : "returned", err ? 0 : result, static_cast<unsigned long long>(vm.executed()), logged);
    if (err)
        std::printf("  %s\n", err.message.c_str());
    return err ? 1 : 0;
}

int cmdQvmRefs(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: reluva-tool qvm-refs <game folder> <vm/x.qvm> <address> <size>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const long long base = std::strtoll(argv[4], nullptr, 0), size = std::strtoll(argv[5], nullptr, 0);
    // Constants inside the range are addresses of a structure's fields (or
    // of the structure itself): per offset, how often, and what the code
    // does with the address next.
    std::map<long long, std::map<std::string, int>> refs;
    const auto& code = vm.code();
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].op != q3::qvmop::Const || code[i].operand < base || code[i].operand >= base + size)
            continue;
        const std::string next = i + 1 < code.size() ? q3::qvmop::name(code[i + 1].op) : "end";
        ++refs[code[i].operand - base][next];
    }
    std::printf("constants between %lld and %lld in %s (offset: count followed-by...):\n", base, base + size, argv[3]);
    for (const auto& [offset, nexts] : refs) {
        int total = 0;
        std::string what;
        for (const auto& [op, n] : nexts) {
            total += n;
            what += " " + op + "x" + std::to_string(n);
        }
        std::printf("  %6lld: %4d %s\n", offset, total, what.c_str());
    }
    return 0;
}

// One instruction, with what its operand probably means: the text a
// constant points at, a float, or the system call a call makes.
void printQvmInstruction(const reluva::idtech3::Qvm& vm, size_t i, bool mark) {
    namespace q3 = reluva::idtech3;
    const auto& code = vm.code();
    const auto& in = code[i];
    std::printf("%s%7zu  %-9s", mark ? "> " : "  ", i, q3::qvmop::name(in.op));
    if (q3::qvmop::operandSize(in.op))
        std::printf(" %d", in.operand);
    if (in.op == q3::qvmop::Enter)
        std::printf("    ; function start, %d bytes of frame", in.operand);
    if (in.op == q3::qvmop::Const) {
        const int32_t v = in.operand;
        if (i + 1 < code.size() && code[i + 1].op == q3::qvmop::Call && v < 0)
            std::printf("    ; system call %d", -1 - v);
        else if (i + 1 < code.size() && code[i + 1].op == q3::qvmop::Call)
            std::printf("    ; calls the function at %d", v);
        else if (v > 0 && uint32_t(v) >= vm.litStart() && uint32_t(v) < vm.litEnd()) {
            const std::string text = vm.string(v, 60);
            bool printable = text.size() >= 2;
            for (char c : text)
                printable = printable && ((c >= 32 && c < 127) || c == '\n');
            if (printable)
                std::printf("    ; \"%s\"", text.c_str());
        }
        const float f = q3::Qvm::asFloat(v);
        const float a = std::fabs(f);
        if ((v & 0x7f800000) != 0 && (v < -65536 || v > 65536) && a > 1e-6f && a < 1e7f)
            std::printf("    ; %g as a float", double(f));
    }
    std::printf("\n");
}

int cmdQvmDis(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool qvm-dis <game folder> <vm/x.qvm> <first instruction> [count]\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -ref <address> [size]\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -str <text>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -syscall <number>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -syscalls <first> <last>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -func <instruction>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -blockcopies\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -fields <address of a pointer>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const auto& code = vm.code();
    // The function holding instruction i: from its ENTER to the next one.
    auto functionAt = [&](size_t i) {
        size_t first = i;
        while (first > 0 && code[first].op != q3::qvmop::Enter)
            --first;
        size_t last = first + 1;
        while (last < code.size() && code[last].op != q3::qvmop::Enter)
            ++last;
        return std::make_pair(first, last);
    };
    if (std::strcmp(argv[4], "-str") == 0 && argc >= 6) {
        // Every function using a string that contains the text, then the
        // functions those call, each listed whole.
        auto lower = [](std::string t) {
            for (char& c : t)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string needle = lower(argv[5]);
        const auto& mem = vm.memory();
        std::map<int32_t, std::string> strings; // address -> text
        for (size_t i = 0; i < vm.litEnd();) {
            size_t j = i;
            while (j < vm.litEnd() && mem[j] >= 32 && mem[j] < 127)
                ++j;
            if (j < vm.litEnd() && mem[j] == 0 && j > i &&
                lower(std::string(mem.begin() + long(i), mem.begin() + long(j))).find(needle) != std::string::npos)
                strings[int32_t(i)] = std::string(mem.begin() + long(i), mem.begin() + long(j));
            i = j + 1;
        }
        std::set<size_t> users, callees;
        for (const auto& [address, text] : strings) {
            std::printf("string at %d: \"%s\"\n", address, text.c_str());
            for (size_t i = 0; i < code.size(); ++i)
                if (code[i].op == q3::qvmop::Const && code[i].operand == address)
                    users.insert(functionAt(i).first);
        }
        for (size_t f : users) {
            const auto [first, last] = functionAt(f);
            for (size_t i = first; i + 1 < last; ++i)
                if (code[i].op == q3::qvmop::Const && code[i + 1].op == q3::qvmop::Call && code[i].operand >= 0 &&
                    size_t(code[i].operand) < code.size() && !users.count(size_t(code[i].operand)))
                    callees.insert(size_t(code[i].operand));
        }
        size_t printed = 0;
        auto list = [&](size_t f, const char* why) {
            const auto [first, last] = functionAt(f);
            std::printf("=== function at %zu (%s), %zu instructions\n", first, why, last - first);
            if (printed > 4000) {
                std::printf("    (not listed: over 4000 lines already)\n");
                return;
            }
            for (size_t i = first; i < last; ++i)
                printQvmInstruction(vm, i, false);
            printed += last - first;
        };
        for (size_t f : users)
            list(f, "uses the string");
        for (size_t f : callees)
            list(f, "called by one above");
        return 0;
    }
    if (std::strcmp(argv[4], "-fields") == 0 && argc >= 6) {
        // The fields used through a pointer kept in a global variable:
        // CONST global, LOAD4 (the pointer), CONST offset, ADD. By offset,
        // how often, and what the code then does (LOAD4: reads, STORE4 or
        // a later store: writes; ARG: passes the field's address).
        const int32_t global = int32_t(std::strtol(argv[5], nullptr, 0));
        std::map<int32_t, std::map<std::string, int>> fields;
        for (size_t i = 0; i + 4 < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand != global || code[i + 1].op != q3::qvmop::Load4)
                continue;
            if (code[i + 2].op == q3::qvmop::Const && code[i + 3].op == q3::qvmop::Add)
                ++fields[code[i + 2].operand][q3::qvmop::name(code[i + 4].op)];
            else
                ++fields[0][q3::qvmop::name(code[i + 2].op)];
        }
        std::printf("fields used through the pointer at %d (offset: count followed-by...):\n", global);
        for (const auto& [offset, nexts] : fields) {
            int total = 0;
            std::string what;
            for (const auto& [op, n] : nexts) {
                total += n;
                what += " " + op + "x" + std::to_string(n);
            }
            std::printf("  %6d: %4d %s\n", offset, total, what.c_str());
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-blockcopies") == 0) {
        // Every structure copied whole, by size: a structure's size shows
        // where the code copies one (a command into the movement code...).
        std::map<int32_t, std::vector<size_t>> sizes;
        for (size_t i = 0; i < code.size(); ++i)
            if (code[i].op == q3::qvmop::BlockCopy)
                sizes[code[i].operand].push_back(i);
        std::printf("block copies by size (bytes: count, first instructions):\n");
        for (const auto& [size, at] : sizes) {
            std::printf("  %6d: %4zu ", size, at.size());
            for (size_t k = 0; k < at.size() && k < 6; ++k)
                std::printf(" %zu", at[k]);
            std::printf("\n");
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-syscall") == 0 && argc >= 6) {
        // The code before every call to one system call: how its arguments are made.
        const int32_t target = -1 - int32_t(std::strtol(argv[5], nullptr, 0));
        int found = 0;
        for (size_t i = 0; i + 1 < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand != target || code[i + 1].op != q3::qvmop::Call)
                continue;
            if (++found > 30) {
                std::printf("(more than 30 calls)\n");
                break;
            }
            const size_t first = std::max(functionAt(i).first, i >= 16 ? i - 16 : 0);
            std::printf("--- call at instruction %zu\n", i);
            for (size_t j = first; j < std::min(code.size(), i + 8); ++j)
                printQvmInstruction(vm, j, j == i);
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-func") == 0 && argc >= 6) {
        // The whole function holding an instruction, then each function it
        // calls, whole.
        const size_t at = size_t(std::strtoull(argv[5], nullptr, 0));
        if (at >= code.size())
            return 1;
        const auto [first, last] = functionAt(at);
        std::set<size_t> callees;
        for (size_t i = first; i + 1 < last; ++i)
            if (code[i].op == q3::qvmop::Const && code[i + 1].op == q3::qvmop::Call && code[i].operand >= 0 &&
                size_t(code[i].operand) < code.size() && size_t(code[i].operand) != first)
                callees.insert(size_t(code[i].operand));
        std::printf("=== function at %zu, %zu instructions\n", first, last - first);
        for (size_t i = first; i < last; ++i)
            printQvmInstruction(vm, i, i == at);
        for (const size_t f : callees) {
            const auto [cf, cl] = functionAt(f);
            std::printf("=== function at %zu (called by it), %zu instructions\n", cf, cl - cf);
            if (cl - cf > 2000) {
                std::printf("    (not listed: over 2000 instructions)\n");
                continue;
            }
            for (size_t i = cf; i < cl; ++i)
                printQvmInstruction(vm, i, false);
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-syscalls") == 0 && argc >= 7) {
        // Every system call from `first` to `last` the code makes, in
        // short: how many calls, and for a few of them what each argument
        // is made from (a number, a text, a local or global variable...),
        // what the code does with the result, and texts the calling
        // function uses (its messages often name what it calls).
        const int lo = std::atoi(argv[5]), hi = std::atoi(argv[6]);
        auto text = [&](int32_t v) -> std::string {
            if (v <= 0 || uint32_t(v) < vm.litStart() || uint32_t(v) >= vm.litEnd())
                return {};
            const std::string t = vm.string(v, 48);
            for (char c : t)
                if (c < 32 || c >= 127)
                    return {};
            return t;
        };
        // What one argument is made from, from the instructions before its ARG.
        auto describe = [&](size_t argAt, size_t from) -> std::string {
            if (argAt == 0 || argAt - 1 < from)
                return "?";
            const auto& p = code[argAt - 1];
            if (p.op == q3::qvmop::Const) {
                if (const std::string t = text(p.operand); !t.empty())
                    return "\"" + t + "\"";
                return std::to_string(p.operand);
            }
            if (p.op == q3::qvmop::Local)
                return "&local" + std::to_string(p.operand);
            if (p.op == q3::qvmop::Load4 && argAt >= 2 && argAt - 2 >= from) {
                const auto& pp = code[argAt - 2];
                if (pp.op == q3::qvmop::Local)
                    return "local" + std::to_string(pp.operand);
                if (pp.op == q3::qvmop::Const)
                    return "[" + std::to_string(pp.operand) + "]";
                if (pp.op == q3::qvmop::Add && argAt >= 3 && code[argAt - 3].op == q3::qvmop::Const)
                    return "[p+" + std::to_string(code[argAt - 3].operand) + "]";
                return "[...]";
            }
            if (p.op == q3::qvmop::Add && argAt >= 2 && code[argAt - 2].op == q3::qvmop::Const)
                return "p+" + std::to_string(code[argAt - 2].operand);
            return std::string("(") + q3::qvmop::name(p.op) + ")";
        };
        for (int n = lo; n <= hi; ++n) {
            const int32_t target = -1 - n;
            std::vector<size_t> sites;
            for (size_t i = 0; i + 1 < code.size(); ++i)
                if (code[i].op == q3::qvmop::Const && code[i].operand == target && code[i + 1].op == q3::qvmop::Call)
                    sites.push_back(i);
            if (sites.empty())
                continue;
            std::printf("== %d: %zu calls\n", n, sites.size());
            for (size_t k = 0; k < sites.size() && k < 3; ++k) {
                const size_t i = sites[k];
                const auto [first, last] = functionAt(i);
                // The arguments: ARGs since the previous call in the function.
                size_t from = i;
                while (from > first && code[from - 1].op != q3::qvmop::Call)
                    --from;
                std::string args;
                for (size_t j = from; j < i; ++j)
                    if (code[j].op == q3::qvmop::Arg)
                        args += " " + std::to_string(code[j].operand) + ":" + describe(j, from);
                std::string after;
                for (size_t j = i + 2; j < last && j < i + 5; ++j)
                    after += std::string(" ") + q3::qvmop::name(code[j].op) +
                             (q3::qvmop::operandSize(code[j].op) ? " " + std::to_string(code[j].operand) : "");
                std::printf("  at %zu (function %zu): args%s; then%s\n", i, first, args.empty() ? " none" : args.c_str(),
                            after.c_str());
                std::set<std::string> seen;
                for (size_t j = first; j < last && seen.size() < 3; ++j)
                    if (code[j].op == q3::qvmop::Const)
                        if (const std::string t = text(code[j].operand); t.size() >= 4 && seen.insert(t).second)
                            std::printf("      uses \"%s\"\n", t.c_str());
            }
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-ref") == 0 && argc >= 6) {
        // The code around every constant pointing into [address, address + size).
        const long long base = std::strtoll(argv[5], nullptr, 0);
        const long long size = argc >= 7 ? std::strtoll(argv[6], nullptr, 0) : 4;
        int found = 0;
        for (size_t i = 0; i < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand < base || code[i].operand >= base + size)
                continue;
            if (++found > 50) {
                std::printf("(more than 50; narrow the range)\n");
                break;
            }
            size_t first = i >= 12 ? i - 12 : 0;
            for (size_t j = i; j > first; --j)
                if (code[j].op == q3::qvmop::Enter) {
                    first = j;
                    break;
                }
            const size_t last = std::min(code.size(), i + 16);
            std::printf("--- offset %lld, instruction %zu\n", code[i].operand - base, i);
            for (size_t j = first; j < last; ++j) {
                if (j > i && code[j].op == q3::qvmop::Enter)
                    break;
                printQvmInstruction(vm, j, j == i);
            }
        }
        return 0;
    }
    const size_t first = size_t(std::strtoull(argv[4], nullptr, 0));
    const size_t count = argc >= 6 ? size_t(std::strtoull(argv[5], nullptr, 0)) : 40;
    for (size_t j = first; j < code.size() && j < first + count; ++j)
        printQvmInstruction(vm, j, false);
    return 0;
}

int cmdUiRun(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool ui-run <game folder> [-frames N] [-menu M] [-size WxH] [-glwords N] [-trace] [-v]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 1, menu = 1, width = 640, height = 480, glwords = 5124;
    bool traceCalls = false, verbose = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-menu") == 0 && i + 1 < argc)
            menu = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-size") == 0 && i + 1 < argc) {
            char* end = nullptr;
            width = int(std::strtol(argv[++i], &end, 10));
            height = *end ? int(std::strtol(end + 1, nullptr, 10)) : 0;
            if (width <= 0 || height <= 0) {
                std::fprintf(stderr, "-size takes WIDTHxHEIGHT, e.g. 1280x960\n");
                return 2;
            }
        }
        else if (std::strcmp(argv[i], "-glwords") == 0 && i + 1 < argc)
            glwords = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-trace") == 0)
            traceCalls = true;
        else if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    q3::CvarStore cvars;
    ef::UiHost ui(set, cvars);
    ui.setScreenSize(width, height);
    ui.setGlconfigWords(glwords);
    ui.print = [](const std::string& text) { std::printf("ui: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : ""); };
    if (traceCalls)
        ui.trace = [](int number, const int32_t* a) {
            std::printf("  syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
    auto step = [&](const char* what, reluva::Error err) {
        std::printf("%s: %s after %llu instructions\n", what, err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(ui.vm().executed()));
        return !err;
    };
    if (!step("load and GetApiVersion", ui.load()))
        return 1;
    bool ok = step("Init", ui.call(ef::UiHost::Init));
    if (ok)
        ok = step("SetActiveMenu", ui.call(ef::UiHost::SetActiveMenu, {menu}));
    for (int f = 0; ok && f < frames; ++f) {
        ui.clearPictures();
        const int32_t time = 1000 + f * 16;
        ok = step("Refresh", ui.refresh(time));
    }

    // What was registered, and whether the engine can find it.
    q3::ShaderLibrary shaders;
    shaders.load(set);
    int missing = 0;
    for (int32_t h = 1; h <= int32_t(ui.shaders().size()); ++h) {
        const std::string& name = ui.shaders().name(h);
        if (!shaders.find(name) && q3::findImage(set, name).empty()) {
            std::printf("shader not found: %s\n", name.c_str());
            ++missing;
        }
    }
    std::printf("%zu cvars, %zu shaders (%d not found), %zu models, %zu sounds registered\n", cvars.all().size(),
                ui.shaders().size(), missing, ui.models().size(), ui.sounds().size());
    std::printf("last frame: %zu pictures\n", ui.pictures().size());
    size_t shown = 0;
    for (const auto& p : ui.pictures()) {
        if (!verbose && ++shown > 40) {
            std::printf("  ... (-v lists them all)\n");
            break;
        }
        std::printf("  %7.1f %7.1f %7.1f x %-7.1f st %.3f %.3f %.3f %.3f  rgba %.2f %.2f %.2f %.2f  %s\n", p.x, p.y, p.w,
                    p.h, p.s1, p.t1, p.s2, p.t2, p.color[0], p.color[1], p.color[2], p.color[3],
                    ui.shaders().name(p.shader).c_str());
    }
    for (const std::string& c : ui.commands())
        std::printf("command: %s\n", c.c_str());
    for (const auto& [number, count] : ui.unhandled())
        std::printf("not implemented: system call %d (%d calls)\n", number, count);
    return ok ? 0 : 1;
}

int cmdGameRun(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool game-run <game folder> <map> [-frames N] [-noclient] [-walk [forward|back|left|right]] [-set name value] [-addbot 'name skill']... [-cgame [-events] [-entities N] [-cgameframes N] [-fire] [-altfire] [-weapon N|all] [-give] [-watch model]] [-buttons] [-trace]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    namespace ef = reluva::idtech3::eliteforce;
    int frames = 10;
    bool traceCalls = false, noClient = false, walk = false, runCgame = false;
    bool probeButtons = false; // -buttons: what holding each button bit does
    // With -cgame: how many client game frames (-cgameframes), and the
    // player holding fire or alt fire (-fire, -altfire) with a weapon
    // (-weapon, the phaser by default) from the second.
    // (-weapon all: each of weapons 1 to 10 in turn, two seconds each.)
    // -give: cheats on and every weapon given first.
    int cgameFrames = 3, fireButtons = 0, fireWeapon = 1;
    bool giveAll = false;
    std::string watchModel; // -watch: print the scene's entities with this model frame by frame
    constexpr int kFramesPerWeapon = 40;
    int dumpEntities = 0; // -entities N: with -cgame, N of the scene's entities word by word
    std::vector<std::pair<std::string, std::string>> settings; // -set name value
    std::vector<std::string> addBots;                           // -addbot "name [skill]"
    bool eventsTest = false; // -events: with -cgame, the player's events against the sounds they bring
    std::string walkDirection = "forward";
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-trace") == 0)
            traceCalls = true;
        else if (std::strcmp(argv[i], "-noclient") == 0)
            noClient = true;
        else if (std::strcmp(argv[i], "-cgame") == 0)
            runCgame = true;
        else if (std::strcmp(argv[i], "-fire") == 0)
            fireButtons |= 1;
        else if (std::strcmp(argv[i], "-altfire") == 0)
            fireButtons |= 32;
        else if (std::strcmp(argv[i], "-weapon") == 0 && i + 1 < argc)
            fireWeapon = std::strcmp(argv[i + 1], "all") == 0 ? -1 : std::atoi(argv[i + 1]), ++i;
        else if (std::strcmp(argv[i], "-give") == 0)
            giveAll = true;
        else if (std::strcmp(argv[i], "-watch") == 0 && i + 1 < argc)
            watchModel = argv[++i];
        else if (std::strcmp(argv[i], "-cgameframes") == 0 && i + 1 < argc)
            cgameFrames = std::max(1, std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "-buttons") == 0)
            probeButtons = true;
        else if (std::strcmp(argv[i], "-entities") == 0 && i + 1 < argc)
            dumpEntities = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-walk") == 0) {
            walk = true;
            // An optional direction: forward (the default), back, left, right.
            if (i + 1 < argc && argv[i + 1][0] != '-')
                walkDirection = argv[++i];
        }
        else if (std::strcmp(argv[i], "-set") == 0 && i + 2 < argc) {
            settings.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        }
        else if (std::strcmp(argv[i], "-addbot") == 0 && i + 1 < argc)
            addBots.emplace_back(argv[++i]);
        else if (std::strcmp(argv[i], "-events") == 0)
            eventsTest = true;
        else
            std::fprintf(stderr, "game-run: ignoring %s (not an option it knows)\n", argv[i]);
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::string map = argv[3];
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    if (map.rfind("maps/", 0) == 0)
        map = map.substr(5);
    Bytes data;
    auto bspOwned = std::make_shared<q3::Bsp>();
    q3::Bsp& bsp = *bspOwned;
    if (reluva::Error err = set.read("maps/" + map + ".bsp", data); err || (err = q3::parseBsp(data, bsp))) {
        std::fprintf(stderr, "maps/%s.bsp: %s\n", map.c_str(), err.message.c_str());
        return 1;
    }
    q3::CvarStore cvars;
    if (probeButtons || fireButtons || giveAll)
        cvars.set("g_holoIntro", "0"); // the player has to be in the game
    if (fireWeapon < 0 && fireButtons)
        cgameFrames = std::max(cgameFrames, 10 + 10 * kFramesPerWeapon);
    for (const auto& [name, value] : settings)
        cvars.set(name, value);
    ef::GameHost game(set, cvars);
    game.print = [](const std::string& text) {
        std::printf("game: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
    };
    if (traceCalls)
        game.trace = [](int number, const int32_t* a) {
            std::printf("  syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
    auto step = [&](const char* what, reluva::Error err) {
        std::printf("%s: %s after %llu instructions\n", what, err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(game.vm().executed()));
        return !err;
    };
    if (reluva::Error err = game.load()) {
        std::printf("load: %s\n", err.message.c_str());
        return 1;
    }
    reluva::Error started = game.startLevel(map, bspOwned);
    std::printf("start level (Init and 3 frames): %s\n", started ? started.message.c_str() : "ok");
    bool ok = !started;
    if (ok && !noClient) {
        // A player joins, as on a listen server: client 0.
        const std::string userinfo = "\\name\\Reluva\\model\\munro\\handicap\\100\\sex\\male\\color\\4"
                                     "\\snaps\\20\\rate\\25000\\ip\\localhost";
        std::string refused;
        reluva::Error err = game.connectClient(0, userinfo, refused);
        std::printf("client 0 connects: %s\n", err ? err.message.c_str() : refused.empty() ? "ok" : ("refused: " + refused).c_str());
        ok = !err;
        if (ok && refused.empty()) {
            err = game.beginClient(0);
            std::printf("client 0 begins: %s\n", err ? err.message.c_str() : "ok");
            ok = !err;
            if (ok && giveAll) {
                // The game's own cheat, once it has seen sv_cheats change.
                cvars.set("sv_cheats", "1", true);
                err = game.runFrame();
                if (!err)
                    err = game.clientCommand(0, "give all");
                std::printf("give all: %s\n", err ? err.message.c_str() : "ok");
                ok = !err;
            }
            const auto o = game.entityVec(0, 272);
            std::printf("client 0's entity: %s at %.1f %.1f %.1f\n", game.linked().count(0) ? "linked" : "not linked",
                        o[0], o[1], o[2]);
        }
    }
    // Bots, added at the server's console as the original's addbot.
    for (const std::string& bot : addBots) {
        if (!ok)
            break;
        bool handled = false;
        reluva::Error err = game.serverCommand("addbot " + bot, &handled);
        std::printf("addbot %s: %s\n", bot.c_str(),
                    err ? err.message.c_str() : handled ? "ok" : "the game didn't take the command");
        ok = !err;
    }
    std::map<int32_t, q3::Vec3f> botStart;
    for (const int32_t c : game.botClients())
        botStart[c] = game.entityVec(c, 272);
    const bool player = ok && !noClient && game.linked().count(0);
    const auto startPos = player ? game.entityVec(0, 272) : q3::Vec3f{};
    for (int f = 0; ok && f < frames; ++f) {
        if (player) {
            // The player does nothing, as a server would see from an idle
            // client: an empty command for the coming frame; with -walk,
            // runs forward (from the third frame, once standing).
            ef::GameHost::UserCommand cmd;
            cmd.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
            if (walk && f >= 2) {
                if (walkDirection == "back")
                    cmd.forward = -127;
                else if (walkDirection == "left")
                    cmd.right = -127;
                else if (walkDirection == "right")
                    cmd.right = 127;
                else
                    cmd.forward = 127;
            }
            if (reluva::Error err = game.clientThink(0, cmd)) {
                step("ClientThink", err);
                ok = false;
                break;
            }
        }
        ok = step("RunFrame", game.runFrame());
        if (player) {
            // The entity's position, and the player state as Quake III lays
            // it out (commandTime 0, origin 20, velocity 32, groundEntityNum
            // 68), to be checked against Elite Force's.
            const auto o = game.entityVec(0, 272);
            const auto po = game.clientVec(0, 20), pv = game.clientVec(0, 32);
            std::printf("  client 0 at %.1f %.1f %.1f; player state: commandTime %d, origin %.1f %.1f %.1f, "
                        "velocity %.1f %.1f %.1f, ground %d; commands read so far %d\n",
                        o[0], o[1], o[2], game.clientInt(0, 0), po[0], po[1], po[2], pv[0], pv[1], pv[2],
                        game.clientInt(0, 68), (game.callCounts().count(36) ? game.callCounts().at(36) : 0));
            // Quake III's pm_type (4), pm_flags (12), pm_time (16); and the
            // game's copy of the last command (client + 464): its time and
            // its weapon (5) and moves (20-22).
            const int32_t cmdTime = game.clientInt(0, 464), cmdMoves = game.clientInt(0, 464 + 20),
                          cmdWeapon = (game.clientInt(0, 464 + 4) >> 8) & 0xff;
            std::printf("    pm_type %d, pm_flags 0x%x, pm_time %d; the game's command: time %d, weapon %d, "
                        "forward %d, right %d, up %d\n",
                        game.clientInt(0, 4), unsigned(game.clientInt(0, 12)), game.clientInt(0, 16), cmdTime,
                        cmdWeapon, int8_t(cmdMoves & 0xff), int8_t((cmdMoves >> 8) & 0xff),
                        int8_t((cmdMoves >> 16) & 0xff));
        }
    }

    if (player && probeButtons && ok) {
        // Each button bit held for a second with the phaser (weapon 1),
        // then let go for a second: which words of the player state it
        // changes, leaving out those that change when nothing is held.
        // Firing shows in its weapon time and ammunition; alt fire should
        // look like firing.
        auto state = [&] {
            std::vector<int32_t> words(ef::GameHost::kPlayerStateSize / 4);
            for (size_t i = 0; i < words.size(); ++i)
                words[i] = game.clientInt(0, int32_t(i * 4));
            return words;
        };
        auto run = [&](int32_t buttons, int frames, std::set<size_t>& changed) {
            for (int f = 0; f < frames && ok; ++f) {
                const auto before = state();
                ef::GameHost::UserCommand cmd;
                cmd.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
                cmd.buttons = buttons;
                cmd.weapon = 1;
                if (reluva::Error err = game.clientThink(0, cmd); err || (err = game.runFrame())) {
                    step("probe", err);
                    ok = false;
                }
                const auto after = state();
                for (size_t i = 1; i < after.size(); ++i) // not the command time
                    if (after[i] != before[i])
                        changed.insert(i * 4);
            }
        };
        std::set<size_t> idle, ignored;
        const int second = 1000 / ef::GameHost::kFrameMs;
        run(0, second, idle);
        for (int bit = 0; bit < 8 && ok; ++bit) { // the buttons are a byte
            std::set<size_t> changed;
            run(1 << bit, second, changed);
            run(0, second, ignored);
            std::string list;
            for (size_t at : changed)
                if (!idle.count(at))
                    list += " " + std::to_string(at);
            std::printf("button bit %2d (%4d): player state words changed:%s\n", bit, 1 << bit,
                        list.empty() ? " none" : list.c_str());
        }
    }
    if (player) {
        const auto o = game.entityVec(0, 272);
        std::printf("client 0 moved %.1f units across and %.1f up over the frames\n",
                    std::sqrt((o[0] - startPos[0]) * (o[0] - startPos[0]) + (o[1] - startPos[1]) * (o[1] - startPos[1])),
                    o[2] - startPos[2]);
    }
    if (player) {
        // What the game's movement code meets: its trace down from where
        // the player is, with a player's box and mask, ignoring the player.
        const auto o = game.entityVec(0, 272);
        const q3::Vec3f mins{-15, -15, -24}, maxs{15, 15, 32}, end{o[0], o[1], o[2] - 64};
        const int32_t mask = q3::contents::Solid | q3::contents::PlayerClip | q3::contents::Body;
        // And sideways: how far a player's box could go along each axis.
        for (int axis = 0; axis < 2; ++axis)
            for (float sign : {1.0f, -1.0f}) {
                q3::Vec3f to = o;
                to[size_t(axis)] += sign * 512;
                to[2] += 1; // just off the floor
                q3::Vec3f from = o;
                from[2] += 1;
                int32_t hit = 0;
                const q3::TraceResult t = game.traceMove(from, mins, maxs, to, 0, mask, hit);
                std::printf("trace %s%c from client 0: %.1f units free, hits %d%s\n", sign > 0 ? "+" : "-",
                            "xy"[axis], t.fraction * 512, hit, t.startSolid ? " (starts inside)" : "");
            }
        for (bool withEntities : {true, false}) {
            int32_t hit = 0;
            const q3::TraceResult t = game.traceMove(o, mins, maxs, end, 0, mask, hit, withEntities);
            std::printf("trace down from client 0 (%s): fraction %.3f, startsolid %d, allsolid %d, hits %d, lands at %.1f\n",
                        withEntities ? "world and entities" : "world only", t.fraction, t.startSolid, t.allSolid, hit,
                        t.endPos[2]);
            if (withEntities && hit >= 0 && hit < 1022) {
                const auto eo = game.entityVec(hit, 272), emin = game.entityVec(hit, 220), emax = game.entityVec(hit, 232);
                std::printf("  entity %d: origin %.1f %.1f %.1f, box %.1f %.1f %.1f to %.1f %.1f %.1f, contents %d, "
                            "brush model %d (model %d), owner %d\n",
                            hit, eo[0], eo[1], eo[2], emin[0], emin[1], emin[2], emax[0], emax[1], emax[2],
                            game.entityInt(hit, 244), game.entityInt(hit, 216), game.entityInt(hit, 160),
                            game.entityInt(hit, 296));
            }
        }
    }
    const auto& gd = game.gameData();
    std::printf("level time %d ms; entity array at %d: %d in use, %d bytes each; clients at %d, %d bytes each\n",
                game.levelTime(), gd.entities, gd.count, gd.entitySize, gd.clients, gd.clientSize);
    std::printf("%zu entities linked into the world, %zu configstrings set, %zu cvars\n", game.linked().size(),
                game.configstrings().size(), cvars.all().size());
    for (const auto& [index, text] : game.configstrings()) {
        std::string shown = text.size() > 100 ? text.substr(0, 100) + "..." : text;
        std::printf("  configstring %4d: %s\n", index, shown.c_str());
    }
    for (const std::string& c : game.consoleCommands())
        std::printf("console command: %s\n", c.c_str());
    if (!addBots.empty() || !game.botClients().empty()) {
        // The bots: where each started and is now, and the bot library's
        // calls the game made, with the first arguments of the first few.
        std::printf("bots: %zu\n", game.botClients().size());
        for (const int32_t c : game.botClients()) {
            const auto o = game.entityVec(c, 272);
            const auto s = botStart.count(c) ? botStart[c] : q3::Vec3f{};
            std::printf("  client %d: %s, at %.1f %.1f %.1f (was %.1f %.1f %.1f)\n", c,
                        game.linked().count(c) ? "linked" : "not linked", o[0], o[1], o[2], s[0], s[1], s[2]);
        }
        const auto& aas = game.botLibrary().aas();
        std::printf("aas: %s\n", aas.loaded() ? (std::to_string(aas.areaCount()) + " areas").c_str() : "not loaded");
        for (const auto& [number, samples] : game.botCallSamples()) {
            const auto counted = game.callCounts().find(number);
            std::printf("  bot call %d: %d calls", number, counted == game.callCounts().end() ? 0 : counted->second);
            for (const auto& args : samples) {
                std::printf(" (");
                for (size_t i = 0; i < args.size(); ++i)
                    std::printf("%s%d", i ? " " : "", args[i]);
                std::printf(")");
            }
            std::printf("\n");
        }
    }
    for (const auto& [client, text] : game.serverCommands()) {
        std::string shown = text.size() > 120 ? text.substr(0, 120) + "..." : text;
        while (!shown.empty() && shown.back() == '\n')
            shown.pop_back();
        std::printf("to client %d: %s\n", client, shown.c_str());
    }
    for (const auto& [client, reason] : game.dropped())
        std::printf("client %d dropped: %s\n", client, reason.c_str());

    // A check of the collision model on its own: a player's box dropped
    // from each spawn point (raised 9 units, as Quake III's game puts
    // players there, since spawn points often sit exactly a player's
    // height above the floor) should land a little below it, not start
    // in a wall.
    std::vector<q3::Entity> ents;
    if (!q3::parseEntities(bsp.entities, ents)) {
        const q3::CollisionWorld world(bsp);
        const q3::Vec3f mins{-15, -15, -24}, maxs{15, 15, 32};
        int spawns = 0, stuck = 0, falling = 0;
        float shortest = 1e9f, longest = 0;
        for (const q3::Entity& e : ents) {
            float o[3];
            if (e.classname().rfind("info_player", 0) != 0 || !e.vector("origin", o))
                continue;
            ++spawns;
            const q3::Vec3f start{o[0], o[1], o[2] + 9}, end{o[0], o[1], o[2] - 4096};
            const q3::TraceResult t =
                world.trace(start, end, mins, maxs, 0, q3::contents::Solid | q3::contents::PlayerClip);
            if (t.startSolid)
                ++stuck;
            else if (t.fraction == 1)
                ++falling;
            else {
                const float drop = start[2] - t.endPos[2];
                shortest = std::min(shortest, drop);
                longest = std::max(longest, drop);
            }
        }
        if (spawns)
            std::printf("collision check: %d spawn points; a player's box dropped from each lands %.1f to %.1f "
                        "units below; %d start inside something, %d fall out of the map\n",
                        spawns, spawns > stuck + falling ? shortest : 0.0f, longest, stuck, falling);
    }
    for (const auto& [number, count] : game.unhandled())
        std::printf("not implemented: system call %d (%d calls)\n", number, count);

    if (ok && runCgame) {
        // The player's client game, started with what the server has set.
        ef::CgameHost cgame(set, cvars);
        cgame.print = [](const std::string& text) {
            std::printf("cgame: %s%s", text.c_str(), text.empty() || text.back() != '\n' ? "\n" : "");
        };
        // The client game's calls, counted by frame phase: idle, then
        // (with -fire or -altfire) holding fire; the first arguments of each.
        int phase = 0; // 0 start-up, 1 idle frames, 2 firing frames
        std::array<std::map<int, int>, 3> phaseCalls;
        std::map<int, std::array<int32_t, 4>> firingArgs;
        std::map<std::string, int> soundsStarted; // -events: S_StartSound by name
        cgame.trace = [&](int number, const int32_t* a) {
            if (eventsTest && number == 28)
                ++soundsStarted[cgame.sounds().name(a[3])];
            ++phaseCalls[size_t(phase)][number];
            if (phase == 2 && !firingArgs.count(number))
                firingArgs[number] = {a[0], a[1], a[2], a[3]};
            if (traceCalls)
                std::printf("  cgame syscall %3d (%d, %d, %d, %d)\n", number, a[0], a[1], a[2], a[3]);
        };
        reluva::Error err = cgame.load();
        if (!err) {
            cgame.setConfigstrings(game.configstrings());
            err = cgame.init(0);
        }
        std::printf("cgame Init: %s after %llu instructions\n", err ? err.message.c_str() : "ok",
                    static_cast<unsigned long long>(cgame.vm().executed()));
        std::printf("cgame loaded %s for collision, %s to draw; music %s / %s\n", cgame.collisionMap().c_str(),
                    cgame.worldMap().c_str(), cgame.music().first.c_str(), cgame.music().second.c_str());
        std::printf("cgame registered %zu models, %zu skins, %zu shaders, %zu sounds; added %zu commands\n",
                    cgame.models().size(), cgame.skins().size(), cgame.shaders().size(), cgame.sounds().size(),
                    cgame.commands().size());
        if (!err && eventsTest) {
            // Fire held for three seconds at a time as the Play page runs it
            // (a command each frame, the server a frame ahead, a snapshot
            // each server frame, the client game drawing every frame), at
            // frame lengths from 7 ms to 80 ms: the events the server gave
            // the player (its event sequence, player state word 108) against
            // the sounds the client game started.
            // serverFirst: the server run up to the command's time before it's
            // given the command (as the original's client stamps commands
            // with the snapshots' time), not after.
            auto runFor = [&](int stepMs, bool serverFirst) {
                soundsStarted.clear();
                const int32_t firstEvent = game.clientInt(0, 108);
                const int32_t firstAmmo = game.clientInt(0, 396);
                const int32_t firstCommandTime = game.clientInt(0, 0);
                int32_t clientTime = game.levelTime();
                auto runServer = [&] {
                    while (game.levelTime() <= clientTime) {
                        if (reluva::Error e = game.runFrame()) {
                            std::printf("server: %s\n", e.message.c_str());
                            return false;
                        }
                        std::vector<std::string> commands;
                        for (const auto& [to, text] : game.serverCommands())
                            if (to == 0 || to == -1)
                                commands.push_back(text);
                        cgame.setServerCommands(commands);
                        cgame.setConfigstrings(game.configstrings());
                        cgame.newSnapshot(game.snapshot(0));
                    }
                    return true;
                };
                for (int t = 0; t < 3000 && ok; t += stepMs) {
                    clientTime += stepMs;
                    if (serverFirst && !runServer())
                        return;
                    ef::GameHost::UserCommand cmd;
                    cmd.serverTime = clientTime;
                    cmd.buttons = fireButtons ? fireButtons : 1;
                    cmd.weapon = uint8_t(std::max(fireWeapon, 1));
                    if (reluva::Error e = game.clientThink(0, cmd)) {
                        std::printf("server: %s\n", e.message.c_str());
                        return;
                    }
                    cgame.addUserCommand(cmd);
                    if (!serverFirst && !runServer())
                        return;
                    cgame.clearScene();
                    cgame.clearPictures();
                    int32_t ignored = 0;
                    if (reluva::Error e = cgame.call(ef::CgameHost::DrawActiveFrame, {clientTime, 0, 0}, ignored)) {
                        std::printf("cgame: %s\n", e.message.c_str());
                        return;
                    }
                }
                std::printf("a command every %d ms, %s: the server gave the player %d events (command time %d to %d, "
                            "ammunition word 396: %d to %d); the client game started these sounds:\n",
                            stepMs, serverFirst ? "the server run first" : "the command first",
                            game.clientInt(0, 108) - firstEvent, firstCommandTime, game.clientInt(0, 0), firstAmmo,
                            game.clientInt(0, 396));
                for (const auto& [name, count] : soundsStarted)
                    std::printf("  %4d  %s\n", count, name.c_str());
            };
            // From 144 frames a second down to 12, as a slow frame rate gives,
            // each both ways; 7 ms again at the end, as each run starts where
            // the last stopped.
            for (const auto& [stepMs, serverFirst] : std::vector<std::pair<int, bool>>{
                     {7, false}, {16, false}, {16, true}, {50, false}, {50, true}, {80, true}, {7, false}})
                runFor(stepMs, serverFirst);
        } else if (!err) {
            // A few frames, a new snapshot each, to see what the client game
            // does with them.
            const auto [at208, at212] = game.svFlagsEvidence();
            std::printf("svFlags: %d linked entities have flags at 208, %d at 212; taken to be at %d\n", at208, at212,
                        game.svFlagsOffset());
            // What the frames add besides models, for working out how the
            // client game draws effects: entities by type, polygons by
            // shader, lights; a sample of each.
            // The entities of the last idle frame and the last firing one,
            // to dump those firing adds.
            std::vector<std::vector<uint8_t>> lastIdleEntities, lastFiringEntities;
            // By the weapon fired (0 when not firing) and the type or shader.
            using Key = std::pair<int, int32_t>;
            std::map<Key, int> typeCounts;
            std::map<Key, std::vector<uint8_t>> typeSamples;
            std::map<Key, int> polyCounts;
            std::map<Key, ef::CgameHost::Scene::Poly> polySamples;
            std::map<int, int> lightCounts;
            // Models (handle, custom shader) seen while firing each weapon
            // that never showed when not firing: projectiles and the like.
            std::map<Key, std::set<std::pair<int32_t, int32_t>>> firedModels;
            std::set<std::pair<int32_t, int32_t>> idleModels;
            std::map<int, ef::CgameHost::Scene::Light> lightSamples;
            int weaponNow = 0;
            // With fire held, the first 10 frames are idle, to compare.
            constexpr int kIdleFrames = 10;
            int idleFrames = 0, firingFrames = 0;
            for (int f = 0; f < cgameFrames; ++f) {
                const bool verbose = f < 3 || f == cgameFrames - 1;
                const bool firing = fireButtons && f >= kIdleFrames;
                if (f) {
                    phase = firing ? 2 : 1;
                    (firing ? firingFrames : idleFrames)++;
                }
                ef::GameHost::UserCommand idle;
                if (f) {
                    idle.serverTime = game.levelTime() + ef::GameHost::kFrameMs;
                    weaponNow = !firing          ? 0
                                : fireWeapon < 0 ? 1 + std::min(9, (f - kIdleFrames) / kFramesPerWeapon)
                                                 : fireWeapon;
                    idle.weapon = uint8_t(fireButtons ? (weaponNow ? weaponNow : std::max(fireWeapon, 1)) : 0);
                    idle.buttons = firing ? fireButtons : 0;
                    if (fireButtons)
                        game.clientThink(0, idle);
                    game.runFrame();
                }
                if (fireButtons && (f == 0 || f == kIdleFrames)) {
                    // The player state's start, word by word: the client game
                    // won't draw the phaser's beam unless the word at 48 is 0.
                    std::printf("frame %2d: player state words 0-96:", f);
                    for (int at = 0; at <= 96; at += 4)
                        std::printf(" %d:%d", at, game.clientInt(0, at));
                    std::printf("\n");
                }
                if (fireButtons && f <= 30 && (f % 5 == 0 || f == kIdleFrames)) {
                    // The player state words firing changes (game-run -buttons):
                    // weapon time 44, flags 104, event sequence 108, events 112,
                    // 116, their parameters 120, 124, and 376, 396.
                    std::printf("frame %2d%s: player state 44 %d, 104 0x%x, 108 %d, 112 %d, 116 %d, 120 %d, 124 %d, "
                                "376 %d, 396 %d\n",
                                f, firing ? " (firing)" : "", game.clientInt(0, 44), unsigned(game.clientInt(0, 104)),
                                game.clientInt(0, 108), game.clientInt(0, 112), game.clientInt(0, 116),
                                game.clientInt(0, 120), game.clientInt(0, 124), game.clientInt(0, 376),
                                game.clientInt(0, 396));
                }
                std::vector<std::string> commands;
                for (const auto& [to, text] : game.serverCommands())
                    if (to == 0 || to == -1)
                        commands.push_back(text);
                cgame.setServerCommands(commands);
                idle.serverTime = game.levelTime();
                cgame.addUserCommand(idle);
                cgame.clearScene();
                auto snap = game.snapshot(0);
                if (verbose)
                    std::printf("snapshot at %d: %zu entities, %d server commands\n", snap.serverTime,
                                snap.entities.size(), snap.serverCommandSequence);
                cgame.newSnapshot(std::move(snap));
                int32_t ignored = 0;
                const reluva::Error frame =
                    cgame.call(ef::CgameHost::DrawActiveFrame, {game.levelTime(), 0, 0}, ignored);
                if (verbose || frame)
                    std::printf("cgame DrawActiveFrame(%d): %s after %llu instructions, %zu pictures\n", game.levelTime(),
                            frame ? frame.message.c_str() : "ok",
                            static_cast<unsigned long long>(cgame.vm().executed()), cgame.pictures().size());
                if (!watchModel.empty()) {
                    // Entities with the watched model: colour, scale (axis
                    // lengths), frames and where.
                    for (const auto& e : cgame.scene().entities) {
                        if (e.bytes.size() < 160)
                            continue;
                        auto word = [&](size_t at) {
                            int32_t v;
                            std::memcpy(&v, &e.bytes[at], 4);
                            return v;
                        };
                        auto real = [&](size_t at) { return q3::Qvm::asFloat(word(at)); };
                        const std::string name = cgame.models().name(word(8));
                        if (word(0) != 0 || name.find(watchModel) == std::string::npos)
                            continue;
                        float len[3];
                        for (int a = 0; a < 3; ++a)
                            len[a] = std::sqrt(real(28 + 12 * size_t(a)) * real(28 + 12 * size_t(a)) +
                                               real(32 + 12 * size_t(a)) * real(32 + 12 * size_t(a)) +
                                               real(36 + 12 * size_t(a)) * real(36 + 12 * size_t(a)));
                        std::printf("  time %d: %s colour %u %u %u %u, axes %.2f %.2f %.2f%s, frame %d (old %d, "
                                    "back %.2f), shader %d, shader time %.3f, flags 0x%x, at %.0f %.0f %.0f\n",
                                    game.levelTime(), name.c_str(), e.bytes[116], e.bytes[117], e.bytes[118],
                                    e.bytes[119],
                                    len[0], len[1], len[2], word(64) ? " (not normalized)" : "", word(80), word(96),
                                    real(100), word(112), real(128), unsigned(word(4)), real(68), real(72), real(76));
                    }
                }
                {
                    const auto& scene = cgame.scene();
                    auto& keep = firing ? lastFiringEntities : lastIdleEntities;
                    if (f) {
                        keep.clear();
                        for (const auto& e : scene.entities)
                            keep.push_back(e.bytes);
                    }
                    for (const auto& e : scene.entities) {
                        int32_t type = 0;
                        if (e.bytes.size() >= 4)
                            std::memcpy(&type, e.bytes.data(), 4);
                        if (type != 0 && typeCounts[{weaponNow, type}]++ == 0)
                            typeSamples[{weaponNow, type}] = e.bytes;
                        if (type == 0 && e.bytes.size() >= 116) {
                            int32_t model = 0, shader = 0;
                            std::memcpy(&model, &e.bytes[8], 4);
                            std::memcpy(&shader, &e.bytes[112], 4);
                            if (!weaponNow)
                                idleModels.insert({model, shader});
                            else if (!idleModels.count({model, shader}))
                                firedModels[{weaponNow, 0}].insert({model, shader});
                        }
                    }
                    for (const auto& poly : scene.polys)
                        if (polyCounts[{weaponNow, poly.shader}]++ == 0)
                            polySamples[{weaponNow, poly.shader}] = poly;
                    if (!scene.lights.empty() && !lightCounts[weaponNow])
                        lightSamples[weaponNow] = scene.lights[0];
                    lightCounts[weaponNow] += int(scene.lights.size());
                }
                if (verbose) {
                    // The frame's 3D scene: the entities added (and the sizes of
                    // the memsets that cleared them: refEntity_t's size), and the
                    // view, read as Quake III's refdef_t: x, y, width, height,
                    // fov x, fov y, view origin (24), axes (36), time (72).
                    const auto& scene = cgame.scene();
                    std::map<int32_t, int> sizes;
                    for (const auto& e : scene.entities)
                        ++sizes[e.clearedSize];
                    std::string sizeText;
                    for (const auto& [size, count] : sizes)
                        sizeText += " " + std::to_string(count) + " cleared by " + std::to_string(size);
                    std::printf("  scene: %zu entities (%s), %d polygons, %d lights, rendered %d times\n",
                                scene.entities.size(), sizeText.c_str(), int(scene.polys.size()), int(scene.lights.size()), scene.renders);
                    if (scene.view.size() >= 80) {
                        auto word = [&](int at) {
                            int32_t v;
                            std::memcpy(&v, &scene.view[size_t(at)], 4);
                            return v;
                        };
                        auto real = [&](int at) { return q3::Qvm::asFloat(word(at)); };
                        std::printf("  view: %d,%d %dx%d, fov %.1f x %.1f, eye %.1f %.1f %.1f, forward %.2f %.2f %.2f, time %d\n",
                                    word(0), word(4), word(8), word(12), real(16), real(20), real(24), real(28),
                                    real(32), real(36), real(40), real(44), word(72));
                    }
                }
                if (f == 0 && dumpEntities) {
                    // A few of the scene's entities word by word, for working
                    // out refEntity_t's layout: different models first. Each
                    // word as a whole number and as a float, and what it
                    // names if it's a registered model, skin or shader handle.
                    const auto& scene = cgame.scene();
                    std::vector<size_t> picked;
                    std::set<int32_t> seen;
                    for (int pass = 0; pass < 2; ++pass)
                        for (size_t i = 0; i < scene.entities.size() && picked.size() < size_t(dumpEntities); ++i) {
                            const auto& b = scene.entities[i].bytes;
                            if (b.size() < 160 || std::find(picked.begin(), picked.end(), i) != picked.end())
                                continue;
                            int32_t key = 0;
                            std::memcpy(&key, &b[8], 4);
                            if (pass == 0 && !seen.insert(key).second)
                                continue;
                            picked.push_back(i);
                        }
                    for (size_t i : picked) {
                        const auto& b = scene.entities[i].bytes;
                        std::printf("  scene entity %zu (cleared by %d):\n", i, scene.entities[i].clearedSize);
                        for (int at = 0; at < 160; at += 4) {
                            int32_t v;
                            std::memcpy(&v, &b[size_t(at)], 4);
                            const float fl = q3::Qvm::asFloat(v);
                            std::string names;
                            if (v > 0 && v < 4096) {
                                if (!cgame.models().name(v).empty())
                                    names += " model:" + cgame.models().name(v);
                                if (!cgame.skins().name(v).empty())
                                    names += " skin:" + cgame.skins().name(v);
                                if (!cgame.shaders().name(v).empty())
                                    names += " shader:" + cgame.shaders().name(v);
                            }
                            const bool showFloat = std::isfinite(fl) && std::fabs(fl) > 1e-6f && std::fabs(fl) < 1e7f;
                            char floatText[32] = "";
                            if (showFloat)
                                std::snprintf(floatText, sizeof floatText, " %.4g", fl);
                            std::printf("    %3d: %11d 0x%08x%s%s\n", at, v, unsigned(v), floatText, names.c_str());
                        }
                    }
                }
                if (f == 0) {
                    // What the frame drew in 2D, by shader.
                    std::map<std::string, int> byShader;
                    for (const auto& p : cgame.pictures())
                        ++byShader[cgame.shaders().name(p.shader)];
                    for (const auto& [name, count] : byShader)
                        std::printf("  %4d x %s\n", count, name.empty() ? "(none)" : name.c_str());
                }
                cgame.clearPictures();
                if (frame)
                    break;
            }
            if (fireButtons && idleFrames && firingFrames) {
                // Calls a frame, idle and firing: what changes when firing.
                std::printf("client game calls a frame, idle (%d frames) against firing (%d frames):\n", idleFrames,
                            firingFrames);
                std::set<int> numbers;
                for (int ph = 1; ph <= 2; ++ph)
                    for (const auto& [number, count] : phaseCalls[size_t(ph)])
                        numbers.insert(number);
                for (int number : numbers) {
                    const float idleRate = float(phaseCalls[1][number]) / float(idleFrames);
                    const float fireRate = float(phaseCalls[2][number]) / float(firingFrames);
                    const auto it = firingArgs.find(number);
                    std::printf("  call %3d: %7.2f idle, %7.2f firing%s", number, idleRate, fireRate,
                                std::fabs(idleRate - fireRate) > 0.05f ? "  <- differs" : "");
                    if (it != firingArgs.end())
                        std::printf("  (first while firing: %d, %d, %d, %d)", it->second[0], it->second[1],
                                    it->second[2], it->second[3]);
                    std::printf("\n");
                }
            }
            // The effects seen over the frames.
            auto dumpWords = [&](const std::vector<uint8_t>& b, size_t size) {
                for (size_t at = 0; at + 4 <= std::min(size, b.size()); at += 4) {
                    int32_t v;
                    std::memcpy(&v, &b[at], 4);
                    const float fl = q3::Qvm::asFloat(v);
                    std::string names;
                    if (v > 0 && v < 4096) {
                        if (!cgame.models().name(v).empty())
                            names += " model:" + cgame.models().name(v);
                        if (!cgame.shaders().name(v).empty())
                            names += " shader:" + cgame.shaders().name(v);
                    }
                    char floatText[32] = "";
                    if (std::isfinite(fl) && std::fabs(fl) > 1e-6f && std::fabs(fl) < 1e7f)
                        std::snprintf(floatText, sizeof floatText, " %.4g", fl);
                    std::printf("    %3zu: %11d 0x%08x%s%s\n", at, v, unsigned(v), floatText, names.c_str());
                }
            };
            if (fireButtons && !lastIdleEntities.empty() && !lastFiringEntities.empty()) {
                // Firing's entities: those of the last firing frame with a model
                // handle (byte 8) more often than in the last idle frame.
                std::map<int32_t, int> idleModels;
                for (const auto& b : lastIdleEntities) {
                    int32_t m = 0;
                    std::memcpy(&m, &b[8], 4);
                    ++idleModels[m];
                }
                int shown = 0;
                for (const auto& b : lastFiringEntities) {
                    int32_t m = 0;
                    std::memcpy(&m, &b[8], 4);
                    if (idleModels[m]-- > 0 || shown >= 4)
                        continue;
                    std::printf("an entity firing adds (model %d%s%s):\n", m, m > 0 ? " " : "",
                                m > 0 ? cgame.models().name(m).c_str() : "");
                    dumpWords(b, 160);
                    ++shown;
                }
            }
            // Types the Play page draws: models (0), quads (1), sprites (2),
            // lines (9, 11); samples of the others.
            auto drawn = [](int32_t type) { return type == 1 || type == 2 || type == 9 || type == 11; };
            std::set<int> weapons{0};
            for (const auto& [key, count] : typeCounts)
                weapons.insert(key.first);
            for (const auto& [key, count] : polyCounts)
                weapons.insert(key.first);
            for (const auto& [weapon, count] : lightCounts)
                weapons.insert(weapon);
            for (int weapon : weapons) {
                std::printf("over %d frames, %s: entities that aren't models, by type:", cgameFrames,
                            weapon ? ("firing weapon " + std::to_string(weapon)).c_str() : "not firing");
                for (const auto& [key, count] : typeCounts)
                    if (key.first == weapon)
                        std::printf(" %d (x%d%s)", key.second, count, drawn(key.second) ? "" : ", not drawn yet");
                std::printf("\n");
                for (const auto& [key, count] : typeCounts)
                    if (key.first == weapon && !drawn(key.second)) {
                        std::printf("  type %d, the first:\n", key.second);
                        dumpWords(typeSamples[key], 160);
                    }
                if (const auto it = firedModels.find({weapon, 0}); it != firedModels.end()) {
                    std::printf("  models only while firing:");
                    for (const auto& [model, shader] : it->second)
                        std::printf(" %s%s%s", model > 0 ? cgame.models().name(model).c_str() : "(no model)",
                                    shader > 0 ? " with " : "", shader > 0 ? cgame.shaders().name(shader).c_str() : "");
                    std::printf("\n");
                }
                for (const auto& [key, count] : polyCounts) {
                    if (key.first != weapon)
                        continue;
                    const auto& sample = polySamples[key];
                    std::printf("  polygons with %s (%d): %d, the first with %d vertices, its first two as 24 bytes "
                                "each:\n",
                                cgame.shaders().name(key.second).c_str(), key.second, count, sample.vertexCount);
                    dumpWords(sample.vertices, 48);
                }
                if (const int lights = lightCounts[weapon]) {
                    const auto& l = lightSamples[weapon];
                    std::printf("  lights: %d, the first at %.1f %.1f %.1f, radius %.1f, colour %.2f %.2f %.2f\n",
                                lights, l.origin[0], l.origin[1], l.origin[2], l.radius, l.color[0], l.color[1],
                                l.color[2]);
                }
            }
            const auto& requests = cgame.snapshotRequests();
            for (size_t i = 0; i < requests.size() && i < 6; ++i)
                std::printf("cgame asked for snapshot %d at %d%s\n", requests[i].first, requests[i].second,
                            i ? (" (" + std::to_string(requests[i].second - requests[i - 1].second) + " from the last)").c_str()
                              : "");
        }
        for (const auto& [number, count] : cgame.unhandled()) {
            const auto& a = cgame.firstUnhandled().at(number);
            std::printf("cgame: not implemented: system call %d (%d calls), first (%d, %d, %d, %d, %d, %d)\n", number,
                        count, a[0], a[1], a[2], a[3], a[4], a[5]);
            // Arguments that point at text, which often says what a call is.
            for (int i = 0; i < 6; ++i) {
                if (a[i] <= 0 || !cgame.vm().valid(a[i], 1))
                    continue;
                const std::string text = cgame.vm().string(a[i], 64);
                bool printable = text.size() >= 2;
                for (char c : text)
                    printable = printable && static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) < 127;
                if (printable)
                    std::printf("  argument %d is text: \"%s\"\n", i, text.c_str());
            }
        }
        ok = ok && !err;
    }
    return ok ? 0 : 1;
}

int cmdFileShow(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool file-show <game folder> <path>\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    Bytes data;
    if (reluva::Error err = set.read(argv[3], data)) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    std::fwrite(data.data(), 1, data.size(), stdout);
    if (!data.empty() && data.back() != '\n')
        std::printf("\n");
    return 0;
}

int cmdShaderShow(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool shader-show <game folder> <shader | model.md3>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> names;
    const std::string what = argv[3];
    const std::string whatExt = what.size() > 4 ? reluva::fs::toLower(what.substr(what.size() - 4)) : "";
    if (whatExt == ".md3" || whatExt == ".mdr") {
        // A model: each surface's shader.
        Bytes data;
        q3::Md3Model model;
        if (reluva::Error err = set.read(what, data); err || (err = q3::parseModel(data, model))) {
            std::fprintf(stderr, "%s: %s\n", what.c_str(), err.message.c_str());
            return 1;
        }
        for (const q3::Md3Surface& surface : model.surfaces) {
            const std::string shader = surface.shaders.empty() ? std::string() : surface.shaders.front();
            std::printf("surface %s: %d vertices, %zu triangles, shader %s\n", surface.name.c_str(),
                        surface.vertexCount, surface.indices.size() / 3, shader.empty() ? "(none)" : shader.c_str());
            if (!shader.empty() && std::find(names.begin(), names.end(), shader) == names.end())
                names.push_back(shader);
        }
        std::printf("%zu frames, %d tags each:", model.frames.size(), model.tagCount);
        for (int t = 0; t < model.tagCount; ++t)
            std::printf(" %s", model.tags[size_t(t)].name.c_str());
        std::printf("\n");
    } else {
        names.push_back(what);
    }
    // Each shader's text, as its script has it: the name, then the block
    // from its brace to the matching one.
    for (const std::string& name : names) {
        std::string base = reluva::fs::toLower(name);
        if (base.size() > 4 && (base.substr(base.size() - 4) == ".tga" || base.substr(base.size() - 4) == ".jpg"))
            base.resize(base.size() - 4);
        bool found = false;
        for (const std::string& file : set.list()) {
            const std::string key = reluva::normalizeArchiveName(file);
            if (key.rfind("scripts/", 0) != 0 || key.size() < 7 || key.compare(key.size() - 7, 7, ".shader") != 0)
                continue;
            Bytes data;
            if (set.read(file, data))
                continue;
            const std::string text(data.begin(), data.end());
            const std::string lower = reluva::fs::toLower(text);
            for (size_t at = lower.find(base); at != std::string::npos && !found; at = lower.find(base, at + 1)) {
                // A whole token at the start of a line, followed by a brace.
                const bool startsToken = at == 0 || static_cast<unsigned char>(lower[at - 1]) <= ' ';
                const size_t after = at + base.size();
                if (!startsToken || (after < lower.size() && static_cast<unsigned char>(lower[after]) > ' '))
                    continue;
                const size_t open = lower.find_first_not_of(" \t\r\n", after);
                if (open == std::string::npos || lower[open] != '{')
                    continue;
                int depth = 0;
                size_t end = open;
                for (; end < text.size(); ++end) {
                    if (text[end] == '{')
                        ++depth;
                    else if (text[end] == '}' && --depth == 0)
                        break;
                }
                std::printf("--- %s, in %s:\n%s\n", name.c_str(), file.c_str(),
                            text.substr(at, std::min(end + 1, text.size()) - at).c_str());
                found = true;
            }
            if (found)
                break;
        }
        if (!found)
            std::printf("--- %s: no script defines it (the image itself, drawn the default way)\n", name.c_str());
    }
    return 0;
}

int cmdPortalCheck(int argc, char** argv) {
    // Every map's portals and mirrors: the surfaces whose shader says
    // `portal`, and the entities that place their cameras (Quake III's
    // misc_portal_surface, aimed by its target, a misc_portal_camera).
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool portal-check <game folder>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    q3::ShaderLibrary shaders;
    shaders.load(set, nullptr);
    Bytes data;
    for (const std::string& name : set.list()) {
        const std::string key = reluva::normalizeArchiveName(name);
        if (key.rfind("maps/", 0) != 0 || key.size() < 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        q3::Bsp bsp;
        if (set.read(name, data) || q3::parseBsp(data, bsp))
            continue;
        std::map<std::string, int> portalSurfaces;
        for (const q3::BspSurface& s : bsp.surfaces)
            if (s.shader >= 0 && size_t(s.shader) < bsp.shaders.size())
                if (const q3::ShaderDef* def = shaders.find(bsp.shaders[size_t(s.shader)].name); def && def->portal)
                    ++portalSurfaces[def->name];
        std::vector<q3::Entity> entities;
        q3::parseEntities(bsp.entities, entities);
        std::vector<const q3::Entity*> portalEntities;
        for (const q3::Entity& e : entities)
            if (e.classname().rfind("misc_portal", 0) == 0)
                portalEntities.push_back(&e);
        if (portalSurfaces.empty() && portalEntities.empty())
            continue;
        std::printf("%s:\n", key.c_str());
        for (const auto& [shader, count] : portalSurfaces)
            std::printf("  %d surface%s with %s\n", count, count == 1 ? "" : "s", shader.c_str());
        for (const q3::Entity* e : portalEntities) {
            std::printf("  %s", e->classname().c_str());
            for (const auto& [k, v] : e->pairs)
                if (k != "classname")
                    std::printf(" %s \"%s\"", k.c_str(), v.c_str());
            std::printf("\n");
        }
    }
    return 0;
}

int cmdShaderCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool shader-check <game folder>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    std::vector<std::string> warnings;
    q3::ShaderLibrary shaders;
    shaders.load(set, &warnings);
    for (const std::string& w : warnings)
        std::printf("problem: %s\n", w.c_str());

    // Feature -> (count, a few shaders using it).
    std::map<std::string, std::pair<int, std::vector<std::string>>> found;
    auto add = [&](const std::string& what, const q3::ShaderDef& d) {
        auto& [n, examples] = found[what];
        ++n;
        if (examples.size() < 3 && std::find(examples.begin(), examples.end(), d.name) == examples.end())
            examples.push_back(d.name);
    };
    static const char* const deformNames[] = {"wave", "normal", "bulge", "move", "autosprite", "autosprite2",
                                              "text", "projectionShadow", "other"};
    for (const q3::ShaderDef& d : shaders.all()) {
        for (const std::string& u : d.unknown)
            add("unknown directive: " + u, d);
        for (const q3::ShaderDeform& df : d.deforms)
            if (df.type != q3::ShaderDeform::Type::Wave && df.type != q3::ShaderDeform::Type::Bulge &&
                df.type != q3::ShaderDeform::Type::Move && df.type != q3::ShaderDeform::Type::Normal)
                add(std::string("not drawn: deformVertexes ") + deformNames[size_t(df.type)], d);
        if (d.deforms.size() > 2)
            add("not drawn: more than two deformVertexes", d);
        if (d.portal)
            add("not drawn: portal (shown as a plain surface)", d);
        for (const q3::ShaderStage& st : d.stages)
            if (st.blendSrc == "gl_src_alpha_saturate")
                add("not drawn: blendFunc GL_SRC_ALPHA_SATURATE", d);
    }
    std::printf("%zu shaders\n", shaders.size());
    for (const auto& [what, info] : found) {
        std::printf("%5d  %s  (e.g.", info.first, what.c_str());
        for (const std::string& e : info.second)
            std::printf(" %s", e.c_str());
        std::printf(")\n");
    }
    return 0;
}

int cmdPk3List(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool pk3-list <game folder> [name filter]\n");
        return 2;
    }
    reluva::SearchSet set;
    if (!mountIdTech3(argv[2], set))
        return 1;
    const std::string filter = argc == 4 ? reluva::normalizeArchiveName(argv[3]) : std::string();
    for (const std::string& name : set.list()) {
        if (!filter.empty() && reluva::normalizeArchiveName(name).find(filter) == std::string::npos)
            continue;
        const reluva::Archive* a = set.find(name);
        long long size = -1;
        if (const auto* zip = dynamic_cast<const reluva::ZipArchive*>(a))
            if (const auto* e = zip->entry(name))
                size = e->size;
        std::printf("%10lld  %-60s %s\n", size, name.c_str(),
                    std::filesystem::path(a->describe()).filename().string().c_str());
    }
    return 0;
}

#endif // RELUVA_ENGINE_IDTECH3

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "hexdump") == 0)
        return cmdHexdump(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "chunks") == 0)
        return cmdChunks(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "words") == 0)
        return cmdWords(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "table") == 0)
        return cmdTable(argc, argv);
#ifdef RELUVA_ENGINE_NEWWORLDS
    if (argc >= 2 && std::strcmp(argv[1], "cat-probe") == 0)
        return cmdCatProbe(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "cat-list") == 0)
        return cmdCatList(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "cat-extract") == 0)
        return cmdCatExtract(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "stb-dump") == 0)
        return cmdStbDump(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "lbp-info") == 0)
        return cmdLbpInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "tex-survey") == 0)
        return cmdTexSurvey(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "tex-keys") == 0)
        return cmdTexKeys(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-info") == 0)
        return cmdGeoInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-survey") == 0)
        return cmdGeoSurvey(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gmf-info") == 0)
        return cmdGmfInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-textures") == 0)
        return cmdGeoTextures(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "mdl-info") == 0)
        return cmdMdlInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "wav-info") == 0)
        return cmdWavInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "wav-decode") == 0)
        return cmdWavDecode(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "fif-info") == 0)
        return cmdFifInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "level-info") == 0)
        return cmdLevelInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gmf-fit") == 0)
        return cmdGmfFit(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-slots") == 0)
        return cmdGeoSlots(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "ground-textures") == 0)
        return cmdGroundTextures(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "gmf-dump") == 0)
        return cmdGmfDump(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-sheet") == 0)
        return cmdGeoSheet(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "geo-faces") == 0)
        return cmdGeoFaces(argc, argv);
#endif
#ifdef RELUVA_ENGINE_IDTECH3
    if (argc >= 2 && std::strcmp(argv[1], "bsp-check") == 0)
        return cmdBspCheck(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "pk3-list") == 0)
        return cmdPk3List(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "image-check") == 0)
        return cmdImageCheck(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "shader-check") == 0)
        return cmdShaderCheck(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "portal-check") == 0)
        return cmdPortalCheck(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "shader-show") == 0)
        return cmdShaderShow(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "file-show") == 0)
        return cmdFileShow(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "model-check") == 0)
        return cmdModelCheck(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "qvm-info") == 0)
        return cmdQvmInfo(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "qvm-run") == 0)
        return cmdQvmRun(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "qvm-refs") == 0)
        return cmdQvmRefs(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "qvm-dis") == 0)
        return cmdQvmDis(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "game-run") == 0)
        return cmdGameRun(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "ui-run") == 0)
        return cmdUiRun(argc, argv);
#endif
    std::fprintf(stderr,
                 "usage:\n"
                 "  reluva-tool hexdump <file> [offset] [length]\n"
                 "  reluva-tool chunks <file>\n"
                 "  reluva-tool words <file> [offset] [count]\n"
                 "  reluva-tool table <file> [values per row] [offset]\n"
#ifdef RELUVA_ENGINE_NEWWORLDS
                 "  reluva-tool cat-probe <file.cat> [-q]\n"
                 "  reluva-tool cat-list <file.cat>\n"
                 "  reluva-tool cat-extract <file.cat> <output dir> [name filter]\n"
                 "  reluva-tool stb-dump <file.stb | file.cat>\n"
                 "  reluva-tool tex-survey <file.cat>...\n"
                 "  reluva-tool tex-keys <file.cat>...\n"
                 "  reluva-tool geo-info <file.geo> [out.obj]\n"
                 "  reluva-tool geo-survey <file.cat>...\n"
                 "  reluva-tool geo-textures [-a] <file.cat>...\n"
                 "  reluva-tool mdl-info <file.mdl | file.cat>\n"
                 "  reluva-tool wav-info <file.wav | file.mp3 | file.cat>\n"
                 "  reluva-tool wav-decode <in.wav | in.mp3> <out.wav>\n"
                 "  reluva-tool fif-info <file.fif | file.cat>\n"
                 "  reluva-tool level-info <level.dat> [file.cat...]\n"
                 "  reluva-tool gmf-fit [-m|-h|-l|-r|-j|-t|-a|-o file] <planet.cat> [map]\n"
                 "  reluva-tool geo-slots <file.geo>...\n"
                 "  reluva-tool ground-textures <planet.cat>\n"
                 "  reluva-tool gmf-dump <planet.cat> <output dir>\n"
                 "  reluva-tool geo-sheet [-l|-t] <out.ppm> <name filter> <file.cat>...\n"
                 "  reluva-tool gmf-info <file.gmf> [record number]\n"
                 "  reluva-tool geo-faces <file.geo>\n"
                 "  reluva-tool lbp-info <file> [output prefix]\n"
#endif
#ifdef RELUVA_ENGINE_IDTECH3
                 "  reluva-tool bsp-check <game folder, e.g. .../BaseEF>\n"
                 "  reluva-tool pk3-list <game folder> [name filter]\n"
                 "  reluva-tool image-check <game folder> [map name filter]\n"
                 "  reluva-tool shader-check <game folder>\n"
                 "  reluva-tool portal-check <game folder>\n"
                 "  reluva-tool shader-show <game folder> <shader | model.md3>\n"
                 "  reluva-tool file-show <game folder> <path>\n"
                 "  reluva-tool model-check <game folder> [name filter] [-v]\n"
                 "  reluva-tool qvm-info <game folder> <vm/x.qvm> [-s]\n"
                 "  reluva-tool qvm-run <game folder> <vm/x.qvm> [command [arg...]] [-limit N]\n"
                 "  reluva-tool qvm-refs <game folder> <vm/x.qvm> <address> <size>\n"
                 "  reluva-tool qvm-dis <game folder> <vm/x.qvm> <first instruction> [count] | -ref <address> [size] | -str <text> | -syscall <number> | -syscalls <first> <last> | -func <instruction> | -blockcopies | -fields <address>\n"
                 "  reluva-tool game-run <game folder> <map> [-frames N] [-noclient] [-walk [forward|back|left|right]] [-set name value] [-addbot 'name skill']... [-cgame [-events] [-entities N] [-cgameframes N] [-fire] [-altfire] [-weapon N|all] [-give] [-watch model]] [-buttons] [-trace]\n"
                 "  reluva-tool ui-run <game folder> [-frames N] [-menu M] [-size WxH] [-glwords N] [-trace] [-v]\n"
#endif
                 "");
    return 2;
}
