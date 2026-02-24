"""Publisher classes for the Zedra publisher SDK.

:class:`Publisher` is an abstract base; :class:`BinaryFilePublisher`
writes events to any binary file-like object in the format consumed by
``zedra_cli replay``.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import BinaryIO

from .event import Event


class Publisher(ABC):
    """Abstract base class for Zedra event publishers."""

    @abstractmethod
    def publish(self, event: Event) -> bool:
        """Publish *event*.  Returns ``True`` on success, ``False`` otherwise."""
        ...

    def close(self) -> None:
        """Release any resources held by this publisher."""
        pass

    def __enter__(self) -> "Publisher":
        return self

    def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
        self.close()
        return False


class BinaryFilePublisher(Publisher):
    """Publisher that writes events to a binary stream.

    The output is compatible with ``zedra_cli replay`` – events are
    appended in little-endian binary format as they arrive.

    Example::

        with BinaryFilePublisher.open("events.bin") as pub:
            pub.publish(Event(tick=1, tie_breaker=0, type=0, payload=b"hello"))
    """

    def __init__(self, dest: BinaryIO) -> None:
        self._dest = dest

    @classmethod
    def open(cls, path: str) -> "BinaryFilePublisher":
        """Open *path* for writing and return a new :class:`BinaryFilePublisher`."""
        return cls(open(path, "wb"))

    def publish(self, event: Event) -> bool:
        """Serialise and write *event* to the underlying stream.

        Returns:
            ``True`` on success; ``False`` if an :class:`OSError` occurred.
        """
        try:
            self._dest.write(event.to_bytes())
            return True
        except OSError:
            return False

    def close(self) -> None:
        self._dest.close()
