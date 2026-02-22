# Zedra

**Deterministic state semantics as infrastructure.**

## The problem

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior therefore depends on wall-clock timing and OS scheduling, which leads to non-deterministic execution, order-dependent race conditions, simulation-to-real divergence, and the inability to achieve exact state replay.

## What Zedra solves

Zedra is a C++20 deterministic, replayable, versioned world-state runtime. It enforces a single structural rule: **concurrency at ingestion only; a single authority for mutation.** Events are ordered by logical time (never OS timing), a single reducer thread is the sole mutator of world state, and readers get lock-free immutable snapshots. An identical event log yields an identical state evolution—enabling exact replay and reliable validation.

---

## Architecture

### Rule: concurrency at ingestion, single authority for mutation

All concurrent work happens at **ingestion**: multiple producers (sensors, controllers, sim) push events into a shared queue. Only one thread—the **reducer**—drains the queue, orders events by logical time, and applies them to world state. No other thread ever mutates state. Readers only observe immutable snapshots.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PRODUCERS (concurrent)                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │
│  │ Sensor A │  │ Sensor B │  │ Controller│  │ Sim / IO │  ...               │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘                     │
│       │             │             │             │                            │
│       │  submit(Event)  submit(Event)  submit(Event)  submit(Event)          │
│       ▼             ▼             ▼             ▼                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │              Lock-free queue (multi-producer, bounded)               │    │
│  │   [ e₁ ] [ e₂ ] [ e₃ ] [ e₄ ] ...   (unordered at ingress)          │    │
│  └─────────────────────────────────────┬───────────────────────────────┘    │
└─────────────────────────────────────────┼───────────────────────────────────┘
                                          │
                                          │  drain → sort by (tick, tie_breaker)
                                          ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                    REDUCER (single thread, single authority)                 │
│                                                                              │
│    batch → sort(events) → for each e: state = WorldState::apply(state, e)   │
│                                    → snapshot_ = make_shared(state)          │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │
                                      │  get_snapshot() → shared_ptr<const WorldState>
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         READERS (lock-free once holding snapshot)            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                                   │
│  │ Planner  │  │ Viz / UI │  │ Logger   │  ...  (immutable, versioned)       │
│  └──────────┘  └──────────┘  └──────────┘                                   │
└─────────────────────────────────────────────────────────────────────────────┘
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
