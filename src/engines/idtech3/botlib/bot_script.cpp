// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot scripts' preprocessor. See bot_script.h.

#include "engines/idtech3/botlib/bot_script.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace reluva::idtech3::botlib {

namespace {

// Punctuation, longest first so the longest match wins.
const char* const kPunctuation[] = {">>=", "<<=", "...", "##", "&&", "||", ">=", "<=", "==", "!=", "*=", "/=", "%=",
                                    "+=", "-=", "++", "--", "&=", "|=", "^=", ">>", "<<", "->", "::", ".*", ";", ",",
                                    "(", ")", "{", "}", "[", "]", "#", "$", "+", "-", "*", "/", "%", "&", "|", "^",
                                    "~", "!", "=", "<", ">", "?", ":", ".", "\\", "@", "`"};

const char* const kEndOfLine = "\n#end"; // marks the end of a directive's tokens

// Splits a file's text into tokens, with their lines.
bool lex(const std::string& name, const std::string& s, std::vector<BotToken>& out, std::string& error) {
    size_t i = 0;
    int line = 1;
    bool lineStart = true, space = true;
    auto isNameChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line, ++i;
            lineStart = space = true;
            continue;
        }
        if (static_cast<unsigned char>(c) <= ' ') {
            ++i;
            space = true;
            continue;
        }
        // A backslash ending a line carries it on.
        if (c == '\\' && (i + 1 < s.size()) && (s[i + 1] == '\n' || (s[i + 1] == '\r' && i + 2 < s.size() && s[i + 2] == '\n'))) {
            i += s[i + 1] == '\n' ? 2 : 3;
            ++line;
            space = true;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') {
                    ++line;
                    lineStart = true;
                }
                ++i;
            }
            i += 2;
            space = true;
            continue;
        }
        BotToken t;
        t.file = name;
        t.line = line;
        t.lineStart = lineStart;
        t.spaceBefore = space;
        lineStart = space = false;
        if (c == '"' || c == '\'') {
            // A string (strings with only white space between joined) or a
            // literal character, with C's escapes.
            t.type = c == '"' ? BotToken::Type::String : BotToken::Type::Literal;
            while (true) {
                ++i;
                while (i < s.size() && s[i] != c) {
                    if (s[i] == '\n') {
                        error = name + ":" + std::to_string(line) + ": newline inside a string";
                        return false;
                    }
                    if (s[i] == '\\' && i + 1 < s.size()) {
                        ++i;
                        switch (s[i]) {
                        case 'n': t.text += '\n'; break;
                        case 'r': t.text += '\r'; break;
                        case 't': t.text += '\t'; break;
                        case 'v': t.text += '\v'; break;
                        case 'b': t.text += '\b'; break;
                        case 'f': t.text += '\f'; break;
                        case 'a': t.text += '\a'; break;
                        case 'x': {
                            int v = 0;
                            while (i + 1 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])))
                                v = v * 16 + std::stoi(std::string(1, s[++i]), nullptr, 16);
                            t.text += char(v);
                            break;
                        }
                        default:
                            if (s[i] >= '0' && s[i] <= '9') {
                                int v = s[i] - '0';
                                while (i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')
                                    v = v * 10 + (s[++i] - '0');
                                t.text += char(v);
                            } else {
                                t.text += s[i];
                            }
                        }
                        ++i;
                        continue;
                    }
                    t.text += s[i++];
                }
                if (i >= s.size()) {
                    error = name + ":" + std::to_string(t.line) + ": missing closing quote";
                    return false;
                }
                ++i;
                if (c != '"')
                    break;
                size_t j = i;
                int lines = 0;
                while (j < s.size() && static_cast<unsigned char>(s[j]) <= ' ')
                    lines += s[j++] == '\n';
                if (j < s.size() && s[j] == '"') {
                    i = j;
                    line += lines;
                    continue;
                }
                break;
            }
            out.push_back(std::move(t));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            t.type = BotToken::Type::Number;
            const size_t start = i;
            if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i += 2;
                while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i])))
                    ++i;
                t.number = double(std::strtoul(s.substr(start, i - start).c_str(), nullptr, 16));
            } else {
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    ++i;
                if (i < s.size() && s[i] == '.') {
                    t.isFloat = true;
                    ++i;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                        ++i;
                }
                if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                    t.isFloat = true;
                    ++i;
                    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                        ++i;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                        ++i;
                }
                t.number = std::strtod(s.substr(start, i - start).c_str(), nullptr);
            }
            t.text = s.substr(start, i - start);
            // Suffixes (f, l, u) left off.
            while (i < s.size() && (s[i] == 'f' || s[i] == 'F' || s[i] == 'l' || s[i] == 'L' || s[i] == 'u' || s[i] == 'U'))
                ++i;
            out.push_back(std::move(t));
            continue;
        }
        if (isNameChar(c)) {
            t.type = BotToken::Type::Name;
            while (i < s.size() && isNameChar(s[i]))
                t.text += s[i++];
            out.push_back(std::move(t));
            continue;
        }
        t.type = BotToken::Type::Punctuation;
        for (const char* p : kPunctuation)
            if (s.compare(i, std::char_traits<char>::length(p), p) == 0) {
                t.text = p;
                break;
            }
        if (t.text.empty())
            t.text = std::string(1, c);
        i += t.text.size();
        out.push_back(std::move(t));
    }
    return true;
}

} // namespace

struct BotScript::Source {
    std::vector<BotToken> tokens;
    size_t pos = 0;
    bool file = true;
    std::string macro; // the macro this expands, while it does
};

struct BotScript::Macro {
    bool function = false;
    std::vector<std::string> params;
    std::vector<BotToken> body;
};

BotScript::BotScript(Loader load, const std::vector<std::string>& defines) : load_(std::move(load)) {
    for (const std::string& d : defines)
        define(d);
}

BotScript::~BotScript() = default;

void BotScript::define(const std::string& line) {
    std::vector<BotToken> tokens;
    std::string error;
    if (!lex("(define)", line, tokens, error) || tokens.empty() || tokens[0].type != BotToken::Type::Name)
        return;
    auto m = std::make_shared<Macro>();
    m->body.assign(tokens.begin() + 1, tokens.end());
    macros_[tokens[0].text] = m;
}

bool BotScript::open(const std::string& path) {
    std::string text;
    if (!load_ || !load_(path, text)) {
        fail("couldn't load " + path);
        return false;
    }
    openText(path, text);
    return error_.empty();
}

void BotScript::openText(const std::string& name, const std::string& text) {
    auto source = std::make_unique<Source>();
    std::string error;
    if (!lex(name, text, source->tokens, error)) {
        if (error_.empty())
            error_ = error;
        return;
    }
    sources_.push_back(std::move(source));
}

void BotScript::fail(const std::string& message) {
    if (error_.empty())
        error_ = where() + ": " + message;
}

std::string BotScript::where() const { return last_.file + ":" + std::to_string(last_.line); }

bool BotScript::raw(BotToken& token) {
    if (!pushed_.empty()) {
        token = pushed_.back();
        pushed_.pop_back();
        return true;
    }
    while (!sources_.empty()) {
        Source& s = *sources_.back();
        if (s.pos < s.tokens.size()) {
            token = s.tokens[s.pos++];
            if (!s.file)
                token.lineStart = false;
            last_ = token;
            return true;
        }
        sources_.pop_back();
    }
    return false;
}

bool BotScript::skipping() const { return !conditionals_.empty() && !conditionals_.back().taking; }

void BotScript::unread(const BotToken& token) { pushed_.push_back(token); }

bool BotScript::readLine(std::vector<BotToken>& out) {
    // The rest of a directive's line: up to a token starting a line, or
    // the end of the file it's in.
    while (true) {
        if (!pushed_.empty()) {
            if (pushed_.back().lineStart)
                break;
        } else {
            if (sources_.empty())
                break;
            const Source& s = *sources_.back();
            if (s.pos >= s.tokens.size() || (s.file && s.tokens[s.pos].lineStart))
                break;
        }
        BotToken t;
        if (!raw(t))
            break;
        out.push_back(t);
    }
    return true;
}

bool BotScript::next(BotToken& token) {
    while (error_.empty()) {
        if (!raw(token))
            return false;
        if (token.is("#") && token.lineStart) {
            if (!directive())
                return false;
            continue;
        }
        if (skipping())
            continue;
        if (token.is("$")) {
            BotToken name;
            if (raw(name) && name.type == BotToken::Type::Name && (name.text == "evalint" || name.text == "evalfloat")) {
                // $evalint(expression), $evalfloat(expression): the value.
                BotToken t;
                if (!next(t) || !t.is("(")) {
                    fail("expected ( after $" + name.text);
                    return false;
                }
                std::vector<BotToken> expr;
                int depth = 1;
                while (next(t)) {
                    if (t.is("("))
                        ++depth;
                    else if (t.is(")") && --depth == 0)
                        break;
                    expr.push_back(t);
                }
                double v = 0;
                bool isFloat = false;
                if (!evaluate(expr, v, isFloat))
                    return false;
                token.type = BotToken::Type::Number;
                token.isFloat = name.text == "evalfloat";
                token.number = token.isFloat ? v : std::trunc(v);
                token.text = std::to_string(token.number);
                return true;
            }
            if (name.text.size())
                unread(name);
            return true;
        }
        if (token.type == BotToken::Type::Name) {
            if (const auto it = macros_.find(token.text); it != macros_.end()) {
                bool active = false;
                for (const auto& s : sources_)
                    active |= !s->file && s->macro == token.text && s->pos <= s->tokens.size();
                if (!active) {
                    if (!expand(token, *it->second))
                        return false;
                    continue;
                }
            }
        }
        return true;
    }
    return false;
}

bool BotScript::expand(const BotToken& name, const Macro& m) {
    std::vector<std::vector<BotToken>> args;
    if (m.function) {
        BotToken t;
        if (!raw(t) || !t.is("(")) {
            // Without arguments it's just the name.
            if (t.text.size())
                unread(t);
            auto source = std::make_unique<Source>();
            source->file = false;
            source->macro = name.text;
            source->tokens.push_back(name);
            sources_.push_back(std::move(source));
            return true;
        }
        args.emplace_back();
        int depth = 1;
        while (true) {
            if (!raw(t)) {
                fail("macro " + name.text + " without closing )");
                return false;
            }
            if (t.is("("))
                ++depth;
            else if (t.is(")") && --depth == 0)
                break;
            else if (t.is(",") && depth == 1) {
                args.emplace_back();
                continue;
            }
            args.back().push_back(t);
        }
        if (args.size() == 1 && args[0].empty() && m.params.empty())
            args.clear();
        if (args.size() != m.params.size()) {
            fail("macro " + name.text + " takes " + std::to_string(m.params.size()) + " arguments");
            return false;
        }
    }
    auto param = [&](const std::string& text) -> int {
        for (size_t i = 0; i < m.params.size(); ++i)
            if (m.params[i] == text)
                return int(i);
        return -1;
    };
    std::vector<BotToken> out;
    for (size_t i = 0; i < m.body.size(); ++i) {
        const BotToken& t = m.body[i];
        if (m.function && t.is("#") && i + 1 < m.body.size() && param(m.body[i + 1].text) >= 0) {
            // #param: the argument as a string.
            BotToken s = t;
            s.type = BotToken::Type::String;
            s.text.clear();
            for (const BotToken& a : args[size_t(param(m.body[++i].text))])
                s.text += (s.text.empty() || !a.spaceBefore ? "" : " ") + a.text;
            out.push_back(s);
            continue;
        }
        if (t.is("##") && !out.empty() && i + 1 < m.body.size()) {
            // a ## b: the two pasted into one.
            const BotToken& n = m.body[++i];
            std::string text = n.text;
            if (const int p = param(n.text); p >= 0) {
                text.clear();
                for (const BotToken& a : args[size_t(p)])
                    text += a.text;
            }
            BotToken& l = out.back();
            l.text += text;
            const bool digits = !l.text.empty() && std::isdigit(static_cast<unsigned char>(l.text[0]));
            l.type = digits ? BotToken::Type::Number : BotToken::Type::Name;
            if (digits)
                l.number = std::strtod(l.text.c_str(), nullptr);
            continue;
        }
        if (m.function && t.type == BotToken::Type::Name)
            if (const int p = param(t.text); p >= 0) {
                for (BotToken a : args[size_t(p)]) {
                    a.lineStart = false;
                    out.push_back(a);
                }
                continue;
            }
        BotToken copy = t;
        copy.file = name.file;
        copy.line = name.line;
        out.push_back(copy);
    }
    if (!out.empty())
        out.front().spaceBefore = name.spaceBefore;
    auto source = std::make_unique<Source>();
    source->file = false;
    source->macro = name.text;
    source->tokens = std::move(out);
    sources_.push_back(std::move(source));
    return true;
}

bool BotScript::directive() {
    std::vector<BotToken> words;
    readLine(words);
    if (words.empty())
        return true; // a lone #
    const BotToken name = words.front();
    std::vector<BotToken> line(words.begin() + 1, words.end());
    const std::string& d = name.text;
    auto condition = [&](bool& value) {
        // `defined NAME` and `defined(NAME)` answered before the macros
        // are expanded, then the line evaluated.
        std::vector<BotToken> tokens;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i].type == BotToken::Type::Name && line[i].text == "defined") {
                size_t j = i + 1;
                const bool paren = j < line.size() && line[j].is("(");
                if (paren)
                    ++j;
                if (j >= line.size()) {
                    fail("defined without a name");
                    return false;
                }
                BotToken v = line[j];
                v.type = BotToken::Type::Number;
                v.number = macros_.count(line[j].text) ? 1 : 0;
                v.isFloat = false;
                v.text = v.number ? "1" : "0";
                tokens.push_back(v);
                i = j + (paren ? 1 : 0);
                continue;
            }
            tokens.push_back(line[i]);
        }
        // The macros in it expanded through the token stream, up to a mark.
        BotToken end;
        end.type = BotToken::Type::Punctuation;
        end.text = kEndOfLine;
        tokens.push_back(end);
        auto source = std::make_unique<Source>();
        source->file = false;
        source->tokens = std::move(tokens);
        sources_.push_back(std::move(source));
        std::vector<BotToken> expanded;
        // Read without the directive and skipping machinery.
        auto saved = conditionals_;
        conditionals_.clear();
        BotToken t;
        while (next(t) && t.text != kEndOfLine)
            expanded.push_back(t);
        conditionals_ = saved;
        double v = 0;
        bool isFloat = false;
        if (!evaluate(expanded, v, isFloat))
            return false;
        value = v != 0;
        return true;
    };
    const bool outer = !skipping();
    if (d == "if" || d == "ifdef" || d == "ifndef") {
        bool value = false;
        if (outer) {
            if (d == "if") {
                if (!condition(value))
                    return false;
            } else {
                if (line.empty() || line[0].type != BotToken::Type::Name) {
                    fail("#" + d + " without a name");
                    return false;
                }
                value = macros_.count(line[0].text) == (d == "ifdef" ? 1u : 0u);
            }
        }
        conditionals_.push_back({outer && value, outer && value, outer});
        return true;
    }
    if (d == "elif" || d == "else" || d == "endif") {
        if (conditionals_.empty()) {
            fail("#" + d + " without #if");
            return false;
        }
        if (d == "endif") {
            conditionals_.pop_back();
            return true;
        }
        const size_t at = conditionals_.size() - 1;
        if (!conditionals_[at].outer || conditionals_[at].taken) {
            conditionals_[at].taking = false;
            return true;
        }
        bool value = true;
        if (d == "elif" && !condition(value))
            return false;
        conditionals_[at].taking = conditionals_[at].taken = value;
        return true;
    }
    if (!outer)
        return true;
    if (d == "include") {
        std::string path;
        if (!line.empty() && line[0].type == BotToken::Type::String)
            path = line[0].text;
        else if (!line.empty() && line[0].is("<"))
            for (size_t i = 1; i < line.size() && !line[i].is(">"); ++i)
                path += line[i].text;
        if (path.empty()) {
            fail("#include without a file name");
            return false;
        }
        for (char& c : path)
            if (c == '\\')
                c = '/';
        return open(path);
    }
    if (d == "define") {
        if (line.empty() || line[0].type != BotToken::Type::Name) {
            fail("#define without a name");
            return false;
        }
        auto m = std::make_shared<Macro>();
        size_t i = 1;
        if (i < line.size() && line[i].is("(") && !line[i].spaceBefore) {
            m->function = true;
            for (++i; i < line.size() && !line[i].is(")"); ++i)
                if (!line[i].is(","))
                    m->params.push_back(line[i].text);
            ++i;
        }
        if (i < line.size())
            m->body.assign(line.begin() + long(i), line.end());
        macros_[line[0].text] = m;
        return true;
    }
    if (d == "undef") {
        if (!line.empty())
            macros_.erase(line[0].text);
        return true;
    }
    if (d == "error") {
        std::string text;
        for (const BotToken& t : line)
            text += t.text + " ";
        fail("#error " + text);
        return false;
    }
    // #line, #pragma, #eval, #evalfloat: nothing the bots' files rely on.
    return true;
}

bool BotScript::evaluate(const std::vector<BotToken>& tokens, double& value, bool& isFloat) {
    // Recursive descent over C's operators, by precedence.
    size_t i = 0;
    bool ok = true;
    isFloat = false;
    std::function<double(int)> binary;
    std::function<double()> unary = [&]() -> double {
        if (i >= tokens.size()) {
            ok = false;
            return 0;
        }
        const BotToken& t = tokens[i++];
        if (t.is("-"))
            return -unary();
        if (t.is("+"))
            return unary();
        if (t.is("!"))
            return unary() == 0 ? 1 : 0;
        if (t.is("~"))
            return double(~int64_t(unary()));
        if (t.is("(")) {
            const double v = binary(0);
            if (i < tokens.size() && tokens[i].is(")"))
                ++i;
            else
                ok = false;
            return v;
        }
        if (t.type == BotToken::Type::Number) {
            isFloat |= t.isFloat;
            return t.number;
        }
        if (t.type == BotToken::Type::Literal)
            return t.text.empty() ? 0 : double(static_cast<unsigned char>(t.text[0]));
        if (t.type == BotToken::Type::Name)
            return 0; // not defined
        ok = false;
        return 0;
    };
    struct Op {
        const char* text;
        int precedence;
    };
    static const Op ops[] = {{"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5},  {"==", 6}, {"!=", 6},
                             {"<", 7},  {"<=", 7}, {">", 7},  {">=", 7}, {"<<", 8}, {">>", 8}, {"+", 9},
                             {"-", 9},  {"*", 10}, {"/", 10}, {"%", 10}};
    binary = [&](int minimum) -> double {
        double left = unary();
        while (ok && i < tokens.size()) {
            const BotToken& t = tokens[i];
            if (t.is("?") && minimum == 0) {
                ++i;
                const double a = binary(0);
                if (i < tokens.size() && tokens[i].is(":"))
                    ++i;
                const double b = binary(0);
                left = left != 0 ? a : b;
                continue;
            }
            const Op* op = nullptr;
            for (const Op& o : ops)
                if (t.is(o.text))
                    op = &o;
            if (!op || op->precedence <= minimum)
                break;
            ++i;
            const double right = binary(op->precedence);
            const std::string o = op->text;
            const auto li = int64_t(left), ri = int64_t(right);
            if (o == "||") left = left != 0 || right != 0;
            else if (o == "&&") left = left != 0 && right != 0;
            else if (o == "|") left = double(li | ri);
            else if (o == "^") left = double(li ^ ri);
            else if (o == "&") left = double(li & ri);
            else if (o == "==") left = left == right;
            else if (o == "!=") left = left != right;
            else if (o == "<") left = left < right;
            else if (o == "<=") left = left <= right;
            else if (o == ">") left = left > right;
            else if (o == ">=") left = left >= right;
            else if (o == "<<") left = double(li << ri);
            else if (o == ">>") left = double(li >> ri);
            else if (o == "+") left = left + right;
            else if (o == "-") left = left - right;
            else if (o == "*") left = left * right;
            else if (o == "/") left = right == 0 ? 0 : isFloat ? left / right : double(li / (ri ? ri : 1));
            else if (o == "%") left = ri == 0 ? 0 : double(li % ri);
        }
        return left;
    };
    value = tokens.empty() ? 0 : binary(0);
    if (!ok || i != tokens.size()) {
        fail("bad expression");
        return false;
    }
    return true;
}

bool BotScript::check(const char* text) {
    BotToken t;
    if (!next(t))
        return false;
    if (t.is(text))
        return true;
    unread(t);
    return false;
}

bool BotScript::expect(const char* text) {
    BotToken t;
    if (!next(t)) {
        fail(std::string("expected ") + text + " at the end");
        return false;
    }
    if (!t.is(text)) {
        fail(std::string("expected ") + text + ", found " + t.text);
        return false;
    }
    return true;
}

bool BotScript::readNumber(double& value, bool* isFloat) {
    BotToken t;
    if (!next(t)) {
        fail("expected a number at the end");
        return false;
    }
    double sign = 1;
    if (t.is("-") || t.is("+")) {
        sign = t.is("-") ? -1 : 1;
        if (!next(t)) {
            fail("expected a number at the end");
            return false;
        }
    }
    if (t.type != BotToken::Type::Number) {
        fail("expected a number, found " + t.text);
        return false;
    }
    value = sign * t.number;
    if (isFloat)
        *isFloat = t.isFloat;
    return true;
}

bool BotScript::readString(std::string& value) {
    BotToken t;
    if (!next(t) || t.type != BotToken::Type::String) {
        fail("expected a string" + (t.text.empty() ? std::string() : ", found " + t.text));
        return false;
    }
    value = t.text;
    return true;
}

bool BotScript::skipBlock() {
    int depth = 1;
    BotToken t;
    while (depth > 0) {
        if (!next(t)) {
            fail("missing }");
            return false;
        }
        if (t.is("{"))
            ++depth;
        else if (t.is("}"))
            --depth;
    }
    return true;
}

} // namespace reluva::idtech3::botlib
