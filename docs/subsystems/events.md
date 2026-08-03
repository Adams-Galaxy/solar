# Events

`events::EventBus<Schema<...>, MaximumObservers>` is an allocation-free typed
fan-out module. Delivery is synchronous by default. `RetainLatest` replays the
latest value to a new observer, while `ScheduledForward` connects an event to
an explicitly owned bounded task queue.

Remote export and other cross-module behavior belong in named adapters.
