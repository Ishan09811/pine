// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <cstdint>
#include <unistd.h>
#include <stdexcept>
#include <variant>

namespace skyline {
    using u128 = __uint128_t; //!< Unsigned 128-bit integer
    using u64 = __uint64_t; //!< Unsigned 64-bit integer
    using u32 = __uint32_t; //!< Unsigned 32-bit integer
    using u16 = __uint16_t; //!< Unsigned 16-bit integer
    using u8 = __uint8_t; //!< Unsigned 8-bit integer
    using i128 = __int128_t; //!< Signed 128-bit integer
    using i64 = __int64_t; //!< Signed 64-bit integer
    using i32 = __int32_t; //!< Signed 32-bit integer
    using i16 = __int16_t; //!< Signed 16-bit integer
    using i8 = __int8_t; //!< Signed 8-bit integer

    using KHandle = u32; //!< The type of a kernel handle

    #define NON_COPYABLE(cls)                                                                     \
        cls(const cls&) = delete;                                                                      \
        cls& operator=(const cls&) = delete

    #define NON_MOVEABLE(cls)                                                                     \
       cls(cls&&) = delete;                                                                           \
       cls& operator=(cls&&) = delete

    namespace constant {
        // Time
        constexpr i64 NsInMicrosecond{1000}; //!< The amount of nanoseconds in a microsecond
        constexpr i64 NsInSecond{1000000000}; //!< The amount of nanoseconds in a second
        constexpr i64 NsInMillisecond{1000000}; //!< The amount of nanoseconds in a millisecond
        constexpr i64 NsInDay{86400000000000UL}; //!< The amount of nanoseconds in a day

        constexpr size_t AddressSpaceSize{1ULL << 39}; //!< The size of the host CPU AS in bytes

        constexpr u16 TlsSlotSize{0x200};

        inline size_t getDynamicPageSize() {
            size_t pageSize = getpagesize();
            if (pageSize == 0) {
                throw std::runtime_error("Failed to retrieve page size");
            }
            return pageSize;
        }

        inline u8 getTlsSlots() {
            size_t slots = getDynamicPageSize() / TlsSlotSize;
            if (slots > 255) {
                throw std::runtime_error("TlsSlots exceeds u8 capacity!");
            }
            return static_cast<u8>(slots);
        }

        const size_t PageSize{getDynamicPageSize()}; //!< The size of a host page
        constexpr size_t PageSizeBits{12}; //!< log2(PageSize)
    }

    /**
     * @brief A deduction guide for overloads required for std::visit with std::variant
     */
    template<class... Ts>
    struct VariantVisitor : Ts ... { using Ts::operator()...; };
    template<class... Ts> VariantVisitor(Ts...) -> VariantVisitor<Ts...>;
}

