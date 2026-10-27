// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/script_text.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace reluva::unreal {

namespace {

// What each conversion token converts to (0x39-0x59; see script.md for
// how the table was checked).
const char* conversionTarget(uint8_t t) {
    static const char* const kTargets[] = {
        "vector",                                    // 0x39 rotator
        "int", "bool", "float",                      // byte
        "byte", "bool", "float",                     // int
        "byte", "int", "float",                      // bool
        "byte", "int", "bool",                       // float
        "int", "bool",                               // object
        "bool",                                      // name
        "byte", "int", "bool", "float", "vector", "rotator", // string
        "bool", "rotator",                           // vector
        "bool",                                      // rotator
        "string", "string", "string", "string", "string", "string", "string", "string", // to string
    };
    const int i = t - token::FirstConversion;
    return i >= 0 && i < int(sizeof kTargets / sizeof kTargets[0]) ? kTargets[i] : "convert";
}

std::string hex4(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%04X", v);
    return buf;
}

class Writer {
public:
    Writer(const PackageSet& packages, const Package& package, const NativeTable& natives)
        : packages_(packages), package_(package), natives_(natives) {}

    std::string out;

    void object(int index, int indent) {
        ScriptObject o;
        if (Error err = readScriptObject(package_, index, o)) {
            line(indent, "// " + err.message);
            return;
        }
        switch (o.kind) {
        case ScriptObject::Kind::Class: classText(index, o); break;
        case ScriptObject::Kind::State: stateText(index, o, indent); break;
        case ScriptObject::Kind::Function: functionText(index, o, indent); break;
        case ScriptObject::Kind::Struct: structText(index, o, indent); break;
        case ScriptObject::Kind::Enum: enumText(index, o, indent); break;
        case ScriptObject::Kind::Const:
            line(indent, "const " + name(index) + " = " + o.constValue + ";");
            break;
        case ScriptObject::Kind::Property: line(indent, varText(index, o, true) + ";"); break;
        }
    }

private:
    void line(int indent, const std::string& text) {
        out.append(size_t(indent) * 4, ' ');
        out += text;
        out += '\n';
    }

    std::string name(int index) const { return package_.name(package_.exports()[size_t(index)].objectName); }
    std::string refName(int32_t ref) const { return ref == 0 ? "None" : package_.objectName(ref); }

    // A struct's fields, in the order the chain holds them (for a
    // function's parameters, the order they were declared).
    std::vector<int> children(int32_t first) const {
        std::vector<int> out;
        for (int32_t ref = first; ref > 0 && out.size() < 10000;) {
            const int index = ref - 1;
            out.push_back(index);
            ScriptObject o;
            if (readScriptObject(package_, index, o))
                break;
            ref = o.next;
        }
        return out;
    }

    std::string typeOf(const ScriptObject& p) const {
        const std::string c = fs::toLower(p.className);
        if (c == "intproperty") return "int";
        if (c == "floatproperty") return "float";
        if (c == "boolproperty") return "bool";
        if (c == "strproperty") return "string";
        if (c == "nameproperty") return "name";
        if (c == "byteproperty") return p.propertyRef ? refName(p.propertyRef) : "byte";
        if (c == "classproperty") return "class<" + refName(p.metaClass) + ">";
        if (c == "objectproperty" || c == "structproperty") return refName(p.propertyRef);
        if (c == "arrayproperty" || c == "fixedarrayproperty") {
            ScriptObject inner;
            const std::string of = p.propertyRef > 0 && !readScriptObject(package_, p.propertyRef - 1, inner)
                                       ? typeOf(inner)
                                       : "?";
            return "array<" + of + ">";
        }
        return p.className;
    }

    // A variable's declaration (without the semicolon).
    std::string varText(int index, const ScriptObject& p, bool instance) const {
        std::string s = instance ? "var" : "local";
        if (instance && (p.propertyFlags & propflag::Edit)) {
            const std::string cat = package_.name(p.category);
            s += "(" + (fs::toLower(cat) == "none" ? std::string() : cat) + ")";
        }
        const std::pair<uint32_t, const char*> words[] = {
            {propflag::Config, "config"},       {propflag::GlobalConfig, "globalconfig"},
            {propflag::Localized, "localized"}, {propflag::Transient, "transient"},
            {propflag::Travel, "travel"},       {propflag::Const, "const"},
            {propflag::EditConst, "editconst"}, {propflag::Native, "native"},
            {propflag::Input, "input"},         {propflag::ExportObject, "export"},
        };
        if (instance)
            for (const auto& [flag, word] : words)
                if (p.propertyFlags & flag)
                    s += std::string(" ") + word;
        s += " " + typeOf(p) + " " + name(index);
        if (p.arrayDim > 1)
            s += "[" + std::to_string(p.arrayDim) + "]";
        return s;
    }

    void enumText(int index, const ScriptObject& o, int indent) {
        std::string s = "enum " + name(index) + " { ";
        for (size_t i = 0; i < o.enumNames.size(); ++i)
            s += (i ? ", " : "") + package_.name(o.enumNames[i]);
        line(indent, s + " };");
    }

    void structText(int index, const ScriptObject& o, int indent) {
        const int32_t super = package_.exports()[size_t(index)].superRef;
        line(indent, "struct " + name(index) + (super ? " extends " + refName(super) : std::string()));
        line(indent, "{");
        for (int c : children(o.children))
            object(c, indent + 1);
        line(indent, "};");
    }

    void functionText(int index, const ScriptObject& f, int indent) {
        std::string head;
        const std::pair<uint32_t, const char*> words[] = {
            {funcflag::Static, "static"},       {funcflag::Final, "final"},     {funcflag::Simulated, "simulated"},
            {funcflag::Singular, "singular"},   {funcflag::Latent, "latent"},   {funcflag::Iterator, "iterator"},
            {funcflag::Exec, "exec"},           {funcflag::NetReliable, "reliable"},
        };
        if (f.functionFlags & funcflag::Native)
            head += f.native ? "native(" + std::to_string(f.native) + ") " : "native ";
        for (const auto& [flag, word] : words)
            if (f.functionFlags & flag)
                head += std::string(word) + " ";
        const bool op = f.functionFlags & funcflag::Operator;
        head += op ? (f.functionFlags & funcflag::PreOperator ? "preoperator " : "operator ")
                   : (f.functionFlags & funcflag::Event ? "event " : "function ");
        std::string ret, params;
        std::vector<std::pair<int, ScriptObject>> locals;
        for (int c : children(f.children)) {
            ScriptObject p;
            if (readScriptObject(package_, c, p) || p.kind != ScriptObject::Kind::Property)
                continue;
            if (p.propertyFlags & propflag::ReturnParm) {
                ret = typeOf(p) + " ";
            } else if (p.propertyFlags & propflag::Parm) {
                std::string s;
                if (p.propertyFlags & propflag::OptionalParm) s += "optional ";
                if (p.propertyFlags & propflag::OutParm) s += "out ";
                if (p.propertyFlags & propflag::CoerceParm) s += "coerce ";
                params += (params.empty() ? "" : ", ") + s + typeOf(p) + " " + name(c);
            } else {
                locals.push_back({c, std::move(p)});
            }
        }
        const std::string shown = op ? package_.name(f.friendlyName) : name(index);
        line(indent, head + ret + shown + "(" + params + ")" + (f.code.statements.empty() ? ";" : ""));
        if (f.code.statements.empty())
            return;
        line(indent, "{");
        for (const auto& [c, p] : locals)
            line(indent + 1, varText(c, p, false) + ";");
        code(f.code, indent + 1);
        line(indent, "}");
    }

    void stateText(int index, const ScriptObject& s, int indent) {
        const int32_t super = package_.exports()[size_t(index)].superRef;
        std::string head = s.stateFlags & 1 ? "state() " : "state ";
        head += name(index);
        if (super && package_.objectName(super) != name(index))
            head += " extends " + refName(super);
        line(indent, head);
        line(indent, "{");
        for (int c : children(s.children))
            object(c, indent + 1);
        if (!s.code.statements.empty()) {
            if (!children(s.children).empty())
                out += '\n';
            code(s.code, indent + 1);
        }
        line(indent, "}");
    }

    void classText(int index, const ScriptObject& c) {
        const Export& e = package_.exports()[size_t(index)];
        std::string head = "class " + name(index);
        if (e.superRef)
            head += " extends " + refName(e.superRef);
        if (c.within && fs::toLower(refName(c.within)) != "object")
            head += " within " + refName(c.within);
        if (c.classFlags & 0x1)
            head += " abstract";
        if (c.classFlags & 0x4)
            head += " config(" + package_.name(c.configName) + ")";
        if (c.classFlags & 0x8)
            head += " transient";
        line(0, head + ";");
        out += '\n';
        // Declarations first, then functions, then states, as source is
        // usually laid out.
        const std::vector<int> fields = children(c.children);
        auto kindOf = [&](int f) {
            ScriptObject o;
            return readScriptObject(package_, f, o) ? ScriptObject::Kind::Const : o.kind;
        };
        for (ScriptObject::Kind k : {ScriptObject::Kind::Const, ScriptObject::Kind::Enum, ScriptObject::Kind::Struct,
                                     ScriptObject::Kind::Property}) {
            bool any = false;
            for (int f : fields)
                if (kindOf(f) == k) {
                    object(f, 0);
                    any = true;
                }
            if (any)
                out += '\n';
        }
        for (ScriptObject::Kind k : {ScriptObject::Kind::Function, ScriptObject::Kind::State})
            for (int f : fields)
                if (kindOf(f) == k) {
                    object(f, 0);
                    out += '\n';
                }
        if (!c.code.statements.empty()) {
            line(0, "// the class's own state code");
            code(c.code, 0);
            out += '\n';
        }
        line(0, "defaultproperties");
        line(0, "{");
        for (const Property& p : c.defaults.items)
            line(1, p.name + (p.arrayIndex ? "(" + std::to_string(p.arrayIndex) + ")" : std::string()) + "=" +
                        describe(package_, p));
        line(0, "}");
    }

    // Statements with their offsets, and a state's labels where they are.
    void code(const Code& c, int indent) {
        std::multimap<uint32_t, int32_t> labels;
        for (const Expr& x : c.exprs)
            for (const auto& [label, offset] : x.labels)
                labels.insert({offset, label});
        for (int s : c.statements) {
            if (s < 0)
                continue;
            const Expr& x = c.exprs[size_t(s)];
            for (auto [it, end] = labels.equal_range(x.offset); it != end; ++it)
                line(indent - 1, package_.name(it->second) + ":");
            if (x.token == token::Nothing || x.token == token::LabelTable)
                continue;
            line(indent, hex4(x.offset) + ": " + statement(c, s));
        }
    }

    std::string statement(const Code& c, int i) {
        const Expr& x = c.exprs[size_t(i)];
        auto a = [&](size_t k) { return k < x.args.size() ? expr(c, x.args[k]) : std::string("?"); };
        using namespace token;
        switch (x.token) {
        case Jump: return "jump " + hex4(uint32_t(x.value));
        case JumpIfNot: return "if (!(" + a(0) + ")) jump " + hex4(uint32_t(x.value));
        case Return: {
            const bool nothing = x.args.empty() || x.args[0] < 0 || c.exprs[size_t(x.args[0])].token == Nothing;
            return nothing ? "return;" : "return " + a(0) + ";";
        }
        case Stop: return "stop;";
        case Switch: return "switch (" + a(0) + ")";
        case Case:
            return x.value == 0xffff ? "default:" : "case " + a(0) + ": (else " + hex4(uint32_t(x.value)) + ")";
        case Iterator: return "foreach " + a(0) + " (to " + hex4(uint32_t(x.value)) + ")";
        case IteratorNext: return "next in foreach";
        case IteratorPop: return "leave foreach";
        case GotoLabel: return "goto " + a(0) + ";";
        case Assert: return "assert(" + a(0) + "); // line " + std::to_string(x.value);
        case LabelTable: {
            std::string s = "labels:";
            for (const auto& [label, offset] : x.labels)
                s += " " + package_.name(label) + "=" + hex4(offset);
            return s;
        }
        default: {
            // An operator as a statement (a += b) without its parentheses.
            std::string e = expr(c, i);
            if (x.native >= 0 && e.size() > 2 && e.front() == '(' && e.back() == ')')
                e = e.substr(1, e.size() - 2);
            return e + ";";
        }
        }
    }

    std::string args(const Code& c, const Expr& x, size_t from = 0) {
        std::vector<std::string> parts;
        for (size_t k = from; k < x.args.size(); ++k) {
            const int a = x.args[k];
            if (a < 0 || c.exprs[size_t(a)].token == token::EndFunctionParms)
                break;
            parts.push_back(c.exprs[size_t(a)].token == token::Nothing ? "" : expr(c, a));
        }
        while (!parts.empty() && parts.back().empty())
            parts.pop_back(); // optional arguments left out
        std::string s;
        for (size_t k = 0; k < parts.size(); ++k)
            s += (k ? ", " : "") + parts[k];
        return s;
    }

    std::string expr(const Code& c, int i) {
        if (i < 0 || size_t(i) >= c.exprs.size())
            return "?";
        const Expr& x = c.exprs[size_t(i)];
        auto a = [&](size_t k) { return k < x.args.size() ? expr(c, x.args[k]) : std::string("?"); };
        using namespace token;
        if (x.native >= 0) {
            const NativeFunction* n = natives_.find(x.native);
            if (!n)
                return "native" + std::to_string(x.native) + "(" + args(c, x) + ")";
            if (n->flags & funcflag::Operator) {
                if (n->params == 2)
                    return "(" + a(0) + " " + n->symbol + " " + a(1) + ")";
                if (n->flags & funcflag::PreOperator)
                    return n->symbol + a(0);
                return a(0) + n->symbol;
            }
            return n->name + "(" + args(c, x) + ")";
        }
        if (x.token >= FirstConversion && x.token <= LastConversion)
            return std::string(conversionTarget(x.token)) + "(" + a(0) + ")";
        char buf[96];
        switch (x.token) {
        case LocalVariable:
        case InstanceVariable:
        case NativeParm: return refName(x.ref);
        case DefaultVariable: return "default." + refName(x.ref);
        case Let:
        case LetBool: return a(0) + " = " + a(1);
        case EatString:
        case BoolVariable:
        case Skip: return a(0);
        case Context: return a(0) + "." + a(1);
        case ClassContext: return a(0) + ".static." + a(1);
        case ArrayElement:
        case DynArrayElement: return a(1) + "[" + a(0) + "]";
        case StructMember: return a(0) + "." + refName(x.ref);
        case DynamicCast: return refName(x.ref) + "(" + a(0) + ")";
        case MetaCast: return "class<" + refName(x.ref) + ">(" + a(0) + ")";
        case VirtualFunction: return package_.name(x.ref) + "(" + args(c, x) + ")";
        case GlobalFunction: return "global." + package_.name(x.ref) + "(" + args(c, x) + ")";
        case FinalFunction: return package_.path(x.ref) + "(" + args(c, x) + ")";
        case New: return "new(" + a(0) + ", " + a(1) + ", " + a(2) + ") " + a(3);
        case StructCmpEq: return "(" + a(0) + " == " + a(1) + ")";
        case StructCmpNe: return "(" + a(0) + " != " + a(1) + ")";
        case Self: return "self";
        case NoObject: return "None";
        case Nothing: return "";
        case True: return "true";
        case False: return "false";
        case IntZero: return "0";
        case IntOne: return "1";
        case IntConst:
        case IntConstByte:
        case ByteConst: return std::to_string(x.value);
        case FloatConst:
            std::snprintf(buf, sizeof buf, "%g", double(x.f[0]));
            return buf;
        case VectorConst:
            std::snprintf(buf, sizeof buf, "vect(%g, %g, %g)", double(x.f[0]), double(x.f[1]), double(x.f[2]));
            return buf;
        case RotationConst:
            std::snprintf(buf, sizeof buf, "rot(%d, %d, %d)", x.rot[0], x.rot[1], x.rot[2]);
            return buf;
        case StringConst:
        case UnicodeStringConst: return "\"" + x.text + "\"";
        case NameConst: return "'" + package_.name(x.ref) + "'";
        case ObjectConst: return x.ref ? package_.className(x.ref) + "'" + package_.path(x.ref) + "'" : "None";
        default: return statement(c, i);
        }
    }

    const PackageSet& packages_;
    const Package& package_;
    const NativeTable& natives_;
};

} // namespace

NativeTable::NativeTable(const PackageSet& packages) {
    for (const std::string& n : packages.names()) {
        const Package* p = packages.get(n);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i) {
            const Export& e = p->exports()[i];
            if (e.classRef == 0 || fs::toLower(p->objectName(e.classRef)) != "function")
                continue;
            ScriptObject f;
            if (readScriptObject(*p, int(i), f) || !(f.functionFlags & funcflag::Native) || f.native == 0)
                continue;
            NativeFunction nf;
            nf.name = p->name(e.objectName);
            nf.symbol = p->name(f.friendlyName);
            nf.flags = f.functionFlags;
            nf.precedence = f.operatorPrecedence;
            for (int32_t ref = f.children; ref > 0;) {
                ScriptObject c;
                if (readScriptObject(*p, ref - 1, c))
                    break;
                if ((c.propertyFlags & propflag::Parm) && !(c.propertyFlags & propflag::ReturnParm))
                    ++nf.params;
                ref = c.next;
            }
            natives_.emplace(f.native, std::move(nf));
        }
    }
}

const NativeFunction* NativeTable::find(int native) const {
    auto it = natives_.find(native);
    return it == natives_.end() ? nullptr : &it->second;
}

std::string scriptText(const PackageSet& packages, const Package& package, int exportIndex,
                       const NativeTable& natives) {
    Writer w(packages, package, natives);
    w.object(exportIndex, 0);
    return w.out;
}

} // namespace reluva::unreal
