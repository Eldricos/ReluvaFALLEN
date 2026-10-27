// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Fallen's dialogue, for a running level (ScriptLevel): its lines read
// from the game's dialogue tables (dialog_table.h), those of this mission
// and player picked by type, speaker, listener and tag with their
// conditions met (FindBestDialogLine), and played (StartDialog): each line
// said for as long as its voice lasts, the speaker and listener set for
// the scripts to see (Level.Talker...), the line shown on the HUD
// (HudText), the speaker's mouth moved (C3DSpeechEvent), its events set
// off, then the line it leads to. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "engines/unreal/package.h"
#include "engines/unreal/properties.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace reluva::unreal {

namespace {

std::string upper(std::string s) {
    for (char& c : s)
        c = char(std::toupper(uint8_t(c)));
    return s;
}

bool same(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

// The general lines' missions: for everyone (GEN), any mission (MEN), or
// one of the three players in any (KEN, SEN, WEN).
bool generalMission(const std::string& m) { return m.size() == 3 && m.substr(1) == "EN"; }

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

} // namespace

// The tables from the game's int folder (beside System), and which lines
// are this level's: those of its own table (LevelInfo's DialogFile) and the
// general one, or of every table if it names none; and of those, the ones
// for whoever the player plays. A line's mission is a letter and a number:
// K, S or W for Kira's, Sisko's or Worf's own, G or M for anyone's; the
// number the mission (01...), OP for Ops, PM the Promenade, EN any. The
// missions follow the story rather than the maps (the Promenade opens with
// a mission 11 line), so the number only makes a line preferred: the map's
// own (M01_KiraL1A's 01, CM05_Kira's 05).
void ScriptLevel::loadDialog() {
    if (dialogLoaded_)
        return;
    dialogLoaded_ = true;
    const std::filesystem::path system = vm_.packages().iniFile().parent_path();
    for (const std::filesystem::path& folder : {system.parent_path() / "int", system.parent_path() / "Int", system})
        if (!readDialogTables(folder, dialogLines_))
            break;
    dialogPlays_.assign(dialogLines_.size(), 0);
    // Lines only ever reached from another (the middle of a conversation)
    // aren't picked to start one.
    dialogFollows_.assign(dialogLines_.size(), false);
    for (const DialogLine& l : dialogLines_)
        for (const std::string& to : {l.next, l.back})
            if (!to.empty())
                for (size_t i = 0; i < dialogLines_.size(); ++i)
                    if (dialogKeyMatches(to, dialogLines_[i].key()))
                        dialogFollows_[i] = true;
    const std::string map = upper(map_.name());
    if (map.size() >= 3 && map[0] == 'M' && std::isdigit(uint8_t(map[1])) && std::isdigit(uint8_t(map[2])))
        dialogNumber_ = map.substr(1, 2);
    else if (map.size() >= 4 && map.compare(0, 2, "CM") == 0 && std::isdigit(uint8_t(map[2])) && std::isdigit(uint8_t(map[3])))
        dialogNumber_ = map.substr(2, 2);
    else if (map == "DS9_OPS")
        dialogNumber_ = "OP";
    else if (map == "DS9_PROMENADE")
        dialogNumber_ = "PM";
    if (const Value* tag = player_ ? prop(player_, "Speaker_Tag") : nullptr) {
        const std::string t = upper(vm_.nameText(tag->i));
        dialogWho_ = t == "KRA" ? 'K' : t == "SKO" ? 'S' : t == "WRF" ? 'W' : 0;
    }
    const Value* file = levelInfo_ ? prop(levelInfo_, "DialogFile") : nullptr;
    const std::string own = file ? upper(file->s) : std::string();
    dialogOurs_.assign(dialogLines_.size(), false);
    for (size_t i = 0; i < dialogLines_.size(); ++i) {
        const DialogLine& l = dialogLines_[i];
        const std::string table = upper(l.table), mission = upper(l.mission);
        const bool tableFits = own.empty() || table == own || table == "GEN_DIALOGUE";
        const bool whoFits = mission.size() == 3 &&
                             (mission[0] == 'G' || mission[0] == 'M' || !dialogWho_ || mission[0] == dialogWho_);
        dialogOurs_[i] = tableFits && whoFits;
    }
}

const std::vector<DialogLine>& ScriptLevel::dialogLines() {
    loadDialog();
    return dialogLines_;
}

VmObject* ScriptLevel::dialogInput() {
    const Value* v = levelInfo_ ? prop(levelInfo_, "DialogInput") : nullptr;
    VmObject* o = v ? v->asObject() : nullptr;
    return o && !o->deleted ? o : nullptr;
}

// A condition from the tables: tests joined by "and" and "or" ("and" first),
// each maybe after "!", each a name and maybe arguments in brackets
// ("SpecialCondition( InTemple ) and ! CommunicationBlocked"). A test is the
// dialogue input's event of that name (CTVE_DialogInput: LowHealth,
// HasItem...), its arguments as its parameters take them; DS9Phase(n) is
// the player's OpsPhase being n (as OpsLevel takes it). One it hasn't is
// false. One in brackets is a note to the writers ("( Use CTVE_PlayerSpeech )"):
// met.
bool ScriptLevel::testCondition(const std::string& condition) {
    if (!condition.empty() && condition.front() == '(')
        return true;
    std::vector<std::string> tokens;
    for (size_t i = 0; i < condition.size();) {
        const char c = condition[i];
        if (std::isspace(uint8_t(c))) {
            ++i;
        } else if (c == '(' || c == ')' || c == ',' || c == '!') {
            tokens.push_back(std::string(1, c));
            ++i;
        } else {
            size_t j = i;
            while (j < condition.size() && !std::isspace(uint8_t(condition[j])) &&
                   !std::strchr("(),!", condition[j]))
                ++j;
            tokens.push_back(condition.substr(i, j - i));
            i = j;
        }
    }
    size_t at = 0;
    auto test = [&]() {
        bool negate = false;
        while (at < tokens.size() && tokens[at] == "!") {
            negate = !negate;
            ++at;
        }
        if (at >= tokens.size())
            return negate;
        const std::string name = tokens[at++];
        std::vector<std::string> args;
        if (at < tokens.size() && tokens[at] == "(") {
            ++at;
            std::string arg;
            while (at < tokens.size() && tokens[at] != ")") {
                if (tokens[at] == ",") {
                    args.push_back(arg);
                    arg.clear();
                } else {
                    arg += arg.empty() ? tokens[at] : " " + tokens[at];
                }
                ++at;
            }
            if (!arg.empty())
                args.push_back(arg);
            ++at; // ")"
        }
        bool result = false;
        if (same(name, "DS9Phase")) {
            const Value* phase = player_ ? prop(player_, "OpsPhase") : nullptr;
            result = phase && !args.empty() && phase->asInt() == std::atoi(args[0].c_str());
        } else if (VmObject* input = dialogInput()) {
            VmFunction* f = vm_.findFunction(input, vm_.name(name));
            if (!f) {
                ++dialog.unknownConditions;
            } else {
                std::vector<Value> values;
                for (size_t k = 0; k < f->params.size() && k < args.size(); ++k) {
                    const VmProperty* p = f->params[k];
                    switch (p->type) {
                    case VType::Byte:
                    case VType::Int: values.push_back(Value::intValue(std::atoi(args[k].c_str()))); break;
                    case VType::Float: values.push_back(Value::floatValue(std::strtof(args[k].c_str(), nullptr))); break;
                    case VType::Bool: values.push_back(Value::boolValue(same(args[k], "true"))); break;
                    case VType::Name: values.push_back(Value::nameValue(vm_.name(args[k]))); break;
                    case VType::String: values.push_back(Value::stringValue(args[k])); break;
                    default: values.push_back(p->zero()); break;
                    }
                }
                result = vm_.callEvent(input, name, values).asBool();
            }
        }
        return negate ? !result : result;
    };
    // a and b or c: (a and b) or c.
    bool any = false;
    while (at < tokens.size()) {
        bool all = test();
        while (at < tokens.size() && same(tokens[at], "and")) {
            ++at;
            all = test() && all;
        }
        any = any || all;
        if (at < tokens.size() && same(tokens[at], "or"))
            ++at;
        else
            break;
    }
    return tokens.empty() || any;
}

// The lines that would do: this level's (or, asked for by tag, which
// names one line, any table's and any player's: a cut scene of Kira's
// starts with a line marked as Worf's mission, START_K10A2, from another
// mission's table), the type, each of the tag, listener, speaker and line
// ID asked for (none asked, any), their conditions met; a line only reached from another only if its ID's asked
// for. The best: one of this mission's over a general one, then the one
// said fewest times, then any of those. Returns how many would do.
int ScriptLevel::findDialogLine(const std::string& type, NameId filterTag, NameId listener, NameId speaker,
                                const std::string& lineId, int& index) {
    loadDialog();
    index = -1;
    auto given = [&](NameId n) { return n != 0 && !same(vm_.nameText(n), "None"); };
    const std::string tag = given(filterTag) ? vm_.nameText(filterTag) : "";
    const std::string to = given(listener) ? vm_.nameText(listener) : "";
    const std::string by = given(speaker) ? vm_.nameText(speaker) : "";
    std::vector<int> matches;
    for (size_t i = 0; i < dialogLines_.size(); ++i) {
        const DialogLine& l = dialogLines_[i];
        if ((!dialogOurs_[i] && tag.empty()) || !same(l.type, type) || same(l.tag, "DoNotPlay"))
            continue;
        if ((!tag.empty() && !same(l.tag, tag)) || (!to.empty() && !same(l.listener, to)) ||
            (!by.empty() && !same(l.speaker, by)) || (!lineId.empty() && !same(l.lineId, lineId)))
            continue;
        if (lineId.empty() && dialogFollows_[i])
            continue;
        if (!l.condition.empty() && !testCondition(l.condition))
            continue;
        matches.push_back(int(i));
    }
    index = bestDialogLine(matches);
    return int(matches.size());
}

int ScriptLevel::bestDialogLine(const std::vector<int>& matches) {
    std::vector<int> best;
    auto rank = [&](int i) {
        const DialogLine& l = dialogLines_[size_t(i)];
        const bool elsewhere = dialogNumber_.empty() || l.mission.size() != 3 || upper(l.mission.substr(1)) != dialogNumber_;
        return std::make_tuple(elsewhere, generalMission(l.mission), dialogPlays_[size_t(i)]);
    };
    for (const int i : matches) {
        if (!best.empty() && rank(i) > rank(best.front()))
            continue;
        if (!best.empty() && rank(i) < rank(best.front()))
            best.clear();
        best.push_back(i);
    }
    if (best.empty())
        return -1;
    dialogRandom_ = dialogRandom_ * 1103515245u + 12345u;
    return best[(dialogRandom_ >> 16) % best.size()];
}

bool ScriptLevel::dialogLineSpecial(int index) {
    loadDialog();
    return index >= 0 && size_t(index) < dialogLines_.size() && !generalMission(dialogLines_[size_t(index)].mission);
}

int ScriptLevel::startDialog(VmObject* self, const std::string& type, NameId filterTag, NameId listener,
                             NameId speaker, const std::string& lineId) {
    int index = -1;
    findDialogLine(type, filterTag, listener, speaker, lineId, index);
    if (index < 0) {
        ++dialog.notFound;
        return 0;
    }
    DialogThread t;
    t.id = nextDialogThread_++;
    t.self = self;
    dialogThreads_.push_back(t);
    ++dialog.started;
    beginLine(dialogThreads_.back(), index);
    return t.id;
}

bool ScriptLevel::dialogPlaying(int thread) const {
    return std::any_of(dialogThreads_.begin(), dialogThreads_.end(), [&](const DialogThread& t) { return t.id == thread; });
}

// How long a line's voice lasts: its sound (the record's name in the
// table's .uax) is a WAV file, its data chunk's size over its bytes a
// second. Without one, as long as reading it takes, about 15 letters a
// second.
float ScriptLevel::lineSeconds(const DialogLine& l) {
    const std::string key = l.table + "." + l.record;
    auto known = dialogSeconds_.find(key);
    if (known != dialogSeconds_.end())
        return known->second;
    float seconds = 0.5f + float(l.text.size()) / 15;
    if (const Package* p = vm_.packages().get(l.table)) {
        const int e = p->findExport(l.record, "Sound");
        std::vector<uint8_t> data;
        if (e >= 0 && !p->readExport(e, data)) {
            const uint8_t riff[4] = {'R', 'I', 'F', 'F'};
            auto at = std::search(data.begin(), data.end(), riff, riff + 4);
            uint32_t rate = 0, bytes = 0;
            for (size_t i = size_t(at - data.begin()) + 12; at != data.end() && i + 8 <= data.size();) {
                const uint32_t size = le32(&data[i + 4]);
                if (std::memcmp(&data[i], "fmt ", 4) == 0 && i + 16 <= data.size())
                    rate = le32(&data[i + 16]); // average bytes a second
                if (std::memcmp(&data[i], "data", 4) == 0) {
                    bytes = size;
                    break;
                }
                i += 8 + size + (size & 1);
            }
            if (rate > 0 && bytes > 0)
                seconds = float(bytes) / float(rate);
        }
    }
    dialogSeconds_[key] = seconds;
    return seconds;
}

// A line begun: who says it and to whom (the actor's GetSpeakerActor for
// their codes: one near the camera, or the HUD's picture of someone on the
// communicator), the level's talker, listener and line for the scripts to
// see, its event set off (the dialogue input's DoSpecialEvent and DoEvent),
// the speaker's and listener's animations and the speaker's face, and the
// line on the HUD for as long as it's said.
void ScriptLevel::beginLine(DialogThread& t, int index) {
    const DialogLine& l = dialogLines_[size_t(index)];
    ++dialogPlays_[size_t(index)];
    ++dialog.lines;
    t.line = index;
    t.seconds = lineSeconds(l);
    t.timeLeft = t.seconds;
    t.pauseLeft = std::max(0.0f, l.pause);
    t.mouthTimer = 0;
    VmObject* asker = t.self && !t.self->deleted ? t.self : levelInfo_;
    auto actorFor = [&](const std::string& code) -> VmObject* {
        if (code.empty() || !asker)
            return nullptr;
        VmObject* a = vm_.callEvent(asker, "GetSpeakerActor", {Value::nameValue(vm_.name(code))}).asObject();
        return a && !a->deleted ? a : nullptr;
    };
    t.speaker = actorFor(l.speaker);
    t.listener = actorFor(l.listener);
    auto pawnOrNone = [&](VmObject* a) { return Value::objectValue(a && vm_.isA(a, "Pawn") ? a : nullptr); };
    if (Value* v = prop(levelInfo_, "Talker"))
        *v = pawnOrNone(t.speaker);
    if (Value* v = prop(levelInfo_, "Listener"))
        *v = pawnOrNone(t.listener);
    if (Value* v = prop(levelInfo_, "CurrentLineType"))
        *v = Value::nameValue(vm_.name(l.type));
    if (Value* v = prop(levelInfo_, "CurrentLineIndex"))
        *v = Value::intValue(index);
    if (Value* v = prop(levelInfo_, "CurrentLineFlags"))
        *v = Value::intValue(0);
    if (!l.event.empty())
        if (VmObject* input = dialogInput()) {
            vm_.callEvent(input, "DoSpecialEvent", {Value::stringValue(l.event)});
            vm_.callEvent(input, "DoEvent", {Value::nameValue(vm_.name(l.event))});
        }
    for (const auto& [who, anim] : {std::pair{t.speaker, l.speakerAnim}, std::pair{t.listener, l.listenerAnim}})
        if (who && !anim.empty() && !who->deleted)
            vm_.callEvent(who, "PlayDialogAnim", {Value::nameValue(vm_.name(anim)), Value::floatValue(1), Value::floatValue(0.2f)});
    for (const auto& [who, face] : {std::pair{t.speaker, l.speakerFace}, std::pair{t.listener, l.listenerFace}}) {
        Value* v = who && !face.empty() ? prop(who, "FacialExpression") : nullptr;
        const VmProperty* p = v ? who->cls->findProperty(vm_.name("FacialExpression")) : nullptr;
        if (p && p->enumNames)
            for (size_t k = 0; k < p->enumNames->size(); ++k)
                if (same(vm_.nameText((*p->enumNames)[k]), face))
                    v->i = int(k);
    }
    const Value* hud = player_ ? prop(player_, "myHUD") : nullptr;
    if (hud && hud->asObject() && !l.text.empty())
        vm_.callEvent(hud->o, "HudText",
                      {Value::stringValue(l.text), Value::floatValue(t.seconds), Value::nameValue(vm_.name(l.speaker)),
                       Value::objectValue(t.speaker)});
}

// Each line on by a tick: while it's said, the speaker's mouth moves
// (C3DSpeechEvent, a shape a tenth of a second from the letters said then:
// Reluva's own, the game's lip-sync data isn't read); then its pause; then
// the line it leads to (one of those its key fits, by the same choice as
// FindBestDialogLine's), or the end: the level's talker and listener
// cleared.
void ScriptLevel::tickDialog(float seconds) {
    for (size_t k = 0; k < dialogThreads_.size();) {
        DialogThread& t = dialogThreads_[k];
        const DialogLine& l = dialogLines_[size_t(t.line)];
        VmObject* speaker = t.speaker && !t.speaker->deleted ? t.speaker : nullptr;
        if (t.timeLeft > 0) {
            t.timeLeft -= seconds;
            t.mouthTimer -= seconds;
            if (speaker && t.mouthTimer <= 0 && t.timeLeft > 0) {
                t.mouthTimer = 0.1f;
                const float through = 1 - t.timeLeft / std::max(0.01f, t.seconds);
                const size_t at = std::min(l.text.size() - (l.text.empty() ? 0 : 1), size_t(through * float(l.text.size())));
                const char c = l.text.empty() ? ' ' : char(std::tolower(uint8_t(l.text[at])));
                const int shape = std::strchr("aeiouy", c) && c ? 1 + (c - 'a') % 4 : std::isalpha(uint8_t(c)) ? 1 : 0;
                vm_.callEvent(speaker, "C3DSpeechEvent", {Value::intValue(shape), Value::floatValue(shape ? 1.0f : 0.0f)});
            }
            if (t.timeLeft <= 0 && speaker)
                vm_.callEvent(speaker, "C3DSpeechEvent", {Value::intValue(0), Value::floatValue(0)});
            ++k;
            continue;
        }
        if (t.pauseLeft > 0) {
            t.pauseLeft -= seconds;
            ++k;
            continue;
        }
        int next = -1;
        if (!l.next.empty()) {
            std::vector<int> matches;
            for (size_t i = 0; i < dialogLines_.size(); ++i) {
                const DialogLine& n = dialogLines_[i];
                if (dialogKeyMatches(l.next, n.key()) && (n.condition.empty() || testCondition(n.condition)))
                    matches.push_back(int(i));
            }
            next = bestDialogLine(matches);
        }
        if (next >= 0) {
            beginLine(t, next);
            ++k;
            continue;
        }
        // The end: the talker and listener it set cleared, if still its.
        if (Value* v = prop(levelInfo_, "Talker"); v && v->asObject() == t.speaker)
            *v = Value::objectValue(nullptr);
        if (Value* v = prop(levelInfo_, "Listener"); v && v->asObject() == t.listener)
            *v = Value::objectValue(nullptr);
        if (dialogThreads_.size() == 1) {
            if (Value* v = prop(levelInfo_, "CurrentLineType"))
                *v = Value::nameValue(vm_.name("None"));
            if (Value* v = prop(levelInfo_, "CurrentLineIndex"))
                *v = Value::intValue(-1);
        }
        dialogThreads_.erase(dialogThreads_.begin() + std::ptrdiff_t(k));
    }
}

std::vector<ScriptLevel::Subtitle> ScriptLevel::subtitles() const {
    std::vector<Subtitle> out;
    for (const DialogThread& t : dialogThreads_)
        if (t.timeLeft > 0) {
            const DialogLine& l = dialogLines_[size_t(t.line)];
            out.push_back({l.speaker, l.text, l.type});
        }
    return out;
}

void registerDialogNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto optionalName = [](NativeCall& c, size_t k) { return c.given.size() > k && c.given[k] ? c.i(k) : 0; };
    auto optionalString = [](NativeCall& c, size_t k) { return c.given.size() > k && c.given[k] ? c.s(k) : std::string(); };
    // FindBestDialogLine(out int Index, string Type, optional name FilterTag, optional name ListenerTag,
    // optional name SpeakerTag, optional string LineID): how many lines would do, the best in Index.
    reg("Actor.FindBestDialogLine", [=](NativeCall& c) {
        int index = -1;
        const int n = c.vm.level ? c.vm.level->findDialogLine(c.s(1), optionalName(c, 2), optionalName(c, 3),
                                                               optionalName(c, 4), optionalString(c, 5), index)
                                 : 0;
        c.args[0] = Value::intValue(index);
        c.result = Value::intValue(n);
    });
    // IsDialogLineSpecial(int Index): one of this mission's own lines, not a
    // general one (the HUD shows that a crewmate has something to say).
    reg("Actor.IsDialogLineSpecial", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.vm.level->dialogLineSpecial(c.i(0)));
    });
    // StartDialog(string Type, optional name FilterTag, optional name ListenerTag, optional name SpeakerTag,
    // optional string LineID): the best line played, and those it leads to; the thread's number, 0 if none.
    reg("Actor.StartDialog", [=](NativeCall& c) {
        c.result = Value::intValue(c.vm.level ? c.vm.level->startDialog(c.self, c.s(0), optionalName(c, 1),
                                                                        optionalName(c, 2), optionalName(c, 3),
                                                                        optionalString(c, 4))
                                              : 0);
    });
    reg("Actor.IsDialogThreadPlaying", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.vm.level->dialogPlaying(c.i(0)));
    });
    reg("Actor.TestCondition", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.vm.level->testCondition(c.s(0)));
    });
    // DialogTick(float DeltaTime): the dialogue input's each tick, while the
    // HUD lets it. The lines play on the level's own tick (cut scenes'
    // too, which this isn't called in); what else the engine did here (the
    // AI's own lines, it may be) isn't known.
    reg("CTVE_DialogInput.DialogTick", [](NativeCall&) {});
}

} // namespace reluva::unreal
