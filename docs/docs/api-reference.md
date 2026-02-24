---
sidebar_position: 8
---

# API Reference

This page documents the public C++ API of `zedra_core`.

## Headers

All public headers are in `zedra_core/include/zedra/`:

| Header | Description |
|--------|-------------|
| `event.hpp` | `Event` struct and FNV-1a hash |
| `state.hpp` | `WorldState` class |
| `lock_free_queue.hpp` | `LockFreeQueue` template |
| `reducer.hpp` | `Reducer` class |
| `egress_item.hpp` | `EgressItem` for post-reduction metadata |

## `zedra::Event`

```cpp
#include <zedra/event.hpp>

struct Event {
    uint64_t tick;
    uint64_t tie_breaker;
    uint32_t type;
    std::vector<uint8_t> payload;
};
```

### Constants

```cpp
static constexpr uint32_t TYPE_UPSERT = 0;
static constexpr uint32_t TYPE_DELETE = 1;
```

### Free Functions

```cpp
// Compute FNV-1a hash over (tick, tie_breaker, type, payload)
uint64_t hash_event(const Event& e);
```

## `zedra::WorldState`

```cpp
#include <zedra/state.hpp>

class WorldState {
public:
    // Apply an event to produce a new immutable WorldState
    static WorldState apply(const WorldState& state, const Event& event);

    // Trim entries outside the sliding window
    static WorldState trim(const WorldState& state, uint64_t window_ticks);

    // Compute deterministic FNV-1a hash over all entries
    uint64_t hash() const;

    // Reducer cycle count
    uint64_t version() const;

    // Number of key-value entries
    size_t key_count() const;

    // Maximum tick seen across all entries
    uint64_t max_tick_seen() const;

    // Iterate over entries
    const std::vector<Entry>& entries() const;
};
```

### `WorldState::Entry`

```cpp
struct Entry {
    uint64_t key;
    std::vector<uint8_t> value;
    uint64_t last_tick;
};
```

## `zedra::LockFreeQueue<T>`

```cpp
#include <zedra/lock_free_queue.hpp>

template <typename T>
class LockFreeQueue {
public:
    explicit LockFreeQueue(size_t capacity);

    // Push an item (non-blocking). Returns false if full.
    bool push(T item);

    // Pop all available items into `out` (non-blocking).
    void drain(std::vector<T>& out);

    // Number of items currently in the queue (approximate).
    size_t size() const;
};
```

## `zedra::Reducer`

```cpp
#include <zedra/reducer.hpp>

class Reducer {
public:
    // Construct with queue capacity and sliding window size
    explicit Reducer(size_t queue_capacity = 65536, uint64_t window_ticks = 0);

    // Start the reducer thread
    void start();

    // Stop the reducer thread (blocks until joined)
    void stop();

    // Submit an event (thread-safe, non-blocking)
    void submit(const Event& event);

    // Get the latest immutable snapshot (thread-safe)
    std::shared_ptr<const WorldState> get_snapshot() const;

    // Stats
    uint64_t events_enqueued() const;
    uint64_t events_applied() const;
    uint64_t events_dropped() const;
};
```

## `zedra::EgressItem`

```cpp
#include <zedra/egress_item.hpp>

struct EgressItem {
    uint64_t version;
    uint64_t hash;
    uint64_t events_applied;
    uint64_t events_dropped;
    uint64_t max_tick_seen;
    uint64_t key_count;
    uint64_t window_ticks;
};
```

## CMake Integration

Add `zedra_core` to your CMakeLists.txt:

```cmake
add_subdirectory(path/to/zedra_core)
target_link_libraries(my_target PRIVATE zedra_core)
```

Or using find_package (if installed):

```cmake
find_package(zedra_core REQUIRED)
target_link_libraries(my_target PRIVATE zedra::zedra_core)
```

## Minimal Example

```cpp
#include <zedra/reducer.hpp>
#include <zedra/event.hpp>
#include <iostream>
#include <thread>

int main() {
    zedra::Reducer reducer(65536, 0);
    reducer.start();

    // Produce events from multiple threads
    std::thread producer([&] {
        for (uint64_t i = 0; i < 1000; ++i) {
            zedra::Event e;
            e.tick = i;
            e.tie_breaker = 0;
            e.type = zedra::Event::TYPE_UPSERT;

            uint64_t key = i % 100;
            e.payload.resize(8);
            memcpy(e.payload.data(), &key, 8);

            reducer.submit(e);
        }
    });

    producer.join();

    // Give the reducer time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read snapshot
    auto snap = reducer.get_snapshot();
    std::cout << "Version: " << snap->version() << "\n";
    std::cout << "Hash: " << std::hex << snap->hash() << "\n";
    std::cout << "Keys: " << snap->key_count() << "\n";

    reducer.stop();
}
```
