#include <zedra/event.hpp>
#include <zedra/lock_free_queue.hpp>
#include <zedra/state.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#ifdef ZEDRA_SIMPLE_TEST
#define RUN_TEST(name) do { std::cerr << "  " #name " ... "; name(); std::cerr << "ok\n"; } while(0)
#else
#include <gtest/gtest.h>
#define RUN_TEST(name) TEST(ChaoticIngestion, name)
#endif

namespace {

constexpr int kNumSensors = 5;
constexpr int kIterationsPerSensor = 200;
constexpr int kTotalEvents = kNumSensors * kIterationsPerSensor;
constexpr std::size_t kQueueCapacity = 2048;
constexpr int kDelayMinMs = 0;
constexpr int kDelayMaxMs = 50;
constexpr int kPublishedSampleSize = 5;
constexpr int kTimelineHead = 10;
constexpr int kTimelineTail = 5;
constexpr int kTimelineFullThreshold = 30;

zedra::Event make_upsert(std::uint64_t tick, std::uint64_t tie_breaker,
                        std::uint64_t key, const void* value, std::size_t value_len) {
  std::vector<std::byte> payload(sizeof(key) + value_len);
  std::memcpy(payload.data(), &key, sizeof(key));
  if (value_len) std::memcpy(payload.data() + sizeof(key), value, value_len);
  return zedra::Event(tick, tie_breaker, 0, std::move(payload));
}

void ChaoticIngestion_LogicalOrderRestoredAfterRandomDelays() {
  zedra::LockFreeQueue<zedra::Event> queue(kQueueCapacity);
  std::atomic<std::uint64_t> global_tick{0};

  std::vector<std::thread> sensors;
  for (int s = 0; s < kNumSensors; ++s) {
    sensors.emplace_back([&queue, &global_tick, s]() {
      std::mt19937 rng(static_cast<std::mt19937::result_type>(s + 12345u));
      std::uniform_int_distribution<int> dist(kDelayMinMs, kDelayMaxMs);
      for (int i = 0; i < kIterationsPerSensor; ++i) {
        std::uint64_t tick = global_tick.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
        std::uint64_t key = tick;
        while (!queue.push(make_upsert(tick, static_cast<std::uint64_t>(s), key, &tick, sizeof(tick)))) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto& t : sensors) t.join();

  std::vector<zedra::Event> raw_drained;
  raw_drained.reserve(kTotalEvents);
  std::size_t drained = queue.drain(raw_drained, 0);
  assert(drained == static_cast<std::size_t>(kTotalEvents) && "queue integrity: must drain 1000 events");

  std::vector<zedra::Event> applied = raw_drained;
  std::sort(applied.begin(), applied.end());

  for (std::size_t i = 0; i + 1 < applied.size(); ++i) {
    assert(applied[i].tick < applied[i + 1].tick ||
           (applied[i].tick == applied[i + 1].tick &&
            applied[i].tie_breaker < applied[i + 1].tie_breaker));
  }

  bool raw_equals_applied = (raw_drained.size() == applied.size());
  if (raw_equals_applied) {
    for (std::size_t i = 0; i < raw_drained.size(); ++i) {
      if (raw_drained[i].tick != applied[i].tick ||
          raw_drained[i].tie_breaker != applied[i].tie_breaker) {
        raw_equals_applied = false;
        break;
      }
    }
  }
  assert(!raw_equals_applied && "arrival order must differ from logical order");

  zedra::WorldState state;
  for (const auto& e : applied) {
    state = zedra::WorldState::apply(state, e);
  }
  assert(state.version() == static_cast<std::uint64_t>(kTotalEvents));

  // Log: drained summary, per-sensor published samples, merged timeline, final state
  std::cerr << "    [chaotic] drained " << drained << " events; raw != applied (order scrambled).\n";

  // Per-sensor published samples (first kPublishedSampleSize events per sensor, in logical order)
  std::vector<std::vector<zedra::Event>> per_sensor(kNumSensors);
  for (const auto& e : applied) {
    if (e.tie_breaker < static_cast<std::uint64_t>(kNumSensors) &&
        static_cast<int>(per_sensor[e.tie_breaker].size()) < kPublishedSampleSize)
      per_sensor[e.tie_breaker].push_back(e);
  }
  std::cerr << "    [chaotic] Published (per sensor, first " << kPublishedSampleSize << " samples each):\n";
  for (int s = 0; s < kNumSensors; ++s) {
    std::cerr << "      sensor " << s << ": ";
    for (std::size_t i = 0; i < per_sensor[s].size(); ++i)
      std::cerr << "(" << per_sensor[s][i].tick << "," << per_sensor[s][i].tie_breaker << ")"
                << (i + 1 < per_sensor[s].size() ? " " : "");
    std::cerr << "\n";
  }

  // Merged timeline (full or subset)
  const bool show_subset = static_cast<int>(applied.size()) > kTimelineFullThreshold;
  std::cerr << "    [chaotic] Merged timeline (logical order"
            << (show_subset ? ", subset" : "") << "):\n";
  if (show_subset) {
    std::cerr << "      first " << kTimelineHead << ": ";
    for (int i = 0; i < kTimelineHead && i < static_cast<int>(applied.size()); ++i)
      std::cerr << "(" << applied[i].tick << "," << applied[i].tie_breaker << ")"
                << (i + 1 < kTimelineHead && i + 1 < static_cast<int>(applied.size()) ? " " : "");
    std::cerr << "\n      ... (" << applied.size() << " total) ...\n      last " << kTimelineTail << ": ";
    for (int j = 0; j < kTimelineTail; ++j) {
      std::size_t i = applied.size() - kTimelineTail + j;
      if (i >= applied.size()) break;
      std::cerr << "(" << applied[i].tick << "," << applied[i].tie_breaker << ")"
                << (j + 1 < kTimelineTail ? " " : "");
    }
    std::cerr << "\n";
  } else {
    std::cerr << "      ";
    for (std::size_t i = 0; i < applied.size(); ++i)
      std::cerr << "(" << applied[i].tick << "," << applied[i].tie_breaker << ")"
                << (i + 1 < applied.size() ? " " : "");
    std::cerr << "\n";
  }

  std::cerr << "    [chaotic] final state version=" << state.version() << " hash=" << state.hash() << "\n";
}

}  // namespace

void run_chaotic_ingestion_tests() {
  RUN_TEST(ChaoticIngestion_LogicalOrderRestoredAfterRandomDelays);
}
