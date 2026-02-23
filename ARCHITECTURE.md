# Zedra Architecture

Zedra is a deterministic world-state runtime. Given the same event log and configuration, it guarantees an identical resulting world state — regardless of scheduling, timing, or ingestion concurrency.

**Core invariant:** Concurrency at ingestion only. All state mutation occurs in a single reducer thread. Same event log. Same config. Same result. Always.

Update this document as the project evolves.

---

## 1. Project Structure

```
[Project Root]/
├── zedra_core/                 # Core C++ library: events, state, queue, reducer
│   ├── include/zedra/          # Public headers
│   │   ├── event.hpp           # Event type and FNV-1a hash
│   │   ├── state.hpp           # WorldState (immutable snapshot, apply/trim)
│   │   ├── lock_free_queue.hpp # Bounded multi-producer single-consumer queue
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
├── zedra_ros/                  # ROS 2 integration (ament_cmake)
│   ├── include/zedra_ros/      # Bridge node header
│   ├── src/
│   │   ├── node.cpp
│   │   └── zedra_bridge_node.cpp
│   ├── msg/
│   │   ├── ZedraEvent.msg      # Inbound: tick, tie_breaker, type, payload
│   │   └── SnapshotMeta.msg    # Outbound: version, hash, stats
│   └── test/
│       └── test_replay_determinism.py
├── zedra_cli/                  # Standalone CLI using zedra_core
│   └── src/main.cpp
├── examples/
│   └── main.cpp
├── scripts/
│   ├── run_tests.sh            # Standalone and optional ROS test runner
│   ├── docker_test.sh          # Build and run zedra_ros tests in Docker
│   └── migrations/
│       └── 001_init.sql        # ClickHouse schema (optional)
├── .github/workflows/          # CI/CD (CMake, ROS 2, Docker multi-arch)
├── CMakeLists.txt              # Root: zedra_core, zedra_cli, examples; optional zedra_ros
├── Dockerfile                  # ROS 2 Kilted image for zedra_ros bridge
├── docker-compose.yml          # zedra service + optional ClickHouse profile
├── README.md
└── ARCHITECTURE.md             # This document
```

---

## 2. System Design

**Design rule:** Concurrency only at ingestion. A single reducer thread mutates world state. Readers observe immutable snapshots.

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

---

## 3. Core Components

### 3.1 zedra_core

The deterministic core library. Events carry logical time `(tick, tie_breaker)`; one reducer thread drains the lock-free queue, sorts by logical time, applies events to `WorldState`, optionally trims by a sliding window, and publishes an immutable snapshot. The same event log and configuration always yield the same state and hash.

**Technologies:** C++20. Header-only queue, event, and state definitions; compiled `state.cpp` and `reducer.cpp`.

**Deployment:** Linked as a library by `zedra_ros`, `zedra_cli`, and examples. No standalone server. No dependency on ROS or any transport layer.

### 3.2 zedra_ros

ROS 2 bridge node. Subscribes to `ZedraEvent` on `/zedra/inbound_events`, feeds events into a `zedra::Reducer`, and publishes `SnapshotMeta` (version, hash, stats) on `/zedra/snapshot_meta` at 1 kHz. Optional ClickHouse integration for event persistence when `ZEDRA_PERSIST_EVENTS=1` and ClickHouse is reachable.

**Technologies:** ROS 2 (Kilted), rclcpp, ament_cmake, custom messages `ZedraEvent` and `SnapshotMeta`.

**Deployment:** `ros2 run zedra_ros zedra_ros_node`, or as the primary process in the Zedra Docker image.

### 3.3 zedra_cli

Standalone executable linking `zedra_core`. Used for local testing, replay, and demos. Built by the root CMake; not part of the ROS workspace.

### 3.4 examples

Minimal executable linking `zedra_core` to demonstrate API usage. Built by the root CMake.

---

## 4. Determinism Guarantees

### Ordering

Events are applied in strict `(tick, tie_breaker)` order. `tick` is the logical timestamp; `tie_breaker` resolves ties. Ordering is enforced via deterministic sort before apply and must be identical across runs. No alternative ordering mechanism is permitted.

Producers are responsible for setting `tick` and `tie_breaker`. Core does not reinterpret producer intent.

### State

`WorldState` is immutable after construction and replaced as a whole after each reduction cycle. Readers observe either the previous snapshot or the new snapshot — never a partially applied state.

"Replaced atomically" refers to a full-snapshot swap via mutex-protected `shared_ptr`, not a single atomic instruction. The current implementation uses a sorted vector-backed structure, deterministic iteration order, and FNV-1a hashing for replay validation.

### Containers

Ordered containers and sorted vectors are allowed. `std::unordered_map` without a fixed hash seed, containers with nondeterministic iteration order, and pointer-identity-based ordering logic are forbidden. Iteration order must be stable across runs.

### Replay

Given a serialized event log and identical reducer logic, replay must produce the identical final state and snapshot hash, preserving strict event ordering. Any divergence is an architectural failure.

---

## 5. Threading and Synchronization

The reducer thread loop: drain → sort → apply → publish snapshot. There is exactly one reducer thread.

**Allowed:** lock-free ingestion queue; mutex for publishing and reading the current snapshot via `get_snapshot()`. The reducer may briefly hold the snapshot mutex after the apply phase.

**Forbidden:** any mutex or blocking synchronization during the apply phase; blocking I/O during apply; anything that blocks event application.

---

## 6. Wall Clock Usage

Wall clock time may be used for optional egress metadata, external logging, and diagnostics. It must not influence event ordering, state mutation, reducer logic, deterministic replay, or snapshot hashing.

---

## 7. Memory Allocation

Determinism does not require zero allocation. The current implementation allocates during `WorldState::apply()`, copies state structures, and constructs new immutable state instances. Allocation patterns that introduce nondeterministic behavior or allocation-dependent ordering logic are forbidden. Arena/pool allocation is a future performance enhancement, not a correctness requirement.

---

## 8. Egress and External Effects

Lock-free push to egress queues, metadata emission outside apply, and non-blocking notification mechanisms are allowed. Blocking I/O inside the apply loop, external system calls during state mutation, and any side-effect that affects determinism are forbidden.

---

## 9. Data Stores

**In-memory world state:** `WorldState` is an immutable key–value snapshot with `last_tick` per key. It is the single source of truth inside the reducer. Optional trim by `window_ticks` retains only keys within the sliding window.

**ClickHouse (optional):** Event persistence when running the full stack with `docker compose --profile clickhouse` and `ZEDRA_PERSIST_EVENTS=1`. Schema in `scripts/migrations/001_init.sql`.

---

## 10. External Integrations

| Service          | Purpose                      | Method                                        |
|------------------|------------------------------|-----------------------------------------------|
| ROS 2 (DDS)      | Ingest events, publish meta  | Topics: `ZedraEvent`, `SnapshotMeta`          |
| ClickHouse       | Event persistence (optional) | HTTP/native client from bridge (when enabled) |

No other external integrations are required for core or bridge behavior.

---

## 11. Deployment and Infrastructure

**Docker:** Single Dockerfile (ROS 2 Kilted) builds the `zedra_ros` bridge. Multi-arch (`linux/amd64`, `linux/arm64`) via buildx.

**Docker Compose:** `zedra` service (bridge node); optional `clickhouse` and `clickhouse-init` services under the `clickhouse` profile.

**CI/CD** (`.github/workflows/cmake-single-platform.yml`):

- **build** — Configure and build with CMake, run CTest for `zedra_core`.
- **test-ros** — In `osrf/ros:kilted-desktop`, build and test `zedra_ros` via `colcon test`, including replay determinism.
- **docker** — On `main` push or release, build multi-arch image and push to `ghcr.io/<repo>:latest`; version tags on release.

**Monitoring:** No project-specific monitoring stack. Standard ROS 2 logging; optional ClickHouse for persisted events.

---

## 12. Security

`zedra_core` and `zedra_ros` have no authentication or authorization. Any node that can publish to `/zedra/inbound_events` can feed the reducer. Event payloads are opaque bytes with no built-in encryption. Use TLS and network hardening for ROS 2 and ClickHouse if the event stream contains sensitive or safety-critical data.

---

## 13. Development and Testing

**Local build:** Build from repo root with CMake. For ROS 2, use a colcon workspace: `colcon build --packages-up-to zedra_ros`, then source `setup.bash`.

**Running tests:**

- `./scripts/run_tests.sh` — builds in `build_standalone`, runs CTest.
- `./scripts/run_tests.sh full` — full test suite.
- `./scripts/run_tests.sh ros` — `zedra_ros` tests (requires sourced ROS 2).
- `./scripts/docker_test.sh` — builds and runs `zedra_ros` tests inside Docker.

**Frameworks:** CTest (C++20), Python (`test_replay_determinism.py`). Requires CMake 3.16+.

---

## 14. Forbidden Patterns

The following are architectural violations:

- Multiple reducer threads
- State mutation outside the reducer
- Wall clock influencing ordering or state
- Blocking mutex during the apply phase
- Non-deterministic container iteration
- Blocking I/O inside the apply loop
- Implicit or alternative event reordering

Any change introducing these must be treated as a formal architectural revision.

---

## 15. Extension Guidelines

**Safe to extend** without architecture review: adapters (ROS, CLI, etc.), replay tools, benchmark harnesses, observability layers, snapshot serialization, non-blocking egress handling.

**Requires architecture review:** reducer logic, ordering semantics, state container types, hashing logic, threading model, snapshot replacement mechanism.

Changes in unsafe areas require updating this document, explicit justification, replay validation across multiple runs, and benchmark comparison against baseline.

---

## 16. Non-Goals

Zedra is not a distributed system, message broker, database, real-time scheduler, or persistence engine. It is a deterministic state reducer.

---

## 17. Future Considerations

- Expose `window_ticks` as a ROS parameter (currently only via core API).
- More structured event types and payload schemas.
- Hardening and observability (metrics, tracing) around the reducer and ROS bridge.
- Arena/pool allocation for performance optimization.

---

## 18. Glossary

| Term | Definition |
|------|------------|
| **Reducer** | Single thread that drains the event queue, sorts by `(tick, tie_breaker)`, applies events to `WorldState`, optionally trims, and publishes the new snapshot. |
| **Logical time** | Ordering defined by `(tick, tie_breaker)` on events, not wall-clock or arrival time. |
| **WorldState** | Immutable key–value snapshot with `last_tick` per key; produced by `WorldState::apply` and optionally `trim`. |
| **window_ticks** | If > 0, state is trimmed so only keys with `last_tick` in `[max_tick_seen - window_ticks, max_tick_seen]` are kept. |
| **FNV-1a** | Hash used for events and state to support deterministic replay and validation. |
| **ZedraEvent** | ROS 2 inbound message: `tick`, `tie_breaker`, `type`, `payload`. |
| **SnapshotMeta** | ROS 2 outbound message: `version`, `hash`, and reducer stats (events enqueued/applied/dropped, ticks, key_count, window_ticks). |

---

## 19. Project Info

| Field | Value |
|-------|-------|
| **Project Name** | Zedra |
| **Repository** | `https://github.com/vselvarajijay/zedra` |
| **Last Updated** | 2025-02-23 |