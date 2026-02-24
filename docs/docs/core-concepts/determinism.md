---
sidebar_position: 4
---

# Determinism Guarantees

Determinism is Zedra's core invariant. This document describes exactly what is guaranteed and how it is enforced.

## The Guarantee

> Given the same event log and the same `window_ticks` configuration, Zedra always produces the **identical final `WorldState`** and **identical snapshot hash**, regardless of:
> - CPU count or scheduling
> - Wall-clock time
> - Order of event submission (ingestion order)
> - Platform (Linux, macOS, etc.)

## How Determinism is Achieved

### 1. Logical Time Ordering

Events are **never applied in ingestion order**. Instead, the reducer drains the queue and sorts by `(tick, tie_breaker)` before applying:

```cpp
std::sort(batch.begin(), batch.end(), [](const Event& a, const Event& b) {
    return std::tie(a.tick, a.tie_breaker) < std::tie(b.tick, b.tie_breaker);
});
```

Same events → same sort order → same application sequence → same state.

### 2. Deterministic Containers

`WorldState` uses a **sorted vector** (not `std::unordered_map`) for key–value storage, ensuring deterministic iteration order on all platforms.

**Forbidden:** `std::unordered_map` without a fixed hash seed, any container with non-deterministic iteration order, pointer-identity-based comparisons.

### 3. Deterministic Hashing (FNV-1a)

The snapshot hash is computed via **FNV-1a** over all `(key, value, last_tick)` triples in sorted key order. No random seeds, no platform-specific behavior.

```
hash = FNV_OFFSET_BASIS
for each entry in sorted order:
    hash = fnv1a(hash, entry.key)
    hash = fnv1a(hash, entry.value)
    hash = fnv1a(hash, entry.last_tick)
```

### 4. No Wall-Clock Influence

Wall-clock time may be used for:
- External logging
- Egress metadata timestamps
- Diagnostics

Wall-clock time must **never** influence:
- Event ordering
- State mutation
- Reducer logic
- Snapshot hashing

### 5. Single Reducer Thread

There is exactly one thread that calls `WorldState::apply()`. No concurrent mutations. No partial states.

## Replay Validation

Given a binary event log, replay must produce the identical final state hash:

```bash
# Capture expected hash from a reference run
./zedra_cli demo
# → Final hash: 0xabc123

# Replay and assert
./zedra_cli replay --expect-hash 0xabc123 events.bin
# Exit code 0 = match, non-zero = divergence
```

Any hash mismatch indicates an architectural violation.

## What Is NOT Guaranteed

| Property | Status |
|----------|--------|
| Same intermediate states across runs | Not guaranteed (batch sizes may vary) |
| Same final state with different `window_ticks` | Not guaranteed |
| Consistent state under different reducer logic | Not guaranteed |
| Ordering of events with identical `(tick, tie_breaker)` | Undefined behavior — producers must set unique tie_breakers |

## Common Pitfalls

### Non-unique `(tick, tie_breaker)` pairs

If two events have the same `(tick, tie_breaker)`, their relative ordering within a batch is undefined. Producers must ensure unique tie-breakers for events at the same tick.

### Queue overflow

Dropped events (due to full queue) change the effective event log. Tune `queue_capacity` to avoid drops in production.

### External state dependencies

If `apply()` logic reads external mutable state (clocks, global variables, etc.), determinism is broken. Event payloads must carry all necessary data.

## Testing Determinism

The test suite includes dedicated determinism tests:

```bash
./scripts/run_tests.sh         # Includes replay and concurrent tests
./scripts/run_tests.sh full    # Full suite including chaotic ingestion
```

Key test files:
- `replay_test.cpp` — verifies same log → same hash
- `concurrent_test.cpp` — verifies concurrent ingestion → same final state
- `behavioral_guarantees.cpp` — asserts ordering invariants
- `chaotic_ingestion_test.cpp` — stress-tests determinism under load
