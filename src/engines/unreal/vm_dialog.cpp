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
#include <cmath>
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

// The tables from the game's int folder (intFolder), and which lines
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
    if (readDialogTables(intFolder(), dialogLines_))
        dialogLines_.clear();
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
                // An actor parameter is the line's speaker (the AI's own
                // lines: "FriendClose( 500 )", "PlayerFaceEnemy"), not
                // written in the table.
                std::vector<Value> values;
                size_t given = 0;
                for (size_t k = 0; k < f->params.size(); ++k) {
                    const VmProperty* p = f->params[k];
                    if (p->type == VType::Object) {
                        values.push_back(Value::objectValue(conditionSubject_));
                        continue;
                    }
                    if (given >= args.size())
                        break;
                    const std::string& arg = args[given++];
                    switch (p->type) {
                    case VType::Byte:
                    case VType::Int: values.push_back(Value::intValue(std::atoi(arg.c_str()))); break;
                    case VType::Float: values.push_back(Value::floatValue(std::strtof(arg.c_str(), nullptr))); break;
                    case VType::Bool: values.push_back(Value::boolValue(same(arg, "true"))); break;
                    case VType::Name: values.push_back(Value::nameValue(vm_.name(arg))); break;
                    case VType::String: values.push_back(Value::stringValue(arg)); break;
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
        if (tag.empty() && !ofThisMission(l))
            continue;
        if (!l.condition.empty() && !testCondition(l.condition))
            continue;
        matches.push_back(int(i));
    }
    // A tag names one line: asked for under another type than the table
    // gives it (the training room's TRAIN01, asked as TRN, is an EVT
    // line), it's still that line.
    if (matches.empty() && !tag.empty())
        for (size_t i = 0; i < dialogLines_.size(); ++i) {
            const DialogLine& l = dialogLines_[i];
            if (same(l.tag, tag) && (l.condition.empty() || testCondition(l.condition)))
                matches.push_back(int(i));
        }
    index = bestDialogLine(matches);
    return int(matches.size());
}

// The best line of a type said by `speaker` (a code), to `listener` (a
// code; with `orUnaddressed`, a line to no one does too), its conditions
// met; with `anyPlace`, one only reached from another may be picked too
// (the generic answers and sign-offs a hail ends with). With
// `needsFirst`, one with a condition (a need: low on ammunition) over
// one without.
int ScriptLevel::pickDialogLine(const std::string& type, const std::string& speaker, const std::string& listener,
                                bool orUnaddressed, bool anyPlace, bool needsFirst, const std::string* actorTag) {
    loadDialog();
    std::vector<int> matches, needs;
    for (size_t i = 0; i < dialogLines_.size(); ++i) {
        const DialogLine& l = dialogLines_[i];
        if (!dialogOurs_[i] || !same(l.type, type) || same(l.tag, "DoNotPlay") || !same(l.speaker, speaker))
            continue;
        // One actor's own (its Dialogue_Tag the line's tag), and the
        // start of what it says, not the lines that run on from it.
        if (actorTag && ((!l.tag.empty() && !same(l.tag, *actorTag)) || runsOn(int(i))))
            continue;
        if (!ofThisMission(l))
            continue;
        if (!listener.empty() && !same(l.listener, listener) && !(orUnaddressed && l.listener.empty()))
            continue;
        if (!anyPlace && dialogFollows_[i])
            continue;
        if (!l.condition.empty() && !testCondition(l.condition))
            continue;
        matches.push_back(int(i));
        if (!l.condition.empty())
            needs.push_back(int(i));
    }
    return bestDialogLine(needsFirst && !needs.empty() ? needs : matches);
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
    // A hail with nothing of this mission's to say: the player's own
    // options to whoever it hails (HOP: a need its conditions show, like
    // ammunition or medical supplies, else asking for a report), then the
    // answer (its NEXTLINE, else that crewmate's report, HBO), then the
    // player signing off (its RETURNLINE, else HCL).
    DialogThread t;
    const std::string me = playerSpeaker();
    const std::string to = listener && !same(vm_.nameText(listener), "None") ? vm_.nameText(listener) : "";
    if (same(type, "HAL") && !to.empty() && !me.empty() && filterTag == 0 && lineId.empty() &&
        (index < 0 || !dialogLineSpecial(index))) {
        const int option = pickDialogLine("HOP", me, to, false, false, true);
        if (option >= 0) {
            index = option;
            const DialogLine& l = dialogLines_[size_t(option)];
            if (l.next.empty())
                t.followUps.push_back({"HBO", to, me});
            if (l.back.empty())
                t.followUps.push_back({"HCL", me, ""});
        }
    }
    if (index < 0) {
        ++dialog.notFound;
        return 0;
    }
    t.id = nextDialogThread_++;
    t.self = self;
    dialogThreads_.push_back(t);
    ++dialog.started;
    beginLine(dialogThreads_.back(), index);
    return t.id;
}

// A line that runs on from the one numbered just before it, of the same
// table, mission, type and speaker, with no tag of its own or the same:
// Kira's log on the Promenade, START_K11A3 (010) then "I haven't been able
// to sleep..." (011); a Vorta's "...Prisoners A-2..." (000) then "Dispatch
// guards..." (001). A line with no NEXTLINE goes on to one so.
int ScriptLevel::runOnFrom(int index) {
    const DialogLine& l = dialogLines_[size_t(index)];
    if (l.lineId.size() != 3 || !std::isdigit(uint8_t(l.lineId[2])) || l.lineId[2] == '0')
        return -1;
    std::string before = l.lineId;
    --before[2];
    for (size_t i = 0; i < dialogLines_.size(); ++i) {
        const DialogLine& o = dialogLines_[i];
        if (o.lineId == before && same(o.mission, l.mission) && same(o.type, l.type) && same(o.speaker, l.speaker) &&
            (l.tag.empty() || same(o.tag, l.tag)) && same(o.table, l.table))
            return int(i);
    }
    return -1;
}

bool ScriptLevel::runsOn(int index) { return runOnFrom(index) >= 0; }

// Whether a line, asked for by type rather than by its tag, may be said
// here: one of this map's mission or a general one, or another mission's
// whose conditions tie it to the story (DS9Phase...). Without the last,
// a hail on M06 got mission 10's "Aye, sir.", and M04's guards M10's.
bool ScriptLevel::ofThisMission(const DialogLine& l) {
    if (dialogNumber_.empty() || generalMission(l.mission) || !l.condition.empty())
        return true;
    return l.mission.size() == 3 && upper(l.mission.substr(1)) == dialogNumber_;
}

// The player's speaker code ("KRA").
std::string ScriptLevel::playerSpeaker() {
    const Value* tag = player_ ? prop(player_, "Speaker_Tag") : nullptr;
    return tag && tag->i ? vm_.nameText(tag->i) : std::string();
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
    currentLine_ = index;
    // Its voice: the Sound of its record in the package of its table's
    // name, heard everywhere (lines are meant to be heard, over the
    // communicator too), one at a time.
    if (const Package* p = vm_.packages().get(l.table))
        if (const int e = p->findExport(l.record, "Sound"); e >= 0) {
            SoundRequest r;
            r.sound = {p, e};
            r.slot = 5; // SLOT_Talk
            r.speech = true;
            if (soundRequests_.size() >= 256)
                soundRequests_.erase(soundRequests_.begin());
            soundRequests_.push_back(r);
        }
    ++dialog.lines;
    t.line = index;
    t.seconds = lineSeconds(l);
    t.timeLeft = t.seconds;
    t.pauseLeft = std::max(0.0f, l.pause);
    t.mouthTimer = 0;
    // Its CAMERA column's flags: V a voice-over (a log), H heard over the
    // communicator (Level.CurrentLineFlags 16 and 32: the heads don't turn
    // to the talker; the HUD shows whoever's heard).
    int flags = 0;
    {
        std::string cam = upper(l.camera);
        cam.erase(std::remove(cam.begin(), cam.end(), ' '), cam.end());
        if (cam.find("V:1") != std::string::npos)
            flags |= 16;
        if (cam.find("H:1") != std::string::npos)
            flags |= 32;
    }
    // A line given back to (its RETURNLINE) once what it leads to is done.
    if (!l.back.empty())
        t.returns.push_back(l.back);
    VmObject* asker = t.self && !t.self->deleted ? t.self : levelInfo_;
    auto actorFor = [&](const std::string& code, bool speaking) -> VmObject* {
        if (code.empty() || !asker)
            return nullptr;
        // The speaker's an actor with the line's tag (its Dialogue_Tag:
        // the Bajorans who hand over their cards), else the nearest with
        // its code; heard over the communicator, the HUD's picture.
        std::vector<Value> args{Value::nameValue(vm_.name(code))};
        args.push_back(Value::nameValue(speaking && !l.tag.empty() ? vm_.name(l.tag) : vm_.name("None")));
        args.push_back(Value::boolValue(speaking && (flags & 32)));
        VmObject* a = vm_.callEvent(asker, "GetSpeakerActor", args).asObject();
        return a && !a->deleted ? a : nullptr;
    };
    t.speaker = t.forcedSpeaker && !t.forcedSpeaker->deleted ? t.forcedSpeaker : actorFor(l.speaker, true);
    currentSpeaker_ = t.speaker;
    t.listener = actorFor(l.listener, false);
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
        *v = Value::intValue(flags);
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
    // Text all in brackets is a note for the voice actors ("[GROAN IN PAIN
    // #1]", the Grigari's "[Blah, blah, blah]"): heard, not shown.
    const bool note = l.text.size() >= 2 && l.text.front() == '[' && l.text.back() == ']';
    if (hud && hud->asObject() && !l.text.empty() && !note)
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
        conditionSubject_ = t.forcedSpeaker;
        auto byKey = [&](const std::string& key) {
            std::vector<int> matches;
            for (size_t i = 0; i < dialogLines_.size(); ++i) {
                const DialogLine& n = dialogLines_[i];
                if (dialogKeyMatches(key, n.key()) && (n.condition.empty() || testCondition(n.condition)))
                    matches.push_back(int(i));
            }
            return bestDialogLine(matches);
        };
        int next = l.next.empty() ? -1 : byKey(l.next);
        if (next < 0 && l.next.empty())
            for (size_t i = 0; i < dialogLines_.size() && next < 0; ++i)
                if (runOnFrom(int(i)) == t.line &&
                    (dialogLines_[i].condition.empty() || testCondition(dialogLines_[i].condition)))
                    next = int(i);
        // Then back to the line a line before gave (RETURNLINE: the
        // player signing off once a crewmate has answered), then what's
        // left of a hail (its answer, its sign-off).
        while (next < 0 && !t.returns.empty()) {
            const std::string back = t.returns.back();
            t.returns.pop_back();
            next = byKey(back);
        }
        while (next < 0 && !t.followUps.empty()) {
            const DialogThread::FollowUp f = t.followUps.front();
            t.followUps.erase(t.followUps.begin());
            next = pickDialogLine(f.type, f.speaker, f.listener, true, true, false);
        }
        conditionSubject_ = nullptr;
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

// "CAM(R:19,15,35,5,-144,0 F:90)": R a place and turn from the speaker
// (x ahead, y to the side, z up, turned with the speaker's yaw; the turn's
// yaw added to the speaker's), A in the world, in degrees; F the field of
// view, D the distance. V and H (most lines') aren't known, so those lines
// leave the camera to the scripts. Reluva's reading of the column.
bool ScriptLevel::dialogueCamera(std::array<float, 3>& at, std::array<int32_t, 3>& rotation, float& fov,
                                 float& dist) {
    if (currentLine_ < 0 || size_t(currentLine_) >= dialogLines_.size())
        return false;
    std::string cam = dialogLines_[size_t(currentLine_)].camera;
    const size_t open = cam.find('('), close = cam.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open)
        return false;
    cam = cam.substr(open + 1, close - open - 1);
    bool placed = false, relative = false;
    float v[6] = {};
    size_t at0 = 0;
    while (at0 < cam.size()) {
        size_t end = cam.find(' ', at0);
        if (end == std::string::npos)
            end = cam.size();
        const std::string part = cam.substr(at0, end - at0);
        at0 = end + 1;
        if (part.size() < 3 || part[1] != ':')
            continue;
        const char key = char(std::toupper(uint8_t(part[0])));
        const std::string value = part.substr(2);
        if (key == 'A' || key == 'R') {
            int n = std::sscanf(value.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            if (n >= 3) {
                placed = true;
                relative = key == 'R';
            }
        } else if (key == 'F') {
            fov = std::strtof(value.c_str(), nullptr);
        } else if (key == 'D') {
            dist = std::strtof(value.c_str(), nullptr);
        }
    }
    if (!placed)
        return false;
    auto units = [](float degrees) { return int32_t(std::lround(degrees * 65536.0f / 360)); };
    rotation = {units(v[3]), units(v[4]), units(v[5])};
    at = {v[0], v[1], v[2]};
    if (relative) {
        VmObject* s = currentSpeaker_;
        if (!s || s->deleted)
            return false;
        const auto loc = Vm::vectorOf(*prop(s, "Location"));
        const auto rot = Vm::rotatorOf(*prop(s, "Rotation"));
        const float yaw = float(rot[1]) * 6.2831853f / 65536;
        at = {loc[0] + v[0] * std::cos(yaw) - v[1] * std::sin(yaw), loc[1] + v[0] * std::sin(yaw) + v[1] * std::cos(yaw),
              loc[2] + v[2]};
        rotation[1] += rot[1];
    }
    return true;
}

// The AI's own lines, which no script asks for: the engine's. A pawn with
// a speaker code (Speaker_Tag), alive, within earshot of the player, says
// one when its enemy appears (FSE: "Over there!"), when it loses it
// (LSE) or when it's dead (KIL), when it gives up (SUR: its scripts'
// Cowering or Surrender), and now and then while it has no enemy (AMB,
// else REP: chatter, groans). Its own lines only (its code as the
// speaker), their conditions about it ("FriendClose( 500 )"). One at a
// time, not over other dialogue, a few seconds apart for each pawn.
// When the game said them is Reluva's reading: the tables give only what.
void ScriptLevel::tickBarks(float seconds) {
    barkTimer_ -= seconds;
    for (auto& [pawn, b] : barks_)
        b.quiet -= seconds, b.ambient -= seconds;
    if (barkTimer_ > 0 || !player_)
        return;
    barkTimer_ = 0.25f; // looked at four times a second
    const bool talking = !dialogThreads_.empty();
    const Value* at = prop(player_, "Location");
    if (!at)
        return;
    const auto here = Vm::vectorOf(*at);
    const NameId none = vm_.name("None");
    std::uniform_real_distribution<float> unit(0, 1);
    for (VmObject* a : actors_) {
        if (a == player_ || a->deleted || !vm_.isA(a, "Pawn"))
            continue;
        const Value* code = prop(a, "Speaker_Tag");
        const Value* health = prop(a, "Health");
        if (!code || code->i == 0 || code->i == none || !health || health->asInt() <= 0)
            continue;
        const Value* loc = prop(a, "Location");
        if (!loc)
            continue;
        const auto p = Vm::vectorOf(*loc);
        const float dx = p[0] - here[0], dy = p[1] - here[1], dz = p[2] - here[2];
        const bool near = dx * dx + dy * dy + dz * dz < 1500.0f * 1500.0f;
        Bark& b = barks_[a];
        const Value* enemyValue = prop(a, "Enemy");
        VmObject* enemy = enemyValue ? enemyValue->asObject() : nullptr;
        if (enemy && enemy->deleted)
            enemy = nullptr;
        const NameId state = a->state ? vm_.name(a->state->name) : 0;
        std::string say;
        if (enemy && enemy != b.enemy && !b.enemy) {
            say = "FSE";
            b.killed = false;
        } else if (b.enemy && !enemy) {
            const Value* h = b.enemy->deleted ? nullptr : prop(b.enemy, "Health");
            say = (!h || h->asInt() <= 0) ? (b.killed ? "" : "KIL") : "LSE";
        } else if (enemy && !b.killed) {
            const Value* h = prop(enemy, "Health");
            if (h && h->asInt() <= 0) {
                say = "KIL";
                b.killed = true;
            }
        }
        if (state != b.state && a->state &&
            (same(a->state->name, "Cowering") || same(a->state->name, "Surrender")))
            say = "SUR";
        b.enemy = enemy;
        b.state = state;
        if (say.empty() && !enemy && near) {
            if (!b.ambientSet) {
                b.ambient = 10 + 30 * unit(barkRandom_);
                b.ambientSet = true;
            } else if (b.ambient <= 0) {
                b.ambient = 20 + 40 * unit(barkRandom_);
                say = "AMB";
            }
        }
        if (say.empty() || !near || talking || b.quiet > 0)
            continue;
        if (startBark(a, say) || (say == "AMB" && startBark(a, "REP"))) {
            b.quiet = 4;
            return; // one at a time
        }
    }
}

bool ScriptLevel::startBark(VmObject* pawn, const std::string& type) {
    const Value* code = prop(pawn, "Speaker_Tag");
    if (!code)
        return false;
    conditionSubject_ = pawn;
    const Value* own = prop(pawn, "Dialogue_Tag");
    const std::string tag = own && own->i && !same(vm_.nameText(own->i), "None") ? vm_.nameText(own->i) : "";
    const int index = pickDialogLine(type, vm_.nameText(code->i), "", true, false, false, &tag);
    conditionSubject_ = nullptr;
    if (index < 0)
        return false;
    DialogThread t;
    t.id = nextDialogThread_++;
    t.self = pawn;
    t.forcedSpeaker = pawn;
    dialogThreads_.push_back(t);
    ++dialog.barks;
    beginLine(dialogThreads_.back(), index);
    return true;
}

void registerDialogNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto optionalName = [](NativeCall& c, size_t k) { return c.given.size() > k && c.given[k] ? c.i(k) : 0; };
    // GetDialogueCameraData(out Vector Pos, out Rotator Rot, out float FOV,
    // out float Dist): the camera the line being said asks for
    // (ScriptLevel::dialogueCamera); left as they are, false, if none.
    reg("Actor.GetDialogueCameraData", [](NativeCall& c) {
        std::array<float, 3> at{};
        std::array<int32_t, 3> rot{};
        float fov = c.f(2), dist = c.f(3);
        if (!c.vm.level || !c.vm.level->dialogueCamera(at, rot, fov, dist)) {
            c.result = Value::boolValue(false);
            return;
        }
        c.args[0] = c.vm.vectorValue(at[0], at[1], at[2]);
        c.args[1] = c.vm.rotatorValue(rot[0], rot[1], rot[2]);
        c.args[2] = Value::floatValue(fov);
        c.args[3] = Value::floatValue(dist);
        c.result = Value::boolValue(true);
    });
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
