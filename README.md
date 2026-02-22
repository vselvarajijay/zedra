# Zedra

**Deterministic state semantics as infrastructure.**

## The problem

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior therefore depends on wall-clock timing and OS scheduling, which leads to non-deterministic execution, order-dependent race conditions, simulation-to-real divergence, and the inability to achieve exact state replay.

## What Zedra solves

Zedra is a C++20 deterministic, replayable, versioned world-state runtime. It enforces a single structural rule: **concurrency at ingestion only; a single authority for mutation.** Events are ordered by logical time (never OS timing), a single reducer thread is the sole mutator of world state, and readers get lock-free immutable snapshots. An identical event log yields an identical state evolution—enabling exact replay and reliable validation.
