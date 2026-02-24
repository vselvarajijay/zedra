---
sidebar_position: 2
---

# World State

`WorldState` is the immutable key–value snapshot that represents the current state of the world at any point in time.

## Design

`WorldState` is **immutable after construction**. It is never mutated in place — instead, a new `WorldState` is produced by `apply()` or `trim()`. This ensures readers always see a consistent, stable snapshot.

```
WorldState (old) ──apply(event)──▶ WorldState (new)
WorldState (old) ──trim(window)──▶ WorldState (trimmed)
```

## Structure

Internally, `WorldState` stores entries in a **sorted vector** (not a hash map), guaranteeing deterministic iteration order across all runs and platforms.

Each entry holds:

| Field | Type | Description |
|-------|------|-------------|
| `key` | `uint64_t` | Unique key for this state entry |
| `value` | `std::vector<uint8_t>` | Opaque value bytes |
| `last_tick` | `uint64_t` | Logical tick at which this key was last updated |

## API

### `apply(state, event) → WorldState`

Applies an event to produce a new immutable `WorldState`:

```cpp
#include <zedra/state.hpp>

auto new_state = zedra::WorldState::apply(current_state, event);
```

- **Upsert** (`type == 0`): Inserts or replaces the key-value pair; updates `last_tick`
- **Delete** (`type == 1`): Removes the key from the state

### `trim(state, window_ticks) → WorldState`

Removes all entries whose `last_tick` is more than `window_ticks` behind the maximum tick seen:

```cpp
auto trimmed = zedra::WorldState::trim(state, window_ticks);
```

Only entries in `[max_tick_seen - window_ticks, max_tick_seen]` are retained. When `window_ticks == 0`, no trimming is performed.

### `hash() → uint64_t`

Computes a deterministic **FNV-1a** hash over all key-value-tick triples in sorted order:

```cpp
uint64_t h = state.hash();
```

Same state content always produces the same hash, enabling replay validation.

### `version() → uint64_t`

Returns the number of reducer cycles that produced this snapshot:

```cpp
uint64_t v = state.version();
```

## Reading Snapshots

Readers never interact with `WorldState` directly through the reducer loop. They call `get_snapshot()` which returns a `shared_ptr<const WorldState>`:

```cpp
auto snapshot = reducer.get_snapshot();

// Access entries
for (const auto& entry : snapshot->entries()) {
    uint64_t key = entry.key;
    const auto& value = entry.value;
    uint64_t tick = entry.last_tick;
}

uint64_t hash = snapshot->hash();
uint64_t version = snapshot->version();
```

The snapshot pointer is valid for as long as any code holds a reference to it.

## Sliding Window

When `window_ticks > 0`, the reducer trims state after each cycle to retain only recent entries:

```
retained: last_tick ∈ [max_tick_seen - window_ticks, max_tick_seen]
```

This bounds memory usage and enables time-windowed replay. Set via `Reducer` constructor or the `--window-ticks` CLI option.

## Atomicity

The reducer publishes the new snapshot via a **mutex-protected `shared_ptr` swap**. Readers calling `get_snapshot()` get either the old or new snapshot, never a partially applied state.
