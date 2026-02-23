#include <zedra/event.hpp>
#include <zedra/lock_free_queue.hpp>
#include <cassert>
#include <iostream>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(UnitLockFreeQueue, name)
#endif

namespace {

zedra::Event event_with_tick(std::uint64_t tick) {
  return zedra::Event(tick, 0, 0, {});
}

void PushThenPop_OrderPreserved() {
  zedra::LockFreeQueue<zedra::Event> q(16);
  for (std::size_t i = 0; i < 10; ++i)
    assert(q.push(event_with_tick(i)));
  for (std::size_t i = 0; i < 10; ++i) {
    zedra::Event e;
    assert(q.try_pop(e));
    assert(e.tick == i);
  }
  zedra::Event e;
  assert(!q.try_pop(e));
}

void InterleavedPushPop() {
  zedra::LockFreeQueue<zedra::Event> q(16);
  for (std::size_t i = 0; i < 10; ++i) {
    assert(q.push(event_with_tick(i)));
    zedra::Event e;
    assert(q.try_pop(e));
    assert(e.tick == i);
  }
  zedra::Event e;
  assert(!q.try_pop(e));
}

void Capacity_RoundsToPowerOfTwo() {
  zedra::LockFreeQueue<zedra::Event> q3(3);
  assert(q3.capacity() == 4);
  zedra::LockFreeQueue<zedra::Event> q1(1);
  assert(q1.capacity() == 1);
  zedra::LockFreeQueue<zedra::Event> q17(17);
  assert(q17.capacity() == 32);
}

void Capacity_PushUpToCapacitySucceeds() {
  zedra::LockFreeQueue<zedra::Event> q(4);
  assert(q.push(event_with_tick(0)));
  assert(q.push(event_with_tick(1)));
  assert(q.push(event_with_tick(2)));
  assert(q.push(event_with_tick(3)));
  assert(!q.push(event_with_tick(4)));
}

void FullThenPopOneThenPushOne() {
  zedra::LockFreeQueue<zedra::Event> q(4);
  for (std::size_t i = 0; i < 4; ++i)
    assert(q.push(event_with_tick(i)));
  assert(!q.push(event_with_tick(99)));
  zedra::Event e;
  assert(q.try_pop(e));
  assert(e.tick == 0);
  assert(q.push(event_with_tick(100)));
  std::vector<zedra::Event> out;
  assert(q.drain(out, 0) == 4);
  assert(out[0].tick == 1);
  assert(out[1].tick == 2);
  assert(out[2].tick == 3);
  assert(out[3].tick == 100);
}

void Empty_TryPopReturnsFalse() {
  zedra::LockFreeQueue<zedra::Event> q(8);
  zedra::Event e;
  assert(!q.try_pop(e));
}

void Empty_DrainReturnsZero() {
  zedra::LockFreeQueue<zedra::Event> q(8);
  std::vector<zedra::Event> out;
  assert(q.drain(out, 0) == 0);
  assert(out.empty());
  assert(q.drain(out, 10) == 0);
}

void Drain_AllWhenMaxZero() {
  zedra::LockFreeQueue<zedra::Event> q(16);
  for (std::size_t i = 0; i < 5; ++i)
    assert(q.push(event_with_tick(i)));
  std::vector<zedra::Event> out;
  assert(q.drain(out, 0) == 5);
  assert(out.size() == 5);
  for (std::size_t i = 0; i < 5; ++i)
    assert(out[i].tick == i);
  assert(!q.try_pop(out[0]));
}

void Drain_MaxEventsCapsCount() {
  zedra::LockFreeQueue<zedra::Event> q(16);
  for (std::size_t i = 0; i < 5; ++i)
    assert(q.push(event_with_tick(i)));
  std::vector<zedra::Event> out;
  assert(q.drain(out, 2) == 2);
  assert(out.size() == 2);
  assert(out[0].tick == 0);
  assert(out[1].tick == 1);
  out.clear();
  assert(q.drain(out, 10) == 3);
  assert(out[0].tick == 2);
  assert(out[1].tick == 3);
  assert(out[2].tick == 4);
}

}  // namespace

void run_unit_lock_free_queue_tests() {
  RUN_TEST(PushThenPop_OrderPreserved);
  RUN_TEST(InterleavedPushPop);
  RUN_TEST(Capacity_RoundsToPowerOfTwo);
  RUN_TEST(Capacity_PushUpToCapacitySucceeds);
  RUN_TEST(FullThenPopOneThenPushOne);
  RUN_TEST(Empty_TryPopReturnsFalse);
  RUN_TEST(Empty_DrainReturnsZero);
  RUN_TEST(Drain_AllWhenMaxZero);
  RUN_TEST(Drain_MaxEventsCapsCount);
}
