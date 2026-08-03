#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"

namespace solar::log
{

struct RetentionReport
{
    std::size_t retained{};
    std::uint64_t lost{};
};

/** Allocation-free retained record storage; rendering belongs to sinks. */
template <typename Record, std::size_t Capacity> class RecordStore
{
    static_assert(Capacity > 0);

  public:
    [[nodiscard]] Result<void> initialize() noexcept
    {
        SpinGuard lock{mutex_};
        head_ = size_ = lost_ = 0;
        return {};
    }
    [[nodiscard]] Result<void> append(Record record) noexcept
    {
        SpinGuard lock{mutex_};
        if (size_ == Capacity) {
            ++lost_;
            records_[head_] = std::move(record);
            head_ = (head_ + 1) % Capacity;
        } else {
            records_[(head_ + size_) % Capacity] = std::move(record);
            ++size_;
        }
        return {};
    }
    template <typename Sink> [[nodiscard]] Result<std::size_t> replay(Sink& sink) const
    {
        SpinGuard lock{mutex_};
        for (std::size_t index{}; index < size_; ++index) {
            if (auto result = sink.write(records_[(head_ + index) % Capacity]); !result)
                return fail<solar::Error>(result.error());
        }
        return size_;
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        SpinGuard lock{mutex_};
        return size_;
    }
    [[nodiscard]] std::uint64_t lost() const noexcept
    {
        SpinGuard lock{mutex_};
        return lost_;
    }
    [[nodiscard]] RetentionReport report() const noexcept
    {
        SpinGuard lock{mutex_};
        return {.retained = size_, .lost = lost_};
    }

  private:
    std::array<Record, Capacity> records_{};
    std::size_t head_{};
    std::size_t size_{};
    std::uint64_t lost_{};
    mutable SpinMutex mutex_{};
};

template <typename Application, typename Record, std::size_t Capacity> struct StaticRecordStore
{
    inline static RecordStore<Record, Capacity> storage{};
    [[nodiscard]] static Result<void> initialize() noexcept
    {
        return storage.initialize();
    }
    [[nodiscard]] static Result<void> append(Record value) noexcept
    {
        return storage.append(std::move(value));
    }
    template <typename Sink> [[nodiscard]] static auto replay(Sink& sink)
    {
        return storage.replay(sink);
    }
    [[nodiscard]] static RetentionReport report() noexcept
    {
        return storage.report();
    }
};

} // namespace solar::log
