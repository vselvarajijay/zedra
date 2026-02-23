#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(SlidingWindow, name)
#endif

namespace {

zedra::Event make_upsert(std::uint64_t tick, std::uint64_t key, const char* value) {
  std::size_t len = std::strlen(value);
  std::vector<std::byte> payload(sizeof(key) + len);
  std::memcpy(payload.data(), &key, sizeof(key));
  if (len) std::memcpy(payload.data() + sizeof(key), value, len);
  return zedra::Event(tick, 0, 0, std::move(payload));
}

void Reducer_TrimEvictsKeysOutsideWindow() {
  const std::uint64_t window_ticks = 20;
  zedra::Reducer r2(1024, nullptr, window_ticks);
  r2.start();
  r2.submit(make_upsert(10, 1, "early"));
  r2.submit(make_upsert(50, 2, "mid"));
  r2.submit(make_upsert(90, 3, "late"));
  r2.submit(make_upsert(200, 4, "now"));

  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = red.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out");
  };
  wait_version(r2, 4);

  auto s = r2.get_snapshot();
  r2.stop();
  assert(s);
  assert(s->version() == 4);
  assert(s->data().size() == 1);
  assert(s->data()[0].first == 4 && "only key 4 (last_tick 200) in window; 1,2,3 evicted");
}

void Reducer_SameEventStreamSameSnapshot() {
  const std::uint64_t window_ticks = 15;
  std::vector<zedra::Event> events = {
      make_upsert(0, 1, "a"),
      make_upsert(10, 2, "b"),
      make_upsert(20, 1, "A"),
      make_upsert(30, 3, "c"),
  };

  zedra::Reducer r1(256, nullptr, window_ticks);
  zedra::Reducer r2(256, nullptr, window_ticks);
  r1.start();
  r2.start();
  for (const auto& e : events) {
    assert(r1.submit(e));
    assert(r2.submit(e));
  }

  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = red.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out");
  };
  wait_version(r1, 4);
  wait_version(r2, 4);

  auto s1 = r1.get_snapshot();
  auto s2 = r2.get_snapshot();
  r1.stop();
  r2.stop();
  assert(s1 && s2);
  assert(s1->version() == s2->version());
  assert(s1->hash() == s2->hash());
  assert(s1->data().size() == s2->data().size());
}

}  // namespace

void run_sliding_window_tests() {
  RUN_TEST(Reducer_TrimEvictsKeysOutsideWindow);
  RUN_TEST(Reducer_SameEventStreamSameSnapshot);
}
