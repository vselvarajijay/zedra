---
sidebar_position: 2
---

# Getting Started

This guide walks you through building Zedra from source, running the CLI demo, and verifying deterministic replay.

## Prerequisites

- **CMake** 3.16 or higher
- **C++20** compatible compiler (GCC 10+, Clang 12+, or MSVC 19.29+)
- **Git**

For ROS 2 integration, additionally:
- **ROS 2** (Kilted or later)
- **colcon** build tool

## Clone the Repository

```bash
git clone https://github.com/vselvarajijay/zedra.git
cd zedra
```

## Build with CMake

Build `zedra_core`, `zedra_cli`, and the examples from the repo root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces:
- `build/zedra_cli/zedra_cli` — the standalone CLI tool
- `build/examples/zedra_example` — the API usage example

## Run the Demo

```bash
./build/zedra_cli/zedra_cli demo
```

This runs a built-in scenario and prints the snapshot version and hash. Run it twice — the hash is always identical:

```
Zedra v1.0.0
Final snapshot hash: 0xabc123...
```

## Run Tests

```bash
./scripts/run_tests.sh        # standard CTest suite
./scripts/run_tests.sh full   # full test suite
```

## Verify Determinism with Replay

Capture an event log and replay it to verify determinism:

```bash
# Run a demo and capture the expected hash
./build/zedra_cli/zedra_cli demo > /tmp/demo_output.txt

# Replay a binary log and assert the hash
./build/zedra_cli/zedra_cli replay --expect-hash 0x<hex> path/to/events.bin
```

Exit code `0` means the hash matched; non-zero means divergence.

## ROS 2 Build

```bash
mkdir -p ~/ros_ws/src
cd ~/ros_ws/src && git clone https://github.com/vselvarajijay/zedra.git
cd ~/ros_ws
colcon build --packages-up-to zedra_ros
source install/setup.bash
```

Then run the bridge node:

```bash
ros2 run zedra_ros zedra_ros_node
```

## Docker (Quick Start)

Pull the pre-built image:

```bash
docker pull ghcr.io/vselvarajijay/zedra:latest
docker run --rm ghcr.io/vselvarajijay/zedra:latest
```

See [Docker Deployment](./docker) for the full stack with ClickHouse.

## Next Steps

- [Architecture](./architecture) — understand the system design
- [CLI Reference](./cli) — all CLI subcommands and options
- [ROS 2 Integration](./ros2-integration) — publishing events and reading snapshots
