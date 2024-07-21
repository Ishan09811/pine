// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2023 Strato Team and Contributors (https://github.com/strato-emu/)

#pragma once

#include <array>
#include "common.h"
#include "jit_core_32.h"

namespace skyline::jit {
    /**
     * @brief The JIT for the 32-bit ARM CPU
     */
    class Jit32 {
      public:
        Jit32(DeviceState &state);

        /**
         * @brief Gets the JIT core for the specified core ID
         */
        JitCore32 &GetCore(u32 coreId);

      private:
        DeviceState &state;
        bool initialised{false};

        std::array<jit::JitCore32, 4> cores;
    };
}
