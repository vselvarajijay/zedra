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
    fnv_mix_bytes(h, v.data(), v.size());
  }
  return h;
}

WorldState::WorldState(std::uint64_t version, Map data)
    : version_(version), data_(std::move(data)) {
  std::sort(data_.begin(), data_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  hash_ = hash_state(*this);
}

WorldState WorldState::apply(const WorldState& current, const Event& event) {
  Map next = current.data_;
  if (event.type == 0 && event.payload.size() >= sizeof(WorldState::Key)) {
    WorldState::Key key;
    std::memcpy(&key, event.payload.data(), sizeof(key));
    Value value(event.payload.begin() + sizeof(WorldState::Key), event.payload.end());
    auto it = std::lower_bound(next.begin(), next.end(), key,
                               [](const auto& p, WorldState::Key k) { return p.first < k; });
    if (it != next.end() && it->first == key) {
      it->second = std::move(value);
    } else {
      next.insert(it, {key, std::move(value)});
    }
  }
  return WorldState(current.version() + 1, std::move(next));
}

}  // namespace zedra
