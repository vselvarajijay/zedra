---
sidebar_position: 3
---

# Reducer

The **Reducer** is the heart of Zedra. It owns the lock-free ingestion queue and the single reducer thread that mutates world state.

## Responsibilities

1. **Accept events** from multiple producers concurrently via `submit()`
2. **Drain** the lock-free queue into a local buffer
3. **Sort** the buffer by `(tick, tie_breaker)` — deterministic ordering
4. **Apply** each event to `WorldState` in order
5. **Optionally trim** entries outside the sliding window
6. **Publish** the new immutable snapshot

## Thread Model

There is exactly **one reducer thread**. All state mutation happens on this thread. Producers and readers never touch `WorldState` directly.

```
Thread 1 (producer) ──submit()──▶┐
Thread 2 (producer) ──submit()──▶├──▶ LockFreeQueue ──▶ Reducer Thread ──▶ WorldState
Thread N (producer) ──submit()──▶┘
                                                              │
Thread A (reader) ◀──get_snapshot()────────────────────────────┘
Thread B (reader) ◀──get_snapshot()────────────────────────────┘
```

## Constructor

```cpp
#include <zedra/reducer.hpp>

zedra::Reducer reducer(
    queue_capacity,  // Max events in the lock-free queue (default: 65536)
    window_ticks     // Sliding window size in ticks (default: 0 = no trim)
);
```

## API

### `submit(event)`

Submit an event from any thread. Non-blocking. Drops the event if the queue is full.

```cpp
reducer.submit(event);
```

### `get_snapshot()`

Get the latest immutable snapshot. Safe to call from any thread, any time.

```cpp
auto snapshot = reducer.get_snapshot();
// snapshot is a shared_ptr<const WorldState>
```

### `start()` / `stop()`

Control the reducer thread lifecycle:

```cpp
reducer.start();  // Start the reducer thread
// ... submit events, read snapshots ...
reducer.stop();   // Signal the thread to finish and join
```

## Reducer Loop

The reducer thread runs the following loop until stopped:

```
while running:
    1. drain queue → local_batch
    2. sort local_batch by (tick, tie_breaker)
    3. for each event in local_batch:
           state = WorldState::apply(state, event)
    4. if window_ticks > 0:
           state = WorldState::trim(state, window_ticks)
    5. publish state via shared_ptr swap
    6. notify egress handlers (if any)
```

## Lock-Free Queue

The ingestion queue is a **bounded multi-producer single-consumer (MPSC)** lock-free ring buffer.

| Property | Value |
|----------|-------|
| Producers | Unlimited (concurrent) |
| Consumers | Exactly 1 (reducer thread) |
| Overflow | Drop — backpressure not propagated |
| Blocking | Never — both push and pop are non-blocking |

Default capacity: **65536 events**. Configurable via `Reducer` constructor or `--queue-capacity` CLI option.

## Egress

The reducer can push metadata to optional egress queues after each reduction cycle. Egress items carry version, hash, and timing stats and can be consumed asynchronously for logging or external systems.

## Example

```cpp
#include <zedra/reducer.hpp>
#include <zedra/event.hpp>

int main() {
    zedra::Reducer reducer(65536, 0);
    reducer.start();

    // Submit from any thread
    zedra::Event e{.tick = 1, .tie_breaker = 0, .type = 0};
    uint64_t key = 42;
    e.payload.resize(8);
    memcpy(e.payload.data(), &key, 8);
    reducer.submit(e);

    // Read snapshot from any thread
    auto snap = reducer.get_snapshot();
    std::cout << "Hash: " << std::hex << snap->hash() << "\n";

    reducer.stop();
}
```
