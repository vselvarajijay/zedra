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
#define RUN_TEST(name) TEST(Replay, name)
#endif

namespace {

zedra::Event make_upsert_event(std::uint64_t tick, std::uint64_t tie_breaker,
                              std::uint64_t key, const void* value, std::size_t value_len) {
  std::vector<std::byte> payload(sizeof(key) + value_len);
  std::memcpy(payload.data(), &key, sizeof(key));
  if (value_len) std::memcpy(payload.data() + sizeof(key), value, value_len);
  return zedra::Event(tick, tie_breaker, 0, std::move(payload));
}

void SameEventSequenceProducesSameHash() {
  std::vector<zedra::Event> events = {
      make_upsert_event(0, 0, 1, "a", 2),
      make_upsert_event(1, 0, 2, "b", 2),
      make_upsert_event(1, 1, 1, "A", 2),
      make_upsert_event(2, 0, 3, "c", 2),
  };

  zedra::Reducer r1(256);
  zedra::Reducer r2(256);
  r1.start();
  r2.start();

  for (const auto& e : events) {
    assert(r1.submit(e) && "submit r1");
    assert(r2.submit(e) && "submit r2");
  }

  auto wait_version = [](zedra::Reducer& r, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto s = r.get_snapshot();
      if (s && s->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out waiting for version");
  };
  wait_version(r1, 4);
  wait_version(r2, 4);

  auto s1 = r1.get_snapshot();
  auto s2 = r2.get_snapshot();
  r1.stop();
  r2.stop();

  assert(s1 && s2);
  assert(s1->version() == s2->version());
  assert(s1->hash() == s2->hash() && "replay must produce identical hash");
}

void EventOrderDeterminesState() {
  zedra::Reducer r(256);
  r.start();
  r.submit(make_upsert_event(0, 0, 1, "first", 5));
  r.submit(make_upsert_event(1, 0, 1, "second", 6));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto s = r.get_snapshot();
  r.stop();
  assert(s);
  assert(s->version() == 2);
  assert(s->data().size() == 1);
  assert(s->data()[0].first == 1);
  std::string val(s->data()[0].second.size(), '\0');
  for (size_t i = 0; i < s->data()[0].second.size(); ++i)
    val[i] = static_cast<char>(static_cast<unsigned char>(s->data()[0].second[i]));
  assert(val == "second");
}

void ReducerLifecycle_SnapshotBeforeStart() {
  zedra::Reducer r(256);
  auto s = r.get_snapshot();
  assert(s);
  assert(s->version() == 0);
  assert(s->data().empty());
}

void ReducerLifecycle_SnapshotAdvancesAfterStartAndSubmits() {
  zedra::Reducer r(256);
  r.start();
  r.submit(make_upsert_event(0, 0, 1, "a", 1));
  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto snap = red.get_snapshot();
      if (snap && snap->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out waiting for version");
  };
  wait_version(r, 1);
  auto s = r.get_snapshot();
  r.stop();
  assert(s);
  assert(s->version() >= 1);
  assert(s->data().size() == 1);
}

void ReducerLifecycle_SnapshotReadableAfterStop() {
  zedra::Reducer r(256);
  r.start();
  r.submit(make_upsert_event(0, 0, 1, "final", 5));
  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto snap = red.get_snapshot();
      if (snap && snap->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out waiting for version");
  };
  wait_version(r, 1);
  r.stop();
  auto s = r.get_snapshot();
  assert(s);
  assert(s->version() == 1);
  assert(s->data().size() == 1);
  std::string val(s->data()[0].second.size(), '\0');
  for (size_t i = 0; i < s->data()[0].second.size(); ++i)
    val[i] = static_cast<char>(static_cast<unsigned char>(s->data()[0].second[i]));
  assert(val == "final");
}

void BatchOrdering_SameTickDifferentTieBreakerDeterministic() {
  std::vector<zedra::Event> events = {
      make_upsert_event(1, 0, 1, "first", 5),
      make_upsert_event(1, 1, 1, "second", 6),
  };
  zedra::Reducer r(256);
  r.start();
  for (const auto& e : events)
    assert(r.submit(e));
  auto wait_version = [](zedra::Reducer& red, std::uint64_t version) {
    for (int i = 0; i < 500; ++i) {
      auto snap = red.get_snapshot();
      if (snap && snap->version() >= version) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false && "timed out waiting for version");
  };
  wait_version(r, 2);
  auto s = r.get_snapshot();
  r.stop();
  assert(s);
  assert(s->version() == 2);
  assert(s->data().size() == 1);
  std::string val(s->data()[0].second.size(), '\0');
  for (size_t i = 0; i < s->data()[0].second.size(); ++i)
    val[i] = static_cast<char>(static_cast<unsigned char>(s->data()[0].second[i]));
  assert(val == "second" && "reducer sorts by (tick, tie_breaker), so (1,1) wins over (1,0)");
}

}  // namespace

void run_replay_tests() {
  RUN_TEST(SameEventSequenceProducesSameHash);
  RUN_TEST(EventOrderDeterminesState);
  RUN_TEST(ReducerLifecycle_SnapshotBeforeStart);
  RUN_TEST(ReducerLifecycle_SnapshotAdvancesAfterStartAndSubmits);
  RUN_TEST(ReducerLifecycle_SnapshotReadableAfterStop);
  RUN_TEST(BatchOrdering_SameTickDifferentTieBreakerDeterministic);
}

