# Kernel API

Include `<solar/kernel.hpp>`. The aggregate exposes time, synchronization,
communication, memory, thread, timer, poll, work, diagnostics, and native
interoperation primitives from `solar::kernel`.

```cpp
template <std::size_t StackBytes> class Thread;
template <typename Message, std::size_t Capacity> class MessageQueue;
template <std::size_t BlockBytes, std::size_t Capacity,
          std::size_t Alignment = alignof(void*)> class MemorySlab;
template <std::size_t Capacity> class Pipe;
template <std::size_t StackBytes> class WorkQueue;
template <typename Message> class MessageQueueRef;
template <std::size_t BlockBytes> class MemorySlabRef;
template <typename Value> class Queue;
template <typename Value> class Fifo;
template <typename Value> class Lifo;
template <typename Value, std::size_t Capacity> class Stack;
template <std::size_t Bytes, std::size_t Alignment = 8> class Heap;

class Mutex;
class RecursiveMutex;
class RecursiveMutexRef;
class ConditionVariableRef;
class Semaphore;
class SemaphoreRef;
class EventFlags;
class EventFlagsRef;
class Timer;
class TimerRef;
class ThreadRef;
class PollSignalRef;
class PipeRef;
class HeapRef;
class HeapResource;
class Mailbox;
class MailboxRef;
class DeferredMailboxReceive;
class SpinLockRef;
class WorkQueueTarget;
class Work;
class DelayableWork;
class TriggeredWork;
class StopSource;
class StopToken;
class Deadline;
class Timeout;
```

Owning primitives are non-copyable and non-movable because Zephyr retains
addresses to their native storage. See {doc}`../../subsystems/kernel`.
