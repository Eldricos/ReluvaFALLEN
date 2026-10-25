// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/mission_script.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace reluva::newworlds {

namespace {

// Words and "quoted strings" of a line, up to a # comment.
bool tokenize(std::string_view line, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '#') {
            break;
        } else if (c == '"') {
            const size_t end = line.find('"', i + 1);
            if (end == std::string_view::npos)
                return false;
            out.emplace_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            size_t end = i;
            while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])) && line[end] != '#')
                ++end;
            out.emplace_back(line.substr(i, end - i));
            i = end;
        }
    }
    return true;
}

bool toInt(const std::string& s, int& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end)
        return false;
    out = int(v);
    return true;
}

bool sameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// "colony-hub" -> "Colony hub"
std::string displayName(const std::string& id) {
    std::string s = id;
    std::replace(s.begin(), s.end(), '-', ' ');
    if (!s.empty())
        s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

const char* sectionName(const std::string& s) {
    static const std::pair<const char*, const char*> names[] = {
        {"command", "Command"},         {"operations", "Operations"}, {"engineering", "Engineering"},
        {"science", "Science"},         {"medical", "Medical"},       {"security", "Security"},
        {"intelligence", "Intelligence"}, {"communications", "Communications"}};
    for (const auto& [id, name] : names)
        if (s == id)
            return name;
    return nullptr;
}

} // namespace

Error parseMissionScripts(std::string_view text, std::vector<MissionScript>& out) {
    out.clear();
    int lineNo = 0;
    std::vector<std::string> t;
    while (!text.empty()) {
        ++lineNo;
        const size_t nl = text.find('\n');
        const std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        auto fail = [&](const std::string& why) {
            return Error::fail("mission script line " + std::to_string(lineNo) + ": " + why);
        };
        if (!tokenize(line, t))
            return fail("unterminated string");
        if (t.empty())
            continue;
        const bool indented = std::isspace(static_cast<unsigned char>(line[0]));
        if (t[0] == "mission") {
            if (t.size() != 3 || t[2].size() != 1)
                return fail("expected: mission <name> <side>");
            out.push_back({});
            out.back().mission = t[1];
            out.back().side = char(std::toupper(static_cast<unsigned char>(t[2][0])));
            continue;
        }
        if (out.empty())
            return fail("expected a mission line first");
        MissionScript& m = out.back();
        if (indented) {
            if (m.handlers.empty())
                return fail("an action outside an on/every block");
            m.handlers.back().actions.push_back({t[0], {t.begin() + 1, t.end()}});
            continue;
        }
        if (t[0] == "site") {
            int a, b;
            if (t.size() != 4 || !toInt(t[2], a) || !toInt(t[3], b))
                return fail("expected: site <name> <a> <b>");
            m.sites[t[1]] = {a, b};
        } else if (t[0] == "objective") {
            int id;
            if (t.size() < 2 || !toInt(t[1], id))
                return fail("expected: objective <string id> [hidden] [optional]");
            MissionScript::Objective o;
            o.id = uint32_t(id);
            for (size_t i = 2; i < t.size(); ++i) {
                if (t[i] == "hidden")
                    o.hidden = true;
                else if (t[i] == "optional")
                    o.optional = true;
                else
                    return fail("unknown objective flag " + t[i]);
            }
            m.objectives.push_back(o);
        } else if (t[0] == "lose") {
            int id;
            if (t.size() < 3 || !toInt(t[1], id))
                return fail("expected: lose <string id> <event>...");
            MissionScript::Handler h;
            h.event = {t[2], {t.begin() + 3, t.end()}};
            h.actions.push_back({"lose", {t[1]}});
            h.line = lineNo;
            m.handlers.push_back(std::move(h));
        } else if (t[0] == "on" || t[0] == "every") {
            if (t.size() < 2)
                return fail("expected an event");
            MissionScript::Handler h;
            h.every = t[0] == "every";
            h.event = {t[1], {t.begin() + 2, t.end()}};
            h.line = lineNo;
            m.handlers.push_back(std::move(h));
        } else {
            return fail("unknown line " + t[0]);
        }
    }
    // Check the actions now that the whole script is read.
    for (const MissionScript& m : out)
        for (const MissionScript::Handler& h : m.handlers)
            for (const MissionScript::Action& a : h.actions) {
                const auto bad = [&](const std::string& why) {
                    return Error::fail("mission " + m.mission + " line " + std::to_string(h.line) + ": " + why);
                };
                int n;
                if (a.verb == "say") {
                    if (a.args.size() != 2)
                        return bad("expected: say <sender> \"text\"");
                } else if (a.verb == "complete" || a.verb == "reveal" || a.verb == "lose") {
                    if (a.args.size() != 1 || !toInt(a.args[0], n))
                        return bad("expected: " + a.verb + " <objective id>");
                } else if (a.verb == "timer") {
                    if (a.args.size() != 2 || !toInt(a.args[1], n))
                        return bad("expected: timer <name> <seconds>");
                } else if (a.verb == "research") {
                    if (a.args.size() != 1)
                        return bad("expected: research <topic>");
                } else if (a.verb != "win") {
                    return bad("unknown action " + a.verb);
                }
            }
    return Error::success();
}

const MissionScript* findMissionScript(std::string_view shortName, char side) {
    for (const MissionScript& s : missionScripts())
        if (sameName(s.mission, shortName) && s.side == std::toupper(static_cast<unsigned char>(side)))
            return &s;
    return nullptr;
}

MissionRun::MissionRun(const MissionScript& script, const MissionScript* generic) : script_(script) {
    for (const auto& h : script.handlers)
        rules_.push_back({&h});
    if (generic)
        for (const auto& h : generic->handlers)
            rules_.push_back({&h});
    for (const auto& o : script.objectives)
        if (!o.hidden)
            revealed_.insert(o.id);
}

bool MissionRun::requirementsMet(const ScriptEvent& want) const {
    // built-all: each argument "name[*count]" (alternatives with |) built.
    for (const std::string& req : want.args) {
        bool any = false;
        size_t from = 0;
        while (from <= req.size() && !any) {
            const size_t bar = std::min(req.find('|', from), req.size());
            const std::string alt = req.substr(from, bar - from);
            const size_t star = alt.find('*');
            const std::string name = alt.substr(0, star);
            const int count = star == std::string::npos ? 1 : std::atoi(alt.c_str() + star + 1);
            const auto it = built_.find(name);
            any = it != built_.end() && it->second >= count;
            from = bar + 1;
        }
        if (!any)
            return false;
    }
    return true;
}

bool MissionRun::matches(const ScriptEvent& want, const ScriptEvent& got) {
    if (want.kind == "built-all")
        return got.kind == "built" && requirementsMet(want);
    if (want.kind != got.kind || want.args.size() > got.args.size())
        return false;
    for (size_t i = 0; i < want.args.size(); ++i)
        if (want.args[i] != "*" && want.args[i] != got.args[i])
            return false;
    return true;
}

std::string MissionRun::expand(const std::string& text, const ScriptEvent& ev) const {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
        if (close == std::string::npos) {
            out += text[i];
            continue;
        }
        const std::string key = text.substr(i + 1, close - i - 1);
        auto pos = [](int v) {
            std::string s = std::to_string(v);
            return std::string(s.size() < 3 ? 3 - s.size() : 0, '0') + s;
        };
        if (key == "a" && ev.a >= 0)
            out += pos(ev.a);
        else if (key == "b" && ev.b >= 0)
            out += pos(ev.b);
        else if (key == "1" && !ev.args.empty())
            out += displayName(ev.args[0]);
        else if (key.rfind("site:", 0) == 0 && script_.sites.count(key.substr(5))) {
            const auto [a, b] = script_.sites.at(key.substr(5));
            out += "<" + pos(a) + ":" + pos(b) + ">";
        } else {
            out += text.substr(i, close - i + 1);
        }
        i = close;
    }
    return out;
}

void MissionRun::run(const MissionScript::Handler& h, const ScriptEvent& ev, std::vector<Output>& out) {
    std::vector<ScriptEvent> follow;
    for (const MissionScript::Action& a : h.actions) {
        if (finished())
            break;
        if (a.verb == "say") {
            const char* section = sectionName(a.args[0]);
            out.push_back({Output::Message, section ? section : a.args[0], expand(a.args[1], ev), 0});
        } else if (a.verb == "complete") {
            const uint32_t id = uint32_t(std::atoi(a.args[0].c_str()));
            if (completed_.insert(id).second) {
                revealed_.insert(id);
                out.push_back({Output::Completed, {}, {}, id});
                follow.push_back({"completed", {a.args[0]}});
            }
        } else if (a.verb == "reveal") {
            const uint32_t id = uint32_t(std::atoi(a.args[0].c_str()));
            if (revealed_.insert(id).second)
                out.push_back({Output::Revealed, {}, {}, id});
        } else if (a.verb == "win") {
            won_ = true;
            out.push_back({Output::Won, {}, {}, 0});
        } else if (a.verb == "lose") {
            lost_ = true;
            out.push_back({Output::Lost, {}, {}, uint32_t(std::atoi(a.args[0].c_str()))});
        } else if (a.verb == "timer") {
            timers_[a.args[0]] = std::atof(a.args[1].c_str());
        } else if (a.verb == "research") {
            follow.push_back({"research-available", {a.args[0]}});
        }
    }
    for (const ScriptEvent& f : follow) {
        auto more = fire(f);
        out.insert(out.end(), more.begin(), more.end());
    }
}

void MissionRun::checkAllCompleted(std::vector<Output>& out) {
    if (allDone_ || finished())
        return;
    bool any = false;
    for (const auto& o : script_.objectives) {
        if (o.optional || !revealed_.count(o.id))
            continue;
        if (!completed_.count(o.id))
            return;
        any = true;
    }
    if (!any)
        return;
    allDone_ = true;
    auto more = fire({"all-completed", {}});
    out.insert(out.end(), more.begin(), more.end());
}

std::vector<MissionRun::Output> MissionRun::fire(const ScriptEvent& event) {
    std::vector<Output> out;
    if (finished() || depth_ > 16)
        return out;
    ++depth_;
    if (event.kind == "built" && !event.args.empty())
        ++built_[event.args[0]];
    for (Rule& r : rules_) {
        if ((r.fired && !r.handler->every) || !matches(r.handler->event, event))
            continue;
        r.fired = true;
        run(*r.handler, event, out);
        if (finished())
            break;
    }
    if (depth_ == 1)
        checkAllCompleted(out);
    --depth_;
    return out;
}

std::vector<MissionRun::Output> MissionRun::advance(double seconds) {
    std::vector<Output> out;
    std::vector<std::string> due;
    for (auto& [name, left] : timers_) {
        left -= seconds;
        if (left <= 0)
            due.push_back(name);
    }
    for (const std::string& name : due) {
        timers_.erase(name);
        auto more = fire({"timer", {name}});
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

double MissionRun::nextTimer() const {
    double next = -1;
    for (const auto& [name, left] : timers_)
        if (next < 0 || left < next)
            next = left;
    return next;
}

MissionRun::State MissionRun::objectiveState(uint32_t id) const {
    if (completed_.count(id))
        return State::Complete;
    return revealed_.count(id) ? State::Incomplete : State::Hidden;
}

std::vector<ScriptEvent> MissionRun::walkthrough() const {
    std::vector<ScriptEvent> out;
    for (const auto& h : script_.handlers) {
        const ScriptEvent& e = h.event;
        // Lost-mission handlers, follow-on events and timers happen by
        // themselves (or shouldn't, in a walkthrough).
        if (h.every || e.kind == "completed" || e.kind == "all-completed" || e.kind == "timer" ||
            e.kind == "research-available" || (!h.actions.empty() && h.actions[0].verb == "lose"))
            continue;
        if (e.kind == "built-all") {
            for (const std::string& req : e.args) {
                const std::string alt = req.substr(0, req.find('|'));
                const size_t star = alt.find('*');
                const int n = star == std::string::npos ? 1 : std::atoi(alt.c_str() + star + 1);
                for (int i = 0; i < n; ++i)
                    out.push_back({"built", {alt.substr(0, star)}});
            }
            continue;
        }
        ScriptEvent ev = e;
        for (std::string& arg : ev.args)
            if (arg == "*")
                arg = "somewhere";
        if (!e.args.empty() && script_.sites.count(e.args.back())) {
            ev.a = script_.sites.at(e.args.back()).first;
            ev.b = script_.sites.at(e.args.back()).second;
        }
        out.push_back(std::move(ev));
    }
    return out;
}

std::string announcementFor(const ScriptEvent& event, const std::vector<MissionRun::Output>& out, char side) {
    const std::string s = side == 'K' ? "k" : side == 'R' ? "r" : "f";
    auto has = [&](MissionRun::Output::Kind k) {
        return std::any_of(out.begin(), out.end(), [&](const MissionRun::Output& o) { return o.kind == k; });
    };
    if (has(MissionRun::Output::Won))
        return s + "miswin";
    if (has(MissionRun::Output::Lost))
        return s + "mislose";
    if (has(MissionRun::Output::Completed))
        return s + "objmet";
    if (event.kind == "power-low")
        return s + "lowpowr";
    if ((event.kind == "short-resources" || event.kind == "short-crew") && side == 'F')
        return "FED002c";
    if (event.kind != "built" || event.args.empty())
        return {};
    // A building's spoken name: its short name in the file's (fab, the
    // Federation's accommodation block, and fcy heard as each finished).
    struct Name {
        const char* building;
        const char *fed, *kln, *rom;
    };
    static constexpr Name kNames[] = {
        {"construction-yard", "cy", "cy", "cy"}, {"vehicle-yard", "vy", "vy", "vy"},
        {"accommodation", "ab", "a", ""},       {"arsenal", "", "a", ""},
        {"power-generator", "pg", "pg", ""},    {"advanced-generator", "apg", "apg", ""},
        {"resource-processor", "rp", "rp", "rp"}, {"mining-station", "ms", "ms", "ms"},
        {"sickbay", "sb", "sb", ""},            {"science-station", "", "ss", ""},
        {"security-center", "sc", "", ""},      {"hydroponics-farm", "hf", "", ""},
        {"livestock-farm", "", "lf", ""},       {"storage-facility", "sf", "sf", "sf"},
        {"transporter-pad", "tp", "tp", "tp"},  {"photon-turret", "pt", "", ""},
        {"colony-hub", "", "ch", ""},           {"disruptor-tank", "", "dt", ""},
        {"disruptor-turret", "", "sg", ""},
    };
    for (const Name& n : kNames)
        if (event.args[0] == n.building) {
            const std::string code = side == 'K' ? n.kln : side == 'R' ? n.rom : n.fed;
            if (code.empty())
                return {};
            return std::string(side == 'K' ? "A_" : "B_") + s + code + "com";
        }
    return {};
}

} // namespace reluva::newworlds
