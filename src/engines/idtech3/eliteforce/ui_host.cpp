// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/ui_host.h"

#include "engines/idtech3/md3.h"

#include <cstring>
#include <fstream>
#include <string>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force ui.qvm system calls. The evidence for each number is in
// docs/formats/idtech3/eliteforce-syscalls.md.
enum UiCall : int {
    Error_ = 0,
    Print = 1,
    Milliseconds = 2,
    CvarSet = 3,
    CvarVariableValue = 4,
    CvarVariableStringBuffer = 5,
    CvarSetValue = 6,
    CvarReset = 7,
    CvarCreate = 8,
    CvarInfoStringBuffer = 9,
    Argc = 10,
    Argv = 11,
    CmdExecuteText = 12,
    FsFOpenFile = 13,
    FsRead = 14,
    FsWrite = 15,
    FsFCloseFile = 16,
    FsGetFileList = 17,
    RRegisterModel = 18,
    RRegisterSkin = 19,
    RRegisterShaderNoMip = 20,
    RClearScene = 21,
    RAddRefEntityToScene = 22,
    RAddPolyToScene = 23,
    RAddLightToScene = 24,
    RRenderScene = 25,
    RSetColor = 26,
    RDrawStretchPic = 27,
    UpdateScreen = 28,
    CmLerpTag = 29, // Quake III's number; (orientation_t*, model, startFrame, endFrame, frac, tagName)
    SRegisterSound = 31,
    SStartLocalSound = 32,
    KeyKeynumToStringBuf = 33,
    KeyGetBindingBuf = 34,
    KeySetBinding = 35,
    KeyIsDown = 36,
    KeyGetOverstrikeMode = 37,
    KeySetOverstrikeMode = 38,
    KeyClearStates = 39,
    KeyGetCatcher = 40,
    KeySetCatcher = 41,
    GetClipboardData = 42,
    GetGlconfig = 43,
    GetClientState = 44,
    GetConfigString = 45,
    CvarRegister = 54,
    CvarUpdate = 55,
    MemoryRemaining = 56,
    SetCdKey = 57, // (key) -> whether it's taken; seen on the CD key screen's Accept
    RModelBounds = 58, // (model, mins[3], maxs[3]); seen after registering the phaser model
};

// glconfig_t as Elite Force's menus use it (qvm-refs): renderer, vendor
// and version strings of 1024 bytes and a 2048-byte extensions string,
// then a word of Elite Force's own and Quake III's 17 words, to byte 5192.
// Quake III's extensions string is 8192 bytes.
constexpr int32_t kGlconfigStrings = 1024 * 3 + 2048;
constexpr int32_t kGlconfigSize = 5192;
// uiClientState_t: three words, then three 1024-byte strings.
constexpr int32_t kClientStateSize = 12 + 3 * 1024;

} // namespace

UiHost::UiHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), vmFiles_(files) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

Error UiHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/ui.qvm", data))
        return Error::fail("vm/ui.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/ui.qvm: " + err.message);
    int32_t version = 0;
    if (Error err = call(GetApiVersion, {}, version))
        return err;
    if (version != kApiVersion)
        return Error::fail("vm/ui.qvm reports API version " + std::to_string(version) + ", not " +
                           std::to_string(kApiVersion));
    return Error::success();
}

Error UiHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    Error err = vm_.call(command, args, result);
    // The menus ask for the CD key until ui_cdkeychecked is set, which
    // they do once one is accepted. With a key kept from an earlier run,
    // that has happened.
    if (!err && command == Init && cdKeyLooksValid(storedCdKey()))
        cvars_.set("ui_cdkeychecked", "1", true);
    return err;
}

Error UiHost::refresh(int32_t time) {
    setTime(time);
    int32_t fullscreen = 0;
    if (Error err = call(IsFullscreen, {}, fullscreen))
        return err;
    if (!fullscreen && !mainMenuNotFullscreen_) {
        if (Error err = call(SetActiveMenu, {1})) // the main menu
            return err;
        // It should cover the screen now; if it doesn't, opening it every
        // frame would only keep resetting it.
        if (Error err = call(IsFullscreen, {}, fullscreen))
            return err;
        if (!fullscreen) {
            mainMenuNotFullscreen_ = true;
            log_ += "Reluva: the main menu doesn't report covering the screen; not reopening it\n";
        }
    }
    return call(Refresh, {time});
}

bool UiHost::cdKeyLooksValid(const std::string& key) {
    // The menus' own rule: up to 22 characters, 18 of them letters or
    // digits (the rest, say, dashes).
    if (key.size() > 22)
        return false;
    int alphanumeric = 0;
    for (char c : key)
        alphanumeric += (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    return alphanumeric == 18;
}

const std::string& UiHost::storedCdKey() {
    if (!cdKeyRead_) {
        // Kept by Reluva, not where the original keeps it.
        cdKeyRead_ = true;
        if (!keyFile_.empty()) {
            std::ifstream in(keyFile_);
            std::getline(in, cdKey_);
        }
    }
    return cdKey_;
}

void UiHost::writeGlconfig(int32_t address) {
    if (!vm_.checkWrite(address, kGlconfigSize))
        return;
    std::memset(&vm_.memory()[size_t(address)], 0, kGlconfigSize);
    vm_.writeString(address, "Reluva", 1024);
    vm_.writeString(address + 1024, "Reluva", 1024);
    vm_.writeString(address + 2048, "1.0", 1024);
    // Quake III's 17 words, in its order, from glconfigWords_.
    const int32_t words[17] = {
        2048,   // maxTextureSize
        2,      // numTextureUnits
        32, 24, 8, // colour, depth, stencil bits
        0, 0,   // driverType (ICD), hardwareType (generic)
        0,      // deviceSupportsGamma
        0, 1,   // textureCompression, textureEnvAddAvailable
        width_, height_,
        Qvm::fromFloat(float(width_) / float(height_)),
        60,     // displayFrequency
        0, 0, 0, // isFullscreen, stereoEnabled, smpActive
    };
    for (int i = 0; i < 17; ++i)
        if (glconfigWords_ + 4 * i + 4 <= kGlconfigSize)
            vm_.writeInt(address + glconfigWords_ + 4 * i, words[i]);
}

void UiHost::writeClientState(int32_t address) {
    if (!vm_.checkWrite(address, kClientStateSize))
        return;
    std::memset(&vm_.memory()[size_t(address)], 0, kClientStateSize);
    vm_.writeInt(address, 1); // connState: disconnected
}

std::vector<std::string> UiHost::recentCalls() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < recent_.size(); ++i) {
        const Recent& r = recent_[(recentNext_ + i) % recent_.size()];
        if (r.number < 0)
            continue;
        std::string line = "system call " + std::to_string(r.number) + " (" + std::to_string(r.args[0]) + ", " +
                           std::to_string(r.args[1]) + ", " + std::to_string(r.args[2]) + ", " +
                           std::to_string(r.args[3]) + ")";
        if (!r.text.empty())
            line += " \"" + r.text + "\"";
        out.push_back(std::move(line));
    }
    return out;
}

int32_t UiHost::systemCall(Qvm& vm, int number, const int32_t* a) {
    if (trace)
        trace(number, a);
    {
        Recent& r = recent_[recentNext_];
        recentNext_ = (recentNext_ + 1) % recent_.size();
        r.number = number;
        std::copy(a, a + 4, r.args);
        r.text.clear();
        if (number != SetCdKey)
            for (int i = 0; i < 2 && r.text.empty(); ++i) {
                if (a[i] <= 0 || !vm.valid(a[i], 1))
                    continue;
                std::string t = vm.string(a[i], 60);
                bool printable = t.size() >= 2;
                for (char c : t)
                    printable = printable && c >= 32 && c < 127;
                if (printable)
                    r.text = std::move(t);
            }
    }
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case Error_: vm.abort("ui.qvm: " + str(0)); return 0;
    case Print: {
        const std::string text = str(0, 4096);
        if (print)
            print(text);
        else
            log_ += text;
        return 0;
    }
    case Milliseconds: return time_;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
    case CvarVariableValue: return Qvm::fromFloat(cvars_.value(str(0, 256)));
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case CvarSetValue: {
        // Quake III writes whole numbers without a decimal point.
        const float v = f(1);
        const std::string text = v == float(int32_t(v)) ? std::to_string(int32_t(v)) : std::to_string(v);
        cvars_.set(str(0, 256), text);
        return 0;
    }
    case CvarReset:
        if (const CvarStore::Cvar* cv = cvars_.find(str(0, 256)))
            cvars_.set(cv->name, cv->defaultValue);
        return 0;
    case CvarCreate: cvars_.create(str(0, 256), str(1, 256), a[2]); return 0;
    case CvarInfoStringBuffer: {
        std::string info;
        for (const CvarStore::Cvar& cv : cvars_.all())
            if (cv.flags & a[0])
                info += "\\" + cv.name + "\\" + cv.value;
        vm.writeString(a[1], info, a[2]);
        return 0;
    }
    case Argc: return 0;
    case Argv: vm.writeString(a[1], "", a[2]); return 0;
    case CmdExecuteText: commands_.push_back(str(1, 4096)); return 0;
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case FsGetFileList: return vmFiles_.list(vm, a[0], a[1], a[2], a[3]);
    case RRegisterModel: return models_.add(str(0, 256));
    case RRegisterSkin: return skins_.add(str(0, 256));
    case RRegisterShaderNoMip: return shaders_.add(str(0, 256));
    case RClearScene: scene_.clear(); return 0;
    case RAddRefEntityToScene: scene_.addEntity(vm, a[0], 0); return 0;
    case RAddPolyToScene: scene_.addPoly(vm, a[0], a[1], a[2]); return 0; // (shader, numVerts, verts)
    case RAddLightToScene: // (origin, intensity, r, g, b)
        scene_.addLight(vm, a[0], Qvm::asFloat(a[1]), Qvm::asFloat(a[2]), Qvm::asFloat(a[3]), Qvm::asFloat(a[4]));
        return 0;
    case RRenderScene: scene_.render(vm, a[0], pictures_.size()); return 0;
    case UpdateScreen: return 0; // drawn each frame anyway
    case CmLerpTag: {
        auto [it, added] = md3s_.try_emplace(a[1]);
        if (added) {
            std::vector<uint8_t> data;
            auto md3 = std::make_shared<Md3Model>();
            const std::string& name = models_.name(a[1]);
            if (!name.empty() && !files_.read(name, data) && !parseModel(data, *md3))
                it->second = std::move(md3);
        }
        return writeLerpTag(vm, a[0], it->second.get(), a[2], a[3], Qvm::asFloat(a[4]), str(5, 64));
    }
    case RSetColor:
        if (!a[0])
            color_ = {1, 1, 1, 1};
        else if (!vm.valid(a[0], 16))
            vm.abort("R_SetColor: colour outside the program's memory");
        else
            for (int i = 0; i < 4; ++i)
                color_[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
        return 0;
    case RDrawStretchPic:
        pictures_.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], color_});
        return 0;
    case SRegisterSound: return sounds_.add(str(0, 256));
    case SStartLocalSound: // (sound, channel); the menus use channel 6
        if (startLocalSound && !sounds_.name(a[0]).empty())
            startLocalSound(sounds_.name(a[0]), a[1]);
        return 0;
    case KeyKeynumToStringBuf: vm.writeString(a[1], KeyBindings::keyName(a[0]), a[2]); return 0;
    case KeyGetBindingBuf: vm.writeString(a[1], bindings ? bindings->get(a[0]) : std::string(), a[2]); return 0;
    case KeySetBinding: // (key, command): the Controls menu
        if (bindings)
            bindings->set(a[0], str(1, 1024));
        return 0;
    case KeyIsDown: return 0;
    case KeyGetOverstrikeMode: return overstrike_;
    case KeySetOverstrikeMode: overstrike_ = a[0] != 0; return 0;
    case KeyClearStates: return 0;
    case KeyGetCatcher: return catcher_;
    case KeySetCatcher: catcher_ = a[0]; return 0;
    case GetClipboardData: vm.writeString(a[0], "", a[1]); return 0;
    case GetGlconfig: writeGlconfig(a[0]); return 0;
    case GetClientState: writeClientState(a[0]); return 0;
    case GetConfigString: vm.writeString(a[1], "", a[2]); return 0;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case MemoryRemaining: return 8 << 20;
    case SetCdKey: {
        // The CD key screen's Accept, after the menus' own check: whether
        // the engine takes the key. The original engine also checks it
        // against its own rules for the online service, which is gone;
        // Reluva applies the menus' rule again and keeps the key.
        const std::string key = str(0, 64);
        if (!cdKeyLooksValid(key))
            return 0;
        cdKey_ = key;
        cdKeyRead_ = true;
        if (!keyFile_.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(keyFile_.parent_path(), ec);
            std::ofstream(keyFile_) << cdKey_ << '\n';
        }
        return 1;
    }
    case RModelBounds: {
        // The first frame's box, as the original renderer gives; zeros for
        // a model it can't read.
        auto [it, added] = modelBounds_.try_emplace(a[0]);
        if (added) {
            std::vector<uint8_t> data;
            Md3Model md3;
            const std::string& name = models_.name(a[0]);
            if (!name.empty() && !files_.read(name, data) && !parseModel(data, md3) && !md3.frames.empty())
                it->second = {md3.frames[0].mins, md3.frames[0].maxs};
        }
        for (int i = 0; i < 3; ++i) {
            vm.writeInt(a[1] + 4 * i, Qvm::fromFloat(it->second.first[size_t(i)]));
            vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(it->second.second[size_t(i)]));
        }
        return 0;
    }
    default: ++unhandled_[number]; return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
