---
sidebar_position: 1
---

# Events

An **Event** is the fundamental unit of change in Zedra. All state mutations are expressed as events.

## Structure

```cpp
struct Event {
    uint64_t tick;         // Logical timestamp
    uint64_t tie_breaker;  // Disambiguates same-tick events
    uint32_t type;         // Event type (0 = upsert, 1 = delete, ...)
    std::vector<uint8_t> payload;  // Opaque bytes
};
```

## Logical Time

Zedra does not use wall-clock time for ordering. Instead, each event carries a **logical timestamp** composed of two fields:

| Field | Description |
|-------|-------------|
| `tick` | Primary logical timestamp; monotonically increasing per producer |
| `tie_breaker` | Disambiguates events with the same `tick` |

The reducer sorts all events by `(tick, tie_breaker)` before applying them, ensuring deterministic ordering regardless of arrival order.

:::important
Producers are responsible for setting `tick` and `tie_breaker`. Zedra does not reinterpret or modify these values.
:::

## Event Types

| Value | Meaning |
|-------|---------|
| `0` | Upsert — insert or update the key in `WorldState` |
| `1` | Delete — remove the key from `WorldState` |

Additional types may be defined by the application.

## Payload Format

The payload format for an upsert is:

```
[8 bytes: key (uint64, little-endian)] [N bytes: value]
```

For a delete, only the 8-byte key is required.

## Hashing

Each event is hashed using **FNV-1a** over `(tick, tie_breaker, type, payload)`. The hash is used for replay validation and state integrity checks.

## Binary Event Log Format

The `zedra_cli replay` command reads a binary event log in little-endian format:

| Field | Type | Size |
|-------|------|------|
| `tick` | uint64 | 8 bytes |
| `tie_breaker` | uint64 | 8 bytes |
| `type` | uint32 | 4 bytes |
| `payload_len` | uint32 | 4 bytes |
| `payload` | bytes | `payload_len` bytes |

Events are stored sequentially with no header or framing.

## Creating Events

### C++

```cpp
#include <zedra/event.hpp>

zedra::Event e;
e.tick = 42;
e.tie_breaker = 0;
e.type = 0;  // upsert

uint64_t key = 1001;
std::string value = "sensor_data";

e.payload.resize(8 + value.size());
memcpy(e.payload.data(), &key, 8);
memcpy(e.payload.data() + 8, value.data(), value.size());

reducer.submit(e);
```

### ROS 2 C++

```cpp
#include <zedra_ros/msg/zedra_event.hpp>

zedra_ros::msg::ZedraEvent msg;
msg.tick = 42;
msg.tie_breaker = 0;
msg.type = 0;  // upsert

std::vector<uint8_t> payload(8 + value.size());
memcpy(payload.data(), &key, 8);
memcpy(payload.data() + 8, value.data(), value.size());
msg.payload = payload;

publisher->publish(msg);
```

### ROS 2 Python

```python
from zedra_ros.msg import ZedraEvent

msg = ZedraEvent()
msg.tick = 42
msg.tie_breaker = 0
msg.type = 0  # upsert
msg.payload = list(key.to_bytes(8, 'little')) + list(value_bytes)

publisher.publish(msg)
```
