# Logging

`log::StaticLogger<Application, Sources, Domains, Sinks, HistoryCapacity>` is
an explicitly owned, allocation-free module. It retains encoded records in a
bounded ring, renders only for delivery or replay, and reports eviction.

Applications declare sources, domains, and sinks directly. Solar does not
replace Zephyr's logging frontend. A console, Remote stream, or persistence
backend is an ordinary sink adapter. ISR code records counters or uses a
purpose-built no-wait ingress; it does not take the logger's thread lock.
