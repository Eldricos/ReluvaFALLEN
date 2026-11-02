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
#include <sstream>

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
                    // A name left out before a number ("LowAmmo( 0.2 )":
                    // any weapon, under a fifth): the name stays None.
                    if (p->type == VType::Name && k + 1 < f->params.size() && args.size() < f->params.size()) {
                        char* end = nullptr;
                        std::strtod(args[given].c_str(), &end);
                        if (end && *end == 0 && !args[given].empty()) {
                            values.push_back(p->zero());
                            continue;
                        }
                    }
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
    // This mission's first, then another's its conditions tie to the
    // story, then the general ones, then another mission's that only
    // happen to fit (M03's hail answered "Stand by, Captain", not M04's
    // "I can't get a signal lock"); the least said of those.
    auto rank = [&](int i) {
        const DialogLine& l = dialogLines_[size_t(i)];
        const bool elsewhere = dialogNumber_.empty() || l.mission.size() != 3 || upper(l.mission.substr(1)) != dialogNumber_;
        const int tier = !elsewhere ? 0 : generalMission(l.mission) ? 2 : l.condition.empty() ? 3 : 1;
        return std::make_pair(tier, dialogPlays_[size_t(i)]);
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
    if (same(type, "HAL") && !to.empty() && !me.empty() && filterTag == 0 && lineId.empty()) {
        // A need (low on ammunition, hurt, poisoned) or the line being
        // blocked comes first, over the mission's own line, which waits
        // for the next hail.
        const int option = pickDialogLine("HOP", me, to, false, false, true);
        const bool need = option >= 0 && !dialogLines_[size_t(option)].condition.empty();
        if (option >= 0 && (need || index < 0 || !dialogLineSpecial(index))) {
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
//
// A cut scene's (CIN) lines are a conversation: they run on from the line
// before them in the table (the next number there, whoever says it, past
// gaps and from 009 to 010) up to the next line with a tag that starts a
// part of it (START_G00A2). A tag naming the line's own speaker (Zoto's
// lines are tagged ZTO, the actor who says them) doesn't. The opening's
// lab, START_G00A: Terell's order, Zoto's "No, Terell! We can't contain
// the Orb's verteron emissions!", Terell's "We're out of time, Zoto..."
// (only the first was said, the scene running on without the rest).
int ScriptLevel::runOnFrom(int index) {
    if (runOnCache_.size() != dialogLines_.size())
        runOnCache_.assign(dialogLines_.size(), -2);
    int& known = runOnCache_[size_t(index)];
    if (known == -2)
        known = findRunOn(index);
    return known;
}

int ScriptLevel::findRunOn(int index) {
    const DialogLine& l = dialogLines_[size_t(index)];
    if (same(l.type, "CIN")) {
        const bool speakersTag = !l.tag.empty() && l.record.size() >= 3 && same(l.tag, l.record.substr(0, 3));
        if (!l.tag.empty() && !speakersTag)
            return -1;
        int before = -1;
        for (size_t i = 0; i < dialogLines_.size(); ++i) {
            const DialogLine& o = dialogLines_[i];
            if (int(i) == index || !same(o.table, l.table) || !same(o.mission, l.mission) || !same(o.type, l.type) ||
                o.lineId.size() != l.lineId.size() || !(o.lineId < l.lineId))
                continue;
            if (before < 0 || dialogLines_[size_t(before)].lineId < o.lineId)
                before = int(i);
        }
        return before;
    }
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
// Whether a line is for whoever the player plays: Kira's, Sisko's or
// Worf's own (K, S, W) only for them ("Stand by, Major" is KEN's).
bool ScriptLevel::forPlayer(const DialogLine& l) const {
    if (!dialogWho_ || l.mission.size() != 3)
        return true;
    const char m = char(std::toupper(uint8_t(l.mission[0])));
    return (m != 'K' && m != 'S' && m != 'W') || m == dialogWho_;
}

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
    if (dialogSaid_.size() >= 256)
        dialogSaid_.erase(dialogSaid_.begin());
    dialogSaid_.push_back(index);
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
    // Its EVENT: words apart by spaces, each a tag to trigger, or with a
    // '-' before it one of the HUD's beam-downs ("-BeamDownAmmo
    // -BeamDownShield"; tags have hyphens inside, "go-ODO-2").
    if (VmObject* input = l.event.empty() ? nullptr : dialogInput()) {
        std::istringstream words(l.event);
        for (std::string w; words >> w;) {
            if (w.size() > 1 && w[0] == '-')
                vm_.callEvent(input, "DoSpecialEvent", {Value::stringValue(w.substr(1))});
            else
                vm_.callEvent(input, "DoEvent", {Value::nameValue(vm_.name(w))});
        }
    }
    // A face in the animation's column (CIN_Dialogue's Obanak lines have
    // FEX_HAPPY, FEX_DETERMINED there and no face) is a face: played as an
    // animation the mesh hasn't got, it left him in his bind pose, arms out.
    auto isFace = [](const std::string& s) { return s.size() > 4 && same(s.substr(0, 4), "FEX_"); };
    for (const auto& [who, anim] : {std::pair{t.speaker, l.speakerAnim}, std::pair{t.listener, l.listenerAnim}})
        if (who && !anim.empty() && !isFace(anim) && !who->deleted)
            vm_.callEvent(who, "PlayDialogAnim", {Value::nameValue(vm_.name(anim)), Value::floatValue(1), Value::floatValue(0.2f)});
    for (const auto& [who, face] :
         {std::pair{t.speaker, !l.speakerFace.empty() || !isFace(l.speakerAnim) ? l.speakerFace : l.speakerAnim},
          std::pair{t.listener, !l.listenerFace.empty() || !isFace(l.listenerAnim) ? l.listenerFace : l.listenerAnim}}) {
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
                if (dialogKeyMatches(key, n.key()) && forPlayer(n) && (n.condition.empty() || testCondition(n.condition)))
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
            // Signed off already (the answer's own return line: "Sisko
            // out."): no second sign-off.
            if (same(f.type, "HCL") && same(l.type, "HCL"))
                continue;
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

// The AI's own lines, which no script asks for: CTVE_DialogInput's native
// DialogTick, each tick the HUD lets dialogue events happen (no letterbox),
// as Engine.dll does it (read for what it does; see unreal_vm.md). Each
// pawn's state is in its own DLG_ variables.
namespace {
// DLG_Flags: a line type this pawn has no lines of at all (found once).
enum : int32_t {
    kLED = 0x1, kSUR = 0x2, kTTP = 0x4, kAMB = 0x8, kBAT = 0x10, kINT = 0x20, kREP = 0x40, kPAN = 0x80,
    kDIE = 0x100, kFSE = 0x200, kLSE = 0x400, kKIL = 0x800, kFHE = 0x1000, kGPI = 0x2000,
};
} // namespace

void ScriptLevel::dialogTick(VmObject* input, float dt) {
    VmObject* player = player_;
    if (!player || player->deleted)
        return;
    auto obj = [&](VmObject* o, const char* name) -> VmObject* {
        const Value* v = o ? prop(o, name) : nullptr;
        VmObject* r = v ? v->asObject() : nullptr;
        return r && !r->deleted ? r : nullptr;
    };
    auto num = [&](VmObject* o, const char* name) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? v->asFloat() : 0.0f;
    };
    auto flag = [&](VmObject* o, const char* name) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v && v->asBool();
    };
    auto text = [&](VmObject* o, const char* name) {
        const Value* v = o ? prop(o, name) : nullptr;
        return v ? upper(vm_.toString(*v)) : std::string();
    };
    if (VmObject* hud = obj(player, "myHUD"); hud && vm_.callEvent(hud, "IsLetterBoxOn").asBool())
        return;
    if (VmObject* start = obj(player, "PlayerStart"); start && flag(start, "bNoAutoTriggerDialog"))
        return;
    const float now = time_;
    const bool noAction = flag(levelInfo_, "bNoActionLevel");
    const auto here = Vm::vectorOf(*prop(player, "Location"));
    const float playerHealth = num(player, "Health");
    std::uniform_real_distribution<float> unit(0, 1);
    auto frand = [&] { return unit(barkRandom_); };
    const NameId none = vm_.name("None");

    const std::vector<VmObject*> pawns = actors_;
    for (VmObject* p : pawns) {
        if (p == player || p->deleted || !vm_.isA(p, "CTVE_Pawn") || flag(p, "bIsPlayer"))
            continue;
        const Value* code = prop(p, "Speaker_Tag");
        if (!code || code->i == 0 || code->i == none)
            continue;
        Value* flagsValue = prop(p, "DLG_Flags");
        if (!flagsValue)
            continue;
        const auto at = Vm::vectorOf(*prop(p, "Location"));
        const std::array<float, 3> toPawn{at[0] - here[0], at[1] - here[1], at[2] - here[2]};
        const float dist = std::sqrt(toPawn[0] * toPawn[0] + toPawn[1] * toPawn[1] + toPawn[2] * toPawn[2]);
        const float health = num(p, "Health");
        VmObject* enemy = obj(p, "Enemy");
        const float lastSpoke = num(p, "DLG_LastSpeakTime");
        // (The engine also needs the pawn's zone drawn in the last 5
        // seconds; Reluva doesn't keep that.)
        if (now - lastSpoke > 0.5f && !flag(p, "bForceStasis") && dist < 800) {
            int32_t flags = flagsValue->asInt();
            if (noAction)
                flags |= ~kTTP;
            *flagsValue = Value::intValue(flags);
            auto open = [&](int32_t bit) { return (flags & bit) == 0; };
            VmObject* oldEnemy = obj(p, "DLG_OldEnemy");
            const float oldHealth = num(p, "DLG_OldHealth");
            const bool friendly = flag(p, "bFriend");
            const std::string command = text(p, "DLG_Command"), oldCommand = text(p, "DLG_OldCommand");
            std::string type;
            int32_t bit = 0;
            bool decided = false; // a type chosen, or nothing to say this tick
            auto pick = [&](const char* t, int32_t b) {
                type = t, bit = b, decided = true;
            };
            if (health <= 0) {
                if (oldHealth > 0 && open(kDIE))
                    pick("DIE", kDIE);
                decided = true;
            } else if (health < oldHealth && open(kPAN)) {
                if (frand() > 0.3f)
                    pick("PAN", kPAN);
                else // a breath instead (FS_MaleBreath)
                    vm_.callEvent(p, "PlayFoleySound", {Value::intValue(15)}), decided = true;
            } else if (open(kFSE) && enemy && enemy != oldEnemy && num(enemy, "Health") > 0 &&
                       vm_.callEvent(p, "IsActorAnEnemy", {Value::objectValue(enemy)}).asBool() &&
                       !(flag(enemy, "bIsPlayer") && friendly)) {
                pick("FSE", kFSE);
            } else if (open(kLSE) && !enemy && oldEnemy && num(oldEnemy, "Health") > 0 &&
                       !(flag(oldEnemy, "bIsPlayer") && friendly)) {
                pick("LSE", kLSE);
            } else if (open(kKIL) && oldCommand != "KIL" && playerHealth <= 0 && obj(player, "Enemy") == p && !friendly) {
                pick("KIL", kKIL);
                if (Value* c = prop(p, "DLG_Command"))
                    *c = Value::stringValue("KIL");
            } else if (open(kFHE) && command == "FHE" && oldCommand != "FHE") {
                pick("FHE", kFHE);
            } else if (open(kGPI) && command == "GPI" && oldCommand != "GPI") {
                pick("GPI", kGPI);
            }
            const float attempted = num(p, "DLG_LastAttemptedSpeakTime");
            if (!decided && !(attempted != 0 && now <= attempted + 4)) {
                // Now and then: a chance of dt / every seconds a tick, once
                // it's been quiet that long (or has never spoken).
                auto chance = [&](float quiet, float every) {
                    return (lastSpoke == 0 || now - lastSpoke > quiet) && frand() * every < dt;
                };
                const bool calm = !enemy || num(enemy, "Health") <= 0 || (friendly && obj(enemy, "Enemy") != p);
                if (flag(p, "bIsLeading") && playerHealth > 0) {
                    if (open(kLED) && dist < 300 && chance(4, 4))
                        pick("LED", kLED);
                    decided = true;
                } else if (input && vm_.callEvent(input, "PawnSurrenderOrPray", {Value::objectValue(p)}).asBool()) {
                    if (open(kSUR) && dist < 300 && chance(4, 4))
                        pick("SUR", kSUR);
                    decided = true;
                } else if (open(kTTP) && talksToPlayer(p, dist, noAction)) {
                    pick("TTP", kTTP);
                } else if (!noAction) {
                    const Value* own = prop(p, "Dialogue_Tag");
                    const bool tagged = own && own->i && own->i != none;
                    if (open(kAMB) && chance(15, tagged ? 5.0f : 15.0f) && calm)
                        pick("AMB", kAMB);
                    else if (chance(4, 4)) {
                        if (open(kBAT) && flag(p, "bIsAttacking") && enemy && num(enemy, "Health") > 0 && !friendly)
                            pick("BAT", kBAT);
                        else if (open(kINT) && same(vm_.nameText(prop(p, "AnimSequence") ? prop(p, "AnimSequence")->i : 0),
                                                     "InterrogateBajoran"))
                            pick("INT", kINT);
                    }
                    if (type.empty() && open(kREP) && chance(5, 5) && calm)
                        pick("REP", kREP);
                }
            }
            if (!type.empty())
                sayOwnLine(p, type, bit, dist);
        }
        // What it was, for the next tick (out of range or not).
        if (Value* v = prop(p, "DLG_OldHealth"))
            *v = Value::intValue(int(health));
        if (Value* v = prop(p, "DLG_OldEnemy"))
            *v = Value::objectValue(obj(p, "Enemy"));
        if (Value* v = prop(p, "DLG_OldCommand"))
            *v = Value::stringValue(prop(p, "DLG_Command") ? vm_.toString(*prop(p, "DLG_Command")) : std::string());
    }
}

// PawnTalkToPlayer: no enemy, the player alive and standing about (not
// faster than 200) within 200, and the pawn facing the player (or nearly,
// within 75); on a no-action level the player looking at it too.
bool ScriptLevel::talksToPlayer(VmObject* p, float dist, bool noAction) {
    const Value* e = prop(p, "Enemy");
    if ((e && e->asObject()) || !player_ || dist > 200 || dist <= 0)
        return false;
    const Value* h = prop(player_, "Health");
    if (!h || h->asFloat() <= 0)
        return false;
    const auto v = Vm::vectorOf(*prop(player_, "Velocity"));
    if (std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) > 200)
        return false;
    const auto from = Vm::vectorOf(*prop(player_, "Location")), to = Vm::vectorOf(*prop(p, "Location"));
    const std::array<float, 3> dir{(to[0] - from[0]) / dist, (to[1] - from[1]) / dist, (to[2] - from[2]) / dist};
    auto facing = [&](VmObject* a) {
        const auto r = Vm::rotatorOf(*prop(a, "Rotation"));
        const auto f = rotationAxes({r[0], r[1], r[2]})[0];
        return f[0] * dir[0] + f[1] * dir[1] + f[2] * dir[2];
    };
    const float pawnDot = facing(p);
    if (noAction)
        return facing(player_) >= 0.7f && (pawnDot < -0.7f || (dist < 75 && pawnDot < 0.7f));
    return pawnDot < -0.7f || (dist < 75 && pawnDot < -0.3f);
}

// The chosen line, if the pawn has one and the player could hear it: seen
// (TestCanSeeMe) past (1.2 - ObstructionFactor) x 800. A type it has no
// lines of at all isn't tried again (DLG_Flags); else a failed try waits 4
// seconds (DLG_LastAttemptedSpeakTime), a talk-to-player line busy 1.
void ScriptLevel::sayOwnLine(VmObject* p, const std::string& type, int32_t bit, float dist) {
    Value* flags = prop(p, "DLG_Flags");
    auto failed = [&](float ago) {
        if (!(flags->asInt() & bit))
            if (Value* v = prop(p, "DLG_LastAttemptedSpeakTime"))
                *v = Value::floatValue(time_ - ago);
    };
    const Value* obstruction = prop(p, "ObstructionFactor");
    if (dist >= (1.2f - (obstruction ? obstruction->asFloat() : 0)) * 800) {
        const auto eye = Vm::vectorOf(*prop(player_, "Location"));
        const auto at = Vm::vectorOf(*prop(p, "Location"));
        if (!lineOfSight(player_, eye, at)) {
            failed(0);
            return;
        }
    }
    const std::string speaker = vm_.nameText(prop(p, "Speaker_Tag")->i);
    if (!hasDialogLines(type, speaker)) {
        *flags = Value::intValue(flags->asInt() | bit);
        return;
    }
    // Not over other dialogue: the engine queues it (whether it waits there
    // isn't known); Reluva tries again a second later, as for a busy TTP.
    const Value* lineType = prop(levelInfo_, "CurrentLineType");
    const std::string current = lineType ? upper(vm_.nameText(lineType->i)) : "";
    if (!dialogThreads_.empty() ||
        (type == "TTP" && (current == "HAL" || current == "HOP" || current == "HBO" || current == "HCL"))) {
        failed(3);
        return;
    }
    const int index = ownLine(p, type);
    if (index < 0) {
        failed(0);
        return;
    }
    if (type == "TTP") {
        Value* last = prop(p, "DLG_LastSpokenTTP");
        // The same talk again, or a general one after one: not said.
        if (last && last->asInt() != -1 && (last->asInt() == index || !dialogLineSpecial(index))) {
            failed(3);
            return;
        }
        if (last)
            *last = Value::intValue(index);
    }
    playOwnLine(p, index);
    if (Value* v = prop(p, "DLG_LastSpeakTime"))
        *v = Value::floatValue(time_);
    if (Value* v = prop(p, "DLG_LastSpokenLine"))
        *v = Value::intValue(index);
}

bool ScriptLevel::hasDialogLines(const std::string& type, const std::string& speaker) {
    loadDialog();
    for (size_t i = 0; i < dialogLines_.size(); ++i)
        if (dialogOurs_[i] && same(dialogLines_[i].type, type) && same(dialogLines_[i].speaker, speaker))
            return true;
    return false;
}

int ScriptLevel::ownLine(VmObject* pawn, const std::string& type) {
    const Value* code = prop(pawn, "Speaker_Tag");
    if (!code)
        return -1;
    conditionSubject_ = pawn;
    const Value* own = prop(pawn, "Dialogue_Tag");
    const std::string tag = own && own->i && !same(vm_.nameText(own->i), "None") ? vm_.nameText(own->i) : "";
    const int index = pickDialogLine(type, vm_.nameText(code->i), "", true, false, false, &tag);
    conditionSubject_ = nullptr;
    return index;
}

void ScriptLevel::playOwnLine(VmObject* pawn, int index) {
    DialogThread t;
    t.id = nextDialogThread_++;
    t.self = pawn;
    t.forcedSpeaker = pawn;
    dialogThreads_.push_back(t);
    ++dialog.barks;
    beginLine(dialogThreads_.back(), index);
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
    // HUD lets it: the AI's own lines (ScriptLevel::dialogTick).
    reg("CTVE_DialogInput.DialogTick", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->dialogTick(c.self, c.f(0));
    });
}

} // namespace reluva::unreal
