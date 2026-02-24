# zedra-publisher

Python publisher SDK for the [Zedra](https://github.com/vselvarajijay/zedra) deterministic world-state runtime.

## Overview

`zedra-publisher` provides the `Event` data class and publisher helpers for producing events in the binary format consumed by `zedra_cli replay` and the ROS 2 bridge node (`zedra_ros`).

## Installation

```bash
pip install zedra-publisher
```

Or from [GitHub Packages](https://github.com/vselvarajijay/zedra/packages):

```bash
pip install zedra-publisher \
  --index-url https://__token__:YOUR_GITHUB_TOKEN@pypi.pkg.github.com/vselvarajijay/
```

## Quick Start

```python
from zedra_publisher import Event, BinaryFilePublisher

# Write events to a binary log file (compatible with `zedra_cli replay`)
with BinaryFilePublisher.open("events.bin") as pub:
    pub.publish(Event(tick=1, tie_breaker=0, type=0, payload=b"\x01\x00key\x00value"))
    pub.publish(Event(tick=2, tie_breaker=0, type=0, payload=b"\x02\x00key\x00value2"))
```

Replay the log with the CLI:

```bash
zedra replay events.bin
```

## Binary Format

Events are serialised in little-endian binary format – one record per event:

| Field         | Type    | Description                            |
|---------------|---------|----------------------------------------|
| `tick`        | uint64  | Logical timestamp / nanosecond counter |
| `tie_breaker` | uint64  | Per-source monotonic tie-break counter |
| `type`        | uint32  | Event discriminator (0 = upsert)       |
| `payload_len` | uint32  | Length of the following payload        |
| `payload`     | bytes   | Serialised event data                  |

## API

### `Event`

```python
@dataclass
class Event:
    tick: int
    tie_breaker: int
    type: int
    payload: bytes = b""

    def to_bytes(self) -> bytes: ...
    @classmethod
    def from_bytes(cls, data: bytes, offset: int = 0) -> tuple[Event, int]: ...
```

### `Publisher` (abstract)

```python
class Publisher(ABC):
    def publish(self, event: Event) -> bool: ...
    def close(self) -> None: ...
```

### `BinaryFilePublisher`

```python
class BinaryFilePublisher(Publisher):
    def __init__(self, dest: BinaryIO) -> None: ...

    @classmethod
    def open(cls, path: str) -> BinaryFilePublisher: ...
```

## Development

```bash
cd zedra_publisher
pip install -e ".[dev]"
pytest tests/
```
