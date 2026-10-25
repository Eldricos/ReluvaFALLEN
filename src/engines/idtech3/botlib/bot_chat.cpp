// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' chat. See bot_chat.h.

#include "engines/idtech3/botlib/bot_chat.h"

#include <algorithm>
#include <cctype>

namespace reluva::idtech3::botlib {

namespace {

constexpr float kRecentTime = 20; // a line isn't used again for this long
constexpr int kGenderFemale = 1, kGenderMale = 2;

char upper(char c) { return char(std::toupper(static_cast<unsigned char>(c))); }
bool equalNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return upper(x) == upper(y); });
}
bool separator(char c) { return c == ' ' || c == '.' || c == ',' || c == '!'; }

// Where `word` is in `s` as a whole word (at a word's start, ended by the
// text's end or a separator), from `from`; npos if not.
size_t findWord(const std::string& s, const std::string& word, size_t from = 0) {
    if (word.empty())
        return std::string::npos;
    size_t i = from;
    while (i + word.size() <= s.size()) {
        bool same = true;
        for (size_t j = 0; j < word.size() && same; ++j)
            same = upper(s[i + j]) == upper(word[j]);
        if (same && (i + word.size() == s.size() || separator(s[i + word.size()])))
            return i;
        // On to the next word.
        while (i < s.size() && !separator(s[i]))
            ++i;
        if (i >= s.size())
            break;
        ++i;
    }
    return std::string::npos;
}

// Each whole-word `synonym` replaced by `replacement`, unless it is part
// of the replacement already there.
void replaceWords(std::string& s, const std::string& synonym, const std::string& replacement) {
    size_t at = findWord(s, synonym);
    while (at != std::string::npos) {
        bool inside = false;
        for (size_t r = findWord(s, replacement); r != std::string::npos && !inside; r = findWord(s, replacement, r + 1))
            inside = r <= at && at < r + replacement.size();
        if (!inside) {
            s.replace(at, synonym.size(), replacement);
            if (s.size() >= BotChat::kMessageSize)
                s.resize(BotChat::kMessageSize - 1);
        }
        at = findWord(s, synonym, at + replacement.size());
    }
}

// A line of a chat: strings, variables (numbers) and random words
// (names), separated by commas, ended by a semicolon.
bool readMessage(BotScript& script, std::string& out) {
    out.clear();
    BotToken t;
    while (true) {
        if (!script.next(t)) {
            script.fail("chat message without its end");
            return false;
        }
        if (t.type == BotToken::Type::String)
            out += t.text;
        else if (t.type == BotToken::Type::Number && !t.isFloat)
            out += std::string(1, BotChat::kEscape) + "v" + std::to_string(int(t.number)) + BotChat::kEscape;
        else if (t.type == BotToken::Type::Name)
            out += std::string(1, BotChat::kEscape) + "r" + t.text + BotChat::kEscape;
        else {
            script.fail("unknown chat message part " + t.text);
            return false;
        }
        if (out.size() >= BotChat::kMessageSize) {
            script.fail("chat message too long");
            return false;
        }
        if (script.check(";"))
            return true;
        if (!script.expect(","))
            return false;
    }
}

} // namespace

int BotChat::stringContains(const std::string& a, const std::string& b, bool caseSensitive) {
    if (b.size() > a.size())
        return -1;
    for (size_t i = 0; i + b.size() <= a.size(); ++i) {
        bool same = true;
        for (size_t j = 0; j < b.size() && same; ++j)
            same = caseSensitive ? a[i + j] == b[j] : upper(a[i + j]) == upper(b[j]);
        if (same)
            return int(i);
    }
    return -1;
}

void BotChat::unifyWhiteSpaces(std::string& s) {
    // Anything but letters, digits and ()?:'/,.[]-_+= counts as white
    // space: runs of it become one space, none at either end.
    auto word = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || std::string("()?:'/,.[]-_+=").find(c) != std::string::npos;
    };
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && !word(s[i]))
            ++i;
        size_t j = i;
        while (j < s.size() && word(s[j]))
            ++j;
        if (j > i) {
            if (!out.empty())
                out += ' ';
            out += s.substr(i, j - i);
        }
        i = j;
    }
    s = out;
}

void BotChat::setup(const BotScript::Loader& load, const std::function<std::string(const std::string&)>& libVar,
                    std::string* log) {
    synonyms_.clear();
    randoms_.clear();
    templates_.clear();
    replies_.clear();
    auto file = [&](const char* var, const char* fallback) {
        const std::string v = libVar ? libVar(var) : std::string();
        return v.empty() ? std::string(fallback) : v;
    };
    auto note = [&](const BotScript& s) {
        if (log && !s.error().empty())
            *log += s.error() + "\n";
    };
    BotToken t;
    {
        // Synonyms: context { [ ("word", weight), ("other", weight) ] ... }
        BotScript s(load);
        if (s.open(file("synfile", "syn.c"))) {
            std::vector<int> contexts;
            int context = 0;
            while (s.next(t)) {
                if (t.type == BotToken::Type::Number) {
                    contexts.push_back(int(t.number));
                    context |= int(t.number);
                    if (!s.expect("{"))
                        break;
                } else if (t.is("}")) {
                    if (contexts.empty()) {
                        s.fail("too many }");
                        break;
                    }
                    context &= ~contexts.back();
                    contexts.pop_back();
                } else if (t.is("[")) {
                    Synonyms list;
                    list.context = context;
                    do {
                        std::string word;
                        double weight = 0;
                        if (!s.expect("(") || !s.readString(word) || !s.expect(",") || !s.readNumber(weight) ||
                            !s.expect(")"))
                            break;
                        list.words.emplace_back(word, float(weight));
                        list.total += float(weight);
                    } while (!s.check("]") && s.expect(","));
                    if (!s.error().empty())
                        break;
                    if (list.words.size() >= 2)
                        synonyms_.push_back(std::move(list));
                } else {
                    s.fail("unexpected " + t.text);
                    break;
                }
            }
        }
        note(s);
    }
    {
        // Random words: name = { messages }
        BotScript s(load);
        if (s.open(file("rndfile", "rnd.c"))) {
            while (s.next(t)) {
                if (t.type != BotToken::Type::Name || !s.expect("=") || !s.expect("{")) {
                    s.fail("bad random list at " + t.text);
                    break;
                }
                std::vector<std::string> strings;
                std::string m;
                while (!s.check("}") && readMessage(s, m))
                    strings.push_back(m);
                if (!s.error().empty())
                    break;
                randoms_.emplace_back(t.text, std::move(strings));
            }
        }
        note(s);
    }
    auto readPieces = [&](BotScript& s, const char* end, std::vector<Piece>& pieces) {
        bool lastVariable = false;
        while (s.next(t)) {
            Piece p;
            if (t.type == BotToken::Type::Number && !t.isFloat) {
                if (lastVariable || t.number >= BotMatch::kVariables) {
                    s.fail("bad match variable");
                    return false;
                }
                p.variable = true;
                p.index = int(t.number);
                lastVariable = true;
            } else if (t.type == BotToken::Type::String) {
                p.strings.push_back(t.text);
                while (s.check("|")) {
                    std::string alt;
                    if (!s.readString(alt))
                        return false;
                    p.strings.push_back(alt);
                }
                if (std::none_of(p.strings.begin(), p.strings.end(), [](const std::string& x) { return x.empty(); }))
                    lastVariable = false;
            } else {
                s.fail("invalid token " + t.text);
                return false;
            }
            pieces.push_back(std::move(p));
            if (s.check(end))
                return true;
            if (!s.expect(","))
                return false;
        }
        return false;
    };
    {
        // Match templates: context { pieces = (type, subtype); ... }
        BotScript s(load);
        if (s.open(file("matchfile", "match.c"))) {
            while (s.next(t)) {
                if (t.type != BotToken::Type::Number || !s.expect("{")) {
                    s.fail("expected a context, found " + t.text);
                    break;
                }
                const int context = int(t.number);
                while (!s.check("}") && s.error().empty()) {
                    Template m;
                    m.context = context;
                    double type = 0, subtype = 0;
                    if (!readPieces(s, "=", m.pieces) || !s.expect("(") || !s.readNumber(type) || !s.expect(",") ||
                        !s.readNumber(subtype) || !s.expect(")") || !s.expect(";"))
                        break;
                    m.type = int(type);
                    m.subtype = int(subtype);
                    templates_.push_back(std::move(m));
                }
                if (!s.error().empty())
                    break;
            }
        }
        note(s);
    }
    if (libVar && libVar("nochat") != "" && libVar("nochat") != "0")
        return;
    {
        // Replies: [keys] = priority { messages }
        BotScript s(load);
        std::string probe;
        if (!load(file("rchatfile", "rchat.c"), probe)) {
            if (log)
                *log += file("rchatfile", "rchat.c") + " not found: the bots won't reply to chat\n";
        } else if (s.open(file("rchatfile", "rchat.c"))) {
            while (s.next(t)) {
                if (!t.is("[")) {
                    s.fail("expected [, found " + t.text);
                    break;
                }
                Reply r;
                do {
                    ReplyKey k;
                    if (s.check("&"))
                        k.must = true;
                    else if (s.check("!"))
                        k.mustNot = true;
                    if (s.check("name"))
                        k.kind = ReplyKey::Name;
                    else if (s.check("female"))
                        k.kind = ReplyKey::Female;
                    else if (s.check("male"))
                        k.kind = ReplyKey::Male;
                    else if (s.check("it"))
                        k.kind = ReplyKey::Genderless;
                    else if (s.check("(")) {
                        k.kind = ReplyKey::Match;
                        if (!readPieces(s, ")", k.match))
                            break;
                    } else if (s.check("<")) {
                        k.kind = ReplyKey::BotNames;
                        do {
                            std::string n;
                            if (!s.readString(n))
                                break;
                            k.text += (k.text.empty() ? "" : "\\") + n;
                        } while (s.check(","));
                        if (!s.expect(">"))
                            break;
                    } else if (!s.readString(k.text)) {
                        break;
                    }
                    r.keys.push_back(std::move(k));
                    s.check(",");
                } while (!s.check("]") && s.error().empty());
                double priority = 0;
                if (!s.error().empty() || !s.expect("=") || !s.readNumber(priority) || !s.expect("{"))
                    break;
                r.priority = float(priority);
                std::string m;
                while (!s.check("}") && readMessage(s, m))
                    r.messages.push_back({m});
                if (!s.error().empty())
                    break;
                replies_.push_back(std::move(r));
            }
        }
        note(s);
    }
    if (log)
        *log += std::to_string(synonyms_.size()) + " synonym lists, " + std::to_string(randoms_.size()) +
                " random lists, " + std::to_string(templates_.size()) + " match templates, " +
                std::to_string(replies_.size()) + " replies\n";
}

BotChat::State* BotChat::state(int handle) {
    return handle >= 1 && size_t(handle) <= states_.size() ? states_[size_t(handle) - 1].get() : nullptr;
}
const BotChat::State* BotChat::state(int handle) const {
    return handle >= 1 && size_t(handle) <= states_.size() ? states_[size_t(handle) - 1].get() : nullptr;
}

int BotChat::allocState() {
    for (size_t i = 0; i < states_.size(); ++i)
        if (!states_[i]) {
            states_[i] = std::make_unique<State>();
            return int(i) + 1;
        }
    states_.push_back(std::make_unique<State>());
    return int(states_.size());
}

void BotChat::freeState(int handle) {
    if (state(handle))
        states_[size_t(handle) - 1].reset();
}

bool BotChat::loadChatFile(int handle, const BotScript::Loader& load, const std::string& file,
                           const std::string& name) {
    State* cs = state(handle);
    if (!cs)
        return false;
    // Each file's chat read once, shared by the bots using it.
    const std::string key = file + "|" + name;
    for (const auto& [k, chat] : chatFiles_)
        if (k == key) {
            cs->chat = chat;
            return true;
        }
    // chat "name" { type "kind" { messages } ... }; other chats skipped.
    BotScript s(load);
    if (!s.open(file))
        return false;
    auto chat = std::make_shared<std::vector<ChatType>>();
    bool found = false;
    BotToken t;
    while (s.next(t)) {
        std::string chatName;
        if (!t.is("chat") || !s.readString(chatName) || !s.expect("{")) {
            s.fail("expected chat, found " + t.text);
            break;
        }
        if (!equalNoCase(chatName, name)) {
            if (!s.skipBlock())
                break;
            continue;
        }
        found = true;
        while (!s.check("}") && s.error().empty()) {
            ChatType type;
            if (!s.expect("type") || !s.readString(type.name) || !s.expect("{"))
                break;
            std::string m;
            while (!s.check("}") && readMessage(s, m))
                type.messages.push_back({m});
            chat->push_back(std::move(type));
        }
    }
    if (!s.error().empty() || !found)
        return false;
    chatFiles_.emplace_back(key, chat);
    cs->chat = chat;
    return true;
}

void BotChat::setGender(int handle, int gender) {
    if (State* cs = state(handle))
        cs->gender = gender == kGenderFemale || gender == kGenderMale ? gender : 0;
}

void BotChat::setName(int handle, const std::string& name) {
    if (State* cs = state(handle))
        cs->name = name.substr(0, 31);
}

void BotChat::queueConsoleMessage(int handle, int type, const std::string& text) {
    State* cs = state(handle);
    if (!cs || cs->console.size() >= 1024)
        return;
    if (++cs->nextHandle > 8192)
        cs->nextHandle = 1;
    cs->console.push_back({cs->nextHandle, now(), type, text.substr(0, kMessageSize - 1)});
}

void BotChat::removeConsoleMessage(int handle, int message) {
    State* cs = state(handle);
    if (!cs)
        return;
    for (auto it = cs->console.begin(); it != cs->console.end(); ++it)
        if (it->handle == message) {
            cs->console.erase(it);
            return;
        }
}

const BotChat::ConsoleMessage* BotChat::nextConsoleMessage(int handle) const {
    const State* cs = state(handle);
    return cs && !cs->console.empty() ? &cs->console.front() : nullptr;
}

int BotChat::numConsoleMessages(int handle) const {
    const State* cs = state(handle);
    return cs ? int(cs->console.size()) : 0;
}

int BotChat::numInitialChats(int handle, const std::string& type) const {
    const State* cs = state(handle);
    if (!cs || !cs->chat)
        return 0;
    for (const ChatType& t : *cs->chat)
        if (equalNoCase(t.name, type))
            return int(t.messages.size());
    return 0;
}

BotMatch BotChat::withVariables(BotMatch match, const std::vector<const char*>& vars) {
    // The game's words (names, places) after the text, as variables 0-7.
    for (size_t i = 0; i < vars.size() && i < size_t(BotMatch::kVariables); ++i) {
        if (!vars[i])
            continue;
        match.offset[i] = int(match.text.size());
        match.length[i] = int(std::char_traits<char>::length(vars[i]));
        match.text += vars[i];
    }
    return match;
}

void BotChat::initialChat(int handle, const std::string& type, int mcontext, const std::vector<const char*>& vars) {
    State* cs = state(handle);
    if (!cs || !cs->chat)
        return;
    for (ChatType& t : *cs->chat) {
        if (!equalNoCase(t.name, type))
            continue;
        // A line not used lately, at random; else the one used longest ago.
        std::vector<Message*> fresh;
        for (Message& m : t.messages)
            if (m.time <= now())
                fresh.push_back(&m);
        Message* chosen = nullptr;
        if (fresh.empty()) {
            for (Message& m : t.messages)
                if (!chosen || m.time < chosen->time)
                    chosen = &m;
        } else {
            chosen = fresh[std::uniform_int_distribution<size_t>(0, fresh.size() - 1)(random_)];
            chosen->time = now() + kRecentTime;
        }
        if (chosen)
            cs->message = construct(chosen->text, mcontext, withVariables(BotMatch{}, vars), 0, false);
        return;
    }
}

bool BotChat::replyChat(int handle, const std::string& message, int mcontext, int vcontext,
                        const std::vector<const char*>& vars) {
    State* cs = state(handle);
    if (!cs)
        return false;
    BotMatch match;
    match.text = message.substr(0, kMessageSize - 1);
    // The reply with the highest priority whose keys fit the message: all
    // its & keys, none of its ! keys, and one of the others.
    Reply* best = nullptr;
    Message* bestMessage = nullptr;
    BotMatch bestMatch;
    for (Reply& r : replies_) {
        bool found = false;
        for (const ReplyKey& k : r.keys) {
            bool res = false;
            switch (k.kind) {
            case ReplyKey::Name: res = stringContains(message, cs->name, false) >= 0; break;
            case ReplyKey::BotNames: res = stringContains(k.text, cs->name, false) >= 0; break;
            case ReplyKey::Female: res = cs->gender == kGenderFemale; break;
            case ReplyKey::Male: res = cs->gender == kGenderMale; break;
            case ReplyKey::Genderless: res = cs->gender == 0; break;
            case ReplyKey::Match: res = stringsMatch(k.match, match); break;
            case ReplyKey::String: res = findWord(message, k.text) != std::string::npos; break;
            }
            if (k.must && !res) {
                found = false;
                break;
            }
            if (k.mustNot && res) {
                found = false;
                break;
            }
            if (!k.must && !k.mustNot && res)
                found = true;
        }
        if (!found || (best && r.priority <= best->priority) || r.messages.empty())
            continue;
        std::vector<Message*> fresh;
        for (Message& m : r.messages)
            if (m.time <= now())
                fresh.push_back(&m);
        if (fresh.empty())
            continue;
        best = &r;
        bestMessage = fresh[std::uniform_int_distribution<size_t>(0, fresh.size() - 1)(random_)];
        bestMatch = match;
    }
    if (!bestMessage)
        return false;
    bestMessage->time = now() + kRecentTime;
    cs->message = construct(bestMessage->text, mcontext, withVariables(bestMatch, vars), vcontext, true);
    return true;
}

int BotChat::chatLength(int handle) const {
    const State* cs = state(handle);
    return cs ? int(cs->message.size()) : 0;
}

void BotChat::enterChat(int handle, int client, int sendTo) {
    State* cs = state(handle);
    if (!cs || cs->message.empty())
        return;
    const std::string text = takeChatMessage(handle);
    if (command)
        command(client, (sendTo == 1 ? "say_team " : "say ") + text);
}

std::string BotChat::takeChatMessage(int handle) {
    State* cs = state(handle);
    if (!cs)
        return {};
    std::string text;
    for (char c : cs->message)
        if (c != '~')
            text += c;
    cs->message.clear();
    return text;
}

bool BotChat::stringsMatch(const std::vector<Piece>& pieces, BotMatch& match) const {
    // Each string piece found in order (one of its alternatives); a
    // variable takes what lies between.
    int lastVariable = -1;
    size_t pos = 0;
    for (const Piece& p : pieces) {
        if (p.variable) {
            match.offset[size_t(p.index)] = int(pos);
            lastVariable = p.index;
            continue;
        }
        size_t next = std::string::npos, used = 0;
        for (const std::string& alt : p.strings) {
            if (alt.empty()) {
                next = pos;
                used = 0;
                break;
            }
            const int index = stringContains(match.text.substr(pos), alt, false);
            if (index < 0)
                continue;
            if (lastVariable >= 0) {
                next = pos + size_t(index);
                match.length[size_t(lastVariable)] = int(next) - match.offset[size_t(lastVariable)];
                lastVariable = -1;
                used = alt.size();
                break;
            }
            if (index == 0) {
                next = pos;
                used = alt.size();
                break;
            }
        }
        if (next == std::string::npos)
            return false;
        pos = next + used;
    }
    if (lastVariable >= 0) {
        match.length[size_t(lastVariable)] = int(match.text.size()) - match.offset[size_t(lastVariable)];
        return true;
    }
    return pos >= match.text.size();
}

bool BotChat::findMatch(const std::string& text, BotMatch& match, int context) const {
    match = BotMatch{};
    match.text = text.substr(0, kMessageSize - 1);
    while (!match.text.empty() && match.text.back() == '\n')
        match.text.pop_back();
    for (const Template& t : templates_) {
        if (!(t.context & context))
            continue;
        match.offset.fill(-1);
        if (stringsMatch(t.pieces, match)) {
            match.type = t.type;
            match.subtype = t.subtype;
            return true;
        }
    }
    return false;
}

std::string BotChat::matchVariable(const BotMatch& match, int variable) {
    if (variable < 0 || variable >= BotMatch::kVariables || match.offset[size_t(variable)] < 0)
        return {};
    const size_t at = size_t(match.offset[size_t(variable)]);
    return at < match.text.size() ? match.text.substr(at, size_t(std::max(0, match.length[size_t(variable)]))) : "";
}

void BotChat::replaceSynonyms(std::string& s, int context) const {
    // Every other word of a list in the context becomes its first.
    for (const Synonyms& list : synonyms_)
        if (list.context & context)
            for (size_t i = 1; i < list.words.size(); ++i)
                replaceWords(s, list.words[i].first, list.words[0].first);
}

void BotChat::replaceWeightedSynonyms(std::string& s, int context) {
    // For variety: each list's words all become one of them, chosen by weight.
    for (const Synonyms& list : synonyms_) {
        if (!(list.context & context))
            continue;
        const float pick = std::uniform_real_distribution<float>(0, 1)(random_) * list.total;
        if (pick <= 0)
            continue;
        float sum = 0;
        size_t chosen = list.words.size();
        for (size_t i = 0; i < list.words.size(); ++i) {
            sum += list.words[i].second;
            if (pick < sum) {
                chosen = i;
                break;
            }
        }
        if (chosen >= list.words.size())
            continue;
        for (size_t i = 0; i < list.words.size(); ++i)
            if (i != chosen)
                replaceWords(s, list.words[i].first, list.words[chosen].first);
    }
}

void BotChat::replaceReplySynonyms(std::string& s, int context) const {
    // Word by word: one starting with a synonym becomes the list's first.
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ')
            ++i;
        if (i >= s.size())
            break;
        bool done = false;
        for (const Synonyms& list : synonyms_) {
            if (!(list.context & context))
                continue;
            for (size_t k = 1; k < list.words.size() && !done; ++k) {
                if (findWord(s, list.words[k].first, i) != i)
                    continue;
                if (findWord(s, list.words[0].first, i) == i)
                    continue;
                s.replace(i, list.words[k].first.size(), list.words[0].first);
                done = true;
            }
            if (done)
                break;
        }
        while (i < s.size() && static_cast<unsigned char>(s[i]) > ' ')
            ++i;
    }
}

bool BotChat::expand(std::string& out, const std::string& message, int mcontext, const BotMatch& match, int vcontext,
                     bool reply) {
    out.clear();
    bool expanded = false;
    bool ok = true;
    for (size_t i = 0; i < message.size() && ok;) {
        if (message[i] != kEscape) {
            out += message[i++];
            continue;
        }
        const char kind = i + 1 < message.size() ? message[i + 1] : 0;
        const size_t end = message.find(kEscape, i + 2);
        const std::string inner = message.substr(i + 2, end == std::string::npos ? std::string::npos : end - i - 2);
        i = end == std::string::npos ? message.size() : end + 1;
        if (kind == 'v') {
            const int v = std::atoi(inner.c_str());
            if (v < 0 || v >= BotMatch::kVariables) {
                ok = false;
                break;
            }
            if (match.offset[size_t(v)] >= 0) {
                std::string word = matchVariable(match, v);
                if (reply)
                    replaceReplySynonyms(word, vcontext);
                else
                    replaceSynonyms(word, vcontext);
                out += word;
            }
        } else if (kind == 'r') {
            const std::vector<std::string>* list = nullptr;
            for (const auto& [name, strings] : randoms_)
                if (name == inner)
                    list = &strings;
            if (!list || list->empty()) {
                ok = false;
                break;
            }
            out += (*list)[std::uniform_int_distribution<size_t>(0, list->size() - 1)(random_)];
            expanded = true;
        }
        if (out.size() >= kMessageSize) {
            out.resize(kMessageSize - 1);
            ok = false;
        }
    }
    replaceWeightedSynonyms(out, mcontext);
    return ok && expanded;
}

std::string BotChat::construct(const std::string& message, int mcontext, const BotMatch& match, int vcontext,
                               bool reply) {
    std::string source = message, out;
    for (int i = 0; i < 10; ++i) {
        const bool more = expand(out, source, mcontext, match, vcontext, reply);
        source = out;
        if (!more)
            break;
    }
    return out;
}

} // namespace reluva::idtech3::botlib
