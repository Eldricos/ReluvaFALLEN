// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// A token of a bot script (the files under botfiles/: characters, item
// and weapon weights, chats), after the preprocessor.
struct BotToken {
    enum class Type { String, Literal, Number, Name, Punctuation } type = Type::Name;
    std::string text; // a string's or literal's without its quotes
    double number = 0;
    bool isFloat = false; // a number with a point or exponent
    std::string file;
    int line = 0;
    bool lineStart = false; // the first on its line
    bool spaceBefore = false; // white space just before it
    bool is(const char* t) const { return type != Type::String && text == t; }
};

// Reads the bot library's scripts the way its preprocessor does, a C-like
// one: #include, #define (with parameters), #undef, #if/#ifdef/#ifndef/
// #elif/#else/#endif with integer and float expressions, and
// $evalint(...)/$evalfloat(...) in the text. Facts of the format from the
// GPL ioquake3 bot library (l_precomp.c, l_script.c); new code. Includes
// are looked up through `load`, which is given paths as written (the bot
// library's base folder, botfiles/, is the loader's business).
class BotScript {
public:
    using Loader = std::function<bool(const std::string& path, std::string& text)>;
    // `defines`: lines of "NAME value" defined before the file is read
    // (the game's global defines).
    BotScript(Loader load, const std::vector<std::string>& defines = {});
    ~BotScript();

    bool open(const std::string& path);
    // Reads text directly, as if from a file named `name`.
    void openText(const std::string& name, const std::string& text);

    // The next token, false at the end or on an error.
    bool next(BotToken& token);
    void unread(const BotToken& token);
    // The next token if it is `text`.
    bool check(const char* text);
    // The next token, which must be `text` (else an error).
    bool expect(const char* text);
    // A number with an optional sign before it.
    bool readNumber(double& value, bool* isFloat = nullptr);
    bool readString(std::string& value);
    // Skips a { ... } block whose opening brace was read.
    bool skipBlock();

    // The first error, with where it is ("file:line: ...").
    const std::string& error() const { return error_; }
    void fail(const std::string& message);
    std::string where() const;

private:
    struct Source;
    struct Macro;
    bool raw(BotToken& token);
    bool directive();
    bool expand(const BotToken& name, const Macro& m);
    bool readLine(std::vector<BotToken>& out);
    bool evaluate(const std::vector<BotToken>& tokens, double& value, bool& isFloat);
    void define(const std::string& line);
    bool skipping() const;

    Loader load_;
    std::vector<std::unique_ptr<Source>> sources_; // files and macro expansions, innermost last
    std::vector<BotToken> pushed_;
    std::map<std::string, std::shared_ptr<Macro>> macros_;
    struct Conditional {
        bool taking;  // this branch's tokens are read
        bool taken;   // some branch was
        bool outer;   // the enclosing one is being read
    };
    std::vector<Conditional> conditionals_;
    std::string error_;
    BotToken last_;
};

} // namespace reluva::idtech3::botlib
