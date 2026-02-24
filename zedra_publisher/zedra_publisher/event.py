"""Event data class for the Zedra publisher SDK.

Mirrors ``zedra::Event`` from the C++ core library.  The binary
serialisation format matches the ``zedra_cli`` replay format:

  tick        – uint64 (little-endian)
  tie_breaker – uint64 (little-endian)
  type        – uint32 (little-endian)
  payload_len – uint32 (little-endian)
  payload     – payload_len bytes
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

_HEADER = struct.Struct("<QQII")  # tick, tie_breaker, type, payload_len


@dataclass
class Event:
    """Immutable event ordered by ``(tick, tie_breaker)``.

    Attributes:
        tick:         Logical timestamp or nanosecond wall-clock value.
                      The upstream producer is responsible for consistency.
        tie_breaker:  Deterministic ordering when ticks collide
                      (e.g. a per-source monotonic counter).
        type:         Event discriminator (e.g. ``0`` = upsert in current
                      ``zedra_core`` semantics).
        payload:      Serialised event data; interpretation is by *type*.
    """

    tick: int
    tie_breaker: int
    type: int
    payload: bytes = field(default_factory=bytes)

    def __lt__(self, other: "Event") -> bool:
        if self.tick != other.tick:
            return self.tick < other.tick
        return self.tie_breaker < other.tie_breaker

    def to_bytes(self) -> bytes:
        """Serialise to the Zedra binary log format (little-endian)."""
        payload = bytes(self.payload)
        return _HEADER.pack(self.tick, self.tie_breaker, self.type, len(payload)) + payload

    @classmethod
    def from_bytes(cls, data: bytes, offset: int = 0) -> tuple["Event", int]:
        """Deserialise one event from *data* starting at *offset*.

        Returns:
            A ``(event, bytes_consumed)`` tuple so callers can advance a
            buffer cursor without copying.

        Raises:
            ValueError: if *data* is too short for the header or payload.
        """
        header_size = _HEADER.size
        if len(data) - offset < header_size:
            raise ValueError(
                f"Insufficient data for event header: need {header_size} bytes"
            )
        tick, tie_breaker, type_, payload_len = _HEADER.unpack_from(data, offset)
        payload_start = offset + header_size
        payload_end = payload_start + payload_len
        if len(data) < payload_end:
            raise ValueError(
                f"Insufficient data for payload: need {payload_len} bytes"
            )
        return (
            cls(tick=tick, tie_breaker=tie_breaker, type=type_, payload=data[payload_start:payload_end]),
            payload_end - offset,
        )
