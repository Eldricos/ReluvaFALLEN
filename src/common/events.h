// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva {

// Key codes are backend-independent. Printable keys use their lowercase
// ASCII value; everything else uses the values below.
namespace key {
constexpr uint32_t Unknown = 0;
constexpr uint32_t Backspace = 8;
constexpr uint32_t Tab = 9;
constexpr uint32_t Return = 13;
constexpr uint32_t Escape = 27;
constexpr uint32_t Space = 32;
constexpr uint32_t Delete = 127;

constexpr uint32_t Up = 0x100;
constexpr uint32_t Down = 0x101;
constexpr uint32_t Left = 0x102;
constexpr uint32_t Right = 0x103;
constexpr uint32_t Insert = 0x104;
constexpr uint32_t Home = 0x105;
constexpr uint32_t End = 0x106;
constexpr uint32_t PageUp = 0x107;
constexpr uint32_t PageDown = 0x108;
constexpr uint32_t F1 = 0x110; // F1..F12 are consecutive
constexpr uint32_t Kp0 = 0x120; // the numeric keypad's 0..9 are consecutive, whatever NumLock says
constexpr uint32_t KpPeriod = 0x12a;
constexpr uint32_t KpEnter = 0x12b;

constexpr uint32_t LShift = 0x130;
constexpr uint32_t RShift = 0x131;
constexpr uint32_t LCtrl = 0x132;
constexpr uint32_t RCtrl = 0x133;
constexpr uint32_t LAlt = 0x134;
constexpr uint32_t RAlt = 0x135;
} // namespace key

namespace keymod {
constexpr uint16_t Shift = 1 << 0;
constexpr uint16_t Ctrl = 1 << 1;
constexpr uint16_t Alt = 1 << 2;
} // namespace keymod

enum class EventType {
    None,
    Quit,              // user closed the window: leave the engine and the app
    ReturnToLauncher,  // user asked to go back to the launcher
    KeyDown,
    KeyUp,
    MouseMove,
    MouseDown,
    MouseUp,
    MouseWheel,
    TextInput, // text typed, after the keyboard layout and dead keys (System::setTextInput)
};

enum class MouseButton { None, Left, Right, Middle };

struct Event {
    EventType type = EventType::None;
    uint32_t key = key::Unknown; // KeyDown/KeyUp
    uint16_t mods = 0;           // KeyDown/KeyUp, and the mouse events (keymod bits)
    bool repeat = false;         // KeyDown auto-repeat
    int mouseX = 0, mouseY = 0;  // mouse events, in game framebuffer pixels
    float moveX = 0, moveY = 0;  // MouseMove: how far the mouse moved, in its own units (System::setRelativeMouse)
    MouseButton button = MouseButton::None;
    int clicks = 0;              // MouseDown/MouseUp: 1, or 2 for a double click, ...
    int wheelY = 0;
    char text[32] = {};          // TextInput: UTF-8, NUL-terminated
};

} // namespace reluva
