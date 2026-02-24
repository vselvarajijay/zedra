---
sidebar_position: 1
---

# Introduction

**Zedra** is a deterministic world-state runtime for robotics. Given the same event log and configuration, it guarantees an identical resulting world state — regardless of scheduling, timing, or ingestion concurrency.

## The Problem

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior ends up depending on OS scheduling and wall-clock timing, causing:

- **Race conditions** — sensors and controllers write to shared state in unpredictable orders
- **Sim-to-real divergence** — simulation and hardware produce different outcomes for the same inputs
- **Unreproducible execution** — bugs cannot be reliably replicated and debugged

## The Solution

Zedra enforces one rule to fix this:

> **Concurrency at ingestion only; one thread mutates state.**

```
┌─────────────────────────────────────────────────────────┐
│  PRODUCERS (concurrent)                                 │
│  Sensors, controllers, sim → submit(Event)              │
└────────────────────────────┬────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────┐
│  Lock-free queue (bounded MPSC)                         │
└────────────────────────────┬────────────────────────────┘
                             │ drain → sort by (tick, tie_breaker)
                             ▼
┌─────────────────────────────────────────────────────────┐
│  REDUCER (single thread)                                │
│  apply(state, event) → optional trim → publish snapshot │
└────────────────────────────┬────────────────────────────┘
                             │ get_snapshot() → immutable snapshot
                             ▼
┌─────────────────────────────────────────────────────────┐
│  READERS (planner, viz, logger) — lock-free snapshots   │
└─────────────────────────────────────────────────────────┘
```

Multiple producers (sensors, controllers, sim) push events into a lock-free queue. A single reducer thread drains the queue, sorts by logical time (`tick`, `tie_breaker`), and applies events to world state in order. Readers get immutable snapshots. **Same event log and same window config always produce the same state.**

## Key Properties

| Property | Description |
|----------|-------------|
| **Deterministic** | Same inputs always produce the same state and hash |
| **Concurrent ingestion** | Multiple producers write simultaneously without locks |
| **Lock-free reads** | Readers get immutable snapshots without blocking |
| **Replay validation** | Binary event logs can be replayed to verify state hashes |
| **ROS 2 ready** | Drop-in bridge node for `/zedra/inbound_events` |

## Components

- **`zedra_core`** — Core C++20 library: events, world state, lock-free queue, reducer
- **`zedra_ros`** — ROS 2 bridge node that feeds `ZedraEvent` messages into the reducer
- **`zedra_cli`** — Standalone command-line tool for demos and event log replay

## Next Steps

- [Getting Started](./getting-started) — build and run Zedra locally
- [Architecture](./architecture) — deep dive into the system design
- [Core Concepts](./core-concepts/events) — events, world state, reducer, and determinism guarantees
