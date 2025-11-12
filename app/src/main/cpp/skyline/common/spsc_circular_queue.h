// SPDX-License-Identifier: GPL-3.0
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)

#pragma once

#include <atomic>
#include <vector>
#include <thread>
#include <cassert>
#include <utility>
#include <new>

namespace skyline {
    template <typename Type>
    class SpscCircularQueue {
      private:
        std::vector<std::byte> buffer;
        const size_t capacity; 
        std::atomic<size_t> head{0};
        std::atomic<size_t> tail{0};

        Type* ptr(size_t i) noexcept {
            return reinterpret_cast<Type*>(buffer.data() + i * sizeof(Type));
        }

      public:
        explicit SpscCircularQueue(size_t size)
          : buffer((size + 1) * sizeof(Type)), capacity(size + 1) {}

        SpscCircularQueue(const CircularQueue&) = delete;
        SpscCircularQueue& operator=(const CircularQueue&) = delete;
        SpscCircularQueue(CircularQueue&&) = delete;
        SpscCircularQueue& operator=(CircularQueue&&) = delete;

       ~SpscCircularQueue() {
            while (!Empty()) {
                auto* p = ptr(head.load());
                std::destroy_at(p);
                head.store((head.load() + 1) % capacity);
            }
        }

        bool Empty() const noexcept {
            return head.load(std::memory_order_acquire) ==
                   tail.load(std::memory_order_acquire);
        }

        bool Full() const noexcept {
            auto next = (tail.load(std::memory_order_relaxed) + 1) % capacity;
            return next == head.load(std::memory_order_acquire);
        }

        void Push(const Type& item) {
            auto next = (tail.load(std::memory_order_relaxed) + 1) % capacity;
            while (next == head.load(std::memory_order_acquire))
                std::this_thread::yield(); 

            auto* dest = ptr(tail.load(std::memory_order_relaxed));
            std::construct_at(dest, item);

            tail.store(next, std::memory_order_release);
        }

        void Push(Type&& item) {
            auto next = (tail.load(std::memory_order_relaxed) + 1) % capacity;
            while (next == head.load(std::memory_order_acquire))
                std::this_thread::yield();
 
            auto* dest = ptr(tail.load(std::memory_order_relaxed));
            std::construct_at(dest, std::move(item));

            tail.store(next, std::memory_order_release);
        }

        template <typename... Args>
        void Emplace(Args&&... args) {
            auto next = (tail.load(std::memory_order_relaxed) + 1) % capacity;
            while (next == head.load(std::memory_order_acquire))
                std::this_thread::yield();

            auto* dest = ptr(tail.load(std::memory_order_relaxed));
            std::construct_at(dest, std::forward<Args>(args)...);

            tail.store(next, std::memory_order_release);
        }

        Type Pop() {
            while (Empty())
                std::this_thread::yield();

            auto* src = ptr(head.load(std::memory_order_relaxed));
            Type item = std::move(*src);
            std::destroy_at(src);

            head.store((head.load(std::memory_order_relaxed) + 1) % capacity,
                       std::memory_order_release);
            return item;
        }

        template <typename F1, typename F2>
        [[noreturn]] void Process(F1 function, F2 preWait) {
            for (;;) {
                while (Empty()) {
                    preWait();
                    std::this_thread::yield();
                }

                while (!Empty()) {
                    auto* src = ptr(head.load(std::memory_order_relaxed));
                    function(*src);
                    std::destroy_at(src);
                    head.store((head.load(std::memory_order_relaxed) + 1) % capacity,
                               std::memory_order_release);
                }
            }
        }

        void Append(span<Type> bufferSpan) {
            for (const auto& item : bufferSpan)
                Push(item);
        }

        template <typename TransformedType, typename Transformation>
        void AppendTranform(TransformedType& container, Transformation transform) {
            for (const auto& item : container)
                Push(transform(item));
        }
    };
}  // namespace skyline
