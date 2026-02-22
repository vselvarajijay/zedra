#ifndef ZEDRA_REDUCER_HPP
#define ZEDRA_REDUCER_HPP

#include <zedra/event.hpp>
#include <zedra/lock_free_queue.hpp>
#include <zedra/state.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace zedra {

/// Single authority that drains the event queue, orders by logical time,
/// applies events to world state, and publishes immutable snapshots.
class Reducer {
 public:
  explicit Reducer(std::size_t queue_capacity = 65536);
  ~Reducer();

  Reducer(const Reducer&) = delete;
  Reducer& operator=(const Reducer&) = delete;

  /// Submit an event (lock-free push). Returns false if queue is full.
  bool submit(Event event);

  /// Latest snapshot. Brief lock on read.
  std::shared_ptr<const WorldState> get_snapshot() const;

  void start();
  void stop();

 private:
  void run();

  LockFreeQueue queue_;
  mutable std::mutex snapshot_mutex_;
  std::shared_ptr<const WorldState> snapshot_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace zedra

#endif  // ZEDRA_REDUCER_HPP
