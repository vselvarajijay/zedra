#ifndef ZEDRA_EGRESS_ITEM_HPP
#define ZEDRA_EGRESS_ITEM_HPP

#include <zedra/event.hpp>
#include <chrono>

namespace zedra {

/// Item pushed to egress: event plus wall-clock time when the batch was committed to egress.
/// Used for ClickHouse ingestion_ts and deterministic replay ordering.
struct EgressItem {
  std::chrono::system_clock::time_point ingestion_ts;
  Event event;

  EgressItem() = default;
  EgressItem(std::chrono::system_clock::time_point ts, Event e)
      : ingestion_ts(ts), event(std::move(e)) {}
};

}  // namespace zedra

#endif  // ZEDRA_EGRESS_ITEM_HPP
