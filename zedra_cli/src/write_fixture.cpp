// Writes a small binary-format event log (same 3 events as demo) to stdout.
// Usage: write_fixture [> fixtures/sample.bin]
// Format: per event: tick (uint64), tie_breaker (uint64), type (uint32),
//         payload_len (uint32), payload (bytes). Little-endian.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

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

}  // namespace

int main() {
  std::ostream& out = std::cout;
  auto upsert = [&](std::uint64_t tick, std::uint64_t tie_breaker,
                    std::uint64_t key, const char* value) {
    std::size_t vlen = std::strlen(value);
    std::vector<char> payload(sizeof(key) + vlen);
    std::memcpy(payload.data(), &key, sizeof(key));
    std::memcpy(payload.data() + sizeof(key), value, vlen);
    write_event(out, tick, tie_breaker, 0, payload.data(),
               static_cast<std::uint32_t>(payload.size()));
  };

  upsert(0, 0, 1, "hello");
  upsert(1, 0, 2, "world");
  upsert(2, 0, 1, "zedra");
  return 0;
}
