// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/script.h"

#include "common/fs.h"

namespace reluva::unreal {

namespace {

// Decodes bytecode expressions. Positions are counted twice: in the file
// (the reader) and as the engine lays the code out in memory, where object
// references and names take 4 bytes each whatever their compact size on
// disk; jumps and the stored script size use the second.
class Decoder {
public:
    Decoder(const Package& package, Reader& r, Code& code) : package_(package), r_(r), code_(code) {}

    uint32_t at() const { return mem_; }
    bool failed() const { return !error_.empty() || !r_.ok(); }
    const std::string& error() const { return error_; }

    // One expression; its index in code_.exprs, or -1.
    int expr() {
        if (failed())
            return -1;
        const uint32_t start = mem_;
        const uint8_t t = u8();
        const int index = int(code_.exprs.size());
        code_.exprs.push_back({});
        code_.exprs.back().token = t;
        code_.exprs.back().offset = start;
        auto self = [&]() -> Expr& { return code_.exprs[size_t(index)]; };
        auto arg = [&] {
            const int a = expr();
            self().args.push_back(a);
        };
        // Function arguments, up to EndFunctionParms (kept, so arguments
        // left out are told from the end).
        auto params = [&] {
            for (int n = 0; n < 64 && !failed(); ++n) {
                const int a = expr();
                self().args.push_back(a);
                if (a < 0 || code_.exprs[size_t(a)].token == token::EndFunctionParms)
                    return;
            }
            fail("a call's arguments don't end");
        };
        using namespace token;
        if (t >= FirstNative || (t >= ExtendedNative && t < FirstNative)) {
            self().native = t >= FirstNative ? t : (t - ExtendedNative) * 256 + u8();
            params();
            return index;
        }
        if (t >= FirstConversion && t <= LastConversion) {
            arg();
            return index;
        }
        switch (t) {
        case LocalVariable:
        case InstanceVariable:
        case DefaultVariable:
        case ObjectConst:
        case NativeParm:
            self().ref = ref();
            break;
        case NameConst:
            self().ref = name();
            break;
        case Return:
        case GotoLabel:
        case EatString:
        case BoolVariable:
            arg();
            break;
        case Switch:
            self().size = u8();
            arg();
            break;
        case Jump:
            self().value = u16();
            break;
        case JumpIfNot:
            self().value = u16();
            arg();
            break;
        case Assert:
            self().value = u16();
            arg();
            break;
        case Case:
            self().value = u16();
            if (self().value != 0xffff)
                arg();
            break;
        case Stop:
        case Nothing:
        case EndFunctionParms:
        case Self:
        case IntZero:
        case IntOne:
        case True:
        case False:
        case NoObject:
        case IteratorPop:
        case IteratorNext:
            break;
        case LabelTable:
            for (int n = 0; n < 1024 && !failed(); ++n) {
                const int32_t label = name();
                const uint32_t offset = u32();
                if (fs::toLower(package_.name(label)) == "none")
                    break;
                self().labels.push_back({label, offset});
            }
            break;
        case Let:
        case LetBool:
        case DynArrayElement:
        case ArrayElement:
            arg();
            arg();
            break;
        case New:
            for (int k = 0; k < 4; ++k)
                arg();
            break;
        case Context:
        case ClassContext:
            arg();
            self().value = u16();
            self().size = u8();
            arg();
            break;
        case MetaCast:
        case DynamicCast:
        case StructMember:
            self().ref = ref();
            arg();
            break;
        case Skip:
            self().value = u16();
            arg();
            break;
        case VirtualFunction:
        case GlobalFunction:
            self().ref = name();
            params();
            break;
        case FinalFunction:
            self().ref = ref();
            params();
            break;
        case IntConst:
            self().value = int32_t(u32());
            break;
        case FloatConst:
            self().f[0] = f32();
            break;
        case StringConst: {
            std::string s;
            for (uint8_t c; (c = u8()) != 0 && !failed();)
                s += char(c);
            self().text = std::move(s);
            break;
        }
        case UnicodeStringConst: {
            std::string s;
            for (uint16_t c; (c = u16()) != 0 && !failed();)
                s += c < 0x80 ? char(c) : '?';
            self().text = std::move(s);
            break;
        }
        case RotationConst:
            for (int32_t& v : self().rot)
                v = int32_t(u32());
            break;
        case VectorConst:
            for (float& v : self().f)
                v = f32();
            break;
        case ByteConst:
        case IntConstByte:
            self().value = u8();
            break;
        case Iterator:
            arg();
            self().value = u16();
            break;
        case StructCmpEq:
        case StructCmpNe:
            self().ref = ref();
            arg();
            arg();
            break;
        default: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "unknown token 0x%02x at %u", t, start);
            fail(buf);
            return -1;
        }
        }
        return failed() ? -1 : index;
    }

    void fail(const std::string& why) {
        if (error_.empty())
            error_ = why;
    }

private:
    uint8_t u8() {
        mem_ += 1;
        return r_.u8();
    }
    uint16_t u16() {
        mem_ += 2;
        return r_.u16();
    }
    uint32_t u32() {
        mem_ += 4;
        return r_.u32();
    }
    float f32() {
        mem_ += 4;
        return r_.f32();
    }
    int32_t ref() {
        mem_ += 4;
        return r_.index();
    }
    int32_t name() {
        mem_ += 4;
        const int32_t n = r_.index();
        if (n < 0 || size_t(n) >= package_.names().size())
            fail("name " + std::to_string(n) + " out of range");
        return n;
    }

    const Package& package_;
    Reader& r_;
    Code& code_;
    uint32_t mem_ = 0;
    std::string error_;
};

} // namespace

Error decodeCode(const Package& package, Reader& r, uint32_t scriptSize, Code& out) {
    out = {};
    out.size = scriptSize;
    if (scriptSize > 0x100000)
        return Error::fail("a script of " + std::to_string(scriptSize) + " bytes");
    Decoder d(package, r, out);
    while (d.at() < scriptSize && !d.failed())
        out.statements.push_back(d.expr());
    if (d.failed())
        return Error::fail(d.error().empty() ? "the code runs past the end" : d.error());
    if (d.at() != scriptSize)
        return Error::fail("the code ends at " + std::to_string(d.at()) + ", not " + std::to_string(scriptSize));
    return Error::success();
}

bool isScriptClass(std::string_view className) {
    const std::string c = fs::toLower(className);
    return c == "const" || c == "enum" || c == "struct" || c == "function" || c == "state" || c == "class" ||
           (c.size() > 8 && c.compare(c.size() - 8, 8, "property") == 0);
}

Error readScriptObject(const Package& package, int index, ScriptObject& out) {
    out = {};
    if (index < 0 || size_t(index) >= package.exports().size())
        return Error::fail("no export " + std::to_string(index));
    const Export& e = package.exports()[size_t(index)];
    const std::string what = package.path(index + 1);
    out.className = e.classRef == 0 ? "Class" : package.objectName(e.classRef);
    const std::string cls = fs::toLower(out.className);
    if (!isScriptClass(cls))
        return Error::fail(what + ": a " + out.className + ", not a script object");
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(e.serialOffset));
    // Classes start with their fields; the rest with their own (empty)
    // properties.
    if (e.classRef != 0) {
        PropertyList props;
        if (Error err = beginObject(package, index, r, props))
            return err;
    }
    out.super = r.index();
    out.next = r.index();
    auto done = [&]() -> Error {
        if (!r.ok())
            return Error::fail(what + ": runs past the end");
        out.trailing = r.remaining();
        return Error::success();
    };
    if (cls == "const") {
        out.kind = ScriptObject::Kind::Const;
        out.constValue = r.string(package.version());
        return done();
    }
    if (cls == "enum") {
        out.kind = ScriptObject::Kind::Enum;
        const int32_t n = r.index();
        if (n < 0 || n > 256)
            return Error::fail(what + ": " + std::to_string(n) + " names");
        for (int32_t i = 0; i < n; ++i)
            out.enumNames.push_back(r.index());
        return done();
    }
    if (cls.size() > 8 && cls.compare(cls.size() - 8, 8, "property") == 0) {
        out.kind = ScriptObject::Kind::Property;
        out.arrayDim = r.i32();
        out.propertyFlags = r.u32();
        out.category = r.index();
        if (out.propertyFlags & propflag::Net)
            out.repOffset = r.u16();
        if (cls == "byteproperty" || cls == "objectproperty" || cls == "structproperty" || cls == "arrayproperty" ||
            cls == "fixedarrayproperty" || cls == "classproperty")
            out.propertyRef = r.index();
        if (cls == "classproperty")
            out.metaClass = r.index();
        if (cls == "fixedarrayproperty")
            out.fixedCount = r.i32();
        return done();
    }
    // Structs, functions, states and classes.
    out.kind = cls == "function" ? ScriptObject::Kind::Function
               : cls == "state"  ? ScriptObject::Kind::State
               : cls == "class"  ? ScriptObject::Kind::Class
                                 : ScriptObject::Kind::Struct;
    out.scriptText = r.index();
    out.children = r.index();
    out.friendlyName = r.index();
    out.line = r.i32();
    out.textPos = r.i32();
    const uint32_t scriptSize = r.u32();
    if (!r.ok())
        return Error::fail(what + ": runs past the end");
    if (Error err = decodeCode(package, r, scriptSize, out.code))
        return Error::fail(what + ": " + err.message);
    if (out.kind == ScriptObject::Kind::Function) {
        out.native = r.u16();
        out.operatorPrecedence = r.u8();
        out.functionFlags = r.u32();
        if (out.functionFlags & funcflag::Net)
            out.repOffset = r.u16();
        return done();
    }
    if (out.kind == ScriptObject::Kind::Struct)
        return done();
    out.probeMask = r.u64();
    out.ignoreMask = r.u64();
    out.labelTableOffset = r.u16();
    out.stateFlags = r.u32();
    if (out.kind == ScriptObject::Kind::State)
        return done();
    out.classFlags = r.u32();
    r.bytes(out.guid.data(), out.guid.size());
    const int32_t deps = r.index();
    if (deps < 0 || deps > 4096)
        return Error::fail(what + ": " + std::to_string(deps) + " dependencies");
    for (int32_t i = 0; i < deps && r.ok(); ++i) {
        ScriptObject::Dependency d;
        d.cls = r.index();
        d.deep = r.u32();
        d.scriptTextCrc = r.u32();
        out.dependencies.push_back(d);
    }
    const int32_t imports = r.index();
    if (imports < 0 || imports > 4096)
        return Error::fail(what + ": " + std::to_string(imports) + " package imports");
    for (int32_t i = 0; i < imports && r.ok(); ++i)
        out.packageImports.push_back(r.index());
    out.within = r.index();
    out.configName = r.index();
    if (!r.ok())
        return Error::fail(what + ": runs past the end");
    if (Error err = readProperties(package, r, out.defaults))
        return Error::fail(what + ": its defaults: " + err.message);
    return done();
}

} // namespace reluva::unreal
