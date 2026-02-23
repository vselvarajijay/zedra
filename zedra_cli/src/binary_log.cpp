#include "binary_log.hpp"
#include <cstring>
#include <istream>
#include <utility>

namespace zedra_cli {

namespace {

inline bool read_le(std::istream& in, void* buf, std::size_t n) {
  return static_cast<bool>(in.read(static_cast<char*>(buf), n)) &&
         in.gcount() == static_cast<std::streamsize>(n);
}

template <typename T>
bool read_le(std::istream& in, T& out) {
  return read_le(in, &out, sizeof(T));
}

}  // namespace

std::pair<std::vector<zedra::Event>, std::string> read_events_binary(
    std::istream& in) {
  std::vector<zedra::Event> events;
  while (in) {
    std::uint64_t tick = 0;
    std::uint64_t tie_breaker = 0;
    std::uint32_t type = 0;
    std::uint32_t payload_len = 0;
    if (!read_le(in, tick)) {
      if (events.empty() && in.eof() && in.gcount() == 0)
        return {events, ""};  // empty file
      if (events.empty()) return {{}, "failed to read event header"};
      return {events, "truncated event record (header)"};
    }
    if (!read_le(in, tie_breaker))
      return {events, "truncated event record (tie_breaker)"};
    if (!read_le(in, type)) return {events, "truncated event record (type)"};
    if (!read_le(in, payload_len))
      return {events, "truncated event record (payload_len)"};
    if (payload_len > kMaxPayloadLen)
      return {events,
              "payload_len " + std::to_string(payload_len) +
                  " exceeds maximum " + std::to_string(kMaxPayloadLen)};
    std::vector<std::byte> payload(payload_len);
    if (payload_len > 0 && !read_le(in, payload.data(), payload_len))
      return {events, "truncated event record (payload)"};
    events.push_back(
        zedra::Event(tick, tie_breaker, type, std::move(payload)));
  }
  return {events, ""};
}

namespace {

inline std::uint64_t load_le64(const char* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
inline std::uint32_t load_le32(const char* p) {
  std::uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

}  // namespace

std::pair<std::vector<zedra::Event>, std::string> read_events_binary(
    const char* data, std::size_t size) {
  std::vector<zedra::Event> events;
  std::size_t pos = 0;
  while (pos + 24 <= size) {
    std::uint64_t tick = load_le64(data + pos);
    std::uint64_t tie_breaker = load_le64(data + pos + 8);
    std::uint32_t type = load_le32(data + pos + 16);
    std::uint32_t payload_len = load_le32(data + pos + 20);
    pos += 24;
    if (payload_len > kMaxPayloadLen)
      return {events,
              "payload_len " + std::to_string(payload_len) +
                  " exceeds maximum " + std::to_string(kMaxPayloadLen)};
    if (pos + payload_len > size)
      return {events, "truncated event record (payload)"};
    std::vector<std::byte> payload(payload_len);
    if (payload_len > 0)
      std::memcpy(payload.data(), data + pos, payload_len);
    pos += payload_len;
    events.push_back(
        zedra::Event(tick, tie_breaker, type, std::move(payload)));
  }
  if (pos != size && events.empty())
    return {{}, "failed to read event header"};
  return {events, ""};
}

}  // namespace zedra_cli
