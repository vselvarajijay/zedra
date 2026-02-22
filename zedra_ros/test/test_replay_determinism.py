#!/usr/bin/env python3
"""
Replay determinism test: publish 10k ZedraEvents, record bag, replay twice,
assert both runs produce the same SnapshotMeta.hash.
"""
import os
import subprocess
import sys
import tempfile
import threading
import time

try:
    import rclpy
    from rclpy.node import Node
    from zedra_ros.msg import SnapshotMeta, ZedraEvent
except ImportError:
    print("Skipping replay test: rclpy or zedra_ros.msg not available (source install/setup.bash)", file=sys.stderr)
    sys.exit(0)


NUM_EVENTS = 10000
BAG_TOPIC = "/zedra/inbound_events"
META_TOPIC = "/zedra/snapshot_meta"
SPIN_TIMEOUT_SEC = 25
BRIDGE_STARTUP_SEC = 2


def publish_events():
    """Publish NUM_EVENTS deterministic events (type 0 = upsert): tick 0..NUM_EVENTS-1, tie_breaker 0."""
    rclpy.init()
    node = Node("replay_test_publisher")
    pub = node.create_publisher(ZedraEvent, BAG_TOPIC, 10)
    time.sleep(0.5)
    for i in range(NUM_EVENTS):
        msg = ZedraEvent()
        msg.tick = i
        msg.tie_breaker = 0
        msg.type = 0
        key = (i % 3) + 1
        value = f"val_{i}".encode()
        msg.payload = list(key.to_bytes(8, "little")) + list(value)
        pub.publish(msg)
        if (i + 1) % 2000 == 0:
            rclpy.spin_once(node, timeout_sec=0)
    time.sleep(0.5)
    node.destroy_node()
    rclpy.shutdown()


def collect_last_hash(timeout_sec):
    """Subscribe to SnapshotMeta and return the last hash received after spinning for timeout_sec."""
    rclpy.init()
    node = Node("replay_test_subscriber")
    last_hash = [None]
    last_version = [None]

    def cb(msg):
        last_hash[0] = msg.hash
        last_version[0] = msg.version

    node.create_subscription(SnapshotMeta, META_TOPIC, cb, 10)
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.2)
    node.destroy_node()
    rclpy.shutdown()
    return last_hash[0], last_version[0]


def run_bag_play(bag_path):
    """Run ros2 bag play in subprocess (blocking until play finishes)."""
    subprocess.run(
        ["ros2", "bag", "play", bag_path, "--no-loop"],
        check=True,
        capture_output=True,
        timeout=60,
    )


def main():
    bag_dir = tempfile.mkdtemp(prefix="zedra_replay_test_")
    bag_path = os.path.join(bag_dir, "replay_bag")

    # 1. Record bag: run ros2 bag record, then publish 10k events
    proc_record = subprocess.Popen(
        ["ros2", "bag", "record", BAG_TOPIC, "-o", bag_path, "-s", "sqlite3"],
        cwd=bag_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)
    publish_events()
    time.sleep(1)
    proc_record.terminate()
    proc_record.wait(timeout=5)

    # Find the actual bag folder (ros2 bag record creates bag_path_0 etc. sometimes)
    if not os.path.isdir(bag_path) and os.path.exists(bag_dir):
        for name in os.listdir(bag_dir):
            if name.startswith("replay_bag"):
                bag_path = os.path.join(bag_dir, name)
                break
    if not os.path.isdir(bag_path):
        print("ERROR: bag not created at", bag_path, file=sys.stderr)
        sys.exit(1)

    last_hashes = []

    for run in range(2):
        proc_bridge = subprocess.Popen(
            ["ros2", "run", "zedra_ros", "zedra_ros_node"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=os.environ,
        )
        if proc_bridge.returncode is not None:
            print("ERROR: could not start zedra_ros_node", file=sys.stderr)
            sys.exit(1)

        time.sleep(BRIDGE_STARTUP_SEC)
        play_thread = threading.Thread(target=run_bag_play, args=(bag_path,))
        play_thread.start()
        time.sleep(1)
        h, v = collect_last_hash(SPIN_TIMEOUT_SEC)
        last_hashes.append((h, v))
        play_thread.join(timeout=5)
        proc_bridge.terminate()
        proc_bridge.wait(timeout=5)

    if last_hashes[0][0] is None or last_hashes[1][0] is None:
        print("ERROR: did not receive SnapshotMeta in one or both runs", file=sys.stderr)
        sys.exit(1)
    if last_hashes[0][0] != last_hashes[1][0]:
        print("FAIL: hash mismatch run1=%s run2=%s" % (last_hashes[0][0], last_hashes[1][0]), file=sys.stderr)
        sys.exit(1)
    print("PASS: both replays produced identical hash:", last_hashes[0][0])
    try:
        import shutil
        shutil.rmtree(bag_dir, ignore_errors=True)
    except Exception:
        pass
    sys.exit(0)


if __name__ == "__main__":
    main()
