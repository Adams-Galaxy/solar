#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/fatal.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

/** Non-owning access to an initialized Zephyr synchronized heap. */
class HeapRef
{
  public:
    explicit constexpr HeapRef(k_heap& heap) noexcept : heap_(&heap) {}

    [[nodiscard]] Result<void*> allocate(std::size_t bytes,
                                         Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(k_heap_alloc(heap_, bytes, timeout.native_handle()), timeout);
    }

    [[nodiscard]] Result<void*> allocate(std::size_t bytes, const Deadline& deadline) const noexcept
    {
        return allocate(bytes, deadline.remaining());
    }

    [[nodiscard]] Result<void*> try_allocate(std::size_t bytes) const noexcept
    {
        return allocate(bytes, Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> try_allocate_isr(std::size_t bytes) const noexcept
    {
        return allocation_result(k_heap_alloc(heap_, bytes, K_NO_WAIT), Timeout::no_wait());
    }

    [[nodiscard]] Result<void*>
    aligned_allocate(std::size_t alignment, std::size_t bytes,
                     Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (!valid_alignment(alignment)) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(
            k_heap_aligned_alloc(heap_, alignment, bytes, timeout.native_handle()), timeout);
    }

    [[nodiscard]] Result<void*> try_aligned_allocate(std::size_t alignment,
                                                     std::size_t bytes) const noexcept
    {
        return aligned_allocate(alignment, bytes, Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> aligned_allocate(std::size_t alignment, std::size_t bytes,
                                                 const Deadline& deadline) const noexcept
    {
        return aligned_allocate(alignment, bytes, deadline.remaining());
    }

    [[nodiscard]] Result<void*> try_aligned_allocate_isr(std::size_t alignment,
                                                         std::size_t bytes) const noexcept
    {
        if (!valid_alignment(alignment)) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(k_heap_aligned_alloc(heap_, alignment, bytes, K_NO_WAIT),
                                 Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> allocate_zeroed(std::size_t count, std::size_t size,
                                                Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(k_heap_calloc(heap_, count, size, timeout.native_handle()),
                                 timeout);
    }

    [[nodiscard]] Result<void*> allocate_zeroed(std::size_t count, std::size_t size,
                                                const Deadline& deadline) const noexcept
    {
        return allocate_zeroed(count, size, deadline.remaining());
    }

    [[nodiscard]] Result<void*> try_allocate_zeroed(std::size_t count,
                                                    std::size_t size) const noexcept
    {
        return allocate_zeroed(count, size, Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> try_allocate_zeroed_isr(std::size_t count,
                                                        std::size_t size) const noexcept
    {
        if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(k_heap_calloc(heap_, count, size, K_NO_WAIT), Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> reallocate(void* memory, std::size_t bytes,
                                           Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return allocation_result(k_heap_realloc(heap_, memory, bytes, timeout.native_handle()),
                                 timeout);
    }

    [[nodiscard]] Result<void*> reallocate(void* memory, std::size_t bytes,
                                           const Deadline& deadline) const noexcept
    {
        return reallocate(memory, bytes, deadline.remaining());
    }

    [[nodiscard]] Result<void*> try_reallocate(void* memory, std::size_t bytes) const noexcept
    {
        return reallocate(memory, bytes, Timeout::no_wait());
    }

    [[nodiscard]] Result<void*> try_reallocate_isr(void* memory, std::size_t bytes) const noexcept
    {
        return allocation_result(k_heap_realloc(heap_, memory, bytes, K_NO_WAIT),
                                 Timeout::no_wait());
    }

    [[nodiscard]] Result<void> free(void* memory) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_heap_free(heap_, memory);
        return {};
    }

    [[nodiscard]] constexpr k_heap* native_handle() const noexcept
    {
        return heap_;
    }

  private:
    [[nodiscard]] static constexpr bool valid_alignment(std::size_t alignment) noexcept
    {
        return alignment != 0 && std::has_single_bit(alignment);
    }

    [[nodiscard]] static Result<void*> allocation_result(void* memory, Timeout timeout) noexcept
    {
        if (memory != nullptr) {
            return memory;
        }
        return fail<Error>({.status = timeout.is_no_wait() || timeout.is_forever()
                                          ? Status::NoMemory
                                          : Status::Timeout});
    }

    k_heap* heap_;
};

template <std::size_t Bytes, std::size_t Alignment = 8> class Heap
{
    static_assert(Bytes >= Z_HEAP_MIN_SIZE,
                  "SOLAR_DIAGNOSTIC_HEAP_TOO_SMALL: heap storage must satisfy Z_HEAP_MIN_SIZE");
    static_assert(Alignment >= 8 && std::has_single_bit(Alignment),
                  "SOLAR_DIAGNOSTIC_HEAP_ALIGNMENT: heap alignment must be a power of two at "
                  "least eight bytes");

  public:
    static constexpr std::size_t capacity = Bytes;
    static constexpr std::size_t alignment = Alignment;

    Heap() noexcept
    {
        k_heap_init(&heap_, storage_.data(), storage_.size());
    }

    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;

    [[nodiscard]] HeapRef ref() noexcept
    {
        return HeapRef{heap_};
    }
    [[nodiscard]] Result<void*> allocate(std::size_t bytes,
                                         Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().allocate(bytes, timeout);
    }
    [[nodiscard]] Result<void*> try_allocate(std::size_t bytes) noexcept
    {
        return ref().try_allocate(bytes);
    }
    [[nodiscard]] Result<void*> try_allocate_isr(std::size_t bytes) noexcept
    {
        return ref().try_allocate_isr(bytes);
    }
    [[nodiscard]] Result<void*> aligned_allocate(std::size_t requested_alignment, std::size_t bytes,
                                                 Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().aligned_allocate(requested_alignment, bytes, timeout);
    }
    [[nodiscard]] Result<void*> try_aligned_allocate(std::size_t requested_alignment,
                                                     std::size_t bytes) noexcept
    {
        return ref().try_aligned_allocate(requested_alignment, bytes);
    }
    [[nodiscard]] Result<void*> try_aligned_allocate_isr(std::size_t requested_alignment,
                                                         std::size_t bytes) noexcept
    {
        return ref().try_aligned_allocate_isr(requested_alignment, bytes);
    }
    [[nodiscard]] Result<void*> allocate_zeroed(std::size_t count, std::size_t size,
                                                Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().allocate_zeroed(count, size, timeout);
    }
    [[nodiscard]] Result<void*> try_allocate_zeroed(std::size_t count, std::size_t size) noexcept
    {
        return ref().try_allocate_zeroed(count, size);
    }
    [[nodiscard]] Result<void*> try_allocate_zeroed_isr(std::size_t count,
                                                        std::size_t size) noexcept
    {
        return ref().try_allocate_zeroed_isr(count, size);
    }
    [[nodiscard]] Result<void*> reallocate(void* memory, std::size_t bytes,
                                           Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().reallocate(memory, bytes, timeout);
    }
    [[nodiscard]] Result<void*> try_reallocate(void* memory, std::size_t bytes) noexcept
    {
        return ref().try_reallocate(memory, bytes);
    }
    [[nodiscard]] Result<void*> try_reallocate_isr(void* memory, std::size_t bytes) noexcept
    {
        return ref().try_reallocate_isr(memory, bytes);
    }
    [[nodiscard]] Result<void> free(void* memory) noexcept
    {
        return ref().free(memory);
    }
    [[nodiscard]] bool owns(const void* memory) const noexcept
    {
        const auto address = reinterpret_cast<std::uintptr_t>(memory);
        const auto begin = reinterpret_cast<std::uintptr_t>(storage_.data());
        return address >= begin && address < begin + storage_.size();
    }

  private:
    k_heap heap_{};
    alignas(Alignment) std::array<std::byte, Bytes> storage_{};
};

enum class HeapResourceFailure : std::uint8_t
{
    Panic,
#if defined(__cpp_exceptions)
    Throw,
#endif
};

/** PMR bridge with an explicit exhaustion policy selected at construction. */
class HeapResource final : public std::pmr::memory_resource
{
  public:
    explicit HeapResource(HeapRef heap, HeapResourceFailure failure) noexcept
        : heap_(heap), failure_(failure)
    {}

    [[nodiscard]] HeapResourceFailure failure_policy() const noexcept
    {
        return failure_;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        const auto memory = heap_.try_aligned_allocate(alignment, bytes);
        if (memory) {
            return *memory;
        }
        if (failure_ == HeapResourceFailure::Panic) {
            panic(Status::NoMemory);
        }
#if defined(__cpp_exceptions)
        throw std::bad_alloc{};
#else
        panic(Status::NoMemory);
#endif
    }

    void do_deallocate(void* memory, std::size_t, std::size_t) override
    {
        if (!heap_.free(memory)) {
            panic(Status::Invalid);
        }
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    HeapRef heap_;
    HeapResourceFailure failure_;
};

} // namespace solar::kernel
