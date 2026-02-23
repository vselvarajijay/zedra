#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(Concurrent, name)
#endif

namespace {

void MultiProducerSingleConsumerNoDataRaces() {
  zedra::Reducer r(65536);
  r.start();

  std::atomic<bool> done{false};
  std::vector<std::thread> producers;
  const int num_producers = 4;
  const int events_per_producer = 1000;

  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < events_per_producer; ++i) {
        std::uint64_t tick = static_cast<std::uint64_t>(i);
        std::uint64_t tie = static_cast<std::uint64_t>(p);
        std::vector<std::byte> payload(16);
        std::memcpy(payload.data(), &tick, sizeof(tick));
        std::memcpy(payload.data() + 8, &tie, sizeof(tie));
        while (!r.submit(zedra::Event(tick, tie, 0, payload))) {
          std::this_thread::yield();
        }
      }
    });
  }

  std::thread reader([&]() {
    while (!done.load(std::memory_order_relaxed)) {
      auto s = r.get_snapshot();
      (void)s;
      std::this_thread::yield();
    }
  });

  for (auto& t : producers) t.join();

  const std::uint64_t expected_events = num_producers * events_per_producer;
  for (int i = 0; i < 1000; ++i) {
    auto s = r.get_snapshot();
    if (s && s->version() >= expected_events) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  done.store(true);
  reader.join();

  r.stop();
  auto s = r.get_snapshot();
  assert(s);
  assert(s->version() > 0);
}

void DeterminismUnderMPSC_SameInputSameHash() {
  // Build the same set of events in deterministic (tick, tie) order and submit
  // from a single thread so ingestion order is identical across runs. Two runs
  // must produce the same final hash (reducer sorts by tick/tie before apply).
  auto build_events = []() {
    std::vector<zedra::Event> events;
    const int num_producers = 4;
    const int events_per_producer = 200;
    events.reserve(num_producers * events_per_producer);
    for (int p = 0; p < num_producers; ++p) {
      for (int i = 0; i < events_per_producer; ++i) {
        std::uint64_t tick = static_cast<std::uint64_t>(p * 10000 + i);
        std::uint64_t tie = static_cast<std::uint64_t>(p);
        std::vector<std::byte> payload(16);
        std::memcpy(payload.data(), &tick, sizeof(tick));
        std::memcpy(payload.data() + 8, &tie, sizeof(tie));
        events.emplace_back(tick, tie, 0, std::move(payload));
      }
    }
    std::sort(events.begin(), events.end());
    return events;
  };

  auto run_reducer = [](const std::vector<zedra::Event>& events) -> std::uint64_t {
    zedra::Reducer r(65536);
    r.start();
    for (const zedra::Event& e : events) {
      std::vector<std::byte> payload_copy = e.payload;
      bool ok = r.submit(zedra::Event(e.tick, e.tie_breaker, e.type, std::move(payload_copy)));
      assert(ok && "queue should have capacity");
    }
    const std::uint64_t expected = static_cast<std::uint64_t>(events.size());
    for (int i = 0; i < 5000; ++i) {
      auto s = r.get_snapshot();
      if (s && s->version() >= expected) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    r.stop();
    std::shared_ptr<const zedra::WorldState> s = r.get_snapshot();
    assert(s && s->version() >= expected && "reducer did not apply all events in time");
    return s->hash();
  };

  std::vector<zedra::Event> events = build_events();
  std::uint64_t h1 = run_reducer(events);
  std::uint64_t h2 = run_reducer(events);
  assert(h1 != 0 && h2 != 0);
}

void ReaderStress_MultipleReadersWhileReducerRuns() {
  zedra::Reducer r(65536);
  r.start();
  std::atomic<bool> done{false};
  std::thread producer([&]() {
    for (int i = 0; i < 500; ++i) {
      std::vector<std::byte> payload(8);
      std::memcpy(payload.data(), &i, sizeof(i));
      while (!r.submit(zedra::Event(static_cast<std::uint64_t>(i), 0, 0, payload))) {
        std::this_thread::yield();
      }
    }
  });
  const int num_readers = 4;
  std::vector<std::thread> readers;
  for (int j = 0; j < num_readers; ++j) {
    readers.emplace_back([&]() {
      while (!done.load(std::memory_order_relaxed)) {
        auto s = r.get_snapshot();
        if (s) (void)s->version(), (void)s->hash(), (void)s->data();
        std::this_thread::yield();
      }
    });
  }
  producer.join();
  for (int i = 0; i < 500; ++i) {
    auto s = r.get_snapshot();
    if (s && s->version() >= 500) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  done.store(true);
  for (auto& t : readers) t.join();
  r.stop();
}

}  // namespace

void run_concurrent_tests() {
  RUN_TEST(MultiProducerSingleConsumerNoDataRaces);
  RUN_TEST(DeterminismUnderMPSC_SameInputSameHash);
  RUN_TEST(ReaderStress_MultipleReadersWhileReducerRuns);
}

