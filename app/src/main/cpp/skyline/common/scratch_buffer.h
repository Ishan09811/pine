// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project 
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-License-Identifier: GPL-3.0
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)

#pragma once

#include <iterator>
#include <memory>
#include <new>

namespace skyline {

template <typename T>
static std::unique_ptr<T[], void(*)(T*)> allocate_uninitialized(size_t count) {
    void* raw = ::operator new[](count * sizeof(T));
    return std::unique_ptr<T[], void(*)(T*)>(
        static_cast<T*>(raw),
        [](T* p){ ::operator delete[](p); }
    );
}

template <typename T>
class ScratchBuffer {
public:
    using element_type = T;
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;

    ScratchBuffer() = default;

    explicit ScratchBuffer(size_type initial_capacity)
        : last_requested_size{initial_capacity},
          buffer_capacity{initial_capacity},
          buffer{allocate_uninitialized<T>(initial_capacity)} {}

    ScratchBuffer(ScratchBuffer&& other) noexcept {
        swap(other);
        other.last_requested_size = 0;
        other.buffer_capacity = 0;
        other.buffer.reset();
    }

    ScratchBuffer& operator=(ScratchBuffer&& other) noexcept {
        swap(other);
        other.last_requested_size = 0;
        other.buffer_capacity = 0;
        other.buffer.reset();
        return *this;
    }

    void resize(size_type size) {
        if (size > buffer_capacity) {
            auto new_buf = allocate_uninitialized<T>(size);
            std::move(buffer.get(), buffer.get() + buffer_capacity, new_buf.get());
            buffer = std::move(new_buf);
            buffer_capacity = size;
        }
        last_requested_size = size;
    }

    void resize_destructive(size_type size) {
        if (size > buffer_capacity) {
            buffer_capacity = size;
            buffer = allocate_uninitialized<T>(size);
        }
        last_requested_size = size;
    }

    pointer data() noexcept { return buffer.get(); }
    const_pointer data() const noexcept { return buffer.get(); }

    iterator begin() noexcept { return data(); }
    const_iterator begin() const noexcept { return data(); }
    iterator end() noexcept { return data() + last_requested_size; }
    const_iterator end() const noexcept { return data() + last_requested_size; }

    reference operator[](size_type i) { return buffer[i]; }
    const_reference operator[](size_type i) const { return buffer[i]; }

    size_type size() const noexcept { return last_requested_size; }
    size_type capacity() const noexcept { return buffer_capacity; }

    void swap(ScratchBuffer& other) noexcept {
        std::swap(last_requested_size, other.last_requested_size);
        std::swap(buffer_capacity, other.buffer_capacity);
        std::swap(buffer, other.buffer);
    }

private:
    size_type last_requested_size{};
    size_type buffer_capacity{};
    std::unique_ptr<T[], void(*)(T*)> buffer{nullptr, +[](T* p){ ::operator delete[](p); }};
};

} // namespace skyline
