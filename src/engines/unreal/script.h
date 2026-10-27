// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// UnrealScript as packages store it, compiled: classes and what they're
// made of (properties, functions, states, structs, enums, constants: the
// UField subclasses, chained through their Next references), and the
// functions' and states' bytecode, decoded into expression trees. See
// docs/formats/unreal/script.md.

#include "engines/unreal/package.h"
#include "engines/unreal/properties.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::unreal {

// Property flags (a property's PropertyFlags).
namespace propflag {
constexpr uint32_t Edit = 0x1, Const = 0x2, Input = 0x4, ExportObject = 0x8, OptionalParm = 0x10, Net = 0x20,
                   ConstRef = 0x40, Parm = 0x80, OutParm = 0x100, SkipParm = 0x200, ReturnParm = 0x400,
                   CoerceParm = 0x800, Native = 0x1000, Transient = 0x2000, Config = 0x4000, Localized = 0x8000,
                   Travel = 0x10000, EditConst = 0x20000, GlobalConfig = 0x40000;
} // namespace propflag

// Function flags (a function's FunctionFlags).
namespace funcflag {
constexpr uint32_t Final = 0x1, Defined = 0x2, Iterator = 0x4, Latent = 0x8, PreOperator = 0x10, Singular = 0x20,
                   Net = 0x40, NetReliable = 0x80, Simulated = 0x100, Exec = 0x200, Native = 0x400, Event = 0x800,
                   Operator = 0x1000, Static = 0x2000;
} // namespace funcflag

// The bytecode's tokens (the first byte of each expression).
namespace token {
enum : uint8_t {
    LocalVariable = 0x00, InstanceVariable = 0x01, DefaultVariable = 0x02, Return = 0x04, Switch = 0x05,
    Jump = 0x06, JumpIfNot = 0x07, Stop = 0x08, Assert = 0x09, Case = 0x0a, Nothing = 0x0b, LabelTable = 0x0c,
    GotoLabel = 0x0d, EatString = 0x0e, Let = 0x0f, DynArrayElement = 0x10, New = 0x11, ClassContext = 0x12,
    MetaCast = 0x13, LetBool = 0x14, EndFunctionParms = 0x16, Self = 0x17, Skip = 0x18, Context = 0x19,
    ArrayElement = 0x1a, VirtualFunction = 0x1b, FinalFunction = 0x1c, IntConst = 0x1d, FloatConst = 0x1e,
    StringConst = 0x1f, ObjectConst = 0x20, NameConst = 0x21, RotationConst = 0x22, VectorConst = 0x23,
    ByteConst = 0x24, IntZero = 0x25, IntOne = 0x26, True = 0x27, False = 0x28, NativeParm = 0x29,
    NoObject = 0x2a, IntConstByte = 0x2c, BoolVariable = 0x2d, DynamicCast = 0x2e, Iterator = 0x2f,
    IteratorPop = 0x30, IteratorNext = 0x31, StructCmpEq = 0x32, StructCmpNe = 0x33, UnicodeStringConst = 0x34,
    StructMember = 0x36, GlobalFunction = 0x38,
    FirstConversion = 0x39, LastConversion = 0x5f, // each one expression to convert
    ExtendedNative = 0x60, // 0x60-0x6f: native (token - 0x60) * 256 + the next byte
    FirstNative = 0x70,    // 0x70-0xff: native function `token`
};
} // namespace token

// One expression of bytecode, its operands, and its subexpressions (by
// index into Code::exprs).
struct Expr {
    uint8_t token = 0;
    uint32_t offset = 0;   // where it starts, counted as the engine does in memory (see script.md)
    int native = -1;       // native calls: the native function's number
    int32_t ref = 0;       // an object reference, or a name index (functions called by name, name constants)
    int32_t value = 0;     // ints, bytes, jump targets, skips, line numbers
    uint8_t size = 0;      // Context, ClassContext, Switch: a byte operand
    float f[3] = {};       // float, vector constants
    int32_t rot[3] = {};   // rotator constants
    std::string text;      // string constants
    std::vector<int> args; // subexpressions, in order
    std::vector<std::pair<int32_t, uint32_t>> labels; // LabelTable: name index, code offset
};

struct Code {
    std::vector<Expr> exprs;
    std::vector<int> statements; // the top-level expressions, in order
    uint32_t size = 0;           // in memory
};

// Decodes `scriptSize` bytes of bytecode (as the engine counts them in
// memory) from `r`; fails if the tokens don't end exactly there.
Error decodeCode(const Package& package, Reader& r, uint32_t scriptSize, Code& out);

// A UField subclass's export, read: which kind it is and what it stores.
struct ScriptObject {
    enum class Kind { Const, Enum, Property, Struct, Function, State, Class } kind = Kind::Const;
    std::string className; // the export's class ("IntProperty", "Function"...)
    int32_t super = 0, next = 0;

    // Const: its value as written in the source.
    std::string constValue;
    // Enum: its names (name indices).
    std::vector<int32_t> enumNames;

    // Property.
    int32_t arrayDim = 1;
    uint32_t propertyFlags = 0;
    int32_t category = 0; // name index
    uint16_t repOffset = 0;
    // Byte: its enum; Object, Class: its class; Struct: its struct; Array:
    // its inner property; FixedArray: its inner property.
    int32_t propertyRef = 0;
    int32_t metaClass = 0; // Class: the classes it holds are this or below
    int32_t fixedCount = 0;

    // Struct and up.
    int32_t scriptText = 0, children = 0, friendlyName = 0;
    int32_t line = 0, textPos = 0;
    Code code;

    // Function.
    uint16_t native = 0;
    uint8_t operatorPrecedence = 0;
    uint32_t functionFlags = 0;

    // State and Class.
    uint64_t probeMask = 0, ignoreMask = 0;
    uint16_t labelTableOffset = 0;
    uint32_t stateFlags = 0;

    // Class.
    uint32_t classFlags = 0;
    std::array<uint8_t, 16> guid{};
    struct Dependency {
        int32_t cls = 0;
        uint32_t deep = 0, scriptTextCrc = 0;
    };
    std::vector<Dependency> dependencies;
    std::vector<int32_t> packageImports; // name indices
    int32_t within = 0;
    int32_t configName = 0; // name index
    PropertyList defaults;  // the class's default properties

    size_t trailing = 0; // bytes left after what was read (none expected)
};

// Whether an export's class is one of the UField subclasses above.
bool isScriptClass(std::string_view className);

Error readScriptObject(const Package& package, int exportIndex, ScriptObject& out);

} // namespace reluva::unreal
