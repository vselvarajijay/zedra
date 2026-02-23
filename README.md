# Zedra

**Deterministic world state for robotics.**

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior ends up depending on OS scheduling and wall-clock timing, causing race conditions, sim-to-real divergence, and unreproducible execution. Zedra enforces one rule to fix this: **concurrency at ingestion only; one thread mutates state.**

Multiple producers (sensors, controllers, sim) push events into a lock-free queue. A single reducer thread drains the queue, sorts by logical time (tick, tie_breaker), and applies events to world state in order. Readers get immutable snapshots. Same event log, same state, always.

---

## Architecture

### Rule: concurrency at ingestion, single authority for mutation

All concurrent work happens at **ingestion**: multiple producers (sensors, controllers, sim) push events into a shared queue. Only one thread—the **reducer**—drains the queue, orders events by logical time, and applies them to world state. No other thread ever mutates state. Readers only observe immutable snapshots.

```
┌──────────────────────────────────────────────────────────────┐
│                    PRODUCERS (concurrent)                    │
│                                                              │
│  ┌──────────┐ ┌──────────┐ ┌────────────┐ ┌──────────┐       │
│  │ Sensor A │ │ Sensor B │ │ Controller │ │ Sim / IO │ ...   │
│  └────┬─────┘ └────┬─────┘ └─────┬──────┘ └────┬─────┘       │
│       │            │             │              │            │
│       └────────────┴──────┬──────┴──────────────┘            │
│                           │ submit(Event)                    │
│                           ▼                                  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │        Lock-free queue  (multi-producer, bounded)      │  │
│  │     [ e₁ ] [ e₂ ] [ e₃ ] [ e₄ ] ...  (unordered)       │  │
│  └───────────────────────┬────────────────────────────────┘  │
└──────────────────────────┼───────────────────────────────────┘
                           │
                           │  drain → sort by (tick, tie_breaker)
                           │
                           ▼
┌──────────────────────────────────────────────────────────────┐
│              REDUCER  (single thread, single authority)      │
│                                                              │
│   for each e in sorted batch:                                │
│       state    = WorldState::apply(state, e)                 │
│       snapshot = make_shared(state)                          │
└──────────────────────────┬───────────────────────────────────┘
                           │
                           │  get_snapshot() → shared_ptr<const WorldState>
                           │
                           ▼
┌──────────────────────────────────────────────────────────────┐
│            READERS  (lock-free, immutable snapshots)         │
│                                                              │
│  ┌──────────┐     ┌──────────┐     ┌──────────┐              │
│  │ Planner  │     │ Viz / UI │     │  Logger  │  ...         │
│  └──────────┘     └──────────┘     └──────────┘              │
└──────────────────────────────────────────────────────────────┘
```

### Logical time, not wall-clock

Events carry **logical time**: `(tick, tie_breaker)` and `type` + `payload`. Ordering is defined only by these fields—never by arrival time or OS scheduling. The reducer sorts each drained batch by `(tick, tie_breaker)` before applying, so state evolution is deterministic for a given event sequence.

```
  Event ordering:   (tick, tie_breaker)  →  total order
  Same event log   →  same sort order   →  same state evolution   →  replay & validation
```

### Core components

| Component | Role |
|-----------|------|
| **Event** | Immutable record: `tick`, `tie_breaker`, `type`, `payload`. Comparable and hashable (FNV-1a) for deterministic replay. |
| **LockFreeQueue** | Bounded multi-producer, **single-consumer** queue of `Event`. Producers call `push()`; only the reducer calls `drain()`. When full, `push()` returns false (back off or retry). |
| **Reducer** | Single thread that drains the queue, sorts by logical time, applies `WorldState::apply(current, event)` in order, and publishes the latest `WorldState` as a `shared_ptr<const WorldState>`. |
| **WorldState** | Immutable snapshot: version, deterministic hash, and a key-value map (sorted for deterministic iteration). `apply(current, event)` is pure: same inputs ⇒ same new state. |
| **Readers** | Call `get_snapshot()` (brief mutex to read the shared pointer); then hold `shared_ptr<const WorldState>` and read without further locking. |

### Reducer loop (single thread)

```
  while (running) {
      batch = queue.drain(up to N)
      sort(batch) by (tick, tie_breaker)
      for each event e in batch:
          state = WorldState::apply(state, e)
      snapshot = make_shared(WorldState(state))
  }
```

Only this thread mutates `state` and publishes `snapshot`; all other threads either submit events or read the latest snapshot.

### Replay and validation

- **Replay**: Feed the same event log (same order, same payloads) into the reducer → same sequence of `WorldState` snapshots and same final state hash.
- **Validation**: Persist events and state hashes; recompute state from events and compare hashes to detect divergence or corruption.

---

## Core tests (standalone)

From the repo root, build and run the core test suite (unit tests, replay, behavioral guarantees, concurrent tests, chaotic ingestion):

```bash
./scripts/run_tests.sh
```

This configures and builds in `build_standalone`, then runs `ctest` (or the test binary directly if ctest is not available).

---

## ROS 2 integration

**Build:**

```bash
cd your_ws/src && git clone <repo_url> zedra
cd your_ws && colcon build --packages-up-to zedra_ros
source install/setup.bash
```

**Run the bridge:**

```bash
ros2 run zedra_ros zedra_ros_node
# optional: --ros-args -p queue_capacity:=65536
```

**Ingest:** Publish `ZedraEvent` to `/zedra/inbound_events`. **Egress:** `SnapshotMeta` (version + hash) on `/zedra/snapshot_meta` at 1 kHz.

**Publish events — C++:**

```cpp
#include <zedra_ros/msg/zedra_event.hpp>
auto pub = create_publisher<zedra_ros::msg::ZedraEvent>("/zedra/inbound_events", 10);

zedra_ros::msg::ZedraEvent msg;
msg.tick = tick;  msg.tie_breaker = tie_breaker;  msg.type = 0;  // upsert
// payload = 8-byte little-endian key + value bytes
std::vector<uint8_t> payload(8 + value.size());
memcpy(payload.data(), &key, 8);
memcpy(payload.data() + 8, value.data(), value.size());
msg.payload = payload;
pub->publish(msg);
```

**Publish events — Python:**

```python
from zedra_ros.msg import ZedraEvent
pub = node.create_publisher(ZedraEvent, "/zedra/inbound_events", 10)

msg = ZedraEvent()
msg.tick = tick;  msg.tie_breaker = tie_breaker;  msg.type = 0
msg.payload = list(key.to_bytes(8, "little")) + list(value_bytes)
pub.publish(msg)
```

**Read snapshots:** Subscribe to `/zedra/snapshot_meta` for `SnapshotMeta` (version, hash). Same event log → same hash, enabling replay validation.

Add `zedra_ros` to your `package.xml` for message types.

**Test:**

```bash
colcon test --packages-select zedra_ros
```