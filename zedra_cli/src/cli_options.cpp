#include "cli_options.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <tuple>

namespace zedra_cli {

namespace {

bool parse_uint64(const char* s, std::uint64_t& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  unsigned long long v = std::strtoull(s, &end, 10);
  if (end == s || *end != '\0') return false;
  out = static_cast<std::uint64_t>(v);
  return true;
}

bool parse_size_t(const char* s, std::size_t& out) {
  std::uint64_t u = 0;
  if (!parse_uint64(s, u)) return false;
  out = static_cast<std::size_t>(u);
  return true;
}

}  // namespace

void print_global_usage(std::ostream& out) {
  out << "Usage: zedra [global-opts] <subcommand> [args]\n\n"
      << "Global options:\n"
      << "  --queue-capacity N   Ingestion queue capacity (default: 65536)\n"
      << "  --window-ticks N     Sliding window ticks; 0 = no trim (default: 0)\n"
      << "  -h, --help           Show help\n\n"
      << "Subcommands:\n"
      << "  demo                 Run built-in demo scenario\n"
      << "  replay               Replay event log from file or stdin\n\n"
      << "See 'zedra <subcommand> --help' for subcommand-specific options.\n";
}

void print_demo_usage(std::ostream& out) {
  out << "Usage: zedra demo [global-opts]\n\n"
      << "Run the built-in demo scenario. Prints snapshot version and hash.\n\n"
      << "Global options (apply before 'demo'):\n"
      << "  --queue-capacity N   Ingestion queue capacity (default: 65536)\n"
      << "  --window-ticks N     Sliding window ticks; 0 = no trim (default: 0)\n"
      << "  -h, --help           Show this help\n";
}

void print_replay_usage(std::ostream& out) {
  out << "Usage: zedra replay [options] <input>\n\n"
      << "Replay a binary-format event log. Prints final snapshot version and hash.\n\n"
      << "Arguments:\n"
      << "  <input>              Path to event log file, or '-' for stdin\n\n"
      << "Options:\n"
      << "  --expect-hash H      Expected state hash (hex); exit non-zero if mismatch\n\n"
      << "Global options (apply before 'replay'):\n"
      << "  --queue-capacity N   Ingestion queue capacity (default: 65536)\n"
      << "  --window-ticks N     Sliding window ticks; 0 = no trim (default: 0)\n"
      << "  -h, --help           Show this help\n";
}

std::tuple<GlobalOptions, int, std::string> parse_global_options(int argc,
                                                                 char const* const* argv) {
  GlobalOptions opts;
  int i = 1;
  while (i < argc) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      opts.help = true;
      ++i;
      continue;
    }
    if (arg == "--queue-capacity") {
      if (i + 1 >= argc) return {opts, i, "missing value for --queue-capacity"};
      std::size_t val = 0;
      if (!parse_size_t(argv[i + 1], val) || val == 0)
        return {opts, i, "invalid --queue-capacity (must be positive integer)"};
      opts.queue_capacity = val;
      i += 2;
      continue;
    }
    if (arg == "--window-ticks") {
      if (i + 1 >= argc) return {opts, i, "missing value for --window-ticks"};
      std::uint64_t val = 0;
      if (!parse_uint64(argv[i + 1], val))
        return {opts, i, "invalid --window-ticks (must be non-negative integer)"};
      opts.window_ticks = val;
      i += 2;
      continue;
    }
    if (arg.size() >= 2 && arg[0] == '-') {
      return {opts, i, "unknown option: " + arg};
    }
    break;
  }
  return {opts, i, ""};
}

std::tuple<ReplayOptions, std::string> parse_replay_options(int argc,
                                                            char const* const* argv,
                                                            int start_index) {
  ReplayOptions opts;
  int i = start_index;
  while (i < argc) {
    std::string arg = argv[i];
    if (arg == "--expect-hash") {
      if (i + 1 >= argc) return {opts, "missing value for --expect-hash"};
      opts.expect_hash = argv[i + 1];
      i += 2;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      opts.show_help = true;
      return {opts, ""};
    }
    if (arg == "-" || (arg.size() >= 1 && arg[0] != '-')) {
      if (!opts.input_path.empty()) return {opts, "replay expects exactly one <input>"};
      opts.input_path = arg;
      ++i;
      continue;
    }
    return {opts, "unknown option: " + arg};
  }
  if (opts.input_path.empty()) return {opts, "replay requires <input> (file path or '-')"};
  return {opts, ""};
}

bool parse_hex_hash(const std::string& s, std::uint64_t& out) {
  if (s.empty()) return false;
  const char* p = s.c_str();
  if (s.size() >= 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
    p += 2;
  if (!*p) return false;
  char* end = nullptr;
  unsigned long long v = std::strtoull(p, &end, 16);
  if (end == p || *end != '\0') return false;
  out = static_cast<std::uint64_t>(v);
  return true;
}

}  // namespace zedra_cli
