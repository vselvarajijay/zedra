#include <zedra/lock_free_queue.hpp>
#include <algorithm>
#include <vector>

namespace zedra {

namespace {

constexpr std::size_t next_power_of_two(std::size_t n) {
  if (n == 0) return 1;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return n + 1;
}

}  // namespace

LockFreeQueue::LockFreeQueue(std::size_t capacity)
    : capacity_(next_power_of_two(capacity)),
      mask_(capacity_ - 1),
      sequence_(std::make_unique<std::atomic<std::uint64_t>[]>(capacity_)),
      buffer_(std::make_unique<Event[]>(capacity_)) {
  for (std::size_t i = 0; i < capacity_; ++i) {
    sequence_[i].store(i, std::memory_order_relaxed);
  }
}

LockFreeQueue::~LockFreeQueue() = default;

bool LockFreeQueue::push(Event event) {
  const std::uint64_t cap = capacity_;
  std::uint64_t idx;
  for (;;) {
    idx = tail_.load(std::memory_order_relaxed);
    if (idx - head_.load(std::memory_order_acquire) >= cap) {
      return false;
    }
    if (tail_.compare_exchange_weak(idx, idx + 1, std::memory_order_seq_cst)) {
      break;
    }
  }
  std::size_t slot = idx & mask_;
  buffer_[slot] = std::move(event);
  sequence_[slot].store(idx, std::memory_order_seq_cst);
  return true;
}

bool LockFreeQueue::try_pop(Event& out) {
  std::uint64_t h = head_.load(std::memory_order_relaxed);
  if (h >= tail_.load(std::memory_order_seq_cst)) {
    return false;
  }
  std::size_t slot = h & mask_;
  while (sequence_[slot].load(std::memory_order_seq_cst) != h) {
    // spin until producer wrote
  }
  out = std::move(buffer_[slot]);
  sequence_[slot].store(h + capacity_, std::memory_order_release);
  head_.store(h + 1, std::memory_order_release);
  return true;
}

std::size_t LockFreeQueue::drain(std::vector<Event>& out, std::size_t max_events) {
  std::size_t count = 0;
  const std::size_t limit = (max_events > 0) ? max_events : (tail_.load(std::memory_order_seq_cst) - head_.load(std::memory_order_relaxed));
  out.reserve(out.size() + limit);
  Event e;
  while (count < limit && try_pop(e)) {
    out.push_back(std::move(e));
    count++;
  }
  return count;
}

}  // namespace zedra
