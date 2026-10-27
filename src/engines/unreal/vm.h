// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The UnrealScript virtual machine: classes loaded from their compiled
// script (script.h) into a runtime form, objects made from them with
// their properties' values, and an interpreter for the bytecode, with
// states, state code that waits (latent functions), timers and native
// functions written in C++ (vm_natives_*.cpp). See docs/engines/unreal_vm.md.
//
// Values are kept by property, not in raw memory as the original does: an
// object holds one Value per property element, a struct value its fields'
// Values. The layout is Reluva's own; scripts only see properties.

#include "engines/unreal/package_set.h"
#include "engines/unreal/script.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

class ScriptLevel;
class Vm;
struct VmClass;
struct VmFunction;
struct VmObject;
struct VmState;
struct VmStruct;

// A name, interned for the whole VM: names compare without case.
using NameId = int32_t;

enum class VType : uint8_t { None, Byte, Int, Bool, Float, Name, String, Object, Struct };

struct Value {
    VType type = VType::None;
    int32_t i = 0;         // Byte, Int, Bool (0 or 1), Name
    float f = 0;           // Float
    VmObject* o = nullptr; // Object; a class object for class values
    std::string s;         // String
    const VmStruct* st = nullptr; // Struct: which
    std::vector<Value> fields;    // Struct: one per field slot

    static Value byteValue(int v);
    static Value intValue(int32_t v);
    static Value boolValue(bool v);
    static Value floatValue(float v);
    static Value nameValue(NameId v);
    static Value stringValue(std::string v);
    static Value objectValue(VmObject* v);

    // Read as a type, converting as UnrealScript does (None reads as 0,
    // false, "" or None).
    int32_t asInt() const;
    float asFloat() const;
    bool asBool() const;
    VmObject* asObject() const;
};

struct VmProperty {
    std::string name;
    NameId nameId = 0;
    VType type = VType::None;
    bool isClass = false;            // a ClassProperty: holds classes
    VmClass* objectClass = nullptr;  // Object: the class of what it holds; Class: the meta class
    const VmStruct* structType = nullptr;
    const std::vector<NameId>* enumNames = nullptr; // Byte with an enum
    int arrayDim = 1;
    uint32_t flags = 0; // propflag::
    int slot = 0;       // its first element's index in its owner's values

    Value zero() const; // its type's empty value
};

struct VmStruct {
    enum class Kind { Struct, Function, State, Class } kind = Kind::Struct;
    std::string name;
    NameId nameId = 0;
    const Package* package = nullptr; // where it's defined; its code's references are into this
    int exportIndex = -1;
    VmStruct* super = nullptr;
    std::vector<VmProperty*> properties; // inherited ones first, in slot order
    int slots = 0;
    std::vector<Value> defaults; // one per slot
    Code code;
    std::unordered_map<uint32_t, int> statementAt; // code offset -> statement index
    // What each expression's reference resolved to (a property, function,
    // class...), filled in as the code runs.
    mutable std::vector<const void*> resolved;

    virtual ~VmStruct() = default;
    VmProperty* findProperty(NameId name) const;
};

struct VmFunction : VmStruct {
    uint32_t flags = 0; // funcflag::
    int native = 0;
    std::string symbol;   // an operator's ("+=")
    VmStruct* outer = nullptr; // the class or state it's in
    std::vector<VmProperty*> params;
    VmProperty* returnValue = nullptr;
    std::string qualified; // "Class.Name", for natives and messages
};

struct VmState : VmStruct {
    VmClass* owner = nullptr;
    std::map<NameId, VmFunction*> functions; // its own
    std::map<NameId, uint32_t> labels;       // state code labels
    uint32_t stateFlags = 0;
};

struct VmClass : VmState {
    VmClass* parent() const { return static_cast<VmClass*>(super); }
    std::map<NameId, VmState*> states; // its own
    uint32_t classFlags = 0;
    VmObject* object = nullptr; // the class as an object (class values point here)
    bool isA(const VmClass* other) const;
};

struct VmObject {
    VmClass* cls = nullptr;
    std::vector<Value> values; // one per slot of cls
    NameId name = 0;
    VmClass* asClass = nullptr; // this object is a class
    // An object loaded from a package that the VM doesn't run (a texture,
    // a sound, a mesh): where it is.
    const Package* package = nullptr;
    int exportIndex = -1;
    bool deleted = false;
    bool isActor = false;

    // Its state and state code.
    VmState* state = nullptr;
    VmState* codeState = nullptr; // whose state code runs (its state's, or a parent state's it inherits)
    int codeAt = -1;       // the next statement of that code to run; -1 none
    float sleeping = 0;    // seconds a latent Sleep has left
    bool latent = false;   // waiting in a latent function
    int stateSerial = 0;   // counts state changes, so running state code sees one
    std::set<NameId> disabled; // events Disable()d (Tick, Timer...)
    float timerRate = 0, timerCount = 0;
    bool timerLoop = false;

    Value* find(Vm& vm, std::string_view property);
};

// A native function's call: its arguments (by parameter, converted to
// their types; out parameters are written back after), what it returns.
struct NativeCall {
    Vm& vm;
    VmObject* self;
    const VmFunction* function;
    std::vector<Value> args;
    std::vector<bool> given; // whether each argument was given (optional ones)
    Value result;
    float latentSeconds = -1;  // >= 0: the call waits this long (a latent function)
    std::vector<Value> items;  // an iterator's results, put in its first out parameter in turn

    int32_t i(size_t k) const { return k < args.size() ? args[k].asInt() : 0; }
    float f(size_t k) const { return k < args.size() ? args[k].asFloat() : 0; }
    bool b(size_t k) const { return k < args.size() && args[k].asBool(); }
    const std::string& s(size_t k) const;
    VmObject* o(size_t k) const { return k < args.size() ? args[k].asObject() : nullptr; }
};
using NativeFn = std::function<void(NativeCall&)>;

class Vm {
public:
    explicit Vm(const PackageSet& packages);
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    const PackageSet& packages() const { return packages_; }

    NameId name(std::string_view text);
    const std::string& nameText(NameId id) const;

    // A class by "Package.Class" or by name alone (the first package
    // defining it); nullptr if not found or it doesn't load.
    VmClass* findClass(std::string_view name);
    // The class, function, state, struct or property an export is.
    VmClass* classAt(const Package& package, int exportIndex);
    VmStruct* structAt(const Package& package, int exportIndex);
    VmProperty* propertyAt(const Package& package, int exportIndex);
    // The object a reference in `package` names: a class, an object made
    // for a level (registered with adopt()), or one standing for a
    // package's object the VM doesn't run (a texture...).
    VmObject* objectAt(const Package& package, int32_t ref);
    // An object of a class, its properties its class's defaults; its name
    // the class's and a number if none is given. Its Outer, Name and Class
    // (Object's own properties) are set.
    VmObject* create(VmClass* cls, NameId name = 0, VmObject* outer = nullptr);
    // Registers an object as the one a package's export is (level actors).
    void adopt(const Package& package, int exportIndex, VmObject* object);
    // Sets an object's properties from tagged properties read in `package`.
    void applyProperties(VmObject* object, const Package& package, const PropertyList& props);
    // A tagged property's value as a property's type.
    Value valueOf(const VmProperty& p, const Package& package, const Property& data);
    const std::vector<std::unique_ptr<VmObject>>& objects() const { return objects_; }

    // Calls a function by name on an object (an event the engine sends):
    // its state's version first, then its class's. Returns its result, or
    // an empty value if it has none.
    Value callEvent(VmObject* self, std::string_view function, std::vector<Value> args = {});
    VmFunction* findFunction(VmObject* self, NameId name, bool global = false);
    Value invoke(VmFunction* f, VmObject* self, std::vector<Value> args, std::vector<Value*> outs = {});

    // States: sets an object's state (EndState, BeginState) and its state
    // code to the label (Begin by default).
    void gotoState(VmObject* self, NameId state, NameId label = 0);
    // Runs an object's state code until it waits or stops, its timer, and
    // (unless disabled) its Tick event.
    void tick(VmObject* self, float seconds);

    // A class the engine defines in C++ only, with no script (Viewport):
    // made up under its parent so scripts can cast to it and objects of it
    // can be made. Found by name when a reference to it doesn't resolve.
    VmClass* defineIntrinsic(std::string_view name, VmClass* parent);
    VmClass* intrinsic(std::string_view name);

    void registerNative(std::string_view qualifiedName, NativeFn fn);
    bool hasNative(std::string_view qualifiedName) const { return natives_.count(std::string(qualifiedName)) > 0; }

    // Core's structs, and values of them.
    const VmStruct* coreStruct(std::string_view name);
    Value vectorValue(float x, float y, float z);
    Value rotatorValue(int32_t pitch, int32_t yaw, int32_t roll);
    static std::array<float, 3> vectorOf(const Value& v);
    static std::array<int32_t, 3> rotatorOf(const Value& v);
    // Finds an object by its path ("Package.Group.Name"), for
    // DynamicLoadObject.
    VmObject* findObject(std::string_view path);
    // A class's config properties from the .ini files (Vm::applyConfig).
    void applyConfig(VmClass& cls);
    const std::string* configSetting(const std::string& section, const std::string& key);
    // An object's name or a class's, as scripts see it.
    std::string objectName(const VmObject* o) const;
    std::string pathName(const VmObject* o); // string() of it: "Package.Name"

    // Converts a value to a property's type (assignment).
    Value convert(const Value& v, const VmProperty& to) const;
    void assign(Value& dst, const Value& src) const;
    bool equal(const Value& a, const Value& b) const;
    std::string toString(const Value& v) const;
    bool isA(const VmObject* o, std::string_view className);

    // What happened, for diagnostics.
    struct Stats {
        uint64_t calls = 0, statements = 0, nativeCalls = 0;
        int accessedNone = 0;
        std::map<std::string, int> missingNatives; // "Class.Function" -> calls
        std::map<std::string, int> errors;         // message -> times
    };
    Stats stats;
    std::function<void(const std::string&)> logger; // Log() and warnings

    // The level running, for the natives that need the world.
    ScriptLevel* level = nullptr;
    void reportError(const std::string& what);
    // Where scripts are running: " in Class.Function", for natives' errors.
    std::string where() const;

private:
    friend class Interpreter;
    void loadStruct(VmStruct& s, const ScriptObject& so);
    VmFunction* loadFunction(const Package& package, int exportIndex, VmStruct* outer);
    VmState* loadState(const Package& package, int exportIndex, VmClass* owner);
    void addProperty(VmStruct& s, const Package& package, int exportIndex, const ScriptObject& so);
    void prepareCode(VmStruct& s);
    VmObject* classObject(VmClass* cls);
    VmState* findState(VmClass* cls, NameId name);
    VmFunction* nativeFunction(int number);
    void scanNatives();
    Value readStruct(const VmStruct& st, const Package& package, Reader& r);
    Value readValue(const VmProperty& p, const Package& package, Reader& r);
    float pendingLatent_ = -1;
    std::vector<const VmStruct*> running_; // the code running, innermost last
    std::map<int, VmFunction*> nativeFunctions_;
    bool nativesScanned_ = false;
    std::map<NameId, const VmStruct*> coreStructs_;
    std::map<const VmClass*, int> nameCounts_;
    std::map<NameId, std::unique_ptr<VmClass>> intrinsics_;

    const PackageSet& packages_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, NameId> nameIds_;
    std::map<std::pair<const Package*, int>, std::unique_ptr<VmStruct>> structs_;
    using IniFile = std::map<std::string, std::map<std::string, std::string>>; // section, key (lower case): value
    std::map<std::string, IniFile> iniFiles_;
    std::map<std::pair<const Package*, int>, VmProperty*> properties_;
    std::deque<VmProperty> propertyStore_;
    std::deque<std::vector<NameId>> enumStore_;
    std::map<std::pair<const Package*, int>, VmObject*> exportObjects_;
    std::vector<std::unique_ptr<VmObject>> objects_;

    std::map<std::string, NativeFn> natives_;
    int depth_ = 0;
};

// The natives of Core's Object (operators, maths, strings, states).
void registerCoreNatives(Vm& vm);

} // namespace reluva::unreal
