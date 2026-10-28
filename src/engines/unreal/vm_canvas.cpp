// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The HUD's canvas, for a running level (ScriptLevel): a Canvas object
// handed to the player's PreRender and PostRender each frame, as the
// engine does, and its natives (DrawTile, DrawText, StrLen...) kept as a
// list of tiles (a texture's rectangle at a place on the screen, with a
// colour and a style) for the page to draw over the world. Text is tiles
// too, one a letter, from its font's pages (font.h). See
// docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/font.h"
#include "engines/unreal/procedural.h"

#include "common/text.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

using Vec = ScriptLevel::Vec;

// Actor's ERenderStyle: 0 draws nothing (the rest: actors.h).
constexpr uint8_t kStyleNone = 0;

} // namespace

const Font* ScriptLevel::fontOf(VmObject* font) {
    if (!font || !font->package || font->exportIndex < 0)
        return nullptr;
    const auto key = std::make_pair(font->package, font->exportIndex);
    auto it = fonts_.find(key);
    if (it == fonts_.end()) {
        auto f = std::make_shared<Font>();
        if (readFont(vm_.packages(), *font->package, font->exportIndex, *f))
            f.reset();
        it = fonts_.emplace(key, std::move(f)).first;
    }
    return it->second.get();
}

VmObject* ScriptLevel::canvas() {
    if (!canvas_ || canvas_->deleted) {
        VmClass* cls = vm_.findClass("Canvas");
        canvas_ = cls ? vm_.create(cls) : nullptr;
    }
    return canvas_;
}

// A frame's HUD: the canvas made ready (Reset, its size and clip the
// screen's), then the player's PreRender and PostRender with it, which
// hand it to its HUD, then the console's PostRender. What the scripts draw
// is in hud().
void ScriptLevel::renderHud(const HudView& view) {
    hud_ = {};
    hudView_ = hudFrame_ = view;
    hudFrameClear_ = false;
    VmObject* c = canvas();
    if (!c || !player_ || player_->deleted)
        return;
    vm_.callEvent(c, "Reset");
    auto setFloat = [&](const char* name, float v) {
        if (Value* p = prop(c, name))
            *p = Value::floatValue(v);
    };
    auto setInt = [&](const char* name, int v) {
        if (Value* p = prop(c, name))
            *p = Value::intValue(v);
    };
    setInt("SizeX", view.width);
    setInt("SizeY", view.height);
    setFloat("ClipX", float(view.width));
    setFloat("ClipY", float(view.height));
    setFloat("OrgX", 0);
    setFloat("OrgY", 0);
    setFloat("CurX", 0);
    setFloat("CurY", 0);
    vm_.callEvent(player_, "PreRender", {Value::objectValue(c)});
    vm_.callEvent(player_, "PostRender", {Value::objectValue(c)});
    // Then the console's (its messages; the UWindow menus while they're
    // open), on the same canvas, its frame the screen.
    if (VmObject* con = console()) {
        for (const auto& [name, size] : {std::pair{"FrameX", view.width}, std::pair{"FrameY", view.height}})
            if (Value* p = prop(con, name))
                *p = Value::floatValue(float(size));
        vm_.callEvent(c, "Reset");
        setFloat("ClipX", float(view.width));
        setFloat("ClipY", float(view.height));
        setFloat("OrgX", 0);
        setFloat("OrgY", 0);
        hudFrame_ = view;
        vm_.callEvent(con, "PostRender", {Value::objectValue(c)});
    }
}

void ScriptLevel::trackFli(VmObject* tex) {
    if (!tex || flis_.count(tex) || !tex->package)
        return;
    Fli f;
    if (auto anim = openAnimatedTexture(vm_.packages(), *tex->package, tex->exportIndex)) {
        f.frames = anim->frames();
        f.frameSeconds = anim->frameSeconds() > 0 ? anim->frameSeconds() : f.frameSeconds;
    }
    flis_[tex] = f;
}

// An FliTexture played as the HUD has it (SetFlick, SetFlickSpeed): Mode
// stopped (0) leaves it on its frame, once (1) plays to its end and stops
// there, back on the first frame (the last turns into the first), looping
// (2) goes round; a frame shows 1/30 s (its length over its frames) at
// Rate 1, over Rate.
void ScriptLevel::tickFlis(float seconds) {
    for (auto& [tex, f] : flis_) {
        Value* mode = prop(tex, "Mode");
        if (!mode || mode->asInt() == 0 || f.frames <= 0)
            continue;
        Value* time = prop(tex, "FrameTime");
        Value* frame = prop(tex, "FrameNum");
        const Value* rate = prop(tex, "Rate");
        if (!time || !frame)
            continue;
        float t = time->asFloat() + seconds * (rate ? rate->asFloat() : 1);
        int n = std::max(1, frame->asInt());
        while (t >= f.frameSeconds) {
            t -= f.frameSeconds;
            if (++n > f.frames) {
                n = 1;
                if (mode->asInt() == 1) {
                    *mode = Value::byteValue(0);
                    t = 0;
                    break;
                }
            }
        }
        *time = Value::floatValue(t);
        *frame = Value::intValue(n);
    }
}

namespace {

// The canvas's state as the natives use it.
struct Pen {
    float orgX = 0, orgY = 0, clipX = 0, clipY = 0, curX = 0, curY = 0, spaceX = 0, spaceY = 0, curYL = 0;
    std::array<uint8_t, 3> color{255, 255, 255};
    uint8_t style = kStyleNormal;
    bool center = false;
    VmObject* font = nullptr;
};

std::vector<uint32_t> codePoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();)
        out.push_back(text::nextUtf8(s, i));
    return out;
}

} // namespace

void registerCanvasNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto pen = [](NativeCall& c) {
        Pen p;
        VmObject* o = c.self;
        auto f = [&](const char* name) {
            const Value* v = o->find(c.vm, name);
            return v ? v->asFloat() : 0.0f;
        };
        p.orgX = f("OrgX");
        p.orgY = f("OrgY");
        p.clipX = f("ClipX");
        p.clipY = f("ClipY");
        p.curX = f("CurX");
        p.curY = f("CurY");
        p.spaceX = f("SpaceX");
        p.spaceY = f("SpaceY");
        p.curYL = f("CurYL");
        if (const Value* v = o->find(c.vm, "Style"))
            p.style = uint8_t(v->asInt());
        if (const Value* v = o->find(c.vm, "bCenter"))
            p.center = v->asBool();
        if (const Value* v = o->find(c.vm, "Font"))
            p.font = v->asObject();
        if (const Value* v = o->find(c.vm, "DrawColor"); v && v->fields.size() >= 3)
            p.color = {uint8_t(v->fields[0].asInt()), uint8_t(v->fields[1].asInt()), uint8_t(v->fields[2].asInt())};
        return p;
    };
    auto store = [](NativeCall& c, const Pen& p) {
        auto set = [&](const char* name, float v) {
            if (Value* x = c.self->find(c.vm, name))
                *x = Value::floatValue(v);
        };
        set("CurX", p.curX);
        set("CurY", p.curY);
        set("CurYL", p.curYL);
    };
    // A tile: `tex`'s rectangle (u, v, ul, vl in texels) drawn xl by yl at
    // (x, y) in the render frame (SetRenderFrame: from its corner, cut to
    // it); with `clip`, cut to the canvas's clip (from its origin) too, the
    // texture's rectangle cut with it.
    auto tile = [](NativeCall& c, const Pen& p, VmObject* tex, float x, float y, float xl, float yl, float u, float v,
                   float ul, float vl, bool clip, bool text) {
        if (!c.vm.level || !tex || !tex->package || p.style == kStyleNone || xl <= 0 || yl <= 0)
            return;
        auto cut = [](float& at, float& size, float& tu, float& tul, float lo, float hi) {
            if (at < lo) {
                const float d = lo - at;
                tu += tul * d / size;
                tul -= tul * d / size;
                size -= d;
                at = lo;
            }
            if (at + size > hi) {
                const float d = at + size - hi;
                tul -= tul * d / size;
                size -= d;
            }
        };
        if (clip) {
            const float right = p.orgX + p.clipX, bottom = p.orgY + p.clipY;
            if (x >= right || y >= bottom || x + xl <= p.orgX || y + yl <= p.orgY)
                return;
            cut(x, xl, u, ul, p.orgX, right);
            cut(y, yl, v, vl, p.orgY, bottom);
        }
        const ScriptLevel::HudView& frame = c.vm.level->hudFrame();
        if (x >= float(frame.width) || y >= float(frame.height) || x + xl <= 0 || y + yl <= 0)
            return;
        cut(x, xl, u, ul, 0, float(frame.width));
        cut(y, yl, v, vl, 0, float(frame.height));
        x += float(frame.x);
        y += float(frame.y);
        ScriptLevel::HudTile t;
        t.texture = {tex->package, tex->exportIndex};
        t.x = x;
        t.y = y;
        t.w = xl;
        t.h = yl;
        t.u = u;
        t.v = v;
        t.ul = ul;
        t.vl = vl;
        t.color = p.color;
        t.style = p.style;
        t.seeThrough = text || p.style == kStyleMasked;
        if (c.vm.isA(tex, "FliTexture")) {
            c.vm.level->trackFli(tex);
            const Value* n = tex->find(c.vm, "FrameNum");
            t.frame = std::max(1, n ? n->asInt() : 1);
        }
        c.vm.level->hud().tiles.push_back(t);
    };
    // A line of text's size in its font (each letter's width and SpaceX;
    // the tallest letter and SpaceY), times `scale`.
    auto measure = [](NativeCall& c, const Pen& p, const std::vector<uint32_t>& text, float scale, float& w, float& h) {
        w = h = 0;
        const Font* font = c.vm.level ? c.vm.level->fontOf(p.font) : nullptr;
        if (!font)
            return;
        for (const uint32_t ch : text)
            if (const FontCharacter* fc = font->character(ch)) {
                w += (float(fc->width) + p.spaceX) * scale;
                h = std::max(h, (float(fc->height) + p.spaceY) * scale);
            }
        if (h == 0)
            if (const FontCharacter* fc = font->character('A'))
                h = (float(fc->height) + p.spaceY) * scale;
    };
    auto drawLine = [=](NativeCall& c, const Pen& p, const std::vector<uint32_t>& text, float x, float y, float scale,
                        bool clip) {
        const Font* font = c.vm.level ? c.vm.level->fontOf(p.font) : nullptr;
        if (!font)
            return;
        for (const uint32_t ch : text) {
            const FontPage* page = nullptr;
            const FontCharacter* fc = font->character(ch, &page);
            if (!fc)
                continue;
            if (fc->width > 0 && fc->height > 0) {
                const ObjectRef& ref = page->texture;
                VmObject* tex = ref ? c.vm.objectAt(*ref.package, ref.exportIndex + 1) : nullptr;
                tile(c, p, tex, x, y, float(fc->width) * scale, float(fc->height) * scale, float(fc->u), float(fc->v),
                     float(fc->width), float(fc->height), clip, true);
            }
            x += (float(fc->width) + p.spaceX) * scale;
        }
    };
    // Lines as wide as fit from the pen to the clip's right, broken between
    // words (DrawText and StrLen wrap so).
    auto wrapLines = [=](NativeCall& c, const Pen& p, const std::string& s, float scale) {
        std::vector<std::vector<uint32_t>> lines;
        const float room = std::max(1.0f, p.clipX - p.curX);
        std::vector<uint32_t> line, word;
        auto widthOf = [&](const std::vector<uint32_t>& t) {
            float w = 0, h = 0;
            measure(c, p, t, scale, w, h);
            return w;
        };
        auto flushWord = [&] {
            std::vector<uint32_t> tried = line;
            if (!tried.empty())
                tried.push_back(' ');
            tried.insert(tried.end(), word.begin(), word.end());
            if (!line.empty() && widthOf(tried) > room) {
                lines.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            word.clear();
        };
        for (const uint32_t ch : codePoints(s)) {
            if (ch == '\n') {
                flushWord();
                lines.push_back(line);
                line.clear();
            } else if (ch == ' ') {
                flushWord();
            } else {
                word.push_back(ch);
            }
        }
        flushWord();
        if (!line.empty() || lines.empty())
            lines.push_back(line);
        return lines;
    };
    // DrawText(coerce string Text, optional bool CR) and DrawScaledText(Text,
    // float Scale): at the pen, wrapped to the clip, each line starting
    // under the first (or centred in the clip with bCenter); the pen on past
    // it (CR: to the next line's start).
    auto drawText = [=](NativeCall& c, const std::string& s, float scale, bool cr) {
        Pen p = pen(c);
        float lineW = 0, lineH = 0;
        const auto lines = wrapLines(c, p, s, scale);
        float y = p.orgY + p.curY;
        for (size_t i = 0; i < lines.size(); ++i) {
            measure(c, p, lines[i], scale, lineW, lineH);
            const float x = p.center ? p.orgX + (p.clipX - lineW) / 2 : p.orgX + p.curX;
            drawLine(c, p, lines[i], x, y, scale, false);
            if (i + 1 < lines.size())
                y += lineH;
        }
        p.curX += lineW;
        p.curY = y - p.orgY;
        p.curYL = std::max(p.curYL, lineH);
        if (cr) {
            p.curX = 0;
            p.curY += p.curYL;
            p.curYL = 0;
        }
        store(c, p);
    };
    reg("Canvas.DrawText", [=](NativeCall& c) {
        drawText(c, c.vm.toString(c.args[0]), 1, c.given.size() > 1 && c.given[1] ? c.b(1) : true);
    });
    reg("Canvas.DrawScaledText", [=](NativeCall& c) { drawText(c, c.vm.toString(c.args[0]), c.f(1), true); });
    // DrawTextClipped(coerce string Text, optional bool bCheckHotKey): one
    // line at the pen, cut to the clip ('&' marking a menu's hot key left out).
    reg("Canvas.DrawTextClipped", [=](NativeCall& c) {
        std::string s = c.vm.toString(c.args[0]);
        if (c.given.size() > 1 && c.given[1] && c.b(1))
            s.erase(std::remove(s.begin(), s.end(), '&'), s.end());
        const Pen p = pen(c);
        drawLine(c, p, codePoints(s), p.orgX + p.curX, p.orgY + p.curY, 1, true);
    });
    // StrLen(coerce string String, out float XL, out float YL): its size
    // wrapped as DrawText would; TextSize the same on one line.
    reg("Canvas.StrLen", [=](NativeCall& c) {
        const Pen p = pen(c);
        float w = 0, h = 0;
        for (const auto& line : wrapLines(c, p, c.vm.toString(c.args[0]), 1)) {
            float lw = 0, lh = 0;
            measure(c, p, line, 1, lw, lh);
            w = std::max(w, lw);
            h += lh;
        }
        c.args[1] = Value::floatValue(w);
        c.args[2] = Value::floatValue(h);
    });
    reg("Canvas.TextSize", [=](NativeCall& c) {
        const Pen p = pen(c);
        float w = 0, h = 0;
        measure(c, p, codePoints(c.vm.toString(c.args[0])), 1, w, h);
        c.args[1] = Value::floatValue(w);
        c.args[2] = Value::floatValue(h);
    });
    // DrawTile(Texture Tex, float XL, float YL, float U, float V, float UL, float VL):
    // at the pen; the pen on by its width. DrawTileClipped cut to the clip.
    for (const bool clip : {false, true})
        reg(clip ? "Canvas.DrawTileClipped" : "Canvas.DrawTile", [=](NativeCall& c) {
            Pen p = pen(c);
            tile(c, p, c.o(0), p.orgX + p.curX, p.orgY + p.curY, c.f(1), c.f(2), c.f(3), c.f(4), c.f(5), c.f(6), clip,
                 false);
            p.curX += c.f(1);
            p.curYL = std::max(p.curYL, c.f(2));
            store(c, p);
        });
    // DrawActor(Actor A, bool WireFrame, optional bool ClearZ): an actor
    // drawn where it is, over what's drawn, in the view SetRenderFrame last
    // set (the whole screen and the world's camera to begin with): the
    // tricorder's markers, a talker's head in its portrait.
    // DrawClippedActor(Actor A, bool WireFrame, int X, int Y, int XB, int YB,
    // optional bool ClearZ) the same in a rectangle X by Y at (XB, YB) (as
    // UWindow calls it: width, height, left, top), from the frame's corner.
    auto drawActor = [](NativeCall& c, VmObject* a, bool clearZ, const ScriptLevel::HudView& view) {
        if (!c.vm.level || !a)
            return;
        ScriptLevel::HudActor h;
        h.actor = a;
        h.clearZ = clearZ || c.vm.level->hudFrameClear();
        c.vm.level->hudFrameClear() = false;
        h.view = view;
        h.afterTiles = c.vm.level->hud().tiles.size();
        if (const Value* v = a->find(c.vm, "DrawType"))
            h.drawType = v->asInt();
        if (const Value* v = a->find(c.vm, "Style"))
            h.style = v->asInt();
        if (const Value* v = a->find(c.vm, "ScaleGlow"))
            h.scaleGlow = v->asFloat();
        c.vm.level->hud().actors.push_back(h);
    };
    reg("Canvas.DrawActor", [=](NativeCall& c) {
        if (c.vm.level)
            drawActor(c, c.o(0), c.b(2), c.vm.level->hudFrame());
    });
    reg("Canvas.DrawClippedActor", [=](NativeCall& c) {
        if (!c.vm.level)
            return;
        ScriptLevel::HudView v = c.vm.level->hudFrame();
        v.width = std::max(1, c.i(2));
        v.height = std::max(1, c.i(3));
        v.x += c.i(4);
        v.y += c.i(5);
        drawActor(c, c.o(0), c.b(6), v);
    });
    // SetRenderFrame(int X, int Y, int Width, int Height, Vector CamLocation,
    // Rotator CamRotation, optional int FOV, optional bool ClearZ): the view
    // the actors drawn next are drawn in, the depth cleared first with
    // ClearZ (the portraits, the tricorder's picture of the player; the
    // HUD's ResetCanvas sets the whole screen back). Tiles drawn after it
    // are placed from its corner and cut to it, and the canvas's clip
    // (ClipX, ClipY) becomes its size: the star map's blocks scroll inside
    // their window from 0, 0, scaled by the clip it saved before.
    reg("Canvas.SetRenderFrame", [](NativeCall& c) {
        if (!c.vm.level)
            return;
        for (const auto& [name, size] : {std::pair{"ClipX", c.i(2)}, std::pair{"ClipY", c.i(3)}})
            if (Value* v = c.self ? c.self->find(c.vm, name) : nullptr)
                *v = Value::floatValue(float(std::max(1, size)));
        ScriptLevel::HudView& v = c.vm.level->hudFrame();
        v.x = c.i(0);
        v.y = c.i(1);
        v.width = std::max(1, c.i(2));
        v.height = std::max(1, c.i(3));
        v.location = Vm::vectorOf(c.args[4]);
        v.rotation = Vm::rotatorOf(c.args[5]);
        if (c.given.size() > 6 && c.given[6] && c.i(6) > 0)
            v.fov = float(c.i(6));
        // The whole screen again (the HUD's ResetCanvas, with the HUD's own
        // place and a rotation nothing sets): the world's view.
        const ScriptLevel::HudView& world = c.vm.level->hudView();
        if (v.x <= 0 && v.y <= 0 && v.width >= world.width && v.height >= world.height)
            v = world;
        c.vm.level->hudFrameClear() = c.given.size() > 7 && c.given[7] && c.b(7);
    });
    // GetScreenCoords(Vector Location, out int X, out int Y, out float Scale):
    // where a place is in the frame's view, from the frame's corner as
    // tiles are placed (the tricorder puts its bracket there, as a share of
    // the clip); false behind it.
    reg("Canvas.GetScreenCoords", [](NativeCall& c) {
        c.result = Value::boolValue(false);
        if (!c.vm.level)
            return;
        const ScriptLevel::HudView& v = c.vm.level->hudFrame();
        const auto a = rotationAxes({v.rotation[0], v.rotation[1], v.rotation[2]});
        const Vec p = Vm::vectorOf(c.args[0]);
        const float d[3] = {p[0] - v.location[0], p[1] - v.location[1], p[2] - v.location[2]};
        auto dot = [&](int k) { return d[0] * a[size_t(k)][0] + d[1] * a[size_t(k)][1] + d[2] * a[size_t(k)][2]; };
        const float ahead = dot(0);
        if (ahead <= 1)
            return;
        const float focal = float(v.width) / 2 / std::tan(v.fov * 3.14159265f / 360);
        c.args[1] = Value::intValue(int(float(v.width) / 2 + dot(1) / ahead * focal));
        c.args[2] = Value::intValue(int(float(v.height) / 2 - dot(2) / ahead * focal));
        c.args[3] = Value::floatValue(focal / ahead);
        c.result = Value::boolValue(true);
    });
    // DrawPortal: the level seen from an actor, into part of the screen.
    // No script calls it.
    reg("Canvas.DrawPortal", [](NativeCall&) {});
}

} // namespace reluva::unreal
