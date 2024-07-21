// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#pragma once

#include <common.h>
#include <dynarmic/interface/A32/a32.h>
#include <kernel/svc_context.h>
#include "thread_context32.h"
#include "halt_reason.h"

namespace skyline::jit {
    /**
     * @brief A wrapper around a Dynarmic 32-bit JIT object with additional state and functionality, representing a single core of the emulated CPU
     */
    class JitCore32 : public Dynarmic::A32::UserCallbacks {
      private:
        const DeviceState &state;
        u32 coreId;
        u32 lastSwi{};

        Dynarmic::A32::Jit jit;

        /**
         * @brief Creates a new Dynarmic 32-bit JIT instance
         * @note This is only called once in the initialization list because the Dynarmic JIT class is not default constructible
         * @return A new Dynarmic 32-bit JIT instance
         */
        Dynarmic::A32::Jit MakeDynarmicJit();

      public:
        JitCore32(const DeviceState &state, u32 coreId);

        /**
         * @brief Runs the JIT
         * @note This function does not return
         */
        void Run();

        /**
         * @brief Stops execution by setting the given halt flag
         */
        void HaltExecution(HaltReason hr);

        /**
         * @brief Clears a previously set halt flag
         */
        void ClearHalt(HaltReason hr);

        /**
         * @brief Saves the current state of the JIT to the given context.
         */
        void SaveContext(ThreadContext32 &context);

        /**
         * @brief Restores the state of the JIT from the given context.
         */
        void RestoreContext(const ThreadContext32 &context);

        /**
         * @brief Constructs an SvcContext from the current state of the JIT
         */
        kernel::svc::SvcContext MakeSvcContext();

        /**
         * @brief Applies the given SvcContext to the current state of the JIT
         */
        void ApplySvcContext(const kernel::svc::SvcContext &context);

        /**
         * @brief Sets the Thread Pointer register to the specified value
         */
        void SetThreadPointer(u32 threadPtr);

        /**
         * @brief Sets the Thread Local Storage Pointer register to the specified value
         */
        void SetTlsPointer(u32 tlsPtr);

        /**
         * @brief Gets the Program Counter
         */
        u32 GetPC();

        /**
         * @brief Sets the Program Counter to the specified value
         */
        void SetPC(u32 pc);

        /**
         * @brief Gets the Stack Pointer
         */
        u32 GetSP();

        /**
         * @brief Sets the Stack Pointer to the specified value
         */
        void SetSP(u32 sp);

        /**
         * @brief Gets the specified register value
         */
        u32 GetRegister(u32 reg);

        /**
         * @brief Sets the specified register to the given value
         */
        void SetRegister(u32 reg, u32 value);

        /**
         * @brief Handles an SVC call from the JIT
         * @param swi The SVC number
         */
        void SvcHandler(u32 swi);

        // Dynarmic callbacks
      public:
        // @fmt:off
        u8 MemoryRead8(u32 vaddr) override;
        u16 MemoryRead16(u32 vaddr) override;
        u32 MemoryRead32(u32 vaddr) override;
        u64 MemoryRead64(u32 vaddr) override;

        void MemoryWrite8(u32 vaddr, u8 value) override;
        void MemoryWrite16(u32 vaddr, u16 value) override;
        void MemoryWrite32(u32 vaddr, u32 value) override;
        void MemoryWrite64(u32 vaddr, u64 value) override;
        // @fmt:on

        void InterpreterFallback(u32 pc, size_t numInstructions) override;

        void CallSVC(u32 swi) override;

        void ExceptionRaised(u32 pc, Dynarmic::A32::Exception exception) override;

        // Cycle counting callbacks are unused
        void AddTicks(u64 ticks) override {}

        u64 GetTicksRemaining() override { return 0; }

      private:
        /**
         * @brief Reads the memory at the given virtual address
         * @tparam T The type of the value to read
         * @param vaddr The virtual address to read from
         * @return The value read from memory
         */
        template<typename T>
        T MemoryRead(u32 vaddr);

        /**
         * @brief Writes the given value to the memory at the given virtual address
         * @tparam T The type of the value to write
         * @param vaddr The virtual address to write to
         * @param value The value to write to memory
         */
        template<typename T>
        void MemoryWrite(u32 vaddr, T value);
    };
}
