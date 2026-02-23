// Writes a binary-format event log: 100 events from 5 sensors, in shuffled order.
// Usage: write_fixture [> sensors.bin]; then: zedra replay sensors.bin
// Format: per event: tick (uint64), tie_breaker (uint64), type (uint32),
//         payload_len (uint32), payload (bytes). Little-endian.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kNumSensors = 5;
constexpr int kTotalEvents = 100;
constexpr unsigned kShuffleSeed = 42u;

void write_le(std::ostream& out, const void* p, std::size_t n) {
  out.write(static_cast<const char*>(p), n);
}

template <typename T>
void write_le(std::ostream& out, T x) {
  write_le(out, &x, sizeof(T));
}

void write_event(std::ostream& out, std::uint64_t tick, std::uint64_t tie_breaker,
                 std::uint32_t type, const void* payload, std::uint32_t len) {
  write_le(out, tick);
  write_le(out, tie_breaker);
  write_le(out, type);
  write_le(out, len);
  if (len > 0) write_le(out, payload, len);
}

struct EventRecord {
  std::uint64_t tick;
  std::uint64_t tie_breaker;
  std::uint64_t key;
  std::string value;
};

}  // namespace

int main() {
  std::ostream& out = std::cout;

  std::vector<EventRecord> events;
  events.reserve(static_cast<std::size_t>(kTotalEvents));
  for (int i = 0; i < kTotalEvents; ++i) {
    std::uint64_t tick = static_cast<std::uint64_t>(i);
    std::uint64_t tie_breaker = tick % kNumSensors;
    std::uint64_t key = tick;
    std::string value = "sensor_" + std::to_string(static_cast<int>(tie_breaker)) +
                       "_" + std::to_string(static_cast<unsigned long>(tick));
    events.push_back({tick, tie_breaker, key, std::move(value)});
  }

  std::shuffle(events.begin(), events.end(), std::mt19937(kShuffleSeed));

  for (const auto& rec : events) {
    std::size_t vlen = rec.value.size();
    std::vector<char> payload(sizeof(rec.key) + vlen);
    std::memcpy(payload.data(), &rec.key, sizeof(rec.key));
    std::memcpy(payload.data() + sizeof(rec.key), rec.value.data(), vlen);
    write_event(out, rec.tick, rec.tie_breaker, 0, payload.data(),
                static_cast<std::uint32_t>(payload.size()));
  }
  return 0;
}
