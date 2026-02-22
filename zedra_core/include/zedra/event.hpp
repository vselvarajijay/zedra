#ifndef ZEDRA_EVENT_HPP
#define ZEDRA_EVENT_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

namespace zedra {

/// Immutable event for deterministic ordering.
/// Ordered by (tick, tie_breaker); same event log yields same hash for replay.
struct Event {
  std::uint64_t tick{0};
  std::uint64_t tie_breaker{0};
  std::uint32_t type{0};
  std::vector<std::byte> payload;

  Event() = default;
  Event(std::uint64_t t, std::uint64_t tb, std::uint32_t ty, std::vector<std::byte> p = {})
      : tick(t), tie_breaker(tb), type(ty), payload(std::move(p)) {}

  bool operator<(const Event& other) const {
    if (tick != other.tick) return tick < other.tick;
    return tie_breaker < other.tie_breaker;
  }
};

/// Deterministic FNV-1a 64-bit hash of an event (for replay and ordering).
inline std::uint64_t hash_event(const Event& e) {
  constexpr std::uint64_t FNV_offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t FNV_prime = 1099511628211ULL;
  auto h = FNV_offset_basis;
  auto mix = [&h](std::uint64_t x) {
    for (int i = 0; i < 8; ++i) {
      h ^= (x & 0xffULL);
      h *= FNV_prime;
      x >>= 8;
    }
  };
  mix(e.tick);
  mix(e.tie_breaker);
  mix(static_cast<std::uint64_t>(e.type));
  for (std::byte b : e.payload) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(b));
    h *= FNV_prime;
  }
  return h;
}

}  // namespace zedra

#endif  // ZEDRA_EVENT_HPP
