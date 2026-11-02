// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A level saved as the original game saves it: a package (.dsa) that the
// original loads, and so does Reluva. It's written over the map's own
// tables, so the parts the level hasn't changed (its BSP, the movers'
// brushes, the lights' surfaces) are the map's data as they are, their
// references still right; what the level has (its actors, their state,
// the objects they made) is written from the running level. See
// docs/formats/unreal/save.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/game_save.h"
#include "engines/unreal/package_writer.h"
#include "engines/unreal/script.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <set>
#include <sstream>
#include <functional>

namespace reluva::unreal {

namespace {

// The engine's probe events, by their bit in a state frame's mask (Core's
// names from 300 on, The Fallen's in the place of some of the probes).
const char* const kProbes[64] = {
    "Spawned", "Destroyed", "GainedChild", "LostChild", "Probe4", "Probe5", "Trigger", "UnTrigger", "Timer",
    "HitWall", "Falling", "Landed", "ZoneChange", "Touch", "UnTouch", "Bump", "BeginState", "EndState",
    "BaseChange", "Attach", "Detach", "ActorEntered", "ActorLeaving", "KillCredit", "AnimEnd", "EndedRotation",
    "InterpolateEnd", "EncroachingOn", "EncroachedBy", "FootZoneChange", "HeadZoneChange", "PainTimer",
    "SpeechTimer", "MayFall", "Probe34", "C3DAnimEnd", "C3DPreRender", "ChangePhysics", "ChestZoneChange",
    "C3DSpeechEvent", "Tick", "PlayerTick", "Expired", "Probe39", "SeePlayer", "EnemyNotVisible", "HearNoise",
    "UpdateEyeHeight", "SeeMonster", "SeeFriend", "SpecialHandling", "BotDesireability", "Probe48", "Probe49",
    "Probe50", "Probe51", "Probe52", "Probe53", "Probe54", "Probe55", "Probe56", "Probe57", "Probe58", "Probe59"};

constexpr uint32_t kClassTransient = 0x8;
constexpr uint32_t kActorFlags = 0x00070001 | objflag::HasStack;
constexpr uint32_t kObjectFlags = 0x00070000;

bool transientClass(const VmClass* c) {
    for (; c; c = c->parent())
        if (c->classFlags & kClassTransient)
            return true;
    return false;
}

bool sameValue(const Value& a, const Value& b) {
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case VType::Float: return a.f == b.f || (std::isnan(a.f) && std::isnan(b.f));
    case VType::Object: return a.asObject() == b.asObject();
    case VType::String: return a.s == b.s;
    case VType::Struct:
        if (a.fields.size() != b.fields.size())
            return false;
        for (size_t k = 0; k < a.fields.size(); ++k)
            if (!sameValue(a.fields[k], b.fields[k]))
                return false;
        return true;
    default: return a.i == b.i;
    }
}

} // namespace

Error ScriptLevel::writeGameSave(const std::string& path) {
    PackageWriter w(map_);
    const int version = map_.version();
    if (!levelInfo_ || levelInfo_->package != &map_)
        return Error::fail("the level has no LevelInfo of its map's");
    const int32_t levelOuter = map_.exports()[size_t(levelInfo_->exportIndex)].package;

    // What's saved: the level's actors and ghosts, and every object they
    // lead to that the level made, or that's one of the map's own; not the
    // engine's viewport, console and canvas, nor objects of transient
    // classes.
    VmObject* viewport = nullptr;
    VmObject* console = nullptr;
    if (player_)
        if (const Value* v = prop(player_, "Player"); v && v->asObject()) {
            viewport = v->o;
            if (const Value* c = prop(viewport, "Console"))
                console = c->asObject();
        }
    std::map<const VmObject*, int32_t> refs;
    std::vector<VmObject*> saved;
    std::deque<VmObject*> todo;
    auto wanted = [&](VmObject* o) {
        return o && !o->deleted && !o->asClass && o->cls && o != viewport && o != console && o != canvas_ &&
               (!o->package || o->package == &map_) && (o->package || o->cls->package) && !transientClass(o->cls);
    };
    auto add = [&](VmObject* o) {
        if (wanted(o) && !refs.count(o)) {
            refs[o] = 0;
            saved.push_back(o);
            todo.push_back(o);
        }
    };
    for (VmObject* a : actors_)
        add(a);
    for (VmObject* g : ghosts_)
        add(g);
    std::function<void(const Value&)> scan = [&](const Value& v) {
        if (v.type == VType::Object)
            add(v.asObject());
        else if (v.type == VType::Struct)
            for (const Value& f : v.fields)
                scan(f);
    };
    while (!todo.empty()) {
        VmObject* o = todo.front();
        todo.pop_front();
        // The map's own objects that aren't actors (its models, mesh
        // states) are kept as they are: what they lead to is too.
        if (o->package == &map_ && !o->isActor)
            continue;
        for (const Value& v : o->values)
            scan(v);
    }
    // Their exports: the map's by their own numbers, the rest added, named
    // as the level names them (made unique), in their outer.
    std::set<std::pair<int32_t, std::string>> taken; // outer, lower-case name
    for (size_t i = 0; i < map_.exports().size(); ++i)
        taken.insert({map_.exports()[i].package, fs::toLower(map_.name(map_.exports()[i].objectName))});
    auto classImport = [&](const VmClass* c) {
        return w.import(c->package->name() + "." + c->name, "Core", "Class");
    };
    for (VmObject* o : saved)
        if (o->package == &map_)
            refs[o] = o->exportIndex + 1;
    for (VmObject* o : saved) {
        if (o->package)
            continue;
        int32_t outer = levelOuter;
        if (!o->isActor)
            if (const Value* v = prop(o, "Outer"); v && v->asObject() && refs.count(v->o) && refs[v->o] > 0)
                outer = refs[v->o];
        std::string name = vm_.nameText(o->name);
        if (name.empty() || name == "None")
            name = o->cls->name;
        for (int n = 1; taken.count({outer, fs::toLower(name)}); ++n)
            name = vm_.nameText(o->name) + "_" + std::to_string(n);
        taken.insert({outer, fs::toLower(name)});
        refs[o] = w.addExport(classImport(o->cls), outer, name, o->isActor ? kActorFlags : kObjectFlags);
    }

    // An object reference as the save has it.
    auto refOf = [&](VmObject* o) -> int32_t {
        if (!o || o->deleted)
            return 0;
        if (o->asClass) {
            const VmClass* c = o->asClass;
            return c->package ? classImport(c) : 0;
        }
        if (auto it = refs.find(o); it != refs.end())
            return it->second;
        if (o->package && o->package != &map_ && o->exportIndex >= 0) {
            const Package& p = *o->package;
            const Export& e = p.exports()[size_t(o->exportIndex)];
            std::string classPackage = "Core", className = "Object";
            if (e.classRef < 0) {
                const Import& ci = p.imports()[size_t(-e.classRef - 1)];
                className = p.name(ci.objectName);
                const int32_t outer = ci.package;
                classPackage = outer < 0 ? p.name(p.imports()[size_t(-outer - 1)].objectName) : p.name(ci.classPackage);
            } else if (e.classRef > 0) {
                className = p.name(p.exports()[size_t(e.classRef - 1)].objectName);
                classPackage = p.name();
            } else {
                className = "Class";
            }
            return w.import(p.name() + "." + p.path(o->exportIndex + 1), classPackage, className);
        }
        return 0;
    };

    // Values as properties and struct fields store them.
    std::function<void(ByteWriter&, const VmProperty&, const Value&)> valueBytes = [&](ByteWriter& b,
                                                                                         const VmProperty& p,
                                                                                         const Value& v) {
        switch (p.type) {
        case VType::Byte: b.u8(uint8_t(v.asInt())); break;
        case VType::Int: b.i32(v.asInt()); break;
        case VType::Bool: b.u8(v.asBool() ? 1 : 0); break;
        case VType::Float: b.f32(v.asFloat()); break;
        case VType::Name: b.index(w.name(v.i ? vm_.nameText(v.i) : "None")); break;
        case VType::String: b.string(v.s); break;
        case VType::Object: b.index(refOf(v.o)); break;
        case VType::Struct:
            if (p.structType)
                for (const VmProperty* f : p.structType->properties)
                    for (int k = 0; k < f->arrayDim; ++k) {
                        const size_t at = size_t(f->slot + k);
                        valueBytes(b, *f, at < v.fields.size() ? v.fields[at] : f->zero());
                    }
            break;
        default: break;
        }
    };
    // Its properties that differ from its class's defaults, tagged; then None.
    // (A class's own first, then its parent's, as the engine lists them.)
    auto properties = [&](ByteWriter& b, const VmObject* o) {
        const VmClass* c = o->cls;
        std::vector<const VmProperty*> order;
        for (const VmClass* k = c; k; k = k->parent()) {
            const int from = k->parent() ? k->parent()->slots : 0;
            for (const VmProperty* p : k->properties)
                if (p->slot >= from && p->slot < k->slots &&
                    std::find(order.begin(), order.end(), p) == order.end())
                    order.push_back(p);
        }
        for (const VmProperty* p : order) {
            if (p->flags & propflag::Transient)
                continue;
            if (fs::toLower(p->name) == "outer" || fs::toLower(p->name) == "class" || fs::toLower(p->name) == "name")
                continue;
            for (int k = 0; k < p->arrayDim; ++k) {
                const size_t slot = size_t(p->slot + k);
                if (slot >= o->values.size())
                    continue;
                Value v = o->values[slot];
                if (v.type == VType::Object && refOf(v.asObject()) == 0)
                    v.o = nullptr;
                const Value& d = slot < c->defaults.size() ? c->defaults[slot] : p->zero();
                if (sameValue(v, d) || (v.type == VType::Object && !v.o && !d.asObject()))
                    continue;
                if (p->type == VType::None)
                    continue;
                b.index(w.name(p->name));
                if (p->type == VType::Bool) {
                    b.u8(uint8_t(3 | (v.asBool() ? 0x80 : 0)));
                    continue;
                }
                ByteWriter data;
                valueBytes(data, *p, v);
                uint8_t type = 0;
                switch (p->type) {
                case VType::Byte: type = 1; break;
                case VType::Int: type = 2; break;
                case VType::Float: type = 4; break;
                case VType::Object: type = 5; break;
                case VType::Name: type = 6; break;
                case VType::String: type = 13; break;
                case VType::Struct: type = 10; break;
                default: break;
                }
                const size_t n = data.data.size();
                uint8_t sizeCode = n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : n == 12 ? 3 : n == 16 ? 4 : n < 256 ? 5 : n < 65536 ? 6 : 7;
                b.u8(uint8_t(type | sizeCode << 4 | (k > 0 ? 0x80 : 0)));
                if (p->type == VType::Struct)
                    b.index(w.name(p->structType ? p->structType->name : "None"));
                if (sizeCode == 5)
                    b.u8(uint8_t(n));
                else if (sizeCode == 6)
                    b.u16(uint16_t(n));
                else if (sizeCode == 7)
                    b.u32(uint32_t(n));
                if (k > 0) {
                    if (k < 128)
                        b.u8(uint8_t(k));
                    else if (k < 16384) {
                        b.u8(uint8_t(0x80 | (k >> 8)));
                        b.u8(uint8_t(k));
                    } else {
                        b.u8(uint8_t(0xC0 | (k >> 24)));
                        b.u8(uint8_t(k >> 16));
                        b.u8(uint8_t(k >> 8));
                        b.u8(uint8_t(k));
                    }
                }
                b.append(data.data);
            }
        }
        b.index(w.name("None"));
    };
    // A state, or a class as its own state, as an import.
    auto stateRef = [&](const VmState* s) -> int32_t {
        if (!s)
            return 0;
        if (s->kind == VmStruct::Kind::Class)
            return classImport(static_cast<const VmClass*>(s));
        const VmClass* owner = s->owner;
        if (!owner || !owner->package)
            return 0;
        return w.importIn(classImport(owner), s->name, "Core", "State");
    };
    // An actor's state frame: its code's node and its state, the events it
    // listens for (its state's and class's, less those it ignores or has
    // disabled; all, for one that never began), its latent action and
    // where its code is.
    auto frame = [&](ByteWriter& b, VmObject* a) {
        const VmState* state = a->state ? a->state : a->cls;
        const VmState* node = a->codeState && a->codeAt >= 0 ? a->codeState : state;
        b.index(stateRef(node));
        b.index(stateRef(state));
        uint64_t probe = (state->probeMask | a->cls->probeMask) & state->ignoreMask;
        if (const Value* begun = prop(a, "bHasBegunPlay"); begun && !begun->asBool())
            probe = ~0ull;
        for (NameId n : a->disabled)
            for (int k = 0; k < 64; ++k)
                if (fs::toLower(vm_.nameText(n)) == fs::toLower(kProbes[k]))
                    probe &= ~(1ull << k);
        b.u32(uint32_t(probe));
        b.u32(uint32_t(probe >> 32));
        int32_t offset = -1, latent = 0;
        if (a->codeState && a->codeAt >= 0) {
            // The statement it's at; waiting in Sleep, the time left goes
            // in LatentFloat; waiting in anything else, the call before it
            // is made again.
            int want = a->codeAt;
            if (a->latent && a->sleeping <= 0 && want > 0)
                --want;
            if (a->latent && a->sleeping > 0)
                latent = 256;
            for (const auto& [off, statement] : a->codeState->statementAt)
                if (statement == want)
                    offset = int32_t(off);
        }
        b.i32(latent);
        b.index(offset);
    };

    // A C3D mesh state for each saved actor with a C3D mesh and none the
    // map gave it (one spawned): new, its groups as below, no IK targets,
    // bone adjustments or attachments (the scripts set them again).
    std::map<const VmObject*, int32_t> newMeshStates;
    for (VmObject* a : saved) {
        if (!a->isActor || a->package == &map_ || !usesC3DMesh(a))
            continue;
        const Value* ms = prop(a, "C3DOurMeshState");
        if (ms && ms->asObject() && refs.count(ms->o))
            continue;
        const int32_t ref = w.addExport(w.import("Engine.C3DMESHSTATE", "Core", "Class"), refs[a],
                                        "C3DMESHSTATE" + std::to_string(++meshStates_), kObjectFlags);
        newMeshStates[a] = ref;
        ByteWriter m;
        m.index(w.name("None"));
        m.u32(0xFFFFDE80u);
        m.i32(2);
        m.i32(22);
        const Value* mesh = prop(a, "Mesh");
        const std::string meshName = mesh && mesh->asObject() ? vm_.nameText(mesh->o->name) : "";
        m.u16(uint16_t(meshName.size()));
        m.bytes(meshName.data(), meshName.size());
        m.i32(0);
        m.i32(0); // IK targets
        m.i32(0);
        m.i32(5);
        m.i32(-1);
        writeMeshGroups(m, a);
        for (int k = 0; k < 2; ++k) { // bone adjustments, attachments
            m.i32(0);
            m.i32(0);
            m.i32(10);
        }
        const std::string owner = vm_.nameText(a->name);
        m.u16(uint16_t(owner.size()));
        m.bytes(owner.data(), owner.size());
        w.setData(ref, std::move(m.data));
    }

    // The objects' data.
    for (VmObject* o : saved) {
        const int32_t ref = refs[o];
        if (o->package == &map_ && !o->isActor) {
            // A mesh state: its groups as the actor's animation has them.
            if (o->cls && fs::toLower(o->cls->name) == "c3dmeshstate")
                if (const Value* outer = prop(o, "Outer"); outer && outer->asObject() && outer->o->isActor) {
                    std::vector<uint8_t> data;
                    size_t begin = 0, end = 0;
                    if (!map_.readExport(o->exportIndex, data) && meshStateGroupSpan(data, begin, end)) {
                        ByteWriter g;
                        writeMeshGroups(g, outer->o);
                        std::vector<uint8_t> out(data.begin(), data.begin() + std::ptrdiff_t(begin));
                        out.insert(out.end(), g.data.begin(), g.data.end());
                        out.insert(out.end(), data.begin() + std::ptrdiff_t(end), data.end());
                        w.setData(ref, std::move(out));
                    }
                }
            continue;
        }
        ByteWriter b;
        // (Its new mesh state, where it gets one.)
        Value meshStateWas;
        Value* meshState = nullptr;
        if (auto it = newMeshStates.find(o); it != newMeshStates.end())
            if ((meshState = prop(o, "C3DOurMeshState"))) {
                meshStateWas = *meshState;
                refs[&meshStateMarker_] = it->second;
                meshState->type = VType::Object;
                meshState->o = &meshStateMarker_;
            }
        if (o->isActor) {
            if (o->latent && o->sleeping > 0)
                if (Value* v = prop(o, "LatentFloat"))
                    *v = Value::floatValue(o->sleeping);
            frame(b, o);
            w.setFlags(ref, kActorFlags);
        }
        properties(b, o);
        if (meshState)
            *meshState = meshStateWas;
        w.setData(ref, std::move(b.data));
    }

    // The Level object.
    const std::vector<int> levels = map_.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(map_.name() + ": no Level object");
    std::vector<uint8_t> mapLevel;
    if (Error err = map_.readExport(levels.front(), mapLevel))
        return err;
    size_t modelBegin = 0, reachEnd = 0;
    if (!levelModelSpan(mapLevel, version, modelBegin, reachEnd))
        return Error::fail(map_.name() + ": its Level object doesn't read");
    ByteWriter L;
    L.index(w.name("None"));
    // The actors: the LevelInfo, the builder brush's place (none), the rest.
    std::vector<int32_t> list{refs[levelInfo_], 0};
    for (VmObject* a : actors_)
        if (a != levelInfo_ && refs.count(a) && refs[a] > 0)
            list.push_back(refs[a]);
    L.i32(int32_t(list.size()));
    L.i32(int32_t(list.size()));
    for (int32_t r : list)
        L.index(r);
    // The URL.
    L.string("DS9");
    L.string("");
    L.string(map_.name());
    L.string("");
    std::vector<std::string> options{"Name=Player"};
    if (player_ && player_->cls && player_->cls->package)
        options.push_back("Class=" + player_->cls->package->name() + "." + player_->cls->name);
    L.index(int32_t(options.size()));
    for (const std::string& op : options)
        L.string(op);
    L.i32(7777);
    L.i32(1);
    // The model and the reach list, as the map has them.
    L.bytes(mapLevel.data() + modelBegin, reachEnd - modelBegin);
    const Value* now = prop(levelInfo_, "TimeSeconds");
    L.f32(now ? now->asFloat() : time_);
    L.index(0); // first deleted
    for (int k = 0; k < 16; ++k)
        L.index(0); // text blocks
    L.index(0);     // travel
    // The ghosts.
    std::vector<int32_t> ghosts;
    for (VmObject* g : ghosts_)
        if (refs.count(g) && refs[g] > 0)
            ghosts.push_back(refs[g]);
    L.index(int32_t(ghosts.size()));
    for (int32_t g : ghosts)
        L.index(g);
    writeDialogEngine(L, w);
    // The sounds playing (the music the HUD's, playing since it began).
    struct Entry {
        int32_t actor, id;
        std::string path;
        float volume, radius, pitch, start;
    };
    std::vector<Entry> sounds;
    auto pathOf = [](const ObjectRef& s) { return s.package->name() + "." + s.package->path(s.exportIndex + 1); };
    std::map<int32_t, int> listIndex;
    for (size_t k = 0; k < list.size(); ++k)
        listIndex[list[k]] = int(k);
    int none = 0;
    for (const TrackedSound& t : trackedSounds_) {
        if (t.start + t.seconds < time_ || !t.request.actor || !refs.count(t.request.actor) || !t.request.sound)
            continue;
        const int32_t actor = refs[t.request.actor];
        const int index = listIndex.count(actor) ? listIndex[actor] : 0;
        const int32_t id = t.request.slot == 0 ? -16 * ++none : index * 16 + t.request.slot * 2 + (t.request.noOverride ? 1 : 0);
        sounds.push_back({actor, id, pathOf(t.request.sound), t.request.volume, t.request.radius, t.request.pitch, t.start});
    }
    if (VmObject* music = currentMusic(); music && music->package && musicActor_ && refs.count(musicActor_))
        sounds.push_back({refs[musicActor_], -16 * ++none, pathOf({music->package, music->exportIndex}), music_.volume,
                          1e6f, 1, musicStart_});
    L.index(int32_t(sounds.size()));
    for (const Entry& e : sounds) {
        L.index(e.actor);
        L.i32(e.id);
        L.string(e.path);
        L.f32(e.volume);
        L.f32(e.radius);
        L.f32(e.pitch);
        L.f32(e.start);
    }
    L.i32(0);
    w.setData(levels.front() + 1, std::move(L.data));

    // The map's actors the level no longer has (destroyed, editor-only)
    // stay, unreferenced: the engine loads only what the level leads to.
    return w.write(path);
}

// An actor's C3D animation groups: FullBody (its animation), LowerTorso and
// UpperTorso (its layers), then its face channels.
void ScriptLevel::writeMeshGroups(ByteWriter& b, VmObject* a) {
    auto str16 = [&](const std::string& s) {
        b.u16(uint16_t(s.size()));
        b.bytes(s.data(), s.size());
    };
    struct G {
        std::string group, sequence = "NONACTIVE";
        int32_t source = 0, type = 2, state = 0;
        float frame = 0, rate = 1, weight = 1, fromWeight = 0, fromFrame = 0, elapsed = 0, duration = 1;
        std::string pending;
        float pendingFrame = 0, pendingWeight = 0, pendingTween = 0;
        std::string bone;
    };
    std::vector<G> groups;
    G full;
    full.group = "FullBody";
    if (const Value* s = prop(a, "AnimSequence"); s && s->i && vm_.nameText(s->i) != "None") {
        full.sequence = vm_.nameText(s->i);
        full.source = 1;
        full.type = 1;
        full.state = 1;
        full.duration = 0.1f;
        if (const Value* f = prop(a, "AnimFrame"))
            full.frame = std::max(0.0f, f->asFloat());
    }
    groups.push_back(full);
    const auto* layers = layersOf(a);
    for (const char* part : {"LowerTorso", "UpperTorso"}) {
        G g;
        g.group = part;
        if (layers)
            if (auto it = layers->find(vm_.name(part)); it != layers->end() && it->second.sequence) {
                const AnimLayer& l = it->second;
                g.sequence = vm_.nameText(l.sequence);
                g.source = 1;
                g.type = 1;
                g.state = 1;
                g.frame = std::max(0.0f, l.frame);
                g.rate = l.rate;
                g.weight = l.weight;
                g.duration = 0.1f;
                g.bone = l.bone ? vm_.nameText(l.bone) : "";
            }
        groups.push_back(g);
    }
    if (const SkinChannels* skins = skinsOf(a))
        for (const auto& [tag, c] : *skins) {
            G g;
            g.group = vm_.nameText(tag);
            if (c.sequence) {
                g.sequence = vm_.nameText(c.sequence);
                g.type = 4;
                g.state = c.tweening ? 5 : 0;
                g.frame = c.frame;
                g.weight = c.weight;
                g.fromWeight = c.fromWeight;
                g.fromFrame = c.fromFrame;
                g.elapsed = c.elapsed;
                g.duration = c.duration;
            }
            if (c.pending) {
                g.pending = vm_.nameText(c.pendingSequence);
                g.pendingFrame = c.pendingFrame;
                g.pendingWeight = c.pendingWeight;
                g.pendingTween = c.pendingTween;
            }
            groups.push_back(g);
        }
    b.i32(int32_t(groups.size()));
    b.i32(int32_t(groups.size()));
    b.i32(10);
    for (const G& g : groups) {
        str16(g.group);
        str16(g.sequence);
        b.i32(g.source);
        b.f32(g.frame);
        b.i32(g.type);
        b.i32(g.state);
        b.f32(g.rate);
        b.f32(g.weight);
        b.f32(g.fromWeight);
        b.f32(g.fromFrame);
        b.f32(g.elapsed);
        b.f32(0);
        b.f32(g.duration);
        b.f32(0);
        str16(g.pending);
        b.f32(g.pendingFrame);
        b.f32(g.pendingWeight);
        b.f32(g.pendingTween);
        str16(g.bone);
        b.i32(0);
        b.i32(0);
        b.i32(0);
    }
}

// The dialogue engine as the engine keeps it: set up; its missions, types
// and speakers; its lines in key order (flags: 2 the general table's, 1
// running on from the line before; type's priority; times said; place;
// row in the tables; next and return line's places or 0xFFFF; voice, key,
// text, pause, next and return keys, tag, event, condition; a name; a
// camera; the animations and faces); nothing under way; its faces; its
// cameras; and a count.
void ScriptLevel::writeDialogEngine(ByteWriter& b, PackageWriter& w) {
    loadDialog();
    b.i32(1);
    for (size_t t = 0; t < 3; ++t) {
        b.index(int32_t(dialogHeaders_[t].size()));
        for (const auto& [id, word] : dialogHeaders_[t]) {
            b.u16(uint16_t(id));
            b.string(word);
        }
    }
    const size_t n = dialogLines_.size();
    std::vector<int> place(n, 0xFFFF);
    for (size_t k = 0; k < dialogOrder_.size(); ++k)
        place[size_t(dialogOrder_[k])] = int(k);
    // Their cameras, in the tables' order: each CAMERA read in turn (one
    // with no place keeping the one before's), the same ones once.
    struct Camera {
        int32_t flags = 0;
        float v[12] = {};
        bool operator==(const Camera& o) const { return flags == o.flags && std::memcmp(v, o.v, sizeof v) == 0; }
    };
    std::vector<Camera> cameras;
    std::vector<int> cameraOf(n, 0xFFFF);
    {
        Camera c;
        for (size_t i = 0; i < n; ++i) {
            const std::string& cam = dialogLines_[i].camera;
            if (cam.empty() || dialogRecs_[i].flags == 0)
                continue;
            c.flags = 0;
            c.v[6] = 90;
            c.v[7] = 0;
            std::string inner = cam.substr(cam.find('(') + 1);
            inner = inner.substr(0, inner.rfind(')'));
            std::istringstream parts(inner);
            for (std::string part; parts >> part;) {
                if (part.size() < 3 || part[1] != ':')
                    continue;
                const char key = char(std::toupper(uint8_t(part[0])));
                const std::string value = part.substr(2);
                switch (key) {
                case 'A': c.flags |= 1; break;
                case 'R': c.flags |= 2; break;
                case 'C': c.flags |= 4; break;
                case 'D':
                case 'T': c.flags |= 8; break;
                case 'V': c.flags |= value != "0" ? 16 : 0; break;
                case 'H': c.flags |= value != "0" ? 32 : 0; break;
                default: break;
                }
                if (key == 'A' || key == 'R')
                    std::sscanf(value.c_str(), "%f,%f,%f,%f,%f,%f", &c.v[0], &c.v[1], &c.v[2], &c.v[3], &c.v[4], &c.v[5]);
                else if (key == 'F')
                    c.v[6] = std::strtof(value.c_str(), nullptr);
                else if (key == 'D')
                    c.v[7] = std::strtof(value.c_str(), nullptr);
            }
            auto it = std::find(cameras.begin(), cameras.end(), c);
            cameraOf[i] = int(it - cameras.begin());
            if (it == cameras.end())
                cameras.push_back(c);
        }
    }
    auto nameOr = [&](const std::string& s) { return w.name(s.empty() ? "None" : s); };
    b.index(int32_t(n));
    for (const int i : dialogOrder_) {
        const DialogLine& l = dialogLines_[size_t(i)];
        const DialogRec& r = dialogRecs_[size_t(i)];
        const bool general = fs::toLower(l.table) == "gen_dialogue";
        b.u16(uint16_t((general ? 2 : 0) | (r.runOn ? 1 : 0)));
        b.u16(uint16_t(r.group));
        b.u16(uint16_t(std::min(dialogPlays_[size_t(i)], 0xFFFF)));
        b.u16(uint16_t(place[size_t(i)]));
        b.u16(uint16_t(l.row + 1));
        b.u16(uint16_t(r.next >= 0 ? place[size_t(r.next)] : 0xFFFF));
        b.u16(uint16_t(r.back >= 0 ? place[size_t(r.back)] : 0xFFFF));
        b.string(l.record);
        b.string(r.key);
        b.string(l.rawText);
        b.f32(l.pause);
        b.string(r.nextPattern);
        b.string(r.backPattern);
        b.index(nameOr(r.tag));
        b.string(r.event);
        // (Kept as the engine's reading leaves it: brackets and commas spaces.)
        std::string condition = r.condition.empty() ? std::string() : l.rawCondition;
        for (char& ch : condition)
            if (ch == '(' || ch == ')' || ch == ',')
                ch = ' ';
        b.string(condition);
        b.index(w.name("None"));
        b.u16(uint16_t(cameraOf[size_t(i)]));
        b.index(nameOr(l.speakerAnim));
        b.index(nameOr(l.listenerAnim));
        b.index(nameOr(l.speakerFace));
        b.index(nameOr(l.listenerFace));
    }
    b.index(0); // lines being said
    b.index(0); // lines waiting
    b.index(int32_t(dialogHeaders_[3].size()));
    for (const auto& [id, word] : dialogHeaders_[3]) {
        b.u16(uint16_t(id));
        b.string(word);
    }
    // A camera: its flags, then where (A or R: x, y, z, then pitch, yaw,
    // roll in degrees), its field of view and distance.
    b.index(int32_t(cameras.size()));
    for (const Camera& c : cameras) {
        b.i32(c.flags);
        for (float f : c.v)
            b.f32(f);
    }
    b.i32(cameras.empty() ? 0 : int32_t(cameras.size()) - 1);
}

} // namespace reluva::unreal
