#include "binary_log.hpp"
#include "cli_options.hpp"
#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

zedra::Event make_upsert(std::uint64_t tick, std::uint64_t tie_breaker,
                        std::uint64_t key, const char* value) {
  std::size_t len = std::strlen(value);
  std::vector<std::byte> payload(sizeof(key) + len);
  std::memcpy(payload.data(), &key, sizeof(key));
  std::memcpy(payload.data() + sizeof(key), value, len);
  return zedra::Event(tick, tie_breaker, 0, std::move(payload));
}

int run_demo(const zedra_cli::GlobalOptions& global_opts) {
  zedra::Reducer reducer(static_cast<std::size_t>(global_opts.queue_capacity),
                         nullptr, global_opts.window_ticks);
  reducer.start();

  std::vector<zedra::Event> events = {
      make_upsert(0, 0, 1, "hello"),
      make_upsert(1, 0, 2, "world"),
      make_upsert(2, 0, 1, "zedra"),
  };

  for (const auto& e : events) {
    if (!reducer.submit(e)) {
      std::cerr << "zedra demo: queue full\n";
      reducer.stop();
      return 1;
    }
  }

  const std::uint64_t expected_version = static_cast<std::uint64_t>(events.size());
  const int max_wait_ms = 1000;
  const int poll_interval_ms = 2;
  int waited = 0;
  while (waited < max_wait_ms) {
    auto snap = reducer.get_snapshot();
    if (snap && snap->version() >= expected_version) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    waited += poll_interval_ms;
  }

  auto snapshot = reducer.get_snapshot();
  reducer.stop();

  if (!snapshot || snapshot->version() < expected_version) {
    std::cerr << "zedra demo: timeout waiting for events to be applied\n";
    return 1;
  }

  std::cout << "version=" << snapshot->version()
            << " hash=0x" << std::hex << snapshot->hash() << std::dec << "\n";
  std::cout << "keys=" << snapshot->data().size() << "\n";
  for (const auto& [k, v] : snapshot->data()) {
    std::string s(v.value.size(), '\0');
    for (size_t i = 0; i < v.value.size(); ++i)
      s[i] = static_cast<char>(static_cast<unsigned char>(v.value[i]));
    std::cout << "  key=" << k << " value=\"" << s << "\"\n";
  }
  return 0;
}

int run_replay(const zedra_cli::GlobalOptions& global_opts,
               const zedra_cli::ReplayOptions& replay_opts) {
  std::istream* in_ptr = nullptr;
  std::ifstream file;
  if (replay_opts.input_path == "-") {
    in_ptr = &std::cin;
  } else {
    file.open(replay_opts.input_path, std::ios::binary);
    if (!file) {
      std::cerr << "zedra replay: cannot open input '" << replay_opts.input_path
                << "'\n";
      return 1;
    }
    in_ptr = &file;
  }

  // Read entire input into memory then parse from buffer (reliable across
  // platforms; avoids istream behavior after partial read).
  std::vector<char> buf(
      (std::istreambuf_iterator<char>(*in_ptr)),
      std::istreambuf_iterator<char>());
  if (in_ptr->fail() && !in_ptr->eof()) {
    std::cerr << "zedra replay: read error\n";
    return 1;
  }
  auto [events, read_error] =
      zedra_cli::read_events_binary(buf.data(), buf.size());
  if (!read_error.empty()) {
    std::cerr << "zedra replay: " << read_error << "\n";
    return 1;
  }

  zedra::Reducer reducer(static_cast<std::size_t>(global_opts.queue_capacity),
                         nullptr, global_opts.window_ticks);
  reducer.start();

  for (const auto& e : events) {
    if (!reducer.submit(e)) {
      std::cerr << "zedra replay: queue full\n";
      reducer.stop();
      return 1;
    }
  }

  const std::uint64_t expected_version = static_cast<std::uint64_t>(events.size());
  const int max_wait_ms = 1000;
  const int poll_interval_ms = 2;
  int waited = 0;
  while (waited < max_wait_ms) {
    auto snap = reducer.get_snapshot();
    if (snap && snap->version() >= expected_version) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    waited += poll_interval_ms;
  }

  auto snapshot = reducer.get_snapshot();
  if (!snapshot || snapshot->version() < expected_version) {
    std::cerr << "zedra replay: timeout waiting for events to be applied\n";
    reducer.stop();
    return 1;
  }

  if (!replay_opts.expect_hash.empty()) {
    std::uint64_t expected = 0;
    if (!zedra_cli::parse_hex_hash(replay_opts.expect_hash, expected)) {
      std::cerr << "zedra replay: invalid --expect-hash value\n";
      reducer.stop();
      return 1;
    }
    if (snapshot->hash() != expected) {
      std::cerr << "zedra replay: hash mismatch (got 0x" << std::hex
                << snapshot->hash() << ", expected 0x" << expected << std::dec
                << ")\n";
      reducer.stop();
      return 1;
    }
  }

  auto stats = reducer.get_stats();
  reducer.stop();

  std::cout << "version=" << snapshot->version()
            << " hash=0x" << std::hex << snapshot->hash() << std::dec << "\n";
  std::cout << "events_applied=" << stats.events_applied
            << " first_tick=" << stats.first_tick
            << " last_tick=" << stats.last_tick
            << " window_ticks=" << stats.window_ticks << "\n";
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 1) return 1;

  auto [global_opts, next_index, parse_error] =
      zedra_cli::parse_global_options(argc, argv);
  if (!parse_error.empty()) {
    std::cerr << "zedra: " << parse_error << "\n";
    return 1;
  }

  if (next_index >= argc) {
    if (global_opts.help) {
      zedra_cli::print_global_usage(std::cout);
      return 0;
    }
    std::cerr << "zedra: missing subcommand. Use 'zedra --help' for usage.\n";
    return 1;
  }

  std::string subcommand = argv[next_index];
  int sub_start = next_index + 1;

  if (subcommand == "demo") {
    if (global_opts.help) {
      zedra_cli::print_global_usage(std::cout);
      return 0;
    }
    if (sub_start < argc && std::string(argv[sub_start]) == "--help") {
      zedra_cli::print_demo_usage(std::cout);
      return 0;
    }
    return run_demo(global_opts);
  }

  if (subcommand == "replay") {
    if (global_opts.help) {
      zedra_cli::print_global_usage(std::cout);
      return 0;
    }
    auto [replay_opts, replay_error] =
        zedra_cli::parse_replay_options(argc, argv, sub_start);
    if (replay_opts.show_help) {
      zedra_cli::print_replay_usage(std::cout);
      return 0;
    }
    if (!replay_error.empty()) {
      std::cerr << "zedra replay: " << replay_error << "\n";
      return 1;
    }
    return run_replay(global_opts, replay_opts);
  }

  if (global_opts.help) {
    zedra_cli::print_global_usage(std::cout);
    return 0;
  }
  std::cerr << "zedra: unknown subcommand '" << subcommand
            << "'. Use 'zedra --help' for usage.\n";
  return 1;
}
