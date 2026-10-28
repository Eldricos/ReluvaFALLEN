// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The engine's side of the console, for a running level (ScriptLevel):
// console commands as the scripts send them (ConsoleCommand: settings got
// and set, quitting, the screen's size), the keys held down (IsKeyDown)
// and travel to another map (ClientTravel). The keys reach the console's
// own KeyEvent first (LevelPlay::consoleKey). And the cut scenes'
// fast-forward. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm_level.h"

#include "common/fs.h"

namespace reluva::unreal {

namespace {

// The words of a command, split at spaces; the last keeps the rest ("set
// A B some value").
std::vector<std::string> words(const std::string& command, size_t most) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < command.size() && out.size() + 1 < most) {
        while (at < command.size() && command[at] == ' ')
            ++at;
        const size_t end = command.find(' ', at);
        if (at < command.size())
            out.push_back(command.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? command.size() : end + 1;
    }
    while (at < command.size() && command[at] == ' ')
        ++at;
    if (at < command.size())
        out.push_back(command.substr(at));
    return out;
}

// The .ini section a command's "get" and "set" name: "ini:Engine.Hud"
// is [Engine.Hud]; "ini:Engine.Engine.AudioDevice" is the section of the
// class [Engine.Engine] AudioDevice names (Galaxy.GalaxyAudioSubsystem);
// "Class" alone is its own section.
std::string sectionOf(Vm& vm, std::string name) {
    if (fs::toLower(name.substr(0, 4)) == "ini:")
        name = name.substr(4);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        if (const std::string* cls = vm.configSetting(name.substr(0, dot), name.substr(dot + 1));
            cls && cls->find('.') != std::string::npos)
            return *cls;
    return name;
}

// A console command, as Actor's and PlayerPawn's ConsoleCommand run it.
std::string command(NativeCall& c, const std::string& text) {
    const std::vector<std::string> w = words(text, 4);
    if (w.empty())
        return {};
    const std::string verb = fs::toLower(w[0]);
    // "get Section Key": the setting, else the class's default. "Class"
    // of an engine object named in the .ini (GameRenderDevice) is the
    // class it names, as the engine writes a class.
    if (verb == "get" && w.size() >= 3) {
        const std::string section = sectionOf(c.vm, w[1]);
        if (fs::toLower(w[2]) == "class" && section.find('.') != std::string::npos)
            return "Class'" + section + "'";
        if (const std::string* v = c.vm.configSetting(section, w[2]))
            return *v;
        // The engine's own settings no .ini here holds: the video options'
        // world and skin detail. Taken as the first of the game's choices
        // (HIGH, MEDIUM, LOW); Reluva's reading.
        if (fs::toLower(section) == "windrv.windowsclient" &&
            (fs::toLower(w[2]) == "texturedetail" || fs::toLower(w[2]) == "skindetail"))
            return "High";
        if (VmClass* cls = c.vm.findClass(section))
            if (const VmProperty* p = cls->findProperty(c.vm.name(w[2])); p && size_t(p->slot) < cls->defaults.size())
                return c.vm.toString(cls->defaults[size_t(p->slot)]);
        return {};
    }
    // The key bindings, as the controls page reads and changes them:
    // KEYNAME n is key n's name (EInputKey's, less "IK_"), KEYBINDING Key
    // its binding ([Engine.Input]), "SET Input Key Command" binds it from
    // now on.
    if (verb == "keyname" && w.size() >= 2) {
        VmClass* console = c.vm.findClass("Engine.Console");
        auto f = console ? console->functions.find(c.vm.name("KeyEvent")) : decltype(console->functions.end()){};
        if (!console || f == console->functions.end() || f->second->params.empty() || !f->second->params[0]->enumNames)
            return {};
        const std::vector<NameId>& keys = *f->second->params[0]->enumNames;
        const int n = std::atoi(w[1].c_str());
        if (n <= 0 || size_t(n) >= keys.size())
            return {};
        const std::string& name = c.vm.nameText(keys[size_t(n)]);
        return fs::toLower(name.substr(0, 3)) == "ik_" ? name.substr(3) : name;
    }
    if (verb == "keybinding" && w.size() >= 2) {
        const std::string* v = c.vm.configSetting("Engine.Input", w[1]);
        return v ? *v : std::string();
    }
    if (verb == "set" && w.size() >= 3 && fs::toLower(w[1]) == "input") {
        c.vm.setConfig("Engine.Input", w[2], w.size() > 3 ? w[3] : std::string());
        return {};
    }
    // "set Section Key Value": the setting from now on (not written to
    // the .ini), and the class's config property, defaults and every
    // object of it, if it is one.
    if (verb == "set" && w.size() >= 3) {
        const std::string section = sectionOf(c.vm, w[1]);
        const std::string value = w.size() > 3 ? w[3] : std::string();
        c.vm.setConfig(section, w[2], value);
        VmClass* cls = c.vm.findClass(section);
        const VmProperty* p = cls ? cls->findProperty(c.vm.name(w[2])) : nullptr;
        if (!p)
            return {};
        if (size_t(p->slot) < cls->defaults.size())
            c.vm.setFromText(*p, cls->defaults[size_t(p->slot)], value);
        for (const auto& o : c.vm.objects()) {
            if (o->deleted || size_t(p->slot) >= o->values.size())
                continue;
            for (const VmClass* k = o->cls; k; k = k->parent())
                if (k == cls) {
                    c.vm.setFromText(*p, o->values[size_t(p->slot)], value);
                    break;
                }
        }
        return {};
    }
    // "SaveGame [QuickSave]": the level saved (vm_save.cpp).
    if (verb == "savegame") {
        if (c.vm.level)
            if (Error err = c.vm.level->saveGame(w.size() > 1 ? w[1] : std::string()); err && c.vm.logger)
                c.vm.logger("SaveGame: " + err.message);
        return {};
    }
    if (verb == "exit" || verb == "quit") {
        if (c.vm.level)
            c.vm.level->quitAsked() = true;
        return {};
    }
    // The screen's size and colour depth, as the video options list them:
    // the page's window, true colour. Others can't be chosen.
    if (verb == "getcurrentres" || verb == "getres") {
        if (!c.vm.level)
            return {};
        const ScriptLevel::HudView& v = c.vm.level->hudView();
        return std::to_string(v.width) + "x" + std::to_string(v.height);
    }
    if (verb == "getcurrentcolordepth" || verb == "getcolordepths")
        return "32";
    // The rest (sound, detail settings) do nothing yet.
    return {};
}

} // namespace

VmObject* ScriptLevel::console() {
    if (!player_ || player_->deleted)
        return nullptr;
    const Value* viewport = player_->find(vm_, "Player");
    const Value* con = viewport && viewport->asObject() ? viewport->o->find(vm_, "Console") : nullptr;
    return con ? con->asObject() : nullptr;
}

void registerConsoleNatives(Vm& vm) {
    auto reg = [&](const char* name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // ConsoleCommand(string Command): a command's answer ("get"s), else "".
    for (const char* name : {"Actor.ConsoleCommand", "PlayerPawn.ConsoleCommand"})
        reg(name, [](NativeCall& c) { c.result = Value::stringValue(command(c, c.s(0))); });
    // Console's own: true, taken.
    reg("Console.ConsoleCommand", [](NativeCall& c) {
        command(c, c.s(0));
        c.result = Value::boolValue(true);
    });
    // IsKeyDown(int Key): whether that key (an EInputKey) is held.
    reg("Actor.IsKeyDown", [](NativeCall& c) {
        c.result = Value::boolValue(c.vm.level && c.vm.level->keysDown().count(c.i(0)) > 0);
    });
    // FastForward(bool TurnOn): a cut scene skipped (Escape during one),
    // the level run at speed until it's turned off again; whether it was
    // on.
    reg("Actor.FastForward", [](NativeCall& c) {
        if (!c.vm.level)
            return;
        c.result = Value::boolValue(c.vm.level->fastForward());
        c.vm.level->fastForward() = c.b(0);
    });
    // ClientTravel(string URL, ETravelType TravelType, bool bItems): to
    // another map, which the page running the level loads in its place.
    reg("PlayerPawn.ClientTravel", [](NativeCall& c) {
        if (c.vm.level)
            c.vm.level->travel() = c.s(0);
    });
}

} // namespace reluva::unreal
