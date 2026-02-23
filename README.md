# Zedra

**Deterministic world state for robotics.**

Real-time robotics systems mutate shared state under concurrency without deterministic ordering. Behavior ends up depending on OS scheduling and wall-clock timing, causing race conditions, sim-to-real divergence, and unreproducible execution.

Zedra enforces one rule to fix this: **concurrency at ingestion only; one thread mutates state.** Multiple producers (sensors, controllers, sim) push events into a lock-free queue. A single reducer thread drains the queue, sorts by logical time (`tick`, `tie_breaker`), and applies events to world state in order. Readers get immutable snapshots. Same event log and same window config always produce the same state.

For design, components, and guarantees, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Running Tests

From the repo root:

```bash
./scripts/run_tests.sh         # standard suite
./scripts/run_tests.sh full    # full suite
```

For `zedra_ros` replay determinism tests, source your ROS 2 install first:

```bash
./scripts/run_tests.sh ros
```

See [ARCHITECTURE.md §13](ARCHITECTURE.md#13-development-and-testing) for more, including `docker_test.sh`.

---

## Docker

Pre-built images are on the [GitHub Container Registry](https://github.com/vselvarajijay/zedra/pkgs/container/zedra).

```bash
docker pull ghcr.io/vselvarajijay/zedra:latest
docker run --rm ghcr.io/vselvarajijay/zedra:latest
```

For a specific release, use `ghcr.io/vselvarajijay/zedra:v1.0.0`. To bring up the stack with ClickHouse:

```bash
docker compose --profile clickhouse up
```

Images are multi-arch (amd64/arm64). For ARM Mac notes and CI details, see [ARCHITECTURE.md §11](ARCHITECTURE.md#11-deployment-and-infrastructure).

---

## ROS 2 Integration

**Build:**

```bash
cd your_ws/src && git clone zedra
cd your_ws && colcon build --packages-up-to zedra_ros
source install/setup.bash
```

**Run the bridge:**

```bash
ros2 run zedra_ros zedra_ros_node
# optional: --ros-args -p queue_capacity:=65536
```

**Ingest:** publish `ZedraEvent` messages to `/zedra/inbound_events`.

**Egress:** `SnapshotMeta` (version + hash) is published on `/zedra/snapshot_meta` at 1 kHz.

---

### Publishing Events

**C++:**

```cpp
#include <zedra_ros/msg/zedra_event.hpp>

auto pub = create_publisher<zedra_ros::msg::ZedraEvent>("/zedra/inbound_events", 10);

zedra_ros::msg::ZedraEvent msg;
msg.tick = tick;
msg.tie_breaker = tie_breaker;
msg.type = 0;  // upsert

// payload = 8-byte little-endian key + value bytes
std::vector<uint8_t> payload(8 + value.size());
memcpy(payload.data(), &key, 8);
memcpy(payload.data() + 8, value.data(), value.size());
msg.payload = payload;
pub->publish(msg);
```

**Python:**

```python
from zedra_ros.msg import ZedraEvent

pub = node.create_publisher(ZedraEvent, "/zedra/inbound_events", 10)

msg = ZedraEvent()
msg.tick = tick
msg.tie_breaker = tie_breaker
msg.type = 0  # upsert
msg.payload = list(key.to_bytes(8, "little")) + list(value_bytes)
pub.publish(msg)
```

---

### Reading Snapshots

Subscribe to `/zedra/snapshot_meta` for `SnapshotMeta` messages containing version and hash. The same event log always produces the same hash, enabling replay validation.

Add `zedra_ros` to your `package.xml` to use the message types.

**Test:**

```bash
colcon test --packages-select zedra_ros
```