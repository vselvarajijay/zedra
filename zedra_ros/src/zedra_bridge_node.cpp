#include <zedra_ros/zedra_bridge_node.hpp>
#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <chrono>
#include <cinttypes>
#include <sstream>

namespace zedra_ros {

namespace {

/// Reject payloads larger than this to avoid OOM from corrupted/mis-deserialized
/// message size (e.g. when running amd64 image under Rosetta on ARM).
constexpr std::size_t kMaxPayloadSize = 1024 * 1024;  // 1 MiB

std::string to_hex(std::uint64_t value) {
  std::ostringstream os;
  os << std::hex << value;
  return os.str();
}

}  // namespace

ZedraBridgeNode::ZedraBridgeNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("zedra_bridge", options) {
    const std::size_t queue_capacity =
        static_cast<std::size_t>(this->declare_parameter("queue_capacity", 65536));
    reducer_ = std::make_unique<zedra::Reducer>(queue_capacity);
    reducer_->start();

    RCLCPP_INFO(get_logger(), "zedra_bridge running: sub /zedra/inbound_events, pub /zedra/snapshot_meta (1 kHz)");

    sub_ = this->create_subscription<zedra_ros::msg::ZedraEvent>(
        "/zedra/inbound_events", 10, [this](zedra_ros::msg::ZedraEvent::ConstSharedPtr msg) {
          on_inbound_event(msg);
        });

    meta_pub_ = this->create_publisher<zedra_ros::msg::SnapshotMeta>("/zedra/snapshot_meta", 10);
  timer_ = this->create_wall_timer(std::chrono::milliseconds(1), [this]() { publish_snapshot_meta(); });
}

ZedraBridgeNode::~ZedraBridgeNode() {
  if (reducer_) reducer_->stop();
}

void ZedraBridgeNode::on_inbound_event(zedra_ros::msg::ZedraEvent::ConstSharedPtr msg) {
    const std::size_t raw_size = msg->payload.size();
    if (raw_size > kMaxPayloadSize) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000,
          "dropping event with oversized payload (tick=%" PRIu64 ", size=%zu, max=%zu)",
          msg->tick, raw_size, kMaxPayloadSize);
      drop_count_++;
      return;
    }
    std::vector<std::byte> payload(raw_size);
    for (std::size_t i = 0; i < raw_size; ++i) {
      payload[i] = static_cast<std::byte>(msg->payload[i]);
    }
    zedra::Event e(msg->tick, msg->tie_breaker, msg->type, std::move(payload));
    if (reducer_->submit(std::move(e))) {
      enqueued_ok_++;
    } else {
      if (drop_count_++ == 0) {
        RCLCPP_WARN(get_logger(), "zedra queue full, dropping event (tick=%" PRIu64 ")", msg->tick);
      }
    }
}

void ZedraBridgeNode::publish_snapshot_meta() {
    auto snap = reducer_->get_snapshot();
    if (!snap) return;
    auto stats = reducer_->get_stats();
    zedra_ros::msg::SnapshotMeta meta;
    meta.version = snap->version();
    meta.hash = to_hex(snap->hash());
    meta.events_enqueued = enqueued_ok_;
    meta.events_applied = stats.events_applied;
    meta.events_dropped = drop_count_;
    meta.first_tick = stats.first_tick;
    meta.last_tick = stats.last_tick;
    meta.key_count = static_cast<uint32_t>(snap->data().size());
    meta.window_ticks = stats.window_ticks;
    meta_pub_->publish(meta);
}

}  // namespace zedra_ros
