---
sidebar_position: 5
---

# CLI Reference

`zedra_cli` is a standalone command-line tool for running demos, replaying event logs, and verifying determinism.

## Building

From the repository root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# Binary: ./build/zedra_cli/zedra_cli
```

## Global Options

These options appear **before** the subcommand:

| Option | Default | Description |
|--------|---------|-------------|
| `--queue-capacity N` | `65536` | Lock-free queue capacity (max events in flight) |
| `--window-ticks N` | `0` | Sliding window size in ticks (0 = no trim) |
| `-h`, `--help` | — | Show help and exit |

```bash
./zedra_cli --queue-capacity 131072 --window-ticks 1000 demo
```

## Subcommands

### `zedra demo`

Runs the built-in scenario and prints the snapshot version and hash.

```bash
./zedra_cli demo
```

**Output:**

```
Zedra v1.0.0
Final snapshot version: 1
Final snapshot hash: 0xabc123def456
Key count: 5
```

Run twice — the hash is always identical, confirming determinism:

```bash
./zedra_cli demo && ./zedra_cli demo
```

### `zedra replay <input>`

Reads a binary event log from `<input>` (file path or `-` for stdin) and replays it through the reducer.

```bash
./zedra_cli replay path/to/events.bin
./zedra_cli replay -          # read from stdin
```

**Output:**

```
Replay complete.
Final snapshot version: 42
Final snapshot hash: 0xdeadbeef
Events applied: 1000
Key count: 128
```

#### `--expect-hash H`

Exits `0` only if the final snapshot hash matches `H` (hex). Useful in scripts to assert determinism:

```bash
./zedra_cli replay --expect-hash 0xdeadbeef events.bin
echo "Exit code: $?"
```

Exit codes:
- `0` — hash matched (or no `--expect-hash` provided)
- `1` — hash mismatch
- `2` — input error or invalid format

## Event Log Format

The binary format used by `zedra replay` is little-endian:

| Field | Type | Bytes |
|-------|------|-------|
| `tick` | `uint64` | 8 |
| `tie_breaker` | `uint64` | 8 |
| `type` | `uint32` | 4 |
| `payload_len` | `uint32` | 4 |
| `payload` | bytes | `payload_len` |

Events are written sequentially with no framing or header.

### Writing a Log in C++

```cpp
std::ofstream out("events.bin", std::ios::binary);
for (const auto& e : events) {
    uint32_t plen = e.payload.size();
    out.write(reinterpret_cast<const char*>(&e.tick), 8);
    out.write(reinterpret_cast<const char*>(&e.tie_breaker), 8);
    out.write(reinterpret_cast<const char*>(&e.type), 4);
    out.write(reinterpret_cast<const char*>(&plen), 4);
    out.write(reinterpret_cast<const char*>(e.payload.data()), plen);
}
```

### Writing a Log in Python

```python
import struct

with open("events.bin", "wb") as f:
    for e in events:
        payload = bytes(e.payload)
        f.write(struct.pack("<QQII", e.tick, e.tie_breaker, e.type, len(payload)))
        f.write(payload)
```

## Examples

```bash
# Run the built-in demo
./zedra_cli demo

# Replay a log file
./zedra_cli replay logs/run1.bin

# Replay from stdin
cat logs/run1.bin | ./zedra_cli replay -

# Assert determinism against a known hash
./zedra_cli replay --expect-hash 0x1a2b3c4d logs/run1.bin

# Use a larger queue and a sliding window
./zedra_cli --queue-capacity 262144 --window-ticks 500 replay logs/run1.bin
```

## Help

```bash
./zedra_cli --help
./zedra_cli demo --help
./zedra_cli replay --help
```
