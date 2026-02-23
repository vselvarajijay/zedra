#ifndef ZEDRA_ROS__ZEDRA_BRIDGE_NODE_HPP
#define ZEDRA_ROS__ZEDRA_BRIDGE_NODE_HPP

#include <zedra/reducer.hpp>
#include <zedra_ros/msg/snapshot_meta.hpp>
#include <zedra_ros/msg/zedra_event.hpp>
#include <rclcpp/rclcpp.hpp>
#include <cstdint>
#include <memory>

namespace zedra_ros {

class ZedraBridgeNode : public rclcpp::Node {
 public:
  explicit ZedraBridgeNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~ZedraBridgeNode() override;

 private:
  void on_inbound_event(zedra_ros::msg::ZedraEvent::ConstSharedPtr msg);
  void publish_snapshot_meta();

  std::unique_ptr<zedra::Reducer> reducer_;
  rclcpp::Subscription<zedra_ros::msg::ZedraEvent>::SharedPtr sub_;
  rclcpp::Publisher<zedra_ros::msg::SnapshotMeta>::SharedPtr meta_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::uint64_t drop_count_{0};
  std::uint64_t enqueued_ok_{0};
};

}  // namespace zedra_ros

#endif  // ZEDRA_ROS__ZEDRA_BRIDGE_NODE_HPP
