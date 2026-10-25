// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::newworlds {

// Reluva's own mission scripts: what a mission reacts to and how, written
// from recordings of play (docs/formats/newworlds/play*.md), since the
// game keeps its scripts in STNW.exe. The format (docs/formats/newworlds/
// mission-scripts.md):
//
//   mission F1 F                     a mission and the side it's played as
//   site temple 332 152              a named place (a map position)
//   objective 16                     an objective: its string id in the
//   objective 19 hidden              mission's table; hidden until revealed;
//   objective 21 optional            optional ones don't hold up the end
//   lose 21 destroyed colony-hub     lost, with that fail string, on an event
//   on <event> <args>...             once, the first time the event happens
//   every <event> <args>...          each time
//     say <sender> "text"            then actions, indented
//     complete 16 | reveal 19 | win | lose 21
//     research <topic> | timer <name> <seconds>
//
// Events: start; built <building>; built-all <building>[*count][|...]...;
// approach <who> <site>; scan-started, scanned, scan-lost <kind> <site>;
// scanned-nothing <kind>; researched <topic>; entered, captured, arrived
// <site>; destroyed <what>; hub-destroyed, side-failed <side>; completed
// <objective>; all-completed; timer <name>; mine-exhausted, power-low,
// short-resources, short-crew. An argument "*" matches anything. In text,
// {site:name} becomes the site's position, {a}, {b} the event's position,
// {1} the event's first argument (dashes as spaces, capitalised).

struct ScriptEvent {
    std::string kind;
    std::vector<std::string> args;
    int a = -1, b = -1; // a map position, when the event has one
};

struct MissionScript {
    struct Action {
        std::string verb;
        std::vector<std::string> args;
    };
    struct Handler {
        bool every = false;
        ScriptEvent event;
        std::vector<Action> actions;
        int line = 0;
    };
    struct Objective {
        uint32_t id = 0;
        bool hidden = false, optional = false;
    };
    std::string mission;
    char side = 'F';
    std::map<std::string, std::pair<int, int>> sites;
    std::vector<Objective> objectives;
    std::vector<Handler> handlers;
};

// Reads scripts; one text may hold several missions.
Error parseMissionScripts(std::string_view text, std::vector<MissionScript>& out);

// The scripts Reluva has (mission_scripts.cpp): the missions' own, and the
// rules every mission shares ("generic").
const std::vector<MissionScript>& missionScripts();
const MissionScript* findMissionScript(std::string_view shortName, char side);

// A mission being played: feed it events, get back what happens.
class MissionRun {
public:
    struct Output {
        enum Kind { Message, Completed, Revealed, Won, Lost } kind;
        std::string sender, text; // a message
        uint32_t objective = 0;    // Completed, Revealed, Lost: the string id
    };
    enum class State { Incomplete, Complete, Hidden };

    // generic: rules added after the mission's own (may be null).
    MissionRun(const MissionScript& script, const MissionScript* generic);

    std::vector<Output> fire(const ScriptEvent& event);
    // Moves time on, firing timers that run out.
    std::vector<Output> advance(double seconds);
    bool finished() const { return won_ || lost_; }
    bool won() const { return won_; }
    State objectiveState(uint32_t id) const;
    // Seconds to the next timer, or a negative number if none is running.
    double nextTimer() const;

    // The events the script's own handlers wait for, in the order written
    // (which is the order they happened in the recording): a walkthrough.
    std::vector<ScriptEvent> walkthrough() const;

private:
    struct Rule {
        const MissionScript::Handler* handler;
        bool fired = false;
    };
    bool matches(const ScriptEvent& want, const ScriptEvent& got);
    bool requirementsMet(const ScriptEvent& want) const;
    void run(const MissionScript::Handler& h, const ScriptEvent& ev, std::vector<Output>& out);
    std::string expand(const std::string& text, const ScriptEvent& ev) const;
    void checkAllCompleted(std::vector<Output>& out);

    const MissionScript& script_;
    std::vector<Rule> rules_;
    std::map<std::string, int> built_;
    std::set<uint32_t> completed_, revealed_;
    std::map<std::string, double> timers_;
    bool won_ = false, lost_ = false, allDone_ = false;
    int depth_ = 0;
};

// The game's spoken announcement (a stem in Media/Misc) for what a step
// did, for the side ('F', 'K', 'R'): the mission won or lost, an objective
// met, power low, production held up (Federation), a building finished
// (its name, B_f...com, A_k...com, B_r...com); the first of these that
// applies, or empty. Matched in recordings (docs/formats/newworlds/media.md).
std::string announcementFor(const ScriptEvent& event, const std::vector<MissionRun::Output>& out, char side);

} // namespace reluva::newworlds
