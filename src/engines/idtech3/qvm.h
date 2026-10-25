// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// A Quake III virtual machine program (.qvm): game code compiled to
// bytecode, run by an interpreter, which reaches the engine only through
// numbered system calls. See docs/formats/idtech3/qvm.md.
class Qvm {
public:
    // The engine's side: system call `number` with the VM's argument words
    // (args[0] is the first argument; there are at least 16). It returns
    // the call's result. A handler may read and write the VM's memory and
    // call back into it.
    using SystemCall = std::function<int32_t(Qvm& vm, int number, const int32_t* args)>;

    static constexpr int kMainArgs = 13; // vmMain's command and twelve arguments

    Error load(const std::vector<uint8_t>& file);
    void setSystemCall(SystemCall handler) { syscall_ = std::move(handler); }
    // Stops a call (with an error) after this many instructions; 0 = never.
    void setInstructionLimit(uint64_t limit) { limit_ = limit; }

    // Calls vmMain(command, args...); fails on any fault in the program
    // (bad address, bad jump, stack overflow, division by zero...), naming
    // the instruction.
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);

    // From a system call: ends the run once the call returns, with this
    // error from call() (the game code's own fatal errors, or memory a
    // call can't touch). A nested call() made since also fails.
    void abort(std::string message) {
        if (abort_.empty())
            abort_ = std::move(message);
    }

    // Whether a system call may write [address, address + size): inside
    // memory and clear of the code's jump tables (see protectedRanges()).
    // If not, the run is aborted, naming the system call, and false
    // returned. writeInt and writeString check this themselves.
    bool checkWrite(int64_t address, int64_t size);
    // The tables `switch` statements jump through (instruction numbers in
    // the data), found when loading: the program never changes them, so a
    // write there is a fault, caught where it happens rather than when a
    // switch later jumps somewhere absurd. [first, end) byte ranges.
    const std::vector<std::pair<uint32_t, uint32_t>>& protectedRanges() const { return protected_; }

    // The VM's memory, for system calls: addresses are offsets into it.
    std::vector<uint8_t>& memory() { return data_; }
    bool valid(int64_t address, int64_t size) const {
        return address >= 0 && size >= 0 && address + size <= int64_t(data_.size());
    }
    // A NUL-terminated string at `address` (cut at the end of memory or at
    // `max` bytes); empty if the address is outside memory.
    std::string string(int32_t address, size_t max = 4096) const;
    int32_t readInt(int32_t address) const;
    void writeInt(int32_t address, int32_t value);
    // Copies `text` into a buffer of `size` bytes at `address`, cut to fit
    // and NUL-terminated; false (and nothing written) if the buffer isn't
    // inside memory or `size` is 0.
    bool writeString(int32_t address, std::string_view text, int32_t size);
    // A float passed in an argument word.
    static float asFloat(int32_t word);
    static int32_t fromFloat(float value);

    size_t instructionCount() const { return code_.size(); }
    uint64_t executed() const { return executed_; } // by the last call(), including nested ones

    struct Instruction {
        uint8_t op;
        int32_t operand;
    };
    const std::vector<Instruction>& code() const { return code_; }
    // The initialised words end at dataLength(); the literal block
    // (strings) runs from litStart() to litEnd(); zeros follow.
    uint32_t dataLength() const { return dataLength_; }
    uint32_t litStart() const { return litStart_; }
    uint32_t litEnd() const { return litEnd_; }

private:
    Error run(int32_t& result);

    std::vector<Instruction> code_;
    std::vector<uint8_t> data_; // a power of two in size; the stack is at the top
    uint32_t dataLength_ = 0, litStart_ = 0, litEnd_ = 0;
    int32_t programStack_ = 0;  // the stack pointer between calls
    int32_t stackBottom_ = 0;   // the stack may not grow below this
    SystemCall syscall_;
    uint64_t limit_ = 0, executed_ = 0;
    int depth_ = 0; // nested call() through system calls
    std::string abort_;
    std::vector<std::pair<uint32_t, uint32_t>> protected_; // sorted, not overlapping
    uint32_t protectedEnd_ = 0;                             // nothing protected from here up
    int currentSyscall_ = -1;                               // the system call running, for messages

    void findJumpTables();
    // The protected range overlapping [address, address + size), or nullptr.
    const std::pair<uint32_t, uint32_t>* protectedOverlap(int64_t address, int64_t size) const;
};

// The opcodes (docs/formats/idtech3/qvm.md), in their numbering.
namespace qvmop {
enum : uint8_t {
    Undef, Ignore, Break, Enter, Leave, Call, Push, Pop, Const, Local, Jump,
    Eq, Ne, Lti, Lei, Gti, Gei, Ltu, Leu, Gtu, Geu, Eqf, Nef, Ltf, Lef, Gtf, Gef,
    Load1, Load2, Load4, Store1, Store2, Store4, Arg, BlockCopy,
    Sex8, Sex16, Negi, Add, Sub, Divi, Divu, Modi, Modu, Muli, Mulu,
    Band, Bor, Bxor, Bcom, Lsh, Rshi, Rshu, Negf, Addf, Subf, Divf, Mulf, Cvif, Cvfi,
    Count
};
// Bytes of operand after each opcode: 4, 1 (Arg) or 0.
int operandSize(uint8_t op);
const char* name(uint8_t op);
} // namespace qvmop

} // namespace reluva::idtech3
