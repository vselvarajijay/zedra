#ifndef ZEDRA_STATE_HPP
#define ZEDRA_STATE_HPP

#include <zedra/event.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

namespace zedra {

/// Immutable world-state snapshot. Deterministic iteration (sorted by key).
class WorldState {
 public:
  using Key = std::uint64_t;
  using Value = std::vector<std::byte>;
  struct Entry {
    Value value;
    std::uint64_t last_tick{0};
  };
  using Map = std::vector<std::pair<Key, Entry>>;  // sorted by Key

  WorldState() = default;
  WorldState(std::uint64_t version, Map data);

  std::uint64_t version() const { return version_; }
  /// Deterministic 64-bit hash of this snapshot.
  std::uint64_t hash() const { return hash_; }
  const Map& data() const { return data_; }

  /// Apply one event to produce a new WorldState. Deterministic.
  static WorldState apply(const WorldState& current, const Event& event);

  /// Trim keys with last_tick < max_tick - window_ticks. Returns s unchanged if window_ticks == 0.
  static WorldState trim(const WorldState& s, std::uint64_t max_tick, std::uint64_t window_ticks);

 private:
  std::uint64_t version_{0};
  std::uint64_t hash_{0};
  Map data_;
};

/// Compute deterministic FNV-1a hash of a WorldState (version + sorted data).
std::uint64_t hash_state(const WorldState& s);

}  // namespace zedra

#endif  // ZEDRA_STATE_HPP
