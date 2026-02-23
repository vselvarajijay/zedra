#ifndef ZEDRA_CLI_BINARY_LOG_HPP
#define ZEDRA_CLI_BINARY_LOG_HPP

#include <zedra/event.hpp>
#include <iosfwd>
#include <string>
#include <vector>

namespace zedra_cli {

/// Maximum allowed payload size per event (avoid OOM on corrupted input).
constexpr std::uint32_t kMaxPayloadLen = 16 * 1024 * 1024;  // 16 MiB

/// Read binary-format events from stream (little-endian).
/// Format per record: tick (uint64), tie_breaker (uint64), type (uint32),
/// payload_len (uint32), payload (payload_len bytes).
/// On success returns events and empty error. On truncation or invalid data
/// returns partial events (if any) and non-empty error message.
std::pair<std::vector<zedra::Event>, std::string> read_events_binary(
    std::istream& in);

/// Read binary-format events from a contiguous buffer (little-endian).
/// Same format as above. Prefer this when the entire input is already in memory.
std::pair<std::vector<zedra::Event>, std::string> read_events_binary(
    const char* data, std::size_t size);

}  // namespace zedra_cli

#endif  // ZEDRA_CLI_BINARY_LOG_HPP
