// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Saved games, for a running level (ScriptLevel): the level as it is now
// written to a file of Reluva's own (not the original's, which saves the
// level as a package), and a level started from one. What's kept: every
// object the level's actors lead to (their properties, states, state
// code's place, latent waits, timers), the map's actors that are gone,
// and the engine's own records of them (touching, moves under way,
// animation layers and waits, skin channels, attachments, bends, the
// dialogue). The HUD's load list (DrawLoadGameList, LoadByFileID,
// DeleteByFileID) is here too. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iterator>

namespace reluva::unreal {

namespace {

constexpr char kMagic[8] = {'R', 'L', 'V', 'S', 'A', 'V', 'E', '1'};

class Out {
public:
    void u8(uint8_t v) { s_.push_back(char(v)); }
    void i32(int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void i64(int64_t v) { raw(&v, 8); }
    void str(const std::string& v) {
        i32(int32_t(v.size()));
        s_ += v;
    }
    void vec(const std::array<float, 3>& v) {
        for (float f : v)
            f32(f);
    }
    const std::string& data() const { return s_; }

private:
    void raw(const void* p, size_t n) { s_.append(static_cast<const char*>(p), n); }
    std::string s_;
};

class In {
public:
    explicit In(std::string data) : d_(std::move(data)) {}
    uint8_t u8() {
        uint8_t v = 0;
        raw(&v, 1);
        return v;
    }
    int32_t i32() {
        int32_t v = 0;
        raw(&v, 4);
        return v;
    }
    float f32() {
        float v = 0;
        raw(&v, 4);
        return v;
    }
    int64_t i64() {
        int64_t v = 0;
        raw(&v, 8);
        return v;
    }
    std::string str() {
        const int32_t n = i32();
        if (n < 0 || size_t(n) > d_.size() - at_) {
            ok_ = false;
            return {};
        }
        std::string v = d_.substr(at_, size_t(n));
        at_ += size_t(n);
        return v;
    }
    std::array<float, 3> vec() { return {f32(), f32(), f32()}; }
    // A count of things to read, each at least `least` bytes: refused if
    // the file can't hold that many.
    int32_t count(size_t least = 1) {
        const int32_t n = i32();
        if (n < 0 || size_t(n) * least > d_.size() - at_) {
            ok_ = false;
            return 0;
        }
        return n;
    }
    bool ok() const { return ok_; }

private:
    void raw(void* p, size_t n) {
        if (n > d_.size() - at_) {
            ok_ = false;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, d_.data() + at_, n);
        at_ += n;
    }
    std::string d_;
    size_t at_ = 0;
    bool ok_ = true;
};

// How an object reference is written.
enum RefKind : uint8_t {
    RefNone,
    RefSaved,     // one of the saved objects, by its number
    RefPackage,   // an object of a package (a texture, a mesh): its package and export
    RefClass,     // a class, by its package and name (or its name, made up by the engine)
    RefViewport,  // the player's viewport, console and canvas: the new level's own
    RefConsole,
    RefCanvas,
    RefMapExport, // one of the map's objects that isn't an actor (its Level)
};

std::string qualifiedName(const VmClass* c) {
    return c->package ? c->package->name() + "." + c->name : c->name;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

// The header: what's in it, which map, when.
Error ScriptLevel::readSaveInfo(const std::filesystem::path& file, SaveInfo& out) {
    std::ifstream f(file, std::ios::binary);
    char magic[8] = {};
    if (!f.read(magic, 8) || std::memcmp(magic, kMagic, 8) != 0)
        return Error::fail(file.string() + ": not a saved game of Reluva's");
    std::string head(4096, '\0');
    f.read(head.data(), std::streamsize(head.size()));
    head.resize(size_t(f.gcount()));
    In in(head);
    out.file = file;
    out.map = in.str();
    out.description = in.str();
    out.time = in.i64();
    return in.ok() ? Error::success() : Error::fail(file.string() + ": cut short");
}

// The saved games in the save folder, newest first: QuickSave.dsa and
// Save<n>.dss, the game's own names, but Reluva's files.
std::vector<ScriptLevel::SaveInfo> ScriptLevel::savedGames() const {
    std::vector<SaveInfo> out;
    std::error_code ec;
    if (saveFolder_.empty())
        return out;
    for (const auto& entry : std::filesystem::directory_iterator(saveFolder_, ec)) {
        const std::string ext = fs::toLower(entry.path().extension().string());
        SaveInfo info;
        if ((ext == ".dss" || ext == ".dsa") && !readSaveInfo(entry.path(), info))
            out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) {
        return a.time != b.time ? a.time > b.time : a.file.filename() < b.file.filename();
    });
    return out;
}

// The level saved: "QuickSave" to QuickSave.dsa, anything else to the next
// Save<n>.dss, described by the level's title.
Error ScriptLevel::saveGame(const std::string& slot) {
    if (saveFolder_.empty())
        return Error::fail("no folder to save games in");
    std::error_code ec;
    std::filesystem::create_directories(saveFolder_, ec);
    std::filesystem::path file;
    std::string description;
    if (fs::toLower(slot) == "quicksave") {
        file = saveFolder_ / "QuickSave.dsa";
        description = "QuickSave";
    } else {
        int n = 1;
        while (std::filesystem::exists(saveFolder_ / ("Save" + std::to_string(n) + ".dss"), ec))
            ++n;
        file = saveFolder_ / ("Save" + std::to_string(n) + ".dss");
        if (const Value* title = levelInfo_->find(vm_, "Title"); title && !title->s.empty())
            description = title->s;
        else
            description = map_.name();
    }

    // The objects: the level's actors and what they lead to that the level
    // made (not the packages' objects, nor the engine's viewport, console
    // and canvas, which a loaded level makes anew).
    VmObject* viewport = nullptr;
    VmObject* console = nullptr;
    if (player_)
        if (const Value* v = player_->find(vm_, "Player"); v && v->asObject()) {
            viewport = v->o;
            if (const Value* c = viewport->find(vm_, "Console"))
                console = c->asObject();
        }
    std::vector<VmObject*> saved;
    std::map<const VmObject*, int> ids;
    auto mapActor = [&](const VmObject* o) { return o->package == &map_ && o->isActor && o->exportIndex >= 0; };
    auto wanted = [&](const VmObject* o) {
        return o && !o->deleted && !o->asClass && o->cls && (!o->package || mapActor(o)) && o != viewport &&
               o != console && o != canvas_ && (o->package || o->cls->package);
    };
    auto add = [&](VmObject* o) {
        if (wanted(o) && !ids.count(o)) {
            ids[o] = int(saved.size());
            saved.push_back(o);
        }
    };
    for (VmObject* a : actors_)
        add(a);
    std::function<void(const Value&)> follow = [&](const Value& v) {
        if (v.type == VType::Object)
            add(v.o);
        for (const Value& f : v.fields)
            follow(f);
    };
    for (size_t k = 0; k < saved.size(); ++k)
        for (const Value& v : saved[k]->values)
            follow(v);

    Out out;
    out.str(map_.name());
    out.str(description);
    out.i64(int64_t(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
    auto ref = [&](const VmObject* o) {
        if (!o || o->deleted) {
            out.u8(RefNone);
        } else if (auto it = ids.find(o); it != ids.end()) {
            out.u8(RefSaved);
            out.i32(it->second);
        } else if (o == viewport) {
            out.u8(RefViewport);
        } else if (o == console) {
            out.u8(RefConsole);
        } else if (o == canvas_) {
            out.u8(RefCanvas);
        } else if (o->asClass) {
            out.u8(RefClass);
            out.str(qualifiedName(o->asClass));
        } else if (o->package == &map_ && o->exportIndex >= 0) {
            out.u8(RefMapExport);
            out.i32(o->exportIndex);
        } else if (o->package && o->exportIndex >= 0) {
            out.u8(RefPackage);
            out.str(o->package->name());
            out.i32(o->exportIndex);
        } else {
            out.u8(RefNone);
        }
    };
    std::function<void(const Value&)> value = [&](const Value& v) {
        out.u8(uint8_t(v.type));
        switch (v.type) {
        case VType::Byte:
        case VType::Int:
        case VType::Bool: out.i32(v.i); break;
        case VType::Name: out.str(v.i ? vm_.nameText(v.i) : std::string()); break;
        case VType::Float: out.f32(v.f); break;
        case VType::String: out.str(v.s); break;
        case VType::Object: ref(v.o); break;
        case VType::Struct:
            out.i32(int32_t(v.fields.size()));
            for (const Value& f : v.fields)
                value(f);
            break;
        case VType::None: break;
        }
    };
    auto name = [&](NameId n) { out.str(n ? vm_.nameText(n) : std::string()); };

    out.i32(int32_t(saved.size()));
    for (const VmObject* o : saved) {
        if (mapActor(o)) {
            out.u8(0);
            out.i32(o->exportIndex);
        } else {
            out.u8(1);
            out.str(qualifiedName(o->cls));
            name(o->name);
        }
    }
    for (const VmObject* o : saved) {
        out.i32(int32_t(o->values.size()));
        for (const Value& v : o->values)
            value(v);
        name(o->state ? o->state->nameId : 0);
        int depth = -1, d = 0;
        for (const VmStruct* s = o->state; s && s->kind == VmStruct::Kind::State; s = s->super, ++d)
            if (s == o->codeState)
                depth = d;
        out.i32(depth);
        out.i32(o->codeAt);
        out.f32(o->sleeping);
        out.u8(o->latent);
        out.i32(o->stateSerial);
        out.i32(int32_t(o->disabled.size()));
        for (NameId n : o->disabled)
            name(n);
        out.f32(o->timerRate);
        out.f32(o->timerCount);
        out.u8(o->timerLoop);
    }

    // The level's own records.
    auto has = [&](const VmObject* o) { return ids.count(o) > 0; };
    out.i32(int32_t(std::count_if(actors_.begin(), actors_.end(), has)));
    for (VmObject* a : actors_)
        if (has(a))
            out.i32(ids[a]);
    ref(player_);
    ref(game_);
    out.f32(time_);
    out.i32(ticks_);
    out.i32(skinChannels_);
    auto keyed = [&](const auto& table, auto&& write) {
        out.i32(int32_t(std::count_if(table.begin(), table.end(), [&](const auto& e) { return has(e.first); })));
        for (const auto& [o, entry] : table)
            if (has(o)) {
                out.i32(ids[o]);
                write(entry);
            }
    };
    keyed(touching_, [&](const std::vector<VmObject*>& list) {
        out.i32(int32_t(list.size()));
        for (VmObject* t : list)
            ref(t);
    });
    keyed(goals_, [&](const Goal& g) {
        out.u8(uint8_t(g.kind));
        out.vec(g.point);
        ref(g.target);
        out.u8(g.facing);
        out.vec(g.focus);
        ref(g.focusActor);
        out.f32(g.speed);
        out.f32(g.timeLeft);
        out.i32(g.stateSerial);
    });
    out.i32(int32_t(std::count_if(fallers_.begin(), fallers_.end(), has)));
    for (VmObject* f : fallers_)
        if (has(f))
            out.i32(ids[f]);
    keyed(animWaits_, [&](int serial) { out.i32(serial); });
    keyed(layers_, [&](const std::map<NameId, AnimLayer>& groups) {
        out.i32(int32_t(groups.size()));
        for (const auto& [group, l] : groups) {
            name(group);
            name(l.sequence);
            name(l.bone);
            out.u8(l.rest);
            out.f32(l.frame);
            out.f32(l.rate);
            out.u8(l.loop);
            out.u8(l.finished);
            out.f32(l.weight);
            out.f32(l.weightRate);
        }
    });
    keyed(skins_, [&](const std::map<NameId, SkinChannel>& channels) {
        out.i32(int32_t(channels.size()));
        for (const auto& [tag, s] : channels) {
            name(tag);
            name(s.sequence);
            out.f32(s.frame);
            out.f32(s.weight);
            out.f32(s.target);
            out.f32(s.targetWeight);
            out.f32(s.tweenRate);
        }
    });
    keyed(layerWaits_, [&](const std::pair<NameId, int>& w) {
        name(w.first);
        out.i32(w.second);
    });
    keyed(attachments_, [&](const Attachment& a) {
        ref(a.parent);
        name(a.parentPoint);
        name(a.childPoint);
    });
    keyed(bends_, [&](const Bends& b) {
        out.i32(int32_t(b.turns.size()));
        for (const auto& [key, degrees] : b.turns) {
            name(key.first);
            out.i32(key.second);
            out.f32(degrees);
        }
        out.i32(int32_t(b.actorTurns.size()));
        for (const auto& [bone, r] : b.actorTurns) {
            name(bone);
            for (int32_t k : r)
                out.i32(k);
        }
        for (const Bends::Ik& ik : b.limbs) {
            out.u8(ik.place);
            out.u8(ik.turn);
            out.vec(ik.at);
            for (const Vec& axis : ik.axes)
                out.vec(axis);
            out.f32(ik.placeWeight);
            out.f32(ik.turnWeight);
        }
        // The effectors are worked out again from the next tick's animation.
    });
    out.u8(dialogLoaded_);
    out.i32(int32_t(dialogPlays_.size()));
    for (int n : dialogPlays_)
        out.i32(n);
    out.i32(int32_t(dialogThreads_.size()));
    for (const DialogThread& t : dialogThreads_) {
        out.i32(t.id);
        out.i32(t.line);
        ref(t.self);
        ref(t.speaker);
        ref(t.listener);
        out.f32(t.seconds);
        out.f32(t.timeLeft);
        out.f32(t.pauseLeft);
        out.f32(t.mouthTimer);
    }
    out.i32(nextDialogThread_);
    out.i32(int32_t(dialogRandom_));
    const auto& counts = vm_.nameCounts();
    out.i32(int32_t(counts.size()));
    for (const auto& [cls, n] : counts) {
        out.str(qualifiedName(cls));
        out.i32(n);
    }

    // The textures' own properties, which the scripts change and the
    // engine keeps across a load, its packages staying loaded: the HUD's
    // places (SetHudData), the FLC textures' playing (Mode, FrameNum).
    std::vector<const VmObject*> textures;
    for (const auto& o : vm_.objects())
        if (o->package && o->package != &map_ && o->exportIndex >= 0 && !o->asClass && !o->deleted && o->cls &&
            vm_.isA(o.get(), "Texture"))
            textures.push_back(o.get());
    out.i32(int32_t(textures.size()));
    for (const VmObject* t : textures) {
        out.str(t->package->name());
        out.i32(t->exportIndex);
        out.i32(int32_t(t->values.size()));
        for (const Value& v : t->values)
            value(v);
    }

    const std::filesystem::path partial = file.string() + ".part";
    {
        std::ofstream f(partial, std::ios::binary | std::ios::trunc);
        f.write(kMagic, 8);
        f.write(out.data().data(), std::streamsize(out.data().size()));
        if (!f)
            return Error::fail(partial.string() + ": couldn't be written");
    }
    std::filesystem::rename(partial, file, ec);
    if (ec)
        return Error::fail(file.string() + ": " + ec.message());
    return Error::success();
}

// The level as a saved game has it, in place of starting it and logging
// the player in: the map's actors (just loaded) given their saved
// properties, those gone removed, the level's own made again, then the
// level's records. The player gets a new viewport and console, and
// bJustLoaded, as the engine sets it; and the game isn't paused (it saves
// paused).
Error ScriptLevel::restore(const std::filesystem::path& file) {
    std::string data = readFile(file);
    if (data.size() < 8 || std::memcmp(data.data(), kMagic, 8) != 0)
        return Error::fail(file.string() + ": not a saved game of Reluva's");
    In in(data.substr(8));
    if (in.str() != map_.name())
        return Error::fail(file.string() + ": saved on another map");
    in.str();
    in.i64();

    std::map<int, VmObject*> byExport;
    for (VmObject* a : actors_)
        if (a->exportIndex >= 0)
            byExport[a->exportIndex] = a;
    std::vector<VmObject*> objects;
    std::set<VmObject*> kept;
    const int32_t n = in.count(5);
    for (int32_t k = 0; k < n && in.ok(); ++k) {
        VmObject* o = nullptr;
        if (in.u8() == 0) {
            auto it = byExport.find(in.i32());
            if (it != byExport.end())
                o = it->second;
        } else {
            const std::string cls = in.str();
            const std::string objName = in.str();
            if (VmClass* c = vm_.findClass(cls))
                o = vm_.create(c, vm_.name(objName));
        }
        if (!o)
            return Error::fail(file.string() + ": an object that can't be made again");
        objects.push_back(o);
        kept.insert(o);
    }
    for (VmObject* a : actors_)
        if (!kept.count(a))
            a->deleted = true;

    VmObject* viewport = nullptr; // made once the player is known
    auto ref = [&]() -> VmObject* {
        switch (in.u8()) {
        case RefSaved: {
            const int32_t id = in.i32();
            return id >= 0 && size_t(id) < objects.size() ? objects[size_t(id)] : nullptr;
        }
        case RefPackage: {
            const std::string package = in.str();
            const int32_t index = in.i32();
            const Package* p = vm_.packages().get(package);
            return p ? vm_.objectAt(*p, index + 1) : nullptr;
        }
        case RefClass: {
            VmClass* c = vm_.findClass(in.str());
            return c ? c->object : nullptr;
        }
        case RefMapExport: return vm_.objectAt(map_, in.i32() + 1);
        case RefViewport: return reinterpret_cast<VmObject*>(1); // put right once it's made
        case RefConsole: return reinterpret_cast<VmObject*>(2);
        case RefCanvas: return reinterpret_cast<VmObject*>(3);
        default: return nullptr;
        }
    };
    std::vector<Value*> clientRefs; // values pointing at the viewport, console, canvas
    // A value read into its slot (whose struct it keeps), or, if the slot's
    // type isn't what was saved, read and dropped.
    std::function<void(Value&, bool)> value = [&](Value& v, bool keep) {
        const auto type = VType(in.u8());
        Value read;
        keep = keep && type == v.type;
        Value& to = keep ? v : read;
        to.type = type;
        switch (type) {
        case VType::Byte:
        case VType::Int:
        case VType::Bool: to.i = in.i32(); break;
        case VType::Name: {
            const std::string text = in.str();
            to.i = text.empty() ? 0 : vm_.name(text);
            break;
        }
        case VType::Float: to.f = in.f32(); break;
        case VType::String: to.s = in.str(); break;
        case VType::Object:
            to.o = ref();
            if (reinterpret_cast<uintptr_t>(to.o) <= 3 && to.o) {
                if (keep)
                    clientRefs.push_back(&to);
                else
                    to.o = nullptr;
            }
            break;
        case VType::Struct: {
            const int32_t fields = in.count(1);
            if (size_t(fields) != to.fields.size())
                to.fields.resize(size_t(fields));
            for (Value& f : to.fields)
                value(f, keep);
            break;
        }
        case VType::None: break;
        }
    };
    auto name = [&]() -> NameId {
        const std::string text = in.str();
        return text.empty() ? 0 : vm_.name(text);
    };
    for (VmObject* o : objects) {
        const int32_t count = in.count(1);
        if (size_t(count) != o->values.size())
            return Error::fail(file.string() + ": " + o->cls->name + "'s properties don't match its class");
        for (Value& v : o->values)
            value(v, true);
        const NameId stateName = name();
        o->state = stateName ? vm_.findState(o->cls, stateName) : nullptr;
        const int32_t depth = in.i32();
        o->codeState = nullptr;
        int d = 0;
        for (VmStruct* s = o->state; s && s->kind == VmStruct::Kind::State; s = s->super, ++d)
            if (d == depth)
                o->codeState = static_cast<VmState*>(s);
        o->codeAt = in.i32();
        if (!o->codeState)
            o->codeAt = -1;
        o->sleeping = in.f32();
        o->latent = in.u8() != 0;
        o->stateSerial = in.i32();
        o->disabled.clear();
        for (int32_t k = in.count(4); k > 0; --k)
            o->disabled.insert(name());
        o->timerRate = in.f32();
        o->timerCount = in.f32();
        o->timerLoop = in.u8() != 0;
    }

    actors_.clear();
    for (int32_t k = in.count(4); k > 0; --k) {
        const int32_t id = in.i32();
        if (id >= 0 && size_t(id) < objects.size())
            actors_.push_back(objects[size_t(id)]);
    }
    player_ = ref();
    game_ = ref();
    time_ = in.f32();
    ticks_ = in.i32();
    skinChannels_ = in.i32();
    auto object = [&]() -> VmObject* {
        const int32_t id = in.i32();
        return id >= 0 && size_t(id) < objects.size() ? objects[size_t(id)] : nullptr;
    };
    auto keyed = [&](auto&& read) {
        for (int32_t k = in.count(4); k > 0 && in.ok(); --k) {
            VmObject* o = object();
            read(o);
        }
    };
    touching_.clear();
    keyed([&](VmObject* o) {
        std::vector<VmObject*>& list = touching_[o];
        for (int32_t k = in.count(1); k > 0; --k)
            if (VmObject* t = ref())
                list.push_back(t);
    });
    goals_.clear();
    keyed([&](VmObject* o) {
        Goal g;
        g.kind = Goal::Kind(in.u8());
        g.point = in.vec();
        g.target = ref();
        g.facing = in.u8() != 0;
        g.focus = in.vec();
        g.focusActor = ref();
        g.speed = in.f32();
        g.timeLeft = in.f32();
        g.stateSerial = in.i32();
        goals_[o] = g;
    });
    fallers_.clear();
    for (int32_t k = in.count(4); k > 0; --k)
        if (VmObject* o = object())
            fallers_.insert(o);
    animWaits_.clear();
    keyed([&](VmObject* o) { animWaits_[o] = in.i32(); });
    layers_.clear();
    keyed([&](VmObject* o) {
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId group = name();
            AnimLayer l;
            l.sequence = name();
            l.bone = name();
            l.rest = in.u8() != 0;
            l.frame = in.f32();
            l.rate = in.f32();
            l.loop = in.u8() != 0;
            l.finished = in.u8() != 0;
            l.weight = in.f32();
            l.weightRate = in.f32();
            layers_[o][group] = l;
        }
    });
    skins_.clear();
    keyed([&](VmObject* o) {
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId tag = name();
            SkinChannel s;
            s.sequence = name();
            s.frame = in.f32();
            s.weight = in.f32();
            s.target = in.f32();
            s.targetWeight = in.f32();
            s.tweenRate = in.f32();
            skins_[o][tag] = s;
        }
    });
    layerWaits_.clear();
    keyed([&](VmObject* o) {
        const NameId group = name();
        layerWaits_[o] = {group, in.i32()};
    });
    attachments_.clear();
    keyed([&](VmObject* o) {
        Attachment a;
        a.parent = ref();
        a.parentPoint = name();
        a.childPoint = name();
        attachments_[o] = a;
    });
    bends_.clear();
    keyed([&](VmObject* o) {
        Bends& b = bends_[o];
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId bone = name();
            const int axis = in.i32();
            b.turns[{bone, axis}] = in.f32();
        }
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId bone = name();
            Rot r;
            for (int32_t& v : r)
                v = in.i32();
            b.actorTurns[bone] = r;
        }
        for (Bends::Ik& ik : b.limbs) {
            ik.place = in.u8() != 0;
            ik.turn = in.u8() != 0;
            ik.at = in.vec();
            for (Vec& axis : ik.axes)
                axis = in.vec();
            ik.placeWeight = in.f32();
            ik.turnWeight = in.f32();
        }
    });
    if (in.u8())
        loadDialog();
    const int32_t plays = in.count(4);
    for (int32_t k = 0; k < plays; ++k) {
        const int32_t p = in.i32();
        if (size_t(k) < dialogPlays_.size())
            dialogPlays_[size_t(k)] = p;
    }
    dialogThreads_.clear();
    for (int32_t k = in.count(4); k > 0; --k) {
        DialogThread t;
        t.id = in.i32();
        t.line = in.i32();
        t.self = ref();
        t.speaker = ref();
        t.listener = ref();
        t.seconds = in.f32();
        t.timeLeft = in.f32();
        t.pauseLeft = in.f32();
        t.mouthTimer = in.f32();
        if (t.line >= 0 && size_t(t.line) < dialogLines_.size())
            dialogThreads_.push_back(t);
    }
    nextDialogThread_ = in.i32();
    dialogRandom_ = uint32_t(in.i32());
    for (int32_t k = in.count(8); k > 0; --k) {
        const std::string cls = in.str();
        const int32_t count = in.i32();
        if (VmClass* c = vm_.findClass(cls))
            vm_.nameCounts()[c] = std::max(vm_.nameCounts()[c], count);
    }
    for (int32_t k = in.count(12); k > 0 && in.ok(); --k) {
        const std::string package = in.str();
        const int32_t index = in.i32();
        const Package* p = vm_.packages().get(package);
        VmObject* t = p ? vm_.objectAt(*p, index + 1) : nullptr;
        const int32_t count = in.count(1);
        const bool fits = t && size_t(count) == t->values.size();
        Value scratch;
        for (int32_t v = 0; v < count && in.ok(); ++v)
            value(fits ? t->values[size_t(v)] : scratch, fits);
    }
    if (!in.ok() || !player_ || !game_)
        return Error::fail(file.string() + ": cut short");

    // The engine's side of the player, made anew, and what pointed at the
    // old one pointed at it.
    attachViewport();
    if (const Value* v = player_->find(vm_, "Player"))
        viewport = v->asObject();
    VmObject* console = this->console();
    for (Value* v : clientRefs) {
        const auto which = reinterpret_cast<uintptr_t>(v->o);
        v->o = which == 1 ? viewport : which == 2 ? console : canvas();
    }
    if (Value* v = player_->find(vm_, "bJustLoaded"))
        *v = Value::boolValue(true);
    if (Value* v = levelInfo_->find(vm_, "Pauser"))
        *v = Value::stringValue("");
    collidersTick_ = -1;
    posed_.clear();
    posedBones_.clear();
    return Error::success();
}

void registerSaveNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // DrawLoadGameList(): each saved game to the HUD's DrawLoadGameFile(Id,
    // its name, when), newest first, from 1.
    reg("CTVE_Hud.DrawLoadGameList", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        int id = 0;
        for (const ScriptLevel::SaveInfo& s : c.vm.level->savedGames()) {
            char when[32] = {};
            const std::time_t t = std::time_t(s.time);
            if (const std::tm* tm = std::localtime(&t))
                std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
            c.vm.callEvent(c.self, "DrawLoadGameFile",
                           {Value::intValue(++id), Value::stringValue(s.description), Value::stringValue(when)});
        }
    });
    // LoadByFileID(int Id): that game's file to the HUD's HudLoadFile, which
    // travels to it ("?LOAD=Save1.dss").
    reg("CTVE_Hud.LoadByFileID", [](NativeCall& c) {
        if (!c.vm.level || !c.self)
            return;
        const auto saves = c.vm.level->savedGames();
        if (c.i(0) >= 1 && size_t(c.i(0)) <= saves.size())
            c.vm.callEvent(c.self, "HudLoadFile", {Value::stringValue(saves[size_t(c.i(0) - 1)].file.filename().string())});
    });
    // DeleteByFileID(int Id): that game's file deleted.
    reg("CTVE_Hud.DeleteByFileID", [](NativeCall& c) {
        if (!c.vm.level)
            return;
        const auto saves = c.vm.level->savedGames();
        if (c.i(0) >= 1 && size_t(c.i(0)) <= saves.size()) {
            std::error_code ec;
            std::filesystem::remove(saves[size_t(c.i(0) - 1)].file, ec);
        }
    });
}

} // namespace reluva::unreal
