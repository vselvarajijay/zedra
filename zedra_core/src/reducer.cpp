#include <zedra/reducer.hpp>
#include <algorithm>
#include <chrono>
#include <thread>

namespace zedra {

Reducer::Reducer(std::size_t queue_capacity,
                 LockFreeQueue<EgressItem>* egress_queue,
                 std::uint64_t window_ticks)
    : queue_(queue_capacity),
      egress_queue_(egress_queue),
      window_ticks_(window_ticks),
      snapshot_(std::make_shared<WorldState>()) {}

Reducer::~Reducer() {
  stop();
}

bool Reducer::submit(Event event) {
  return queue_.push(std::move(event));
}

std::shared_ptr<const WorldState> Reducer::get_snapshot() const {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  return snapshot_;
}

void Reducer::start() {
  if (running_.exchange(true, std::memory_order_acq_rel)) return;
  thread_ = std::thread(&Reducer::run, this);
}

void Reducer::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) return;
  if (thread_.joinable()) thread_.join();
}

void Reducer::run() {
  WorldState state;
  std::vector<Event> batch;
  batch.reserve(1024);
  std::vector<Event> pending;

  while (running_.load(std::memory_order_acquire)) {
    batch.clear();
    queue_.drain(batch, 1024);

    if (!batch.empty()) {
      pending.insert(pending.end(), batch.begin(), batch.end());
      continue;
    }

    // Empty drain: queue was empty at this moment. Apply all pending in logical order.
    if (pending.empty()) {
      std::this_thread::yield();
      continue;
    }

    std::sort(pending.begin(), pending.end());

    const auto ingestion_ts = std::chrono::system_clock::now();
    if (egress_queue_) {
      for (const Event& e : pending) {
        if (!egress_queue_->push(EgressItem(ingestion_ts, Event(e)))) {
          // Egress full; drop and continue (do not block reducer).
        }
      }
    }

    for (const Event& e : pending) {
      state = WorldState::apply(state, e);
    }

    std::uint64_t batch_max = 0;
    for (const Event& e : pending) batch_max = std::max(batch_max, e.tick);
    max_tick_seen_ = std::max(max_tick_seen_, batch_max);
    if (window_ticks_ > 0)
      state = WorldState::trim(state, max_tick_seen_, window_ticks_);

    pending.clear();

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_ = std::make_shared<WorldState>(state);
    }
  }
}

}  // namespace zedra
