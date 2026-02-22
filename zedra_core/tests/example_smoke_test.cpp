#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(ExampleSmoke, name)
#endif

namespace {

zedra::Event upsert(std::uint64_t tick, std::uint64_t key, const char* value) {
  std::size_t len = std::strlen(value);
  std::vector<std::byte> payload(sizeof(key) + len);
  std::memcpy(payload.data(), &key, sizeof(key));
  std::memcpy(payload.data() + sizeof(key), value, len);
  return zedra::Event(tick, 0, 0, std::move(payload));
}

std::string value_to_string(const std::vector<std::byte>& v) {
  std::string s(v.size(), '\0');
  for (size_t i = 0; i < v.size(); ++i)
    s[i] = static_cast<char>(static_cast<unsigned char>(v[i]));
  return s;
}

void ExampleMainFlow_SnapshotVersionAndContent() {
  zedra::Reducer reducer(1024);
  reducer.start();

  reducer.submit(upsert(0, 1, "hello"));
  reducer.submit(upsert(1, 2, "world"));
  reducer.submit(upsert(2, 1, "zedra"));

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto snapshot = reducer.get_snapshot();
  reducer.stop();

  assert(snapshot);
  assert(snapshot->version() == 3);
  assert(snapshot->data().size() == 2);

  std::uint64_t key1_val = 0;
  std::uint64_t key2_val = 0;
  for (const auto& [k, v] : snapshot->data()) {
    if (k == 1) key1_val = k;
    if (k == 2) key2_val = k;
  }
  assert(key1_val == 1);
  assert(key2_val == 2);

  std::string v1, v2;
  for (const auto& [k, v] : snapshot->data()) {
    if (k == 1) v1 = value_to_string(v);
    if (k == 2) v2 = value_to_string(v);
  }
  assert(v1 == "zedra");
  assert(v2 == "world");
}

void ExampleMainFlow_HashStableAcrossRuns() {
  std::vector<zedra::Event> events = {
      upsert(0, 1, "hello"),
      upsert(1, 2, "world"),
      upsert(2, 1, "zedra"),
  };
  zedra::Reducer r1(1024), r2(1024);
  r1.start();
  r2.start();
  for (const auto& e : events) {
    r1.submit(e);
    r2.submit(e);
  }
  auto wait_version = [](zedra::Reducer& r, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = r.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out");
  };
  wait_version(r1, 3);
  wait_version(r2, 3);
  std::uint64_t h1 = r1.get_snapshot()->hash();
  std::uint64_t h2 = r2.get_snapshot()->hash();
  r1.stop();
  r2.stop();
  assert(h1 == h2);
}

}  // namespace

void run_example_smoke_tests() {
  RUN_TEST(ExampleMainFlow_SnapshotVersionAndContent);
  RUN_TEST(ExampleMainFlow_HashStableAcrossRuns);
}
