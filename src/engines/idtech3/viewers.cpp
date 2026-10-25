// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's shared drawing, and its Files and Textures pages.

#include "engines/idtech3/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace reluva::idtech3 {

namespace {

bool hasExtension(const std::string& name, std::initializer_list<const char*> exts) {
    const std::string key = normalizeArchiveName(name);
    for (const char* e : exts) {
        const std::string ext = e;
        if (key.size() > ext.size() && key.compare(key.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

} // namespace

// --- Page ------------------------------------------------------------------------------

void Page::clear(uint8_t r, uint8_t g, uint8_t b) { fill(0, 0, ctx_.screen.w, ctx_.screen.h, r, g, b); }

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface& s = ctx_.screen;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t* p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string& s, Color c) { font::drawText(ctx_.screen, x, y, s, c); }

void Page::setTitle(const std::string& detail) {
    ctx_.sys.setWindowTitle(std::string("Reluva - ") + ctx_.game.name + " - " + name() + ": " + detail);
}

// --- FileViewer -------------------------------------------------------------------------

FileViewer::FileViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const Archive* a = ctx.files.find(n);
        files_.push_back({n, a ? std::filesystem::path(a->describe()).filename().string() : std::string()});
    }
}

void FileViewer::activate() {
    setTitle(std::to_string(files_.size()) + " files in " + std::to_string(ctx_.files.size()) + " archives");
    draw();
}

void FileViewer::handleEvent(const Event& ev) {
    const int rows = (ctx_.screen.h - 4 * font::kLineHeight) / font::kLineHeight;
    if (ev.type == EventType::MouseWheel)
        top_ -= ev.wheelY * 3;
    else if (ev.type != EventType::KeyDown)
        return;
    else if (ev.key == key::Up)
        --top_;
    else if (ev.key == key::Down)
        ++top_;
    else if (ev.key == key::PageUp)
        top_ -= rows;
    else if (ev.key == key::PageDown)
        top_ += rows;
    else if (ev.key == key::Home)
        top_ = 0;
    else if (ev.key == key::End)
        top_ = static_cast<int>(files_.size());
    else
        return;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(files_.size()) - rows));
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    text(4, 4, "Search path, first match wins:", {150, 200, 255});
    std::string order;
    for (const Archive* a : ctx_.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4 + lh, order, {150, 200, 255});
    if (!ctx_.warnings.empty())
        text(4, 4 + 2 * lh,
             std::to_string(ctx_.warnings.size()) + " problems, the first: " + ctx_.warnings.front(),
             {255, 120, 120});
    const int rows = (ctx_.screen.h - 4 * lh) / lh;
    const int sourceX = ctx_.screen.w - 20 * font::kAdvance;
    for (int i = 0; i < rows && top_ + i < static_cast<int>(files_.size()); ++i) {
        const Row& r = files_[size_t(top_ + i)];
        const int y = 4 * lh + i * lh;
        text(4, y, r.name);
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

// --- TextureViewer ----------------------------------------------------------------------

TextureViewer::TextureViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list())
        if (hasExtension(n, {".tga", ".jpg", ".jpeg", ".png"}))
            names_.push_back(n);
}

void TextureViewer::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || names_.empty())
        return;
    int i = index_;
    if (ev.key == key::Left)
        --i;
    else if (ev.key == key::Right)
        ++i;
    else if (ev.key == key::PageUp)
        i -= 10;
    else if (ev.key == key::PageDown)
        i += 10;
    else if (ev.key == key::Home)
        i = 0;
    else if (ev.key == key::End)
        i = static_cast<int>(names_.size()) - 1;
    else
        return;
    show(std::clamp(i, 0, static_cast<int>(names_.size()) - 1));
}

void TextureViewer::show(int index) {
    index_ = index;
    Surface& s = ctx_.screen;
    // Checkerboard, so transparency shows.
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            uint8_t* p = s.row(y) + x * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index)];
    const std::string where = std::to_string(index + 1) + "/" + std::to_string(names_.size());
    setTitle(name + " (" + where + ")");

    const int lh = font::kLineHeight;
    std::vector<uint8_t> data;
    image::Image img;
    std::string info;
    if (Error err = ctx_.files.read(name, data)) {
        info = err.message;
    } else if (Error derr = image::decode(data, img)) {
        info = derr.message;
    } else {
        // Fit the space under the caption: whole multiples up to 4x when
        // small, nearest-neighbour shrinking when large.
        const int areaW = s.w, areaH = s.h - 3 * lh;
        float scale = std::min(float(areaW) / float(img.w), float(areaH) / float(img.h));
        scale = scale >= 1 ? std::min(4.0f, float(int(scale))) : scale;
        image::Image shown;
        shown.w = std::max(1, int(float(img.w) * scale));
        shown.h = std::max(1, int(float(img.h) * scale));
        shown.rgba.resize(size_t(shown.w) * shown.h * 4);
        for (int y = 0; y < shown.h; ++y)
            for (int x = 0; x < shown.w; ++x) {
                const int sx = std::min(img.w - 1, int(float(x) / scale));
                const int sy = std::min(img.h - 1, int(float(y) / scale));
                std::copy_n(img.pixel(sx, sy), 4, shown.pixel(x, y));
            }
        image::blit(s, shown, (areaW - shown.w) / 2, 3 * lh + (areaH - shown.h) / 2);
        info = std::to_string(img.w) + "x" + std::to_string(img.h);
        if (scale != 1)
            info += " shown at " + std::to_string(int(scale * 100 + 0.5f)) + "%";
    }
    fill(0, 0, s.w, 3 * lh, 16, 18, 24);
    text(4, 4, name + "  " + where);
    const Archive* from = ctx_.files.find(name);
    text(4, 4 + lh, info + (from ? "  from " + std::filesystem::path(from->describe()).filename().string() : ""),
         {150, 200, 255});
}

} // namespace reluva::idtech3

namespace reluva::idtech3 {

namespace {
constexpr const char* kConfigName = "reluva.cfg";
}

void loadConfig(ViewerContext& ctx) {
    auto readFile = [&](const std::string& name, std::string& out) {
        // The player's own files first, then the game's.
        if (!ctx.savePath.empty()) {
            std::ifstream in(ctx.savePath / name, std::ios::binary);
            if (in) {
                out.assign(std::istreambuf_iterator<char>(in), {});
                return true;
            }
        }
        std::vector<uint8_t> data;
        if (ctx.files.read(name, data))
            return false;
        out.assign(data.begin(), data.end());
        return true;
    };
    // The engine's own settings, which its config file keeps (Archive)
    // like the game code's: the menus show and change them.
    static const char* const kEngineCvars[][2] = {
        // Video.
        {"r_mode", "5"}, {"r_fullscreen", "0"}, {"r_customwidth", "1600"}, {"r_customheight", "1024"},
        {"r_picmip", "0"}, {"r_texturemode", "GL_LINEAR_MIPMAP_NEAREST"}, {"r_vertexLight", "0"},
        {"r_subdivisions", "4"}, {"r_lodbias", "0"}, {"r_fastsky", "0"}, {"r_gamma", "1"},
        {"r_dynamiclight", "1"}, {"r_ext_texture_filter_anisotropic", "0"}, {"r_lowEndVideo", "0"},
        {"r_glDriver", "opengl32"}, {"r_allowExtensions", "1"}, {"r_colorbits", "32"}, {"r_texturebits", "32"},
        {"r_ext_compress_textures", "0"},
        // Input.
        {"sensitivity", "5"}, {"m_pitch", "0.022"}, {"m_yaw", "0.022"}, {"m_filter", "0"},
        {"cl_freelook", "1"}, {"cl_run", "1"}, {"cl_yawspeed", "140"}, {"cl_pitchspeed", "140"},
        {"cl_anglespeedkey", "1.5"},
        // Sound.
        {"s_volume", "0.8"}, {"s_musicvolume", "0.25"},
    };
    for (const auto& [name, value] : kEngineCvars)
        ctx.cvars.create(name, value, CvarStore::Archive);
    std::string text;
    std::vector<uint8_t> data;
    if (!ctx.files.read("default.cfg", data))
        ctx.bindings.execute(std::string(data.begin(), data.end()), ctx.cvars, nullptr, readFile);
    if (!ctx.savePath.empty()) {
        std::ifstream in(ctx.savePath / kConfigName, std::ios::binary);
        if (in) {
            text.assign(std::istreambuf_iterator<char>(in), {});
            ctx.bindings.execute(text, ctx.cvars, nullptr, readFile);
        }
    }
    if (ctx.bindings.empty()) {
        // No default.cfg: the usual keys, so there's something to play with.
        ctx.bindings.execute("bind w +forward; bind s +back; bind a +moveleft; bind d +moveright;"
                             "bind SPACE +moveup; bind c +movedown; bind MOUSE1 +attack; bind MOUSE2 +altattack;"
                             "bind MWHEELUP weapprev; bind MWHEELDOWN weapnext; bind SHIFT +speed;"
                             "bind 1 \"weapon 1\"; bind 2 \"weapon 2\"; bind 3 \"weapon 3\"; bind 4 \"weapon 4\";"
                             "bind 5 \"weapon 5\"; bind 6 \"weapon 6\"; bind 7 \"weapon 7\"; bind 8 \"weapon 8\";"
                             "bind 9 \"weapon 9\"",
                             ctx.cvars);
    }
    ctx.bindings.changed = false;
}

float brightness(const ViewerContext& ctx) {
    const CvarStore::Cvar* gamma = ctx.cvars.find("r_gamma");
    return gamma ? std::clamp(ctx.cvars.value("r_gamma"), 0.5f, 3.0f) : 1.0f;
}

VideoSettings videoSettings(const CvarStore& c) {
    VideoSettings v;
    auto integer = [&](const char* name, int fallback) { return c.find(name) ? c.integer(name) : fallback; };
    // r_mode: Quake III's list of sizes; -1 the custom size.
    static const int kModes[][2] = {{320, 240},  {400, 300},   {512, 384},  {640, 480},
                                    {800, 600},  {960, 720},   {1024, 768}, {1152, 864},
                                    {1280, 1024}, {1600, 1200}, {2048, 1536}, {856, 480}};
    if (c.find("r_mode")) {
        const int mode = c.integer("r_mode");
        if (mode == -1) {
            v.width = std::clamp(integer("r_customwidth", 1600), 320, 7680);
            v.height = std::clamp(integer("r_customheight", 1024), 240, 4320);
        } else if (mode >= 0 && mode < int(std::size(kModes))) {
            v.width = kModes[mode][0];
            v.height = kModes[mode][1];
        }
    }
    v.fullscreen = integer("r_fullscreen", 0) != 0;
    v.picmip = std::clamp(integer("r_picmip", 0), 0, 4);
    if (c.find("r_texturemode")) {
        std::string mode = c.string("r_texturemode");
        for (char& ch : mode)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        // GL_<filter>[_MIPMAP_<between levels>]
        v.nearest = mode.rfind("GL_NEAREST", 0) == 0;
        v.mipmaps = mode.find("MIPMAP") != std::string::npos;
        v.mipNearest = mode.size() >= 8 && mode.compare(mode.size() - 8, 8, "_NEAREST") == 0;
    }
    v.anisotropic = integer("r_ext_texture_filter_anisotropic", 0) != 0;
    v.vertexLight = integer("r_vertexLight", 0) != 0;
    if (c.find("r_subdivisions"))
        v.subdivisions = std::clamp(c.value("r_subdivisions"), 1.0f, 80.0f);
    v.lodBias = std::clamp(integer("r_lodBias", 0), 0, 2);
    v.fastSky = integer("r_fastsky", 0) != 0;
    return v;
}

void applyVideoSettings(const VideoSettings& v, ShaderCache& shaders, WorldRenderer* world, ModelCache* models) {
    shaders.setTextureOptions({v.picmip, v.nearest, v.mipmaps, v.mipNearest, v.anisotropic});
    if (world)
        world->setOptions(v.vertexLight, v.subdivisions, v.fastSky);
    if (models)
        models->setLodBias(v.lodBias);
}

void saveConfig(ViewerContext& ctx) {
    if (ctx.savePath.empty())
        return;
    std::string text = "// Reluva's settings for this game: key bindings and kept settings\n";
    text += ctx.bindings.write();
    for (const CvarStore::Cvar& cv : ctx.cvars.all())
        if (cv.flags & CvarStore::Archive)
            text += "seta " + cv.name + " \"" + cv.value + "\"\n";
    std::error_code ec;
    std::filesystem::create_directories(ctx.savePath, ec);
    std::ofstream out(ctx.savePath / kConfigName, std::ios::binary);
    out << text;
    ctx.bindings.changed = false;
}

std::vector<uint32_t> decodeUtf8(const char* s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; s[i];) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0) {
            ++i;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3f >> extra);
        size_t j = 1;
        for (; j <= size_t(extra) && (static_cast<unsigned char>(s[i + j]) >> 6) == 2; ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3f);
        if (j == size_t(extra) + 1)
            out.push_back(cp);
        i += j;
    }
    return out;
}

} // namespace reluva::idtech3
