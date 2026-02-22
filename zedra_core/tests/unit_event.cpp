#include <zedra/event.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(UnitEvent, name)
#endif

namespace {

void EventOrdering_DifferentTick() {
  zedra::Event a(1, 0, 0, {});
  zedra::Event b(2, 0, 0, {});
  assert(a < b);
  assert(!(b < a));
}

void EventOrdering_SameTickDifferentTieBreaker() {
  zedra::Event a(1, 0, 0, {});
  zedra::Event b(1, 1, 0, {});
  assert(a < b);
  assert(!(b < a));
}

void EventOrdering_EqualEvents() {
  zedra::Event a(1, 1, 0, {});
  zedra::Event b(1, 1, 0, {});
  assert(!(a < b));
  assert(!(b < a));
}

void HashEvent_Determinism() {
  std::vector<std::byte> payload = {std::byte{'x'}, std::byte{'y'}};
  zedra::Event e(42, 7, 1, payload);
  std::uint64_t h1 = zedra::hash_event(e);
  std::uint64_t h2 = zedra::hash_event(e);
  assert(h1 == h2);
}

void HashEvent_SensitivityToTick() {
  zedra::Event a(1, 0, 0, {});
  zedra::Event b(2, 0, 0, {});
  assert(zedra::hash_event(a) != zedra::hash_event(b));
}

void HashEvent_SensitivityToTieBreaker() {
  zedra::Event a(1, 0, 0, {});
  zedra::Event b(1, 1, 0, {});
  assert(zedra::hash_event(a) != zedra::hash_event(b));
}

void HashEvent_SensitivityToType() {
  zedra::Event a(1, 0, 0, {});
  zedra::Event b(1, 0, 1, {});
  assert(zedra::hash_event(a) != zedra::hash_event(b));
}

void HashEvent_SensitivityToPayload() {
  zedra::Event a(1, 0, 0, {std::byte{'a'}});
  zedra::Event b(1, 0, 0, {std::byte{'b'}});
  assert(zedra::hash_event(a) != zedra::hash_event(b));
}

void HashEvent_EmptyPayload() {
  zedra::Event e(0, 0, 0, {});
  assert(zedra::hash_event(e) == zedra::hash_event(e));
}

void HashEvent_PayloadWithZeros() {
  std::vector<std::byte> payload(4, std::byte{0});
  zedra::Event e(0, 0, 0, std::move(payload));
  assert(zedra::hash_event(e) == zedra::hash_event(e));
}

void DefaultCtor() {
  zedra::Event e;
  assert(e.tick == 0);
  assert(e.tie_breaker == 0);
  assert(e.type == 0);
  assert(e.payload.empty());
}

}  // namespace

void run_unit_event_tests() {
  RUN_TEST(EventOrdering_DifferentTick);
  RUN_TEST(EventOrdering_SameTickDifferentTieBreaker);
  RUN_TEST(EventOrdering_EqualEvents);
  RUN_TEST(HashEvent_Determinism);
  RUN_TEST(HashEvent_SensitivityToTick);
  RUN_TEST(HashEvent_SensitivityToTieBreaker);
  RUN_TEST(HashEvent_SensitivityToType);
  RUN_TEST(HashEvent_SensitivityToPayload);
  RUN_TEST(HashEvent_EmptyPayload);
  RUN_TEST(HashEvent_PayloadWithZeros);
  RUN_TEST(DefaultCtor);
}
