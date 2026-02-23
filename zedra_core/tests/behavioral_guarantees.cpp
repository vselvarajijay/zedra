#include <zedra/event.hpp>
#include <zedra/lock_free_queue.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(BehavioralGuarantees, name)
#endif

namespace {

zedra::Event make_upsert(std::uint64_t tick, std::uint64_t tie_breaker,
                        std::uint64_t key, const void* value, std::size_t value_len) {
  std::vector<std::byte> payload(sizeof(key) + value_len);
  std::memcpy(payload.data(), &key, sizeof(key));
  if (value_len) std::memcpy(payload.data() + sizeof(key), value, value_len);
  return zedra::Event(tick, tie_breaker, 0, std::move(payload));
}

void ReplayParity_IdenticalLogIdenticalHash() {
  std::vector<zedra::Event> log = {
      make_upsert(0, 0, 1, "x", 1),
      make_upsert(1, 0, 2, "y", 1),
      make_upsert(2, 0, 1, "z", 1),
  };
  zedra::Reducer r1(256), r2(256);
  r1.start();
  r2.start();
  for (const auto& e : log) {
    assert(r1.submit(e));
    assert(r2.submit(e));
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
  auto s1 = r1.get_snapshot();
  auto s2 = r2.get_snapshot();
  r1.stop();
  r2.stop();
  assert(s1->hash() == s2->hash());
  assert(s1->version() == s2->version());
  assert(s1->data().size() == s2->data().size());
}

void ReplayParity_IdenticalEvolutionPath() {
  std::vector<zedra::Event> log = {
      make_upsert(0, 0, 10, "a", 1),
      make_upsert(1, 0, 20, "b", 1),
  };
  zedra::Reducer r1(256), r2(256);
  r1.start();
  r2.start();
  for (const auto& e : log) {
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
  wait_version(r1, 2);
  wait_version(r2, 2);
  auto s1 = r1.get_snapshot();
  auto s2 = r2.get_snapshot();
  r1.stop();
  r2.stop();
  assert(s1->data().size() == 2);
  assert(s2->data().size() == 2);
  assert(s1->data()[0].first == s2->data()[0].first);
  assert(s1->data()[1].first == s2->data()[1].first);
  assert(s1->data()[0].second.size() == s2->data()[0].second.size());
  assert(s1->data()[1].second.size() == s2->data()[1].second.size());
}

void ImmutableSnapshots_ReadersGetConstOnly() {
  zedra::Reducer r(256);
  r.start();
  r.submit(make_upsert(0, 0, 1, "a", 1));
  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = red.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out");
  };
  wait_version(r, 1);
  std::shared_ptr<const zedra::WorldState> snap = r.get_snapshot();
  r.stop();
  assert(snap);
  (void)snap->version();
  (void)snap->hash();
  (void)snap->data();
  assert(snap->data().size() == 1);
}

void QueueFull_SubmitReturnsFalseThenSucceedsAfterDrain() {
  zedra::Reducer r(4);
  // Fill queue before starting reducer so we don't race with the drain loop.
  std::size_t submitted = 0;
  for (; submitted < 4; ++submitted)
    assert(r.submit(make_upsert(static_cast<std::uint64_t>(submitted), 0, 1, "x", 1)));
  assert(submitted == 4);
  assert(!r.submit(make_upsert(99, 0, 1, "y", 1)));
  r.start();
  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = red.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out");
  };
  wait_version(r, 4);
  bool extra = r.submit(make_upsert(100, 0, 2, "z", 1));
  if (!extra) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    extra = r.submit(make_upsert(100, 0, 2, "z", 1));
  }
  assert(extra);
  wait_version(r, 5);
  auto s = r.get_snapshot();
  r.stop();
  assert(s);
  assert(s->version() == 5);
}

}  // namespace

void run_behavioral_guarantees_tests() {
  RUN_TEST(ReplayParity_IdenticalLogIdenticalHash);
  RUN_TEST(ReplayParity_IdenticalEvolutionPath);
  RUN_TEST(ImmutableSnapshots_ReadersGetConstOnly);
  RUN_TEST(QueueFull_SubmitReturnsFalseThenSucceedsAfterDrain);
}
