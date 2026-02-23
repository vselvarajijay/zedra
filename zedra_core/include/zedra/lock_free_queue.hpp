#ifndef ZEDRA_LOCK_FREE_QUEUE_HPP
#define ZEDRA_LOCK_FREE_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace zedra {

namespace detail {

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

}  // namespace detail

/// Bounded multi-producer single-consumer lock-free queue.
/// When full, push() returns false (producer should back off or retry).
/// Single consumer only: only one thread may call try_pop() / drain().
template <typename T>
class LockFreeQueue {
 public:
  /// \param capacity Must be a power of two; enforced by rounding up.
  explicit LockFreeQueue(std::size_t capacity = 65536)
      : capacity_(detail::next_power_of_two(capacity)),
        mask_(capacity_ - 1),
        sequence_(std::make_unique<std::atomic<std::uint64_t>[]>(capacity_)),
        buffer_(std::make_unique<T[]>(capacity_)) {
    for (std::size_t i = 0; i < capacity_; ++i) {
      sequence_[i].store(i, std::memory_order_relaxed);
    }
  }

  ~LockFreeQueue() = default;

  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  /// Push one item. Returns false if queue is full (no overwrite).
  bool push(T item) {
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
    buffer_[slot] = std::move(item);
    sequence_[slot].store(idx, std::memory_order_seq_cst);
    return true;
  }

  /// Pop one item. Returns false if queue is empty. Single-consumer only.
  bool try_pop(T& out) {
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

  /// Drain up to max_items into the given output vector. Returns number popped.
  /// If max_items is 0, drains all available.
  std::size_t drain(std::vector<T>& out, std::size_t max_items = 0) {
    std::size_t count = 0;
    const std::size_t limit = (max_items > 0)
                                  ? max_items
                                  : (tail_.load(std::memory_order_seq_cst) -
                                     head_.load(std::memory_order_relaxed));
    out.reserve(out.size() + limit);
    T item;
    while (count < limit && try_pop(item)) {
      out.push_back(std::move(item));
      count++;
    }
    return count;
  }

  std::size_t capacity() const { return capacity_; }

 private:
  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<std::atomic<std::uint64_t>[]> sequence_;
  std::unique_ptr<T[]> buffer_;
  std::atomic<std::uint64_t> head_{0};
  std::atomic<std::uint64_t> tail_{0};
};

}  // namespace zedra

#endif  // ZEDRA_LOCK_FREE_QUEUE_HPP
