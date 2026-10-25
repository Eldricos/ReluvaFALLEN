// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/cvars.h"

#include "engines/idtech3/qvm.h"

#include <cctype>
#include <cstdlib>

namespace reluva::idtech3 {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

CvarStore::Cvar& CvarStore::create(std::string_view name, std::string_view defaultValue, int32_t flags) {
    if (Cvar* cv = findMutable(name)) {
        // The game code registering a variable the user made: it's the
        // code's now, with the code's default.
        if (cv->flags & UserCreated) {
            cv->flags &= ~UserCreated;
            cv->defaultValue = defaultValue;
        }
        cv->flags |= flags;
        return *cv;
    }
    Cvar cv;
    cv.name = name;
    cv.value = defaultValue;
    cv.defaultValue = defaultValue;
    cv.flags = flags;
    cv.modificationCount = 1;
    cv.handle = int32_t(cvars_.size()) + 1;
    byName_[lower(name)] = cvars_.size();
    cvars_.push_back(std::move(cv));
    return cvars_.back();
}

void CvarStore::set(std::string_view name, std::string_view value, bool force) {
    Cvar* cv = findMutable(name);
    if (!cv) {
        create(name, value, UserCreated);
        return;
    }
    if (!force && (cv->flags & (ReadOnly | Init)))
        return;
    if (cv->value == value)
        return;
    cv->value = value;
    ++cv->modificationCount;
}

const CvarStore::Cvar* CvarStore::find(std::string_view name) const {
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? nullptr : &cvars_[it->second];
}

CvarStore::Cvar* CvarStore::findMutable(std::string_view name) {
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? nullptr : &cvars_[it->second];
}

const CvarStore::Cvar* CvarStore::byHandle(int32_t handle) const {
    return handle >= 1 && size_t(handle) <= cvars_.size() ? &cvars_[size_t(handle) - 1] : nullptr;
}

std::string CvarStore::string(std::string_view name) const {
    const Cvar* cv = find(name);
    return cv ? cv->value : std::string();
}

float CvarStore::value(std::string_view name) const {
    const Cvar* cv = find(name);
    return cv ? std::strtof(cv->value.c_str(), nullptr) : 0.0f;
}

int32_t CvarStore::integer(std::string_view name) const {
    const Cvar* cv = find(name);
    return cv ? int32_t(std::strtol(cv->value.c_str(), nullptr, 10)) : 0;
}

void CvarStore::writeVmCvar(Qvm& vm, int32_t address, int32_t handle) const {
    const Cvar* cv = byHandle(handle);
    if (!cv || !vm.valid(address, kVmCvarSize))
        return;
    vm.writeInt(address, handle);
    vm.writeInt(address + 4, cv->modificationCount);
    vm.writeInt(address + 8, Qvm::fromFloat(std::strtof(cv->value.c_str(), nullptr)));
    vm.writeInt(address + 12, int32_t(std::strtol(cv->value.c_str(), nullptr, 10)));
    vm.writeString(address + 16, cv->value, 256);
}

void CvarStore::updateVmCvar(Qvm& vm, int32_t address) const {
    const Cvar* cv = byHandle(vm.readInt(address));
    if (cv && vm.readInt(address + 4) != cv->modificationCount)
        writeVmCvar(vm, address, cv->handle);
}

} // namespace reluva::idtech3
