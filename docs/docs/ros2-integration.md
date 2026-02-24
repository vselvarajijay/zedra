---
sidebar_position: 6
---

# ROS 2 Integration

`zedra_ros` provides a ROS 2 bridge node that connects the ROS 2 topic system to Zedra's deterministic reducer.

## Overview

```
[ROS 2 nodes] ──ZedraEvent──▶ /zedra/inbound_events ──▶ ZedraBridgeNode (Reducer)
                                                                 │
                                                  SnapshotMeta (1 kHz)
                                                                 │
                                          /zedra/snapshot_meta ◀─┘
```

## Building

### In a Colcon Workspace

```bash
mkdir -p ~/ros_ws/src
cd ~/ros_ws/src
git clone https://github.com/vselvarajijay/zedra.git
cd ~/ros_ws
colcon build --packages-up-to zedra_ros
source install/setup.bash
```

### With Docker

The provided Dockerfile builds `zedra_ros` on ROS 2 Kilted:

```bash
docker build -t zedra .
docker run --rm zedra
```

## Running the Bridge Node

```bash
ros2 run zedra_ros zedra_ros_node
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `queue_capacity` | `65536` | Lock-free queue capacity |

```bash
ros2 run zedra_ros zedra_ros_node --ros-args -p queue_capacity:=131072
```

## Topics

### Inbound: `/zedra/inbound_events`

**Type:** `zedra_ros/msg/ZedraEvent`

Publish events to this topic to feed them into the reducer.

**Message fields:**

| Field | Type | Description |
|-------|------|-------------|
| `tick` | `uint64` | Logical timestamp |
| `tie_breaker` | `uint64` | Disambiguates same-tick events |
| `type` | `uint32` | Event type (0 = upsert, 1 = delete) |
| `payload` | `uint8[]` | Opaque payload bytes |

### Outbound: `/zedra/snapshot_meta`

**Type:** `zedra_ros/msg/SnapshotMeta`

Published at **1 kHz** with the latest snapshot metadata.

**Message fields:**

| Field | Type | Description |
|-------|------|-------------|
| `version` | `uint64` | Reducer cycle count |
| `hash` | `uint64` | FNV-1a hash of current world state |
| `events_enqueued` | `uint64` | Total events submitted |
| `events_applied` | `uint64` | Total events applied |
| `events_dropped` | `uint64` | Total events dropped (queue overflow) |
| `max_tick_seen` | `uint64` | Highest tick in current state |
| `key_count` | `uint64` | Number of keys in current state |
| `window_ticks` | `uint64` | Current sliding window size |

## Publishing Events

### C++

```cpp
#include <rclcpp/rclcpp.hpp>
#include <zedra_ros/msg/zedra_event.hpp>

class MyNode : public rclcpp::Node {
public:
    MyNode() : Node("my_node") {
        pub_ = create_publisher<zedra_ros::msg::ZedraEvent>(
            "/zedra/inbound_events", 10);
    }

    void send_event(uint64_t tick, uint64_t key, const std::string& value) {
        zedra_ros::msg::ZedraEvent msg;
        msg.tick = tick;
        msg.tie_breaker = 0;
        msg.type = 0;  // upsert

        std::vector<uint8_t> payload(8 + value.size());
        memcpy(payload.data(), &key, 8);
        memcpy(payload.data() + 8, value.data(), value.size());
        msg.payload = payload;

        pub_->publish(msg);
    }

private:
    rclcpp::Publisher<zedra_ros::msg::ZedraEvent>::SharedPtr pub_;
};
```

### Python

```python
import rclpy
from rclpy.node import Node
from zedra_ros.msg import ZedraEvent

class MyNode(Node):
    def __init__(self):
        super().__init__('my_node')
        self.pub = self.create_publisher(
            ZedraEvent, '/zedra/inbound_events', 10)

    def send_event(self, tick: int, key: int, value: bytes):
        msg = ZedraEvent()
        msg.tick = tick
        msg.tie_breaker = 0
        msg.type = 0  # upsert
        msg.payload = list(key.to_bytes(8, 'little')) + list(value)
        self.pub.publish(msg)
```

## Reading Snapshots

### C++

```cpp
#include <zedra_ros/msg/snapshot_meta.hpp>

auto sub = create_subscription<zedra_ros::msg::SnapshotMeta>(
    "/zedra/snapshot_meta", 10,
    [](const zedra_ros::msg::SnapshotMeta::SharedPtr msg) {
        RCLCPP_INFO(rclcpp::get_logger("reader"),
            "Version: %lu  Hash: 0x%lx  Keys: %lu",
            msg->version, msg->hash, msg->key_count);
    });
```

### Python

```python
from zedra_ros.msg import SnapshotMeta

def snapshot_callback(msg):
    print(f"Version: {msg.version}  Hash: {msg.hash:#x}  Keys: {msg.key_count}")

node.create_subscription(SnapshotMeta, '/zedra/snapshot_meta', snapshot_callback, 10)
```

## Testing

```bash
colcon test --packages-select zedra_ros --event-handlers console_direct+
colcon test-result --verbose
```

The `test_replay_determinism.py` test verifies that replay produces the same hash across multiple runs.

## Optional: ClickHouse Persistence

When running the full stack with `ZEDRA_PERSIST_EVENTS=1` and ClickHouse reachable, the bridge persists events to ClickHouse for later replay and analysis:

```bash
docker compose --profile clickhouse up
```

See [Docker Deployment](./docker) for setup.
