"""Unit tests for zedra_publisher."""

import io
import struct

import pytest

from zedra_publisher import BinaryFilePublisher, Event


# ---------------------------------------------------------------------------
# Event – construction and ordering
# ---------------------------------------------------------------------------


def test_event_defaults():
    e = Event(tick=1, tie_breaker=2, type=3)
    assert e.tick == 1
    assert e.tie_breaker == 2
    assert e.type == 3
    assert e.payload == bytes()


def test_event_with_payload():
    e = Event(tick=10, tie_breaker=0, type=1, payload=b"hello")
    assert e.payload == b"hello"


def test_event_ordering_by_tick():
    e1 = Event(tick=1, tie_breaker=0, type=0)
    e2 = Event(tick=2, tie_breaker=0, type=0)
    assert e1 < e2
    assert not e2 < e1


def test_event_ordering_by_tie_breaker():
    e1 = Event(tick=5, tie_breaker=0, type=0)
    e2 = Event(tick=5, tie_breaker=1, type=0)
    assert e1 < e2
    assert not e2 < e1


def test_event_equal_is_not_less():
    e = Event(tick=3, tie_breaker=3, type=0)
    assert not e < e


# ---------------------------------------------------------------------------
# Event – binary round-trip
# ---------------------------------------------------------------------------


def _header_fmt():
    return struct.Struct("<QQII")


def test_to_bytes_header_fields():
    e = Event(tick=42, tie_breaker=7, type=2, payload=b"ab")
    raw = e.to_bytes()
    hdr = _header_fmt()
    tick, tb, ty, plen = hdr.unpack_from(raw, 0)
    assert tick == 42
    assert tb == 7
    assert ty == 2
    assert plen == 2
    assert raw[hdr.size:] == b"ab"


def test_to_bytes_empty_payload():
    e = Event(tick=0, tie_breaker=0, type=0)
    raw = e.to_bytes()
    assert len(raw) == _header_fmt().size
    _, _, _, plen = _header_fmt().unpack_from(raw, 0)
    assert plen == 0


def test_from_bytes_round_trip():
    original = Event(tick=99, tie_breaker=3, type=1, payload=b"world")
    raw = original.to_bytes()
    restored, consumed = Event.from_bytes(raw)
    assert consumed == len(raw)
    assert restored.tick == original.tick
    assert restored.tie_breaker == original.tie_breaker
    assert restored.type == original.type
    assert restored.payload == original.payload


def test_from_bytes_with_offset():
    prefix = b"\x00" * 4
    e = Event(tick=1, tie_breaker=0, type=0, payload=b"x")
    raw = prefix + e.to_bytes()
    restored, consumed = Event.from_bytes(raw, offset=4)
    assert consumed == e.to_bytes().__len__()
    assert restored.tick == 1
    assert restored.payload == b"x"


def test_from_bytes_multiple_events():
    events = [
        Event(tick=i, tie_breaker=0, type=0, payload=bytes([i]))
        for i in range(5)
    ]
    buf = b"".join(e.to_bytes() for e in events)
    offset = 0
    restored = []
    while offset < len(buf):
        ev, consumed = Event.from_bytes(buf, offset)
        restored.append(ev)
        offset += consumed
    assert len(restored) == 5
    for orig, rest in zip(events, restored):
        assert orig.tick == rest.tick
        assert orig.payload == rest.payload


def test_from_bytes_truncated_header_raises():
    with pytest.raises(ValueError, match="header"):
        Event.from_bytes(b"\x00\x01\x02")


def test_from_bytes_truncated_payload_raises():
    e = Event(tick=1, tie_breaker=0, type=0, payload=b"hello")
    raw = e.to_bytes()[:-2]  # cut off last 2 bytes of payload
    with pytest.raises(ValueError, match="payload"):
        Event.from_bytes(raw)


# ---------------------------------------------------------------------------
# BinaryFilePublisher
# ---------------------------------------------------------------------------


def test_binary_file_publisher_write_single_event():
    buf = io.BytesIO()
    pub = BinaryFilePublisher(buf)
    e = Event(tick=1, tie_breaker=0, type=0, payload=b"test")
    assert pub.publish(e) is True
    buf.seek(0)
    restored, _ = Event.from_bytes(buf.read())
    assert restored.tick == 1
    assert restored.payload == b"test"


def test_binary_file_publisher_write_multiple_events():
    buf = io.BytesIO()
    events = [Event(tick=i, tie_breaker=0, type=0) for i in range(10)]
    pub = BinaryFilePublisher(buf)
    for e in events:
        assert pub.publish(e) is True
    buf.seek(0)
    raw = buf.read()
    offset, restored = 0, []
    while offset < len(raw):
        ev, n = Event.from_bytes(raw, offset)
        restored.append(ev)
        offset += n
    assert [e.tick for e in restored] == list(range(10))


def test_binary_file_publisher_context_manager(tmp_path):
    path = str(tmp_path / "events.bin")
    with BinaryFilePublisher.open(path) as pub:
        pub.publish(Event(tick=5, tie_breaker=1, type=2, payload=b"ctx"))
    with open(path, "rb") as f:
        raw = f.read()
    restored, _ = Event.from_bytes(raw)
    assert restored.tick == 5
    assert restored.tie_breaker == 1
    assert restored.type == 2
    assert restored.payload == b"ctx"
