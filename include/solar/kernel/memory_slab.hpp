#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

template <std::size_t BlockBytes> class MemorySlabRef;

/** One RAII allocation borrowed from a native Zephyr memory slab. */
template <std::size_t BlockBytes> class MemorySlabBlock
{
  public:
    MemorySlabBlock() = default;

    ~MemorySlabBlock()
    {
        reset();
    }

    MemorySlabBlock(const MemorySlabBlock&) = delete;
    MemorySlabBlock& operator=(const MemorySlabBlock&) = delete;

    MemorySlabBlock(MemorySlabBlock&& other) noexcept
        : slab_(std::exchange(other.slab_, nullptr)), memory_(std::exchange(other.memory_, nullptr))
    {}

    MemorySlabBlock& operator=(MemorySlabBlock&& other) noexcept
    {
        if (this != &other) {
            reset();
            slab_ = std::exchange(other.slab_, nullptr);
            memory_ = std::exchange(other.memory_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] std::span<std::byte, BlockBytes> bytes() noexcept
    {
        return std::span<std::byte, BlockBytes>{static_cast<std::byte*>(memory_), BlockBytes};
    }

    [[nodiscard]] std::span<const std::byte, BlockBytes> bytes() const noexcept
    {
        return std::span<const std::byte, BlockBytes>{static_cast<const std::byte*>(memory_),
                                                      BlockBytes};
    }

    [[nodiscard]] void* data() noexcept
    {
        return memory_;
    }
    [[nodiscard]] const void* data() const noexcept
    {
        return memory_;
    }
    explicit operator bool() const noexcept
    {
        return memory_ != nullptr;
    }

    void reset() noexcept
    {
        if (slab_ != nullptr && memory_ != nullptr) {
            k_mem_slab_free(slab_, memory_);
        }
        slab_ = nullptr;
        memory_ = nullptr;
    }

    [[nodiscard]] void* release() noexcept
    {
        slab_ = nullptr;
        return std::exchange(memory_, nullptr);
    }

  private:
    MemorySlabBlock(k_mem_slab& slab, void* memory) noexcept : slab_(&slab), memory_(memory) {}

    friend class MemorySlabRef<BlockBytes>;

    k_mem_slab* slab_{};
    void* memory_{};
};

/** Non-owning typed access to an initialized native Zephyr memory slab. */
template <std::size_t BlockBytes> class MemorySlabRef
{
    static_assert(BlockBytes > 0,
                  "SOLAR_DIAGNOSTIC_MEMORY_SLAB_ZERO_BLOCK: block size must be non-zero");

  public:
    using Block = MemorySlabBlock<BlockBytes>;

    explicit constexpr MemorySlabRef(k_mem_slab& slab) noexcept : slab_(&slab) {}

    [[nodiscard]] Result<Block> allocate(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        return allocate_native(timeout);
    }

    [[nodiscard]] Result<Block> allocate(const Deadline& deadline) const noexcept
    {
        return allocate(deadline.remaining());
    }

    [[nodiscard]] Result<Block> try_allocate() const noexcept
    {
        return allocate(Timeout::no_wait());
    }

    [[nodiscard]] Result<Block> try_allocate_isr() const noexcept
    {
        return allocate_native(Timeout::no_wait());
    }

    void release(void* block) const noexcept
    {
        if (block != nullptr) {
            k_mem_slab_free(slab_, block);
        }
    }

    [[nodiscard]] std::size_t used() const noexcept
    {
        return k_mem_slab_num_used_get(slab_);
    }
    [[nodiscard]] std::size_t available() const noexcept
    {
        return k_mem_slab_num_free_get(slab_);
    }

  private:
    [[nodiscard]] Result<Block> allocate_native(Timeout timeout) const noexcept
    {

        void* memory{};
        const int result = k_mem_slab_alloc(slab_, &memory, timeout.native_handle());
        if (result == 0) {
            return Block{*slab_, memory};
        }
        if (result == -ENOMEM) {
            return fail<solar::Error>({.status = solar::Status::NoMemory, .native = result});
        }
        if (result == -EAGAIN) {
            return fail<Error>({.status = timeout.is_no_wait() ? Status::NoMemory : Status::Timeout,
                                .native = result});
        }
        return fail<Error>(error_from_errno(result));
    }

    k_mem_slab* slab_;
};

template <std::size_t BlockBytes, std::size_t BlockCount, std::size_t Alignment = alignof(void*)>
class MemorySlab
{
    static_assert(BlockBytes > 0,
                  "SOLAR_DIAGNOSTIC_MEMORY_SLAB_ZERO_BLOCK: block size must be non-zero");
    static_assert(BlockCount > 0,
                  "SOLAR_DIAGNOSTIC_MEMORY_SLAB_ZERO_CAPACITY: block count must be non-zero");
    static_assert(BlockCount <= UINT32_MAX,
                  "SOLAR_DIAGNOSTIC_MEMORY_SLAB_CAPACITY_OVERFLOW: block count exceeds Zephyr's "
                  "limit");
    static_assert(std::has_single_bit(Alignment),
                  "SOLAR_DIAGNOSTIC_MEMORY_SLAB_INVALID_ALIGNMENT: alignment must be a power of "
                  "two");

    static constexpr std::size_t native_alignment = std::max(Alignment, alignof(void*));
    static constexpr std::size_t unaligned_stride = std::max(BlockBytes, sizeof(void*));
    static constexpr std::size_t block_stride =
        (unaligned_stride + native_alignment - 1U) & ~(native_alignment - 1U);

  public:
    using Block = MemorySlabBlock<BlockBytes>;

    static constexpr std::size_t block_size = BlockBytes;
    static constexpr std::size_t capacity = BlockCount;
    static constexpr std::size_t storage_stride = block_stride;

    MemorySlab() noexcept
    {
        __ASSERT_NO_MSG(k_mem_slab_init(&slab_, storage_.data(), block_stride, BlockCount) == 0);
    }

    ~MemorySlab()
    {
        __ASSERT_NO_MSG(used() == 0);
    }

    MemorySlab(const MemorySlab&) = delete;
    MemorySlab& operator=(const MemorySlab&) = delete;
    MemorySlab(MemorySlab&&) = delete;
    MemorySlab& operator=(MemorySlab&&) = delete;

    [[nodiscard]] Result<Block> allocate(Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().allocate(timeout);
    }

    [[nodiscard]] Result<Block> allocate(const Deadline& deadline) noexcept
    {
        return allocate(deadline.remaining());
    }

    [[nodiscard]] Result<Block> try_allocate() noexcept
    {
        return allocate(Timeout::no_wait());
    }

    [[nodiscard]] Result<Block> try_allocate_isr() noexcept
    {
        return ref().try_allocate_isr();
    }

    void release(void* block) noexcept
    {
        ref().release(block);
    }

    [[nodiscard]] std::size_t used() const noexcept
    {
        return k_mem_slab_num_used_get(const_cast<k_mem_slab*>(&slab_));
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        return k_mem_slab_num_free_get(const_cast<k_mem_slab*>(&slab_));
    }

    [[nodiscard]] MemorySlabRef<BlockBytes> ref() noexcept
    {
        return MemorySlabRef<BlockBytes>{slab_};
    }

  private:
    alignas(native_alignment) std::array<std::byte, block_stride * BlockCount> storage_{};
    k_mem_slab slab_{};
};

} // namespace solar::kernel
