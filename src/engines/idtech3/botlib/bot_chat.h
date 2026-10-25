// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// A chat line matched against the match templates (match.c): its type and
// subtype, and where each variable (a name, a place...) is in the text.
struct BotMatch {
    static constexpr int kVariables = 8;
    std::string text;
    int type = 0, subtype = 0;
    std::array<int, kVariables> offset{}, length{}; // offset -1: not set
    BotMatch() { offset.fill(-1); }
};

// The bots' chat: what each says when something happens (its chat file,
// bots/<name>_t.c, by type: "game_enter", "kill_insult"...), its replies to
// what others say (rchat.c), and the files they share: synonyms (syn.c),
// random words (rnd.c) and the templates for understanding chat
// (match.c). Behaviour and formats read from the GPL ioquake3 bot library
// (be_ai_chat.c); docs/formats/idtech3/aas.md; new code.
class BotChat {
public:
    static constexpr size_t kMessageSize = 256;
    static constexpr char kEscape = 1; // starts and ends a variable or random word in a message

    // Reads the shared files (names from the library's settings).
    void setup(const BotScript::Loader& load, const std::function<std::string(const std::string&)>& libVar,
               std::string* log = nullptr);
    std::function<float()> time;           // the library's clock
    std::function<void(int client, const std::string& command)> command;

    int allocState();
    void freeState(int handle);
    bool loadChatFile(int handle, const BotScript::Loader& load, const std::string& file, const std::string& name);
    void setGender(int handle, int gender);
    void setName(int handle, const std::string& name);

    // The console messages a bot has been sent (the game queues them).
    struct ConsoleMessage {
        int handle = 0;
        float time = 0;
        int type = 0;
        std::string text;
    };
    void queueConsoleMessage(int handle, int type, const std::string& text);
    void removeConsoleMessage(int handle, int message);
    const ConsoleMessage* nextConsoleMessage(int handle) const;
    int numConsoleMessages(int handle) const;

    // Choosing a line: of a type from the bot's chat file, or the reply
    // that fits a message best. Kept until entered (said) or taken.
    int numInitialChats(int handle, const std::string& type) const;
    void initialChat(int handle, const std::string& type, int mcontext, const std::vector<const char*>& vars);
    bool replyChat(int handle, const std::string& message, int mcontext, int vcontext,
                   const std::vector<const char*>& vars);
    int chatLength(int handle) const;
    // Says the chosen line: to everyone (0) or the team (1).
    void enterChat(int handle, int client, int sendTo);
    std::string takeChatMessage(int handle);

    bool findMatch(const std::string& text, BotMatch& match, int context) const;
    static std::string matchVariable(const BotMatch& match, int variable);
    static void unifyWhiteSpaces(std::string& s);
    void replaceSynonyms(std::string& s, int context) const;
    static int stringContains(const std::string& a, const std::string& b, bool caseSensitive);

private:
    struct Message {
        std::string text;
        float time = -40; // when last used
    };
    struct Piece {
        bool variable = false;
        int index = 0;
        std::vector<std::string> strings; // alternatives
    };
    struct Template {
        int context = 0, type = 0, subtype = 0;
        std::vector<Piece> pieces;
    };
    struct Synonyms {
        int context = 0;
        std::vector<std::pair<std::string, float>> words; // the first is the one others become
        float total = 0;
    };
    struct ReplyKey {
        enum Kind { String, Name, BotNames, Female, Male, Genderless, Match } kind = String;
        bool must = false, mustNot = false;
        std::string text;
        std::vector<Piece> match;
    };
    struct Reply {
        std::vector<ReplyKey> keys;
        float priority = 0;
        std::vector<Message> messages;
    };
    struct ChatType {
        std::string name;
        std::vector<Message> messages;
    };
    struct State {
        int gender = 0;
        std::string name, message;
        std::shared_ptr<std::vector<ChatType>> chat;
        std::deque<ConsoleMessage> console;
        int nextHandle = 0;
    };
    State* state(int handle);
    const State* state(int handle) const;
    bool stringsMatch(const std::vector<Piece>& pieces, BotMatch& match) const;
    // A message's variables and random words filled in, up to ten times
    // over (random words may hold more), then weighted synonyms.
    std::string construct(const std::string& message, int mcontext, const BotMatch& match, int vcontext, bool reply);
    bool expand(std::string& out, const std::string& message, int mcontext, const BotMatch& match, int vcontext,
                bool reply);
    void replaceWeightedSynonyms(std::string& s, int context);
    void replaceReplySynonyms(std::string& s, int context) const;
    static BotMatch withVariables(BotMatch match, const std::vector<const char*>& vars);
    float now() const { return time ? time() : 0; }

    std::vector<Synonyms> synonyms_;
    std::vector<std::pair<std::string, std::vector<std::string>>> randoms_;
    std::vector<Template> templates_;
    std::vector<Reply> replies_;
    std::vector<std::unique_ptr<State>> states_;
    std::vector<std::pair<std::string, std::shared_ptr<std::vector<ChatType>>>> chatFiles_; // "file|name"
    std::mt19937 random_{12345};
};

} // namespace reluva::idtech3::botlib
