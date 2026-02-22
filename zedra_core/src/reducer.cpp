#include <zedra/reducer.hpp>
#include <algorithm>
#include <chrono>
#include <thread>

namespace zedra {

Reducer::Reducer(std::size_t queue_capacity)
    : queue_(queue_capacity),
      snapshot_(std::make_shared<WorldState>()) {
}

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

  while (running_.load(std::memory_order_acquire)) {
    batch.clear();
    queue_.drain(batch, 1024);

    if (batch.empty()) {
      std::this_thread::yield();
      continue;
    }

    std::sort(batch.begin(), batch.end());

    for (const Event& e : batch) {
      state = WorldState::apply(state, e);
    }

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_ = std::make_shared<WorldState>(state);
    }
  }
}

}  // namespace zedra
