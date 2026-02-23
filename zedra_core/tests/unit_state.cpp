#include <zedra/event.hpp>
#include <zedra/state.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(UnitState, name)
#endif

namespace {

zedra::Event make_upsert(std::uint64_t tick, std::uint64_t tie_breaker,
                        std::uint64_t key, const void* value, std::size_t value_len) {
  std::vector<std::byte> payload(sizeof(key) + value_len);
  std::memcpy(payload.data(), &key, sizeof(key));
  if (value_len) std::memcpy(payload.data() + sizeof(key), value, value_len);
  return zedra::Event(tick, tie_breaker, 0, std::move(payload));
}

void EmptyState_Default() {
  zedra::WorldState s;
  assert(s.version() == 0);
  assert(s.data().empty());
  assert(s.hash() == 0);
}

void EmptyState_ExplicitEmptyMap() {
  zedra::WorldState::Map empty;
  zedra::WorldState s(0, empty);
  assert(s.version() == 0);
  assert(s.data().empty());
  std::uint64_t h = s.hash();
  assert(h == zedra::hash_state(s));
}

void HashState_Determinism() {
  zedra::WorldState::Map data = {{1, {{std::byte{'a'}}, 0}}, {2, {{std::byte{'b'}}, 0}}};
  zedra::WorldState s(1, data);
  assert(zedra::hash_state(s) == zedra::hash_state(s));
}

void HashState_KeyOrderIndependent() {
  zedra::WorldState::Map a = {{2, {{std::byte{'x'}}, 0}}, {1, {{std::byte{'y'}}, 0}}};
  zedra::WorldState::Map b = {{1, {{std::byte{'y'}}, 0}}, {2, {{std::byte{'x'}}, 0}}};
  zedra::WorldState s1(0, std::move(a));
  zedra::WorldState s2(0, std::move(b));
  assert(s1.hash() == s2.hash());
  assert(s1.data().size() == 2);
  assert(s1.data()[0].first == 1);
  assert(s1.data()[1].first == 2);
}

void Apply_NewKey() {
  zedra::WorldState current;
  zedra::Event e = make_upsert(0, 0, 100, "val", 3);
  zedra::WorldState next = zedra::WorldState::apply(current, e);
  assert(next.version() == 1);
  assert(next.data().size() == 1);
  assert(next.data()[0].first == 100);
  assert(next.data()[0].second.value.size() == 3);
  assert(static_cast<char>(next.data()[0].second.value[0]) == 'v');
  assert(static_cast<char>(next.data()[0].second.value[1]) == 'a');
  assert(static_cast<char>(next.data()[0].second.value[2]) == 'l');
}

void Apply_SameKeyOverwrites() {
  zedra::WorldState current;
  zedra::WorldState s1 = zedra::WorldState::apply(current, make_upsert(0, 0, 1, "first", 5));
  zedra::WorldState s2 = zedra::WorldState::apply(s1, make_upsert(1, 0, 1, "second", 6));
  assert(s2.version() == 2);
  assert(s2.data().size() == 1);
  assert(s2.data()[0].first == 1);
  const auto& v = s2.data()[0].second.value;
  std::string val(v.size(), '\0');
  for (size_t i = 0; i < v.size(); ++i)
    val[i] = static_cast<char>(static_cast<unsigned char>(v[i]));
  assert(val == "second");
}

void Apply_TypeNonZeroUnchanged() {
  zedra::WorldState::Map data = {{1, {{std::byte{'a'}}, 0}}};
  zedra::WorldState current(1, std::move(data));
  zedra::Event e(0, 0, 99, {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                            std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}});
  zedra::WorldState next = zedra::WorldState::apply(current, e);
  assert(next.version() == 2);
  assert(next.data().size() == 1);
  assert(next.data()[0].first == 1);
  assert(next.data()[0].second.value.size() == 1);
  assert(next.data()[0].second.value[0] == std::byte{'a'});
}

void Apply_PayloadTooSmallUnchanged() {
  zedra::WorldState::Map data = {{1, {{std::byte{'a'}}, 0}}};
  zedra::WorldState current(1, std::move(data));
  zedra::Event e(0, 0, 0, {std::byte{1}, std::byte{2}});
  zedra::WorldState next = zedra::WorldState::apply(current, e);
  assert(next.version() == 2);
  assert(next.data().size() == 1);
  assert(next.data()[0].first == 1);
}

void Apply_VersionIncrements() {
  zedra::WorldState s;
  s = zedra::WorldState::apply(s, make_upsert(0, 0, 1, "a", 1));
  assert(s.version() == 1);
  s = zedra::WorldState::apply(s, make_upsert(1, 0, 2, "b", 1));
  assert(s.version() == 2);
  s = zedra::WorldState::apply(s, make_upsert(2, 0, 1, "c", 1));
  assert(s.version() == 3);
}

void Apply_DataSortedByKey() {
  zedra::WorldState s;
  s = zedra::WorldState::apply(s, make_upsert(0, 0, 30, "x", 1));
  s = zedra::WorldState::apply(s, make_upsert(1, 0, 10, "y", 1));
  s = zedra::WorldState::apply(s, make_upsert(2, 0, 20, "z", 1));
  assert(s.data().size() == 3);
  assert(s.data()[0].first == 10);
  assert(s.data()[1].first == 20);
  assert(s.data()[2].first == 30);
}

void Apply_StoresLastTick() {
  zedra::WorldState s;
  s = zedra::WorldState::apply(s, make_upsert(10, 0, 1, "a", 1));
  s = zedra::WorldState::apply(s, make_upsert(20, 0, 2, "b", 1));
  s = zedra::WorldState::apply(s, make_upsert(30, 0, 1, "c", 1));
  assert(s.data().size() == 2);
  assert(s.data()[0].first == 1 && s.data()[0].second.last_tick == 30);
  assert(s.data()[1].first == 2 && s.data()[1].second.last_tick == 20);
}

void Trim_EvictsStaleKeys() {
  zedra::WorldState::Map data = {
      {1, {{std::byte{'a'}}, 10}},
      {2, {{std::byte{'b'}}, 20}},
      {3, {{std::byte{'c'}}, 30}},
  };
  zedra::WorldState s(0, std::move(data));
  zedra::WorldState t = zedra::WorldState::trim(s, 30, 15);
  assert(t.version() == s.version());
  assert(t.data().size() == 2);
  assert(t.data()[0].first == 2 && t.data()[1].first == 3);
  assert(t.data()[0].second.last_tick == 20 && t.data()[1].second.last_tick == 30);
}

void Trim_NoOpWhenWindowZero() {
  zedra::WorldState::Map data = {{1, {{std::byte{'a'}}, 0}}};
  zedra::WorldState s(1, std::move(data));
  zedra::WorldState t = zedra::WorldState::trim(s, 100, 0);
  assert(t.data().size() == 1);
  assert(t.data()[0].first == 1);
  assert(t.version() == s.version());
}

void Trim_NoOpWhenAllInWindow() {
  zedra::WorldState::Map data = {
      {1, {{std::byte{'a'}}, 90}},
      {2, {{std::byte{'b'}}, 95}},
  };
  zedra::WorldState s(0, std::move(data));
  zedra::WorldState t = zedra::WorldState::trim(s, 100, 20);
  assert(t.data().size() == 2);
}

void HashState_IncludesLastTick() {
  zedra::WorldState::Map a = {{1, {{std::byte{'x'}}, 10}}};
  zedra::WorldState::Map b = {{1, {{std::byte{'x'}}, 20}}};
  zedra::WorldState s1(0, std::move(a));
  zedra::WorldState s2(0, std::move(b));
  assert(s1.hash() != s2.hash());
}

}  // namespace

void run_unit_state_tests() {
  RUN_TEST(EmptyState_Default);
  RUN_TEST(EmptyState_ExplicitEmptyMap);
  RUN_TEST(HashState_Determinism);
  RUN_TEST(HashState_KeyOrderIndependent);
  RUN_TEST(Apply_NewKey);
  RUN_TEST(Apply_SameKeyOverwrites);
  RUN_TEST(Apply_TypeNonZeroUnchanged);
  RUN_TEST(Apply_PayloadTooSmallUnchanged);
  RUN_TEST(Apply_VersionIncrements);
  RUN_TEST(Apply_DataSortedByKey);
  RUN_TEST(Apply_StoresLastTick);
  RUN_TEST(Trim_EvictsStaleKeys);
  RUN_TEST(Trim_NoOpWhenWindowZero);
  RUN_TEST(Trim_NoOpWhenAllInWindow);
  RUN_TEST(HashState_IncludesLastTick);
}
