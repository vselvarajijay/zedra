#ifndef ZEDRA_CLI_CLI_OPTIONS_HPP
#define ZEDRA_CLI_CLI_OPTIONS_HPP

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace zedra_cli {

/// Global options parsed before subcommand.
struct GlobalOptions {
  std::size_t queue_capacity{65536};
  std::uint64_t window_ticks{0};
  bool help{false};
};

/// Options for the replay subcommand.
struct ReplayOptions {
  std::string input_path;   // file path or "-" for stdin
  std::string expect_hash;  // optional hex hash (with or without 0x prefix)
  bool show_help{false};    // true if --help was passed for replay
};

/// Print global usage (zedra --help).
void print_global_usage(std::ostream& out);

/// Print demo subcommand usage (zedra demo --help).
void print_demo_usage(std::ostream& out);

/// Print replay subcommand usage (zedra replay --help).
void print_replay_usage(std::ostream& out);

/// Parse global options from argv. Consumes options until subcommand or end.
/// argv[0] is program name; parsing starts at argv[1].
/// Returns (global_options, index of first non-consumed arg, error_message).
/// If error_message is non-empty, parsing failed.
std::tuple<GlobalOptions, int, std::string> parse_global_options(int argc,
                                                                 char const* const* argv);

/// Parse replay-specific options from argv starting at start_index.
/// Expects optional --expect-hash H and positional <input>.
/// Returns (ReplayOptions, error_message). If error_message is non-empty, parsing failed.
std::tuple<ReplayOptions, std::string> parse_replay_options(int argc,
                                                            char const* const* argv,
                                                            int start_index);

/// Parse optional hex hash string (with or without 0x prefix) into uint64_t.
/// Returns true and sets out on success; false on parse error.
bool parse_hex_hash(const std::string& s, std::uint64_t& out);

}  // namespace zedra_cli

#endif  // ZEDRA_CLI_CLI_OPTIONS_HPP
