# Kernel API

Include `<solar/kernel.hpp>`. The aggregate exposes time, synchronization,
communication, memory, thread, timer, poll, work, diagnostics, and native
interoperation primitives from `solar::kernel`.

```cpp
template <std::size_t StackBytes> class Thread;
template <typename Message, std::size_t Capacity> class MessageQueue;
template <typename Block, std::size_t Capacity> class MemorySlab;
template <std::size_t Capacity> class Pipe;
template <std::size_t StackBytes> class WorkQueue;

class Mutex;
class Semaphore;
class EventFlags;
class Timer;
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
