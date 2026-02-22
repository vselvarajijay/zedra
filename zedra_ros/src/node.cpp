#include <zedra_ros/zedra_bridge_node.hpp>
#include <rclcpp/executors.hpp>
#include <rclcpp/rclcpp.hpp>
#include <memory>

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<zedra_ros::ZedraBridgeNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
