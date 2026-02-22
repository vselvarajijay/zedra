#ifndef ZEDRA_LOCK_FREE_QUEUE_HPP
#define ZEDRA_LOCK_FREE_QUEUE_HPP

#include <zedra/event.hpp>
#include <atomic>
#include <cstddef>
#include <memory>

namespace zedra {

/// Bounded multi-producer single-consumer lock-free queue of Event.
/// When full, push() returns false (producer should back off or retry).
/// Single consumer only: only one thread may call try_pop() / drain().
class LockFreeQueue {
 public:
  /// \param capacity Must be a power of two; enforced by rounding up.
  explicit LockFreeQueue(std::size_t capacity = 65536);
  ~LockFreeQueue();

  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  /// Push one event. Returns false if queue is full (no overwrite).
  bool push(Event event);

  /// Pop one event. Returns false if queue is empty. Single-consumer only.
  bool try_pop(Event& out);

  /// Drain up to max_events into the given output vector. Returns number popped.
  std::size_t drain(std::vector<Event>& out, std::size_t max_events = 0);

  std::size_t capacity() const { return capacity_; }

 private:
  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<std::atomic<std::uint64_t>[]> sequence_;
  std::unique_ptr<Event[]> buffer_;
  std::atomic<std::uint64_t> head_{0};
  std::atomic<std::uint64_t> tail_{0};
};

}  // namespace zedra

#endif  // ZEDRA_LOCK_FREE_QUEUE_HPP
