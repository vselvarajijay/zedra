#include <zedra/state.hpp>
#include <algorithm>
#include <cstring>

namespace zedra {

namespace {

constexpr std::uint64_t FNV_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t FNV_prime = 1099511628211ULL;

void fnv_mix(std::uint64_t& h, std::uint64_t x) {
  for (int i = 0; i < 8; ++i) {
    h ^= (x & 0xffULL);
    h *= FNV_prime;
    x >>= 8;
  }
}

void fnv_mix_bytes(std::uint64_t& h, const std::byte* p, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i]));
    h *= FNV_prime;
  }
}

}  // namespace

std::uint64_t hash_state(const WorldState& s) {
  std::uint64_t h = FNV_offset_basis;
  fnv_mix(h, s.version());
  for (const auto& [k, v] : s.data()) {
    fnv_mix(h, k);
    fnv_mix_bytes(h, v.value.data(), v.value.size());
    fnv_mix(h, v.last_tick);
  }
  return h;
}

WorldState::WorldState(std::uint64_t version, Map data)
    : version_(version), data_(std::move(data)) {
  std::sort(data_.begin(), data_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  hash_ = hash_state(*this);
}

WorldState WorldState::trim(const WorldState& s, std::uint64_t max_tick, std::uint64_t window_ticks) {
  if (window_ticks == 0) return s;
  const std::uint64_t threshold = max_tick - window_ticks;
  Map kept;
  kept.reserve(s.data().size());
  for (const auto& kv : s.data()) {
    if (kv.second.last_tick >= threshold)
      kept.push_back(kv);
  }
  return WorldState(s.version(), std::move(kept));
}

WorldState WorldState::apply(const WorldState& current, const Event& event) {
  Map next = current.data_;
  if (event.type == 0 && event.payload.size() >= sizeof(WorldState::Key)) {
    WorldState::Key key;
    std::memcpy(&key, event.payload.data(), sizeof(key));
    Value value(event.payload.begin() + sizeof(WorldState::Key), event.payload.end());
    Entry entry{std::move(value), event.tick};
    auto it = std::lower_bound(next.begin(), next.end(), key,
                               [](const auto& p, WorldState::Key k) { return p.first < k; });
    if (it != next.end() && it->first == key) {
      it->second = std::move(entry);
    } else {
      next.insert(it, {key, std::move(entry)});
    }
  }
  return WorldState(current.version() + 1, std::move(next));
}

}  // namespace zedra
