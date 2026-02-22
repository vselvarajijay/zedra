# Logical time or nanosecond timestamp. Upstream is responsible for consistency.
uint64 tick
# Deterministic order when ticks collide (e.g. per-source monotonic counter).
uint64 tie_breaker
# Event discriminator (e.g. 0 = upsert in current zedra_core semantics).
uint32 type
# Serialized event data; interpretation is by type.
uint8[] payload
