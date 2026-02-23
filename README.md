# Zedra

**Deterministic world state for robotics.**

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior ends up depending on OS scheduling and wall-clock timing, causing race conditions, sim-to-real divergence, and unreproducible execution. Zedra enforces one rule to fix this: **concurrency at ingestion only; one thread mutates state.**

Multiple producers (sensors, controllers, sim) push events into a lock-free queue. A single reducer thread drains the queue, sorts by logical time (tick, tie_breaker), and applies events to world state in order. Readers get immutable snapshots. Optionally, state can be trimmed to a sliding time window so only recently updated keys are kept. Same event log (and same window config), same state, always.

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
│       state = WorldState::apply(state, e)                   │
│   if window_ticks > 0:  state = WorldState::trim(state, …)  │
│   snapshot = make_shared(state)                             │
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
| **Reducer** | Single thread that drains the queue, sorts by logical time, applies `WorldState::apply(current, event)` in order, optionally trims state by a sliding window (see below), and publishes the latest `WorldState` as a `shared_ptr<const WorldState>`. Config: `queue_capacity`, optional `egress_queue`, optional `window_ticks`. |
| **WorldState** | Immutable snapshot: version, deterministic hash, and a key–entry map (key → value + `last_tick`), sorted by key. `apply(current, event)` is pure and records the event’s tick per key. Optional `trim(state, max_tick, window_ticks)` drops keys outside the window. Same inputs ⇒ same state. |
| **Readers** | Call `get_snapshot()` (brief mutex to read the shared pointer); then hold `shared_ptr<const WorldState>` and read without further locking. |

### Reducer loop (single thread)

```
  while (running) {
      batch = queue.drain(up to N)
      sort(batch) by (tick, tie_breaker)
      for each event e in batch:
          state = WorldState::apply(state, e)
      max_tick_seen = max(max_tick_seen, max(batch.tick))
      if window_ticks > 0:
          state = WorldState::trim(state, max_tick_seen, window_ticks)
      snapshot = make_shared(WorldState(state))
  }
```

Only this thread mutates `state` and publishes `snapshot`; all other threads either submit events or read the latest snapshot.

### Sliding window (optional)

World state can be limited to a **time window** so the snapshot only contains keys updated within the last `window_ticks` of logical time. This keeps memory bounded and gives a “recent state” view without re-playing raw events.

- **State model:** Each key stores `(value, last_tick)`. On apply, the event’s `tick` is recorded as that key’s `last_tick`.
- **Trim:** After applying a batch, the reducer maintains a global **max_tick_seen** (high-water mark over all applied events). If `window_ticks > 0`, it runs `WorldState::trim(state, max_tick_seen, window_ticks)`, which removes keys with `last_tick < max_tick_seen - window_ticks`.
- **Performance:** Trim is a single O(N) pass over the key set (no event replay). Same event stream and same `window_ticks` ⇒ same snapshot and hash (deterministic).
- **Default:** `window_ticks == 0` (no trim); state grows without bound as before.

### Configuration

| Parameter | Where | Default | Description |
|-----------|--------|---------|-------------|
| **queue_capacity** | `Reducer` ctor, ROS `queue_capacity` | 65536 | Max events in the ingestion queue. When full, `submit()` returns false. |
| **egress_queue** | `Reducer` ctor | `nullptr` | If set, each event (after sort) is pushed here with ingestion timestamp before apply; single consumer only (e.g. logging). |
| **window_ticks** | `Reducer` ctor | 0 | If > 0, trim state to keys with `last_tick` in `[max_tick_seen - window_ticks, max_tick_seen]`. 0 = no trim (cumulative state). |

### Replay and validation

- **Replay**: Feed the same event log (same order, same payloads) into the reducer with the same configuration → same sequence of `WorldState` snapshots and same final state hash. With a sliding window, use the same `window_ticks` for deterministic comparison.
- **Validation**: Persist events and state hashes; recompute state from events and compare hashes to detect divergence or corruption.

---

## Core tests (standalone)

From the repo root, build and run the core test suite (unit tests, replay, behavioral guarantees, concurrent tests, chaotic ingestion):

```bash
./scripts/run_tests.sh
```

This configures and builds in `build_standalone`, then runs the test binary (chaotic by default; use `./scripts/run_tests.sh full` for the full suite). To run **zedra_ros** tests (replay determinism), source your ROS 2 install and run:

```bash
./scripts/run_tests.sh ros
```

CI runs both the standalone CMake tests and the zedra_ros colcon tests.

---

## Docker

Pre-built images are published to [GitHub Container Registry](https://github.com/vselvarajijay/zedra/pkgs/container/zedra) after tests pass on `main` and on each release.

**Pull and run the Zedra ROS bridge:**

```bash
docker pull ghcr.io/vselvarajijay/zedra:latest
docker run --rm ghcr.io/vselvarajijay/zedra:latest
```

For a specific version (when using [releases](https://github.com/vselvarajijay/zedra/releases)), use the version tag, e.g. `ghcr.io/vselvarajijay/zedra:v1.0.0`.

To run the full stack with ClickHouse (optional), use [docker-compose](docker-compose.yml): `docker compose --profile clickhouse up`.

**Running tests inside the image:** From the repo root, build and run zedra_ros tests in one step:

```bash
./scripts/docker_test.sh
```

Or manually: `docker build --platform linux/amd64 -t zedra .` then run the `colcon test` / `colcon test-result` command inside the container (see the script for the exact bash command).

**ARM Macs and multi-arch:** The published image is multi-arch (`linux/amd64`, `linux/arm64`). Prefer native arm64 so you avoid emulation:

```bash
docker run --rm --platform=linux/arm64 ghcr.io/vselvarajijay/zedra:latest
```

If you build locally with Buildx for both platforms:

```bash
docker buildx create --use
docker buildx build --platform linux/amd64,linux/arm64 -t zedra:local --load .
```

If you must run the amd64 image on Apple Silicon (emulation), the replay test can fail with Rosetta memory errors or hash mismatch. You can try giving the container more resources (Docker Desktop → Settings → Resources) and optionally reduce allocator pressure:

```bash
docker run --rm -e MALLOC_ARENA_MAX=2 -e GLIBC_TUNABLES=glibc.malloc.trim_threshold=524288 zedra ...
```

CI runs on native linux/amd64; for production, treat native arm64 support as the supported path.

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
# (window_ticks is not yet exposed as a ROS parameter; use the core API for sliding-window state.)
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