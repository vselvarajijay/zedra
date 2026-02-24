---
sidebar_position: 3
---

# Architecture

Zedra's architecture is built around a single invariant: **concurrency at ingestion only; all state mutation in a single reducer thread.**

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  PRODUCERS (concurrent)                                         │
│  Sensors, controllers, sim → submit(Event)                      │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  Lock-free queue (bounded, multi-producer, single-consumer)     │
└────────────────────────────┬────────────────────────────────────┘
                             │ drain → sort by (tick, tie_breaker)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  REDUCER (single thread)                                        │
│  apply(state, event) → optional trim → publish snapshot         │
└────────────────────────────┬────────────────────────────────────┘
                             │ get_snapshot() → shared_ptr<const WorldState>
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  READERS (planner, viz, logger) — lock-free immutable snapshots │
└─────────────────────────────────────────────────────────────────┘
```

## Project Structure

```
zedra/
├── zedra_core/                 # Core C++ library
│   ├── include/zedra/
│   │   ├── event.hpp           # Event type and FNV-1a hash
│   │   ├── state.hpp           # WorldState (immutable snapshot)
│   │   ├── lock_free_queue.hpp # Bounded MPSC queue
│   │   ├── egress_item.hpp     # Egress item for logging
│   │   └── reducer.hpp         # Reducer: drain → sort → apply → snapshot
│   ├── src/
│   │   ├── state.cpp
│   │   └── reducer.cpp
│   └── tests/
│       ├── unit_event.cpp
│       ├── unit_state.cpp
│       ├── unit_lock_free_queue.cpp
│       ├── replay_test.cpp
│       ├── sliding_window_test.cpp
│       ├── concurrent_test.cpp
│       ├── behavioral_guarantees.cpp
│       ├── chaotic_ingestion_test.cpp
│       └── example_smoke_test.cpp
├── zedra_ros/                  # ROS 2 integration
│   ├── include/zedra_ros/
│   ├── src/
│   │   ├── node.cpp
│   │   └── zedra_bridge_node.cpp
│   ├── msg/
│   │   ├── ZedraEvent.msg      # Inbound: tick, tie_breaker, type, payload
│   │   └── SnapshotMeta.msg    # Outbound: version, hash, stats
│   └── test/
│       └── test_replay_determinism.py
├── zedra_cli/                  # Standalone CLI
│   └── src/main.cpp
├── examples/
│   └── main.cpp
└── scripts/
    ├── run_tests.sh
    └── docker_test.sh
```

## Core Components

### zedra_core

The deterministic core library. Written in C++20 with no dependencies on ROS or any transport layer.

**Key types:**

| Type | Description |
|------|-------------|
| `Event` | Carries logical time `(tick, tie_breaker)`, event `type`, and opaque `payload` bytes |
| `WorldState` | Immutable key–value snapshot; `last_tick` per key; produced by `apply()` and `trim()` |
| `LockFreeQueue` | Bounded multi-producer single-consumer queue for lock-free ingestion |
| `Reducer` | Owns the queue and reducer thread; exposes `submit()` and `get_snapshot()` |

**Linked by:** `zedra_ros`, `zedra_cli`, and examples.

### zedra_ros

ROS 2 bridge node. Subscribes to `ZedraEvent` on `/zedra/inbound_events`, feeds events into a `zedra::Reducer`, and publishes `SnapshotMeta` (version, hash, stats) on `/zedra/snapshot_meta` at 1 kHz.

**ROS 2 deployment:**

```
[ROS 2 nodes] ──ZedraEvent──▶ /zedra/inbound_events ──▶ ZedraBridgeNode (Reducer)
                                                                 │
                                                  SnapshotMeta (1 kHz)
                                                                 │
                                          /zedra/snapshot_meta ◀─┘
                                                    │
                                          [optional: ClickHouse]
```

### zedra_cli

Standalone executable for local testing, replay, and demos.

**Subcommands:**
- `zedra demo` — runs a built-in scenario; prints version and hash
- `zedra replay <input>` — replays a binary event log; prints final version, hash, and stats
- `zedra replay --expect-hash H <input>` — exits non-zero if hash doesn't match `H`

## Threading Model

The reducer thread runs a single loop: **drain → sort → apply → publish snapshot**.

| Layer | Allowed | Forbidden |
|-------|---------|-----------|
| Ingestion | Multi-producer lock-free push | Blocking, mutex |
| Apply phase | Single-thread state mutation | Mutex, blocking I/O |
| Snapshot publish | Mutex-protected `shared_ptr` swap | Blocking during snapshot read |
| Readers | Lock-free immutable snapshot access | State mutation |

## Data Flow

1. **Producers** call `reducer.submit(event)` from any thread
2. Events go into the **lock-free bounded queue** (drops on overflow)
3. The **reducer thread** wakes, drains the queue into a local buffer
4. Buffer is **sorted by `(tick, tie_breaker)`** — deterministic ordering
5. Each event is **applied** to `WorldState` via `apply(state, event)`
6. Optional: **trim** removes entries outside the sliding window (`window_ticks`)
7. New immutable snapshot is **published** via mutex-protected `shared_ptr` swap
8. **Readers** call `reducer.get_snapshot()` for the latest immutable `WorldState`

## Forbidden Patterns

These are architectural violations:

- Multiple reducer threads
- State mutation outside the reducer
- Wall clock influencing event ordering or state
- Blocking mutex during the apply phase
- Non-deterministic container iteration (e.g., unordered maps without fixed hash seeds)
- Blocking I/O inside the apply loop
- Implicit or alternative event reordering

Any change introducing these requires a formal architecture revision.
