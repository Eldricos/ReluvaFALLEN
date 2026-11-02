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
#include <filesystem>
#include <fstream>
#include <sstream>

namespace reluva::unreal {

namespace {

std::string upper(std::string s) {
    for (char& c : s)
        c = char(std::toupper(uint8_t(c)));
    return s;
}

bool same(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

} // namespace

// The dialogue as Engine.dll's dialogue engine keeps it (read for what it
// does; the code is Reluva's own). A level has two tables: the general
// one (GEN_Dialogue) and its own, LevelInfo's DialogFile, else the one
// its map's mission names (M01_KiraL1A: M01_Dialogue); the menu's Entry
// none. Their header rows give each line type's priority (Types: 1 DIE,
// 2 CIN, 3 the hails...) and the faces' order (Faces).
void ScriptLevel::loadDialog() {
    if (dialogLoaded_)
        return;
    dialogLoaded_ = true;
    dialogLines_.clear();
    const std::string map = upper(map_.name());
    if (map == "ENTRY")
        return;
    std::string own;
    if (const Value* file = levelInfo_ ? prop(levelInfo_, "DialogFile") : nullptr; file && !file->s.empty())
        own = upper(file->s);
    else if (map.size() >= 3 && map[0] == 'M' && std::isdigit(uint8_t(map[1])) && std::isdigit(uint8_t(map[2])))
        own = map.substr(0, 3) + "_DIALOGUE";
    if (own.size() > 4 && own.substr(own.size() - 4) == ".TXT")
        own.resize(own.size() - 4);
    std::vector<DialogLine> all;
    if (readDialogTables(intFolder(), all))
        all.clear();
    for (const std::string& table : {std::string("GEN_DIALOGUE"), own}) {
        if (table.empty())
            continue;
        for (const DialogLine& l : all)
            if (upper(l.table) == table)
                dialogLines_.push_back(l);
        // Its header rows.
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(intFolder(), ec)) {
            if (upper(entry.path().stem().string()) != table)
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            for (std::string row; std::getline(in, row);) {
                std::vector<std::string> cells;
                std::stringstream cs(row);
                for (std::string cell; std::getline(cs, cell, '\t');)
                    cells.push_back(cell);
                if (cells.size() >= 3 && same(cells[0], "Types")) {
                    std::istringstream types(cells[2]);
                    for (std::string t; types >> t;)
                        dialogGroups_[upper(t)] = std::atoi(cells[1].c_str());
                } else if (cells.size() >= 3 && same(cells[0], "Faces") && dialogFaces_.empty()) {
                    std::istringstream faces(cells[2]);
                    for (std::string f; faces >> f;)
                        dialogFaces_.push_back(upper(f));
                }
            }
        }
    }
    const size_t n = dialogLines_.size();
    dialogPlays_.assign(n, 0);
    dialogRecs_.assign(n, {});
    auto noted = [](const std::string& s) { return !s.empty() && s.front() == '(' ? std::string() : s; };
    // MISSION TYPE SPEAKER ID, as the tables name a line, as a key.
    auto pattern = [](std::string k) {
        k = upper(k);
        k.resize(12, '?');
        return k.substr(3, 3) + k.substr(6, 3) + "???" + k.substr(9, 3) + k.substr(0, 3);
    };
    for (size_t i = 0; i < n; ++i) {
        const DialogLine& l = dialogLines_[i];
        DialogRec& r = dialogRecs_[i];
        std::string id = l.lineId;
        id.resize(3, ' ');
        r.key = upper(l.type) + upper(l.speaker) + (l.listener.empty() ? "___" : upper(l.listener)) + upper(id) +
                upper(l.mission);
        auto g = dialogGroups_.find(upper(l.type));
        r.group = g != dialogGroups_.end() ? g->second : 9;
        r.tag = noted(l.tag);
        r.event = noted(l.event);
        r.condition = noted(l.condition);
        r.nextPattern = l.next.empty() ? "" : pattern(l.next);
        r.backPattern = l.back.empty() ? "" : pattern(l.back);
        const std::string type = upper(l.type), by = upper(l.speaker), to = upper(l.listener);
        if (type == "HOP") {
            if (r.nextPattern.empty())
                r.nextPattern = "HBO" + to + by;
            if (r.backPattern.empty())
                r.backPattern = "HCL" + by + to;
        } else if (type == "HBO" && !to.empty() && r.nextPattern.empty()) {
            r.nextPattern = "HCL" + to + by;
        }
        // CAMERA's flags: CAM(V:1), CAM(H:1), CAM(R:... F:..).
        std::string cam = upper(l.camera);
        const size_t open = cam.find('(');
        if (open != std::string::npos)
            for (size_t k = open + 1; k + 1 < cam.size(); ++k)
                if (cam[k + 1] == ':' && (k == open + 1 || cam[k - 1] == ' ' || cam[k - 1] == '(') &&
                    !(k + 2 < cam.size() && cam[k + 2] == '0')) {
                    switch (cam[k]) {
                    case 'A': r.flags |= 1; break;
                    case 'R': r.flags |= 2; break;
                    case 'C': r.flags |= 4; break;
                    case 'D':
                    case 'T': r.flags |= 8; break;
                    case 'V': r.flags |= 16; break;
                    case 'H': r.flags |= 32; break;
                    default: break;
                    }
                }
    }
    // A line runs on to the next in the tables when it's of the same type
    // and the next number: that's its next line, whatever NEXTLINE says.
    auto number = [&](size_t i) { return std::atoi(dialogLines_[i].lineId.c_str()); };
    for (size_t i = 0; i + 1 < n; ++i)
        if (same(dialogLines_[i].type, dialogLines_[i + 1].type) && number(i + 1) == number(i) + 1 &&
            !dialogLines_[i + 1].lineId.empty() && std::isdigit(uint8_t(dialogLines_[i + 1].lineId[0]))) {
            dialogRecs_[i + 1].runOn = true;
            dialogRecs_[i].next = int(i + 1);
            dialogRecs_[i].nextPattern.clear();
        }
    dialogOrder_.resize(n);
    for (size_t i = 0; i < n; ++i)
        dialogOrder_[i] = int(i);
    std::stable_sort(dialogOrder_.begin(), dialogOrder_.end(),
                     [&](int a, int b) { return dialogRecs_[size_t(a)].key < dialogRecs_[size_t(b)].key; });
    // NEXTLINE and RETURNLINE resolved now where only one line fits.
    for (DialogRec& r : dialogRecs_)
        for (auto [pat, to] : {std::pair{&r.nextPattern, &r.next}, std::pair{&r.backPattern, &r.back}}) {
            if (pat->empty())
                continue;
            int found = -1, count = 0;
            for (size_t i = 0; i < n && count < 2; ++i)
                if (keyFits(*pat, dialogRecs_[i].key))
                    found = int(i), ++count;
            if (count == 1) {
                *to = found;
                pat->clear();
            }
        }
}

// A key fits a pattern as long as the pattern goes: '?' in the pattern or
// '_' in the key (a line to no one) fits anything.
bool ScriptLevel::keyFits(const std::string& pattern, const std::string& key) {
    if (pattern.size() > key.size())
        return false;
    for (size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] != '?' && key[i] != '_' && std::toupper(uint8_t(pattern[i])) != std::toupper(uint8_t(key[i])))
            return false;
    return true;
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

// The player's speaker code ("KRA").
std::string ScriptLevel::playerSpeaker() {
    const Value* tag = player_ ? prop(player_, "Speaker_Tag") : nullptr;
    return tag && tag->i ? vm_.nameText(tag->i) : std::string();
}

// Whether a line is for whoever the player plays (CheckMissionType): a cut
// scene's always; a G mission's; one of the player's own letter (K, S,
// W); an M mission's for Sisko and Worf.
bool ScriptLevel::missionFits(int index) {
    const DialogLine& l = dialogLines_[size_t(index)];
    if (same(l.type, "CIN"))
        return true;
    const char m = l.mission.empty() ? 0 : char(std::toupper(uint8_t(l.mission[0])));
    const std::string me = upper(playerSpeaker());
    const char who = me.empty() ? 0 : me[0];
    return m == 'G' || (who && m == who) || (m == 'M' && (who == 'S' || who == 'W'));
}

// A condition (`Conditional`): words, brackets and commas apart, gathered
// into terms between AND and OR, taken left to right: a 0 before AND
// gives 0, a 1 before OR gives 1. A term with a '.' in it is about a
// variable, any other the dialogue input's event. 1 met, 0 not, -1 not a
// condition the engine knows.
int ScriptLevel::evalCondition(const std::string& condition, VmObject* actor) {
    std::string text = condition;
    for (char& c : text)
        if (c == '(' || c == ')' || c == ',')
            c = ' ';
    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string w; in >> w;)
        words.push_back(w);
    int last = 1;
    std::vector<std::string> term;
    for (size_t i = 0; i <= words.size(); ++i) {
        const bool end = i == words.size();
        const bool isAnd = !end && same(words[i], "AND"), isOr = !end && same(words[i], "OR");
        if (!end && !isAnd && !isOr) {
            term.push_back(words[i]);
            continue;
        }
        if (term.empty())
            continue;
        last = conditionTerm(term, actor);
        term.clear();
        if (last < 0)
            return -1;
        if (isAnd && last == 0)
            return 0;
        if (isOr && last == 1)
            return 1;
    }
    return last;
}

int ScriptLevel::conditionTerm(const std::vector<std::string>& term, VmObject* actor) {
    const bool variable = std::any_of(term.begin(), term.end(), [](const std::string& w) { return w.find('.') != std::string::npos; });
    if (variable) {
        // "Class.Property op value" (or "Class Property op value"): the
        // actor's property, if it's of the class.
        if (!actor)
            return 0;
        std::string cls, property;
        size_t at = 0;
        const size_t dot = term[0].find('.');
        if (dot != std::string::npos && dot > 0) {
            cls = term[0].substr(0, dot), property = term[0].substr(dot + 1), at = 1;
        } else if (term.size() >= 2) {
            cls = term[0], property = term[1], at = 2;
        }
        if (at + 2 > term.size() || !vm_.findClass(cls))
            return -1;
        if (!vm_.isA(actor, cls))
            return -1;
        const Value* v = prop(actor, property.c_str());
        if (!v)
            return -1;
        const std::string& op = term[at];
        const std::string& want = term[at + 1];
        if (v->type == VType::Float || v->type == VType::Int || v->type == VType::Byte) {
            const float a = v->asFloat(), b = std::strtof(want.c_str(), nullptr);
            if (op == "<") return a < b;
            if (op == "<=") return a <= b;
            if (op == ">") return a > b;
            if (op == ">=") return a >= b;
            if (op == "=" || op == "==") return a == b;
            if (op == "!=") return a != b;
            return -1;
        }
        const std::string a = upper(vm_.toString(*v));
        if (op == "=" || op == "==") return a == upper(want);
        if (op == "!=") return a != upper(want);
        return -1;
    }
    bool negate = false;
    std::vector<std::string> words;
    for (std::string w : term) {
        if (w.find('!') != std::string::npos) {
            negate = true;
            w.erase(std::remove(w.begin(), w.end(), '!'), w.end());
        }
        if (!w.empty())
            words.push_back(w);
    }
    if (words.empty())
        return -1;
    VmObject* input = dialogInput();
    if (!input)
        return 0;
    const std::string name = words[0];
    std::vector<std::string> args(words.begin() + 1, words.end());
    auto isNumber = [](const std::string& s) {
        char* end = nullptr;
        std::strtod(s.c_str(), &end);
        return !s.empty() && end && *end == 0;
    };
    std::string event = name;
    std::vector<Value> values;
    static const char* const known[] = {
        "ObjectiveCompleted", "PlayerFaceEnemy", "EnemyIsA", "EnemyIsIntelligent", "FriendClose", "AlarmTriggered",
        "TurretTriggered", "TagExists", "PlayerIs", "LowShield", "Poisoned", "LowHealth", "RescuedMoreThan",
        "SpecialCondition", "InPlace", "CommunicationBlocked", "CloseToBajoranSurvivor", "HasItem", "ItemReady",
        "UsingItem", "InBeamDownZone", "TimePassed", "CanBeamDownItems", "PlayerHoldingWeapon", "LowAmmo", "DS9Phase"};
    if (std::none_of(std::begin(known), std::end(known), [&](const char* k) { return same(name, k); })) {
        ++dialog.unknownConditions;
        return -1;
    }
    if (same(name, "DS9Phase")) {
        event = "OpsLevel";
        if (args.size() == 1 && isNumber(args[0]))
            values = {Value::stringValue(""), Value::intValue(std::atoi(args[0].c_str()))};
        else if (args.size() >= 2)
            values = {Value::stringValue(args[0]), Value::intValue(std::atoi(args[1].c_str()))};
        else
            return -1;
    } else if (same(name, "LowAmmo")) {
        if (!args.empty() && isNumber(args[0]) && std::strtof(args[0].c_str(), nullptr) != 0)
            values = {Value::nameValue(vm_.name("None")), Value::floatValue(std::strtof(args[0].c_str(), nullptr))};
        else if (args.size() >= 2)
            values = {Value::nameValue(vm_.name(args[0])), Value::floatValue(float(std::atoi(args[1].c_str())))};
        else
            return -1;
    } else {
        if (same(name, "PlayerIs"))
            event = "IsPlayer";
        else if (same(name, "InPlace"))
            event = "PlayerInPlace";
        if (same(name, "ObjectiveCompleted") &&
            (args.empty() || std::atoi(args[0].c_str()) < 1 || std::atoi(args[0].c_str()) > 9))
            return -1;
        VmFunction* f = vm_.findFunction(input, vm_.name(event));
        if (!f)
            return -1;
        size_t given = 0;
        for (const VmProperty* p : f->params) {
            if (p->type == VType::Object) {
                VmObject* who = actor;
                if (same(name, "PlayerFaceEnemy")) {
                    const Value* e = actor ? prop(actor, "Enemy") : nullptr;
                    who = e ? e->asObject() : nullptr;
                }
                values.push_back(Value::objectValue(who));
                continue;
            }
            if (given >= args.size())
                return -1;
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
    }
    const bool met = vm_.callEvent(input, event, values).asBool();
    return (negate ? !met : met) ? 1 : 0;
}

bool ScriptLevel::testCondition(const std::string& condition) {
    const int r = evalCondition(condition, nullptr);
    return r != 0; // a condition the engine can't test reads as met ("Test Failed")
}

// The best line whose key fits `pattern`, as FindBestDialogLine: of those
// fitting (not lines that run on from another), the only one as it is;
// else each scored: asked for by tag, only that tag's; otherwise a tagged
// line only for the caller whose Dialogue_Tag it is (100,000); for whoever
// the player plays; its condition met (1,000,000); 10,000 off for each
// hundred of its number; the times it's been said off (a pain cry's: a
// random 0-14). The highest, the later in key order of equals. `count`:
// how many fit.
int ScriptLevel::bestLine(const std::string& pattern, NameId filterTag, VmObject* caller, int* count) {
    loadDialog();
    std::vector<int> fits;
    for (const int i : dialogOrder_)
        if (!dialogRecs_[size_t(i)].runOn && keyFits(pattern, dialogRecs_[size_t(i)].key))
            fits.push_back(i);
    if (count)
        *count = int(fits.size());
    if (fits.size() == 1)
        return fits[0];
    const std::string want = filterTag && !same(vm_.nameText(filterTag), "None") ? vm_.nameText(filterTag) : "";
    std::string callerTag;
    if (const Value* t = caller ? prop(caller, "Dialogue_Tag") : nullptr; t && t->i)
        callerTag = vm_.nameText(t->i);
    int best = -1;
    double bestScore = 0;
    for (const int i : fits) {
        const DialogRec& r = dialogRecs_[size_t(i)];
        const DialogLine& l = dialogLines_[size_t(i)];
        double score = 0;
        if (!want.empty()) {
            if (!same(r.tag, want))
                continue;
        } else if (!r.tag.empty()) {
            if (!same(r.tag, callerTag))
                continue;
            score += 100000;
        }
        if (!missionFits(i))
            continue;
        if (!r.condition.empty()) {
            if (evalCondition(r.condition, caller) <= 0)
                continue;
            score += 1000000;
        }
        score -= 10000.0 * (std::atoi(l.lineId.c_str()) / 100);
        if (same(l.type, "PAN")) {
            dialogRandom_ = dialogRandom_ * 1103515245u + 12345u;
            score -= double((dialogRandom_ >> 16) % 15);
        } else {
            score -= dialogPlays_[size_t(i)];
        }
        if (best < 0 || score >= bestScore)
            best = i, bestScore = score;
    }
    return best;
}

// FindBestDialogLine: the key from the type, the speaker (asked, else the
// caller's own Speaker_Tag), the listener and the line's number; nothing
// but a type, and no tag, finds nothing.
int ScriptLevel::findDialogLine(const std::string& type, NameId filterTag, NameId listener, NameId speaker,
                                const std::string& lineId, int& index, VmObject* caller) {
    loadDialog();
    index = -1;
    auto given = [&](NameId n) { return n != 0 && !same(vm_.nameText(n), "None"); };
    std::string by = given(speaker) ? upper(vm_.nameText(speaker)) : "";
    if (by.empty())
        if (const Value* t = caller ? prop(caller, "Speaker_Tag") : nullptr; t && given(t->i))
            by = upper(vm_.nameText(t->i));
    const std::string to = given(listener) ? upper(vm_.nameText(listener)) : "";
    if (by.empty() && to.empty() && lineId.empty() && !given(filterTag))
        return 0;
    auto three = [](std::string s) {
        s.resize(3, '?');
        return s;
    };
    const std::string pattern = upper(type) + three(by) + three(to) + (lineId.empty() ? "???" : three(upper(lineId)));
    int count = 0;
    index = bestLine(pattern, filterTag, caller, &count);
    if (index < 0)
        ++dialog.notFound;
    return count;
}

bool ScriptLevel::dialogLineSpecial(int index) {
    loadDialog();
    if (index < 0 || size_t(index) >= dialogLines_.size() || dialogPlays_[size_t(index)] > 0)
        return false;
    const DialogRec& r = dialogRecs_[size_t(index)];
    if (std::atoi(dialogLines_[size_t(index)].lineId.c_str()) >= 800)
        return false;
    return (!r.tag.empty() && r.tag.size() != 3) || !r.condition.empty();
}

// StartDialog: a type of three letters. A hail with no tag is the player's
// options to whom it hails (HOP), asked as the player. One asked by tag
// that finds nothing as a hail is tried once as the options.
int ScriptLevel::startDialog(VmObject* self, const std::string& type, NameId filterTag, NameId listener,
                             NameId speaker, const std::string& lineId) {
    loadDialog();
    std::string t = upper(type);
    if (t.size() != 3)
        return 0;
    VmObject* caller = self;
    const bool tagged = filterTag && !same(vm_.nameText(filterTag), "None");
    if (t == "HAL" && !tagged) {
        const std::string to = listener && !same(vm_.nameText(listener), "None") ? vm_.nameText(listener) : "";
        if (to.size() != 3)
            return 0;
        t = "HOP";
        caller = player_;
    }
    int index = -1;
    findDialogLine(t, filterTag, listener, speaker, lineId, index, caller);
    if (index < 0 && t == "HAL" && tagged)
        findDialogLine("HOP", filterTag, listener, speaker, lineId, index, caller);
    if (index < 0)
        return 0;
    // A new hail cuts the first hail-type line playing.
    if (t[0] == 'H')
        for (DialogThread& p : dialogThreads_)
            if (!p.queued && upper(dialogLines_[size_t(p.line)].type)[0] == 'H') {
                endLine(p, true);
                break;
            }
    dialogThreads_.erase(std::remove_if(dialogThreads_.begin(), dialogThreads_.end(),
                                        [](const DialogThread& p) { return p.line < 0; }),
                         dialogThreads_.end());
    const int id = nextDialogThread_++;
    if (!startLine(id, index, caller))
        return 0;
    ++dialog.started;
    return id;
}

// Who says a line: the caller, if it's the line's speaker; else (or for a
// voice-over or the communicator's, V or H) whom the player's
// GetSpeakerActor names for its code.
VmObject* ScriptLevel::speakerFor(int index, VmObject* caller) {
    const DialogLine& l = dialogLines_[size_t(index)];
    const DialogRec& r = dialogRecs_[size_t(index)];
    const Value* code = caller && !caller->deleted ? prop(caller, "Speaker_Tag") : nullptr;
    if (!(r.flags & 48) && code && same(vm_.nameText(code->i), l.speaker))
        return caller;
    if (!player_ || player_->deleted)
        return nullptr;
    VmObject* a = vm_.callEvent(player_, "GetSpeakerActor",
                                {Value::nameValue(vm_.name(l.speaker)),
                                 Value::nameValue(r.tag.empty() ? vm_.name("None") : vm_.name(r.tag)),
                                 Value::boolValue((r.flags & 48) != 0)})
                     .asObject();
    return a && !a->deleted ? a : nullptr;
}

// StartLine: a line for another character than the player's swapped for
// the player's own (its mission's letter, then M); its speaker found. A
// speaker busy with a line more important than this one refuses it; one
// with a less important (or the same type, or a HUD line) is cut off;
// otherwise it waits its turn, and lines waiting for that speaker that
// matter less are dropped.
int ScriptLevel::startLine(int thread, int index, VmObject* caller, int back, const std::string& backPattern) {
    if (!missionFits(index)) {
        const std::string key = dialogRecs_[size_t(index)].key;
        const std::string me = upper(playerSpeaker());
        int swapped = -1;
        for (char letter : {me.empty() ? char(0) : me[0], char('M')}) {
            if (!letter || (letter == 'M' && me[0] != 'S' && me[0] != 'W'))
                continue;
            std::string want = key;
            want[want.size() - 3] = letter;
            for (size_t i = 0; i < dialogRecs_.size() && swapped < 0; ++i)
                if (dialogRecs_[i].key == want)
                    swapped = int(i);
            if (swapped >= 0)
                break;
        }
        if (swapped < 0)
            return 0;
        index = swapped;
    }
    VmObject* speaker = speakerFor(index, caller);
    const int group = dialogRecs_[size_t(index)].group;
    const std::string type = upper(dialogLines_[size_t(index)].type);
    bool queue = false;
    if (speaker)
        for (DialogThread& p : dialogThreads_) {
            if (p.queued || p.speaker != speaker || p.line < 0)
                continue;
            const int playing = dialogRecs_[size_t(p.line)].group;
            const std::string playingType = upper(dialogLines_[size_t(p.line)].type);
            if (playing < group && playingType != "HUD")
                return 0;
            if (group < playing || playingType == type || playingType == "HUD")
                endLine(p, true);
            else
                queue = true;
        }
    dialogThreads_.erase(std::remove_if(dialogThreads_.begin(), dialogThreads_.end(),
                                        [&](const DialogThread& p) {
                                            return p.line < 0 || (p.queued && p.speaker == speaker && speaker &&
                                                                  dialogRecs_[size_t(p.line)].group > group);
                                        }),
                         dialogThreads_.end());
    DialogThread t;
    t.id = thread;
    t.self = caller;
    t.speaker = speaker;
    t.back = back;
    t.backPattern = backPattern;
    t.line = index;
    t.queued = queue;
    dialogThreads_.push_back(t);
    if (queue)
        return 2;
    beginLine(dialogThreads_.back(), index);
    return 1;
}

// How long a line's voice lasts: its sound (the record's name in the
// table's .uax) is a WAV file, its data chunk's size over its bytes a
// second; 3.5 seconds if it has none.
float ScriptLevel::lineSeconds(const DialogLine& l) {
    const std::string key = l.table + "." + l.record;
    auto known = dialogSeconds_.find(key);
    if (known != dialogSeconds_.end())
        return known->second;
    float seconds = 3.5f;
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

// The level's Talker, Listener and CurrentLine fields: a line takes them
// if none has them, or it's of a more important type, or as important and
// said nearer the camera.
void ScriptLevel::writeLineFields(DialogThread& t) {
    const DialogRec& r = dialogRecs_[size_t(t.line)];
    auto current = std::find_if(dialogThreads_.begin(), dialogThreads_.end(),
                                [&](const DialogThread& o) { return o.id == currentThread_ && !o.queued && o.line >= 0; });
    if (current != dialogThreads_.end() && current->id != t.id) {
        const int theirs = dialogRecs_[size_t(current->line)].group;
        bool take = r.group < theirs;
        if (r.group == theirs) {
            Vec eye{};
            auto distance = [&](VmObject* a) {
                const Value* l = a && !a->deleted ? prop(a, "Location") : nullptr;
                if (!l)
                    return 1e30f;
                const auto at = Vm::vectorOf(*l);
                return (at[0] - eye[0]) * (at[0] - eye[0]) + (at[1] - eye[1]) * (at[1] - eye[1]) +
                       (at[2] - eye[2]) * (at[2] - eye[2]);
            };
            take = listenerAt(eye) && distance(t.speaker) < distance(current->speaker);
        }
        if (!take)
            return;
    }
    currentThread_ = t.id;
    if (Value* v = prop(levelInfo_, "Talker"))
        *v = Value::objectValue(t.speaker);
    if (Value* v = prop(levelInfo_, "Listener"))
        *v = Value::objectValue(t.listener);
    if (Value* v = prop(levelInfo_, "CurrentLineType"))
        *v = Value::nameValue(vm_.name(dialogLines_[size_t(t.line)].type));
    if (Value* v = prop(levelInfo_, "CurrentLineIndex"))
        *v = Value::intValue(t.line);
    if (Value* v = prop(levelInfo_, "CurrentLineFlags"))
        *v = Value::intValue(r.flags);
}

// A line begun: its voice through the speaker's PlaySpeech (louder for
// death, pain, first sight and battle cries, quieter for EXG; no reverb
// for a voice-over, no communicator filter for a HUD line); then, said by
// a pawn, its listener (none for a hail; else the nearest pawn within
// 800, the player counted at a fifth of its distance, one with the
// line's listener code first, only such a one in a cut scene), the level's
// fields, its animations (while the pawn's frozen) and face, and the line
// on the HUD (not a note in brackets).
void ScriptLevel::beginLine(DialogThread& t, int index) {
    const DialogLine& l = dialogLines_[size_t(index)];
    const DialogRec& r = dialogRecs_[size_t(index)];
    ++dialogPlays_[size_t(index)];
    if (dialogSaid_.size() >= 256)
        dialogSaid_.erase(dialogSaid_.begin());
    dialogSaid_.push_back(index);
    currentLine_ = index;
    currentSpeaker_ = t.speaker;
    ++dialog.lines;
    t.line = index;
    t.queued = false;
    t.seconds = lineSeconds(l);
    t.elapsed = 0;
    t.phoneme = 0;
    const std::string type = upper(l.type);
    t.hudClock = type == "HUD";
    const Value* dt = prop(levelInfo_, "DeltaTime");
    t.timeLeft = (dt ? dt->asFloat() : 0) + t.seconds + std::max(0.0f, l.pause) + 0.25f;
    if (r.back >= 0 || !r.backPattern.empty())
        t.back = r.back, t.backPattern = r.backPattern;
    VmObject* speaker = t.speaker;
    if (const Package* p = vm_.packages().get(l.table))
        if (const int e = p->findExport(l.record, "Sound"); e >= 0) {
            const float volume = type == "DIE" || type == "PAN" || type == "FSE" || type == "BAT" ? 1.4f
                                 : type == "EXG"                                                  ? 0.3f
                                                                                                  : 0.65f;
            if (speaker)
                vm_.callEvent(speaker, "PlaySpeech",
                              {Value::objectValue(vm_.objectAt(*p, e + 1)), Value::floatValue(volume),
                               Value::boolValue((r.flags & 16) != 0), Value::boolValue(type == "HUD")});
            else {
                SoundRequest q;
                q.sound = {p, e};
                q.slot = 5; // SLOT_Talk
                q.volume = volume;
                q.speech = true;
                if (soundRequests_.size() >= 256)
                    soundRequests_.erase(soundRequests_.begin());
                soundRequests_.push_back(q);
            }
        }
    if (!speaker || !vm_.isA(speaker, "Pawn"))
        return;
    // The listener.
    t.listener = nullptr;
    if (type[0] != 'H') {
        const bool letterbox = [&] {
            const Value* hud = player_ ? prop(player_, "myHUD") : nullptr;
            return hud && hud->asObject() && vm_.callEvent(hud->o, "IsLetterBoxOn").asBool();
        }();
        const auto from = Vm::vectorOf(*prop(speaker, "Location"));
        float best = 800;
        bool coded = false;
        for (VmObject* a : actors_) {
            if (a == speaker || a->deleted || !vm_.isA(a, "Pawn"))
                continue;
            const Value* code = prop(a, "Speaker_Tag");
            const bool hasCode = !l.listener.empty() && code && same(vm_.nameText(code->i), l.listener);
            if ((letterbox || coded) && !hasCode)
                continue;
            const auto at = Vm::vectorOf(*prop(a, "Location"));
            float d = std::sqrt((at[0] - from[0]) * (at[0] - from[0]) + (at[1] - from[1]) * (at[1] - from[1]) +
                                (at[2] - from[2]) * (at[2] - from[2]));
            if (const Value* pl = prop(a, "bIsPlayer"); pl && pl->asBool())
                d *= 0.2f;
            if (hasCode && !coded) {
                coded = true;
                t.listener = a, best = d;
            } else if (d < best) {
                t.listener = a, best = d;
            }
        }
    }
    const bool note = !l.text.empty() && l.text.front() == '[';
    if (!note)
        writeLineFields(t);
    for (const auto& [who, anim] : {std::pair{t.speaker, l.speakerAnim}, std::pair{t.listener, l.listenerAnim}})
        if (who && !anim.empty() && !who->deleted && who->state && same(who->state->name, "Frozen"))
            vm_.callEvent(who, "PlayDialogAnim", {Value::nameValue(vm_.name(anim)), Value::floatValue(1), Value::floatValue(0.3f)});
    for (const auto& [who, face] : {std::pair{t.speaker, l.speakerFace}, std::pair{t.listener, l.listenerFace}}) {
        if (!who || face.empty() || who->deleted)
            continue;
        const auto f = std::find(dialogFaces_.begin(), dialogFaces_.end(), upper(face));
        if (f == dialogFaces_.end())
            continue;
        if (Value* v = prop(who, "FacialExpression"))
            v->i = int(f - dialogFaces_.begin());
        if (Value* v = prop(who, "FacialTimer"))
            *v = Value::floatValue(time_ + t.timeLeft + 0.5f);
    }
    const Value* hud = player_ ? prop(player_, "myHUD") : nullptr;
    if (hud && hud->asObject() && !l.text.empty() && !note)
        vm_.callEvent(hud->o, "HudText",
                      {Value::stringValue(l.text), Value::floatValue(t.seconds + std::max(0.0f, l.pause)),
                       Value::nameValue(vm_.name(l.speaker)), Value::objectValue(t.speaker)});
}

// A line over: the level's fields cleared if they were its, its voice
// stopped; cut off, that's all (its thread ends). Said to the end, its
// EVENT happens (each word a tag to trigger, or with '-' before it one of
// the HUD's beam-downs), then its thread goes on: its next line (found
// now, as FindBestDialogLine would for no one, if the tables didn't name
// just one), else the line its thread is to return to (not a sign-off
// after a sign-off), else the thread ends.
void ScriptLevel::endLine(DialogThread& t, bool cut) {
    const int line = t.line;
    if (line < 0)
        return;
    const DialogRec& r = dialogRecs_[size_t(line)];
    if (currentThread_ == t.id) {
        currentThread_ = 0;
        if (Value* v = prop(levelInfo_, "Talker"))
            *v = Value::objectValue(nullptr);
        if (Value* v = prop(levelInfo_, "Listener"))
            *v = Value::objectValue(nullptr);
        if (Value* v = prop(levelInfo_, "CurrentLineType"))
            *v = Value::nameValue(vm_.name("None"));
        if (Value* v = prop(levelInfo_, "CurrentLineIndex"))
            *v = Value::intValue(-1);
        if (Value* v = prop(levelInfo_, "CurrentLineFlags"))
            *v = Value::intValue(0);
    }
    VmObject* speaker = t.speaker && !t.speaker->deleted ? t.speaker : nullptr;
    if (speaker) {
        if (soundStops_.size() >= 256)
            soundStops_.erase(soundStops_.begin());
        soundStops_.push_back({speaker, 5});
        // Its voice stopped (the driver's StopSound: the mouth closed).
        for (LipChannel& c : lipChannels_)
            if (c.actor == speaker && c.slot == 5)
                lipSyncStop(c);
    }
    t.line = -1;
    if (cut)
        return;
    if (VmObject* input = r.event.empty() ? nullptr : dialogInput()) {
        std::istringstream words(r.event);
        for (std::string w; words >> w;) {
            if (w.size() > 1 && w[0] == '-')
                vm_.callEvent(input, "DoSpecialEvent", {Value::stringValue(w.substr(1))});
            else
                vm_.callEvent(input, "DoEvent", {Value::nameValue(vm_.name(w))});
        }
    }
    if (t.speaker && t.speaker->deleted)
        return; // its speaker gone: the thread ends
    int next = r.next;
    if (next < 0 && !r.nextPattern.empty())
        next = bestLine(r.nextPattern, 0, nullptr, nullptr);
    if (next < 0 && (t.back >= 0 || !t.backPattern.empty())) {
        const int back = t.back >= 0 ? t.back : bestLine(t.backPattern, 0, nullptr, nullptr);
        t.back = -1;
        t.backPattern.clear();
        if (back >= 0 && !(same(dialogLines_[size_t(back)].type, "HCL") && same(dialogLines_[size_t(line)].type, "HCL")))
            next = back;
    }
    if (next < 0)
        return;
    pendingLines_.push_back({t.id, next, t.self, t.back, t.backPattern});
}

// Each tick: the lines' clocks run (a HUD line's while paused too); lines
// done end and lead on; lines waiting start once their speakers are free.
// (The mouths follow the voices: vm_sound.cpp's lip-sync.)
void ScriptLevel::tickDialog(float seconds) {
    const bool stopped = paused();
    for (size_t k = 0; k < dialogThreads_.size(); ++k) {
        DialogThread& t = dialogThreads_[k];
        if (t.queued || t.line < 0 || (stopped && !t.hudClock))
            continue;
        t.timeLeft -= seconds;
        t.elapsed += seconds;
        if (t.timeLeft <= 0)
            endLine(dialogThreads_[k], false);
    }
    dialogThreads_.erase(std::remove_if(dialogThreads_.begin(), dialogThreads_.end(),
                                        [](const DialogThread& p) { return p.line < 0; }),
                         dialogThreads_.end());
    // The lines that follow, in their threads.
    std::vector<PendingLine> pending;
    pending.swap(pendingLines_);
    for (const PendingLine& p : pending)
        startLine(p.thread, p.line, p.caller, p.back, p.backPattern);
    // Those waiting, once their speaker is free.
    for (size_t k = 0; k < dialogThreads_.size(); ++k) {
        DialogThread& t = dialogThreads_[k];
        if (!t.queued)
            continue;
        const bool busy = std::any_of(dialogThreads_.begin(), dialogThreads_.end(), [&](const DialogThread& o) {
            return !o.queued && o.line >= 0 && o.speaker == t.speaker;
        });
        if (!busy)
            beginLine(t, t.line);
    }
}

// IsDialogThreadPlaying: a line of it said or waiting.
bool ScriptLevel::dialogPlaying(int thread) const {
    return std::any_of(dialogThreads_.begin(), dialogThreads_.end(),
                       [&](const DialogThread& t) { return t.id == thread && t.line >= 0; }) ||
           std::any_of(pendingLines_.begin(), pendingLines_.end(), [&](const PendingLine& p) { return p.thread == thread; });
}

std::vector<ScriptLevel::Subtitle> ScriptLevel::subtitles() const {
    std::vector<Subtitle> out;
    for (const DialogThread& t : dialogThreads_)
        if (!t.queued && t.line >= 0 && t.elapsed < t.seconds) {
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
    // A talk to the player waits while one plays or a hail's on.
    const Value* lineType = prop(levelInfo_, "CurrentLineType");
    const std::string current = lineType ? upper(vm_.nameText(lineType->i)) : "";
    const bool talking = std::any_of(dialogThreads_.begin(), dialogThreads_.end(), [&](const DialogThread& t) {
        return !t.queued && t.line >= 0 && same(dialogLines_[size_t(t.line)].type, "TTP");
    });
    if (type == "TTP" && (talking || current == "HAL" || current == "HOP" || current == "HBO" || current == "HCL")) {
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
    // Then as any line: StartLine's rules for a busy speaker.
    playOwnLine(p, index);
    if (Value* v = prop(p, "DLG_LastSpeakTime"))
        *v = Value::floatValue(time_);
    if (Value* v = prop(p, "DLG_LastSpokenLine"))
        *v = Value::intValue(index);
}

bool ScriptLevel::hasDialogLines(const std::string& type, const std::string& speaker) {
    loadDialog();
    for (size_t i = 0; i < dialogLines_.size(); ++i)
        if (same(dialogLines_[i].type, type) && same(dialogLines_[i].speaker, speaker))
            return true;
    return false;
}

// A pawn's own line of a type: FindBestDialogLine for it (its speaker
// code; its own tagged lines).
int ScriptLevel::ownLine(VmObject* pawn, const std::string& type) {
    int index = -1;
    findDialogLine(type, 0, 0, 0, "", index, pawn);
    return index;
}

void ScriptLevel::playOwnLine(VmObject* pawn, int index) {
    if (startLine(nextDialogThread_++, index, pawn))
        ++dialog.barks;
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
                                                               optionalName(c, 4), optionalString(c, 5), index, c.self)
                                 : 0;
        c.args[0] = Value::intValue(index);
        c.result = Value::intValue(n);
    });
    // IsDialogLineSpecial(int Index): never said yet, numbered under 800,
    // and tagged (more than a speaker's code) or with a condition (the HUD
    // shows that a crewmate has something to say).
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
    // TestCondition(string Condition): met, or not a condition the engine
    // can test ("Test Failed", read as met).
    reg("Actor.TestCondition", [](NativeCall& c) {
        if (!c.vm.level) {
            c.result = Value::boolValue(false);
            return;
        }
        const int r = c.vm.level->evalCondition(c.s(0), c.self);
        if (r < 0)
            c.vm.reportError("TestCondition: Test Failed: " + c.s(0));
        c.result = Value::boolValue(r != 0);
    });
    // DialogTick(float DeltaTime): the dialogue input's each tick, while the
    // HUD lets it: the AI's own lines (ScriptLevel::dialogTick).
    reg("CTVE_DialogInput.DialogTick", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->dialogTick(c.self, c.f(0));
    });
}

} // namespace reluva::unreal
