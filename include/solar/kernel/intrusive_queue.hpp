#pragma once

#include <atomic>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

/** Storage whose first machine word is reserved exclusively for Zephyr. */
template <typename T> class alignas(void*) IntrusiveNode
{
  public:
    template <typename... Arguments>
    explicit IntrusiveNode(Arguments&&... arguments) noexcept(
        std::is_nothrow_constructible_v<T, Arguments...>)
        : value_(std::forward<Arguments>(arguments)...)
    {}

    IntrusiveNode(const IntrusiveNode&) = delete;
    IntrusiveNode& operator=(const IntrusiveNode&) = delete;
    IntrusiveNode(IntrusiveNode&&) = delete;
    IntrusiveNode& operator=(IntrusiveNode&&) = delete;

    ~IntrusiveNode()
    {
        __ASSERT_NO_MSG(!linked());
    }

    [[nodiscard]] T& value() noexcept
    {
        return value_;
    }

    [[nodiscard]] const T& value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] bool linked() const noexcept
    {
        return owner_.load(std::memory_order_acquire) != nullptr;
    }

  private:
    template <typename> friend class QueueRef;

    void* kernel_link_{};
    T value_;
    std::atomic<const void*> owner_{nullptr};
};

template <typename T> class QueueRef
{
  public:
    using Node = IntrusiveNode<T>;
    static_assert(std::is_standard_layout_v<Node>,
                  "SOLAR_DIAGNOSTIC_INTRUSIVE_NODE_LAYOUT: node values must preserve a standard "
                  "layout");
    static_assert(offsetof(Node, kernel_link_) == 0,
                  "SOLAR_DIAGNOSTIC_INTRUSIVE_NODE_LINK: Zephyr's reserved link must be first");

    explicit constexpr QueueRef(k_queue& queue) noexcept : queue_(&queue) {}

    [[nodiscard]] Result<void> append(Node& node) const noexcept
    {
        if (!claim(node)) {
            return fail<Error>({.status = Status::Already});
        }
        k_queue_append(queue_, &node);
        return {};
    }

    [[nodiscard]] Result<void> prepend(Node& node) const noexcept
    {
        if (!claim(node)) {
            return fail<Error>({.status = Status::Already});
        }
        k_queue_prepend(queue_, &node);
        return {};
    }

    [[nodiscard]] Result<void> insert_after(Node& previous, Node& node) const noexcept
    {
        if (previous.owner_.load(std::memory_order_acquire) != queue_) {
            return fail<Error>({.status = Status::NotFound});
        }
        if (!claim(node)) {
            return fail<Error>({.status = Status::Already});
        }
        k_queue_insert(queue_, &previous, &node);
        return {};
    }

    [[nodiscard]] Result<bool> unique_append(Node& node) const noexcept
    {
        const auto owner = node.owner_.load(std::memory_order_acquire);
        if (owner == queue_) {
            return false;
        }
        if (owner != nullptr || !claim(node)) {
            return fail<Error>({.status = Status::Already});
        }
        if (!k_queue_unique_append(queue_, &node)) {
            node.owner_.store(nullptr, std::memory_order_release);
            return false;
        }
        return true;
    }

    [[nodiscard]] Result<void> append_list(std::span<Node* const> nodes) const noexcept
    {
        if (nodes.empty()) {
            return fail<Error>({.status = Status::Invalid});
        }

        std::size_t claimed{};
        for (; claimed < nodes.size(); ++claimed) {
            if (nodes[claimed] == nullptr || !claim(*nodes[claimed])) {
                for (std::size_t index = 0; index < claimed; ++index) {
                    release(*nodes[index]);
                }
                return fail<Error>(
                    {.status = nodes[claimed] == nullptr ? Status::Invalid : Status::Already});
            }
        }

        for (std::size_t index = 0; index + 1 < nodes.size(); ++index) {
            nodes[index]->kernel_link_ = nodes[index + 1];
        }
        nodes.back()->kernel_link_ = nullptr;
        const int result = k_queue_append_list(queue_, nodes.front(), nodes.back());
        if (result == 0) {
            return {};
        }
        for (auto* node : nodes) {
            release(*node);
        }
        return fail<Error>(error_from_errno(result));
    }

    [[nodiscard]] Result<Node*> get(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return get_native(timeout);
    }

    [[nodiscard]] Result<Node*> get(const Deadline& deadline) const noexcept
    {
        return get(deadline.remaining());
    }

    [[nodiscard]] Result<Node*> try_get() const noexcept
    {
        return get(Timeout::no_wait());
    }

    [[nodiscard]] Result<Node*> try_get_isr() const noexcept
    {
        return get_native(Timeout::no_wait());
    }

    [[nodiscard]] bool remove(Node& node) const noexcept
    {
        if (node.owner_.load(std::memory_order_acquire) != queue_) {
            return false;
        }
        if (!k_queue_remove(queue_, &node)) {
            return false;
        }
        release(node);
        return true;
    }

    void cancel_wait() const noexcept
    {
        k_queue_cancel_wait(queue_);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return k_queue_is_empty(queue_) != 0;
    }

    [[nodiscard]] Node* peek_head() const noexcept
    {
        return static_cast<Node*>(k_queue_peek_head(queue_));
    }

    [[nodiscard]] Node* peek_tail() const noexcept
    {
        return static_cast<Node*>(k_queue_peek_tail(queue_));
    }

    [[nodiscard]] constexpr k_queue* native_queue() const noexcept
    {
        return queue_;
    }

  private:
    [[nodiscard]] bool claim(Node& node) const noexcept
    {
        const void* expected = nullptr;
        return node.owner_.compare_exchange_strong(expected, queue_, std::memory_order_acq_rel);
    }

    static void release(Node& node) noexcept
    {
        node.kernel_link_ = nullptr;
        node.owner_.store(nullptr, std::memory_order_release);
    }

    [[nodiscard]] Result<Node*> get_native(Timeout timeout) const noexcept
    {
        auto* node = static_cast<Node*>(k_queue_get(queue_, timeout.native_handle()));
        if (node != nullptr) {
            release(*node);
            return node;
        }
        return fail<Error>({.status = timeout.is_no_wait() ? Status::WouldBlock : Status::Timeout});
    }

    k_queue* queue_;
};

/** General zero-copy intrusive queue. The queue borrows every linked node. */
template <typename T> class Queue
{
  public:
    using Node = IntrusiveNode<T>;

    Queue() noexcept
    {
        k_queue_init(&queue_);
    }

    ~Queue()
    {
        __ASSERT_NO_MSG(ref().empty());
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    [[nodiscard]] QueueRef<T> ref() noexcept
    {
        return QueueRef<T>{queue_};
    }

    [[nodiscard]] Result<void> append(Node& node) noexcept
    {
        return ref().append(node);
    }
    [[nodiscard]] Result<void> prepend(Node& node) noexcept
    {
        return ref().prepend(node);
    }
    [[nodiscard]] Result<void> insert_after(Node& previous, Node& node) noexcept
    {
        return ref().insert_after(previous, node);
    }
    [[nodiscard]] Result<bool> unique_append(Node& node) noexcept
    {
        return ref().unique_append(node);
    }
    [[nodiscard]] Result<void> append_list(std::span<Node* const> nodes) noexcept
    {
        return ref().append_list(nodes);
    }
    [[nodiscard]] Result<Node*> get(Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().get(timeout);
    }
    [[nodiscard]] Result<Node*> get(const Deadline& deadline) noexcept
    {
        return ref().get(deadline);
    }
    [[nodiscard]] Result<Node*> try_get() noexcept
    {
        return ref().try_get();
    }
    [[nodiscard]] Result<Node*> try_get_isr() noexcept
    {
        return ref().try_get_isr();
    }
    [[nodiscard]] bool remove(Node& node) noexcept
    {
        return ref().remove(node);
    }
    void cancel_wait() noexcept
    {
        ref().cancel_wait();
    }
    [[nodiscard]] bool empty() noexcept
    {
        return ref().empty();
    }
    [[nodiscard]] Node* peek_head() noexcept
    {
        return ref().peek_head();
    }
    [[nodiscard]] Node* peek_tail() noexcept
    {
        return ref().peek_tail();
    }

  private:
    k_queue queue_{};
};

/** FIFO spelling with the general queue's insertion surface intentionally hidden. */
template <typename T> class Fifo
{
  public:
    using Node = IntrusiveNode<T>;

    [[nodiscard]] Result<void> put(Node& node) noexcept
    {
        return queue_.append(node);
    }
    [[nodiscard]] Result<void> put_list(std::span<Node* const> nodes) noexcept
    {
        return queue_.append_list(nodes);
    }
    [[nodiscard]] Result<Node*> get(Timeout timeout = Timeout::forever()) noexcept
    {
        return queue_.get(timeout);
    }
    [[nodiscard]] Result<Node*> get(const Deadline& deadline) noexcept
    {
        return queue_.get(deadline);
    }
    [[nodiscard]] Result<Node*> try_get() noexcept
    {
        return queue_.try_get();
    }
    [[nodiscard]] Result<Node*> try_get_isr() noexcept
    {
        return queue_.try_get_isr();
    }
    void cancel_wait() noexcept
    {
        queue_.cancel_wait();
    }
    [[nodiscard]] bool empty() noexcept
    {
        return queue_.empty();
    }
    [[nodiscard]] Node* peek_head() noexcept
    {
        return queue_.peek_head();
    }
    [[nodiscard]] Node* peek_tail() noexcept
    {
        return queue_.peek_tail();
    }
    [[nodiscard]] QueueRef<T> ref() noexcept
    {
        return queue_.ref();
    }

  private:
    Queue<T> queue_;
};

/** LIFO spelling backed by Zephyr's queue prepend/get behavior. */
template <typename T> class Lifo
{
  public:
    using Node = IntrusiveNode<T>;

    [[nodiscard]] Result<void> put(Node& node) noexcept
    {
        return queue_.prepend(node);
    }
    [[nodiscard]] Result<Node*> get(Timeout timeout = Timeout::forever()) noexcept
    {
        return queue_.get(timeout);
    }
    [[nodiscard]] Result<Node*> get(const Deadline& deadline) noexcept
    {
        return queue_.get(deadline);
    }
    [[nodiscard]] Result<Node*> try_get() noexcept
    {
        return queue_.try_get();
    }
    [[nodiscard]] Result<Node*> try_get_isr() noexcept
    {
        return queue_.try_get_isr();
    }
    void cancel_wait() noexcept
    {
        queue_.cancel_wait();
    }
    [[nodiscard]] bool empty() noexcept
    {
        return queue_.empty();
    }
    [[nodiscard]] Node* peek_head() noexcept
    {
        return queue_.peek_head();
    }
    [[nodiscard]] QueueRef<T> ref() noexcept
    {
        return queue_.ref();
    }

  private:
    Queue<T> queue_;
};

} // namespace solar::kernel
