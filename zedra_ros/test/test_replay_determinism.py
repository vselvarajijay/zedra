#!/usr/bin/env python3
"""
Replay determinism test: publish 10k ZedraEvents, record bag, replay twice,
assert both runs produce the same SnapshotMeta.hash.
"""
import os
import signal
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

# Skip when running amd64 image under Rosetta on ARM (middleware allocates huge payload on deserialize).
# Exit 77 so ament_add_test SKIP_RETURN_CODE marks the test as skipped, not failed.
if os.environ.get("SKIP_REPLAY_DETERMINISM") == "1":
    print("Skipping replay test: SKIP_REPLAY_DETERMINISM=1 (e.g. Docker amd64 on ARM host)", file=sys.stderr)
    sys.exit(77)


NUM_EVENTS = 10000
BAG_TOPIC = "/zedra/inbound_events"
META_TOPIC = "/zedra/snapshot_meta"
# Smaller queue reduces bridge startup allocation (~4x less) to avoid OOM/bad_alloc in CI.
QUEUE_CAPACITY = 16384
# Tuned for stability in Docker/CI (slower startup and replay)
SPIN_TIMEOUT_SEC = 40
BRIDGE_STARTUP_SEC = 5
PLAY_START_DELAY_SEC = 8  # wait after starting play so DDS discovery completes before we sample
PLAY_JOIN_TIMEOUT_SEC = 15


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


def _meta_header(meta):
    """Format determinism header line from a SnapshotMeta message."""
    window = "off" if meta.window_ticks == 0 else str(meta.window_ticks)
    return (
        "enq=%s applied=%s dropped=%s tick=[%s..%s] keys=%s window=%s hash=%s"
        % (
            meta.events_enqueued,
            meta.events_applied,
            meta.events_dropped,
            meta.first_tick,
            meta.last_tick,
            meta.key_count,
            window,
            meta.hash,
        )
    )


def collect_until_fully_reduced(timeout_sec, required_applied, required_dropped=0):
    """
    Subscribe to SnapshotMeta; wait until timeout. Return (final_meta, last_meta).
    final_meta: last meta where events_applied >= required_applied and events_dropped <= required_dropped, or None.
    last_meta: last meta received (for error reporting).
    """
    rclpy.init()
    node = Node("replay_test_subscriber")
    fully_reduced = [None]  # last meta that met the requirement
    last_any = [None]

    def cb(msg):
        last_any[0] = msg
        if msg.events_applied >= required_applied and msg.events_dropped <= required_dropped:
            fully_reduced[0] = msg

    node.create_subscription(SnapshotMeta, META_TOPIC, cb, 10)
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.2)
    node.destroy_node()
    rclpy.shutdown()
    return fully_reduced[0], last_any[0]


def run_bag_play(bag_path):
    """Run ros2 bag play in subprocess (blocking until play finishes). No --loop = play once (default in ROS 2 Kilted)."""
    try:
        result = subprocess.run(
            ["ros2", "bag", "play", bag_path],
            capture_output=True,
            timeout=60,
            text=True,
        )
        if result.returncode != 0:
            print("ros2 bag play failed (exit %d):" % result.returncode, file=sys.stderr)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
            if result.stdout:
                print(result.stdout, file=sys.stderr)
            sys.exit(1)
    except subprocess.TimeoutExpired as e:
        print("ros2 bag play timed out:", e, file=sys.stderr)
        sys.exit(1)


def main():
    bag_dir = tempfile.mkdtemp(prefix="zedra_replay_test_")
    bag_path = os.path.join(bag_dir, "replay_bag")

    # 1. Record bag: run ros2 bag record, then publish 10k events
    proc_record = subprocess.Popen(
        ["ros2", "bag", "record", BAG_TOPIC, "-o", bag_path, "-s", "sqlite3"],
        cwd=bag_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    time.sleep(1)
    publish_events()
    time.sleep(1)
    proc_record.terminate()
    try:
        proc_record.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc_record.kill()
        proc_record.wait()
    record_stderr = proc_record.stderr.read() if proc_record.stderr else ""

    # Find the actual bag folder (ros2 bag record creates bag_path_0 etc. sometimes)
    if not os.path.isdir(bag_path) and os.path.exists(bag_dir):
        for name in os.listdir(bag_dir):
            if name.startswith("replay_bag"):
                bag_path = os.path.join(bag_dir, name)
                break
    if not os.path.isdir(bag_path):
        print("ERROR: bag not created at", bag_path, file=sys.stderr)
        if record_stderr:
            print("ros2 bag record stderr:", record_stderr, file=sys.stderr)
        sys.exit(1)

    results = []  # list of (final_meta, last_meta) per run

    for run in range(2):
        proc_bridge = subprocess.Popen(
            [
                "ros2", "run", "zedra_ros", "zedra_ros_node",
                "--ros-args", "-p", "queue_capacity:=%d" % QUEUE_CAPACITY,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=os.environ,
            text=True,
        )
        time.sleep(0.5)  # allow quick crash to set returncode
        if proc_bridge.returncode is not None:
            print("ERROR: could not start zedra_ros_node (exit %s)" % proc_bridge.returncode, file=sys.stderr)
            if proc_bridge.stderr:
                print(proc_bridge.stderr.read(), file=sys.stderr)
            sys.exit(1)

        time.sleep(BRIDGE_STARTUP_SEC)
        play_thread = threading.Thread(target=run_bag_play, args=(bag_path,))
        play_thread.start()
        time.sleep(PLAY_START_DELAY_SEC)
        final_meta, last_meta = collect_until_fully_reduced(
            SPIN_TIMEOUT_SEC, required_applied=NUM_EVENTS, required_dropped=0
        )
        results.append((final_meta, last_meta))
        play_thread.join(timeout=PLAY_JOIN_TIMEOUT_SEC)
        proc_bridge.terminate()
        try:
            proc_bridge.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc_bridge.kill()
            proc_bridge.wait()
        if proc_bridge.returncode is not None and proc_bridge.returncode != 0 and proc_bridge.returncode != -signal.SIGTERM:
            bridge_stderr = proc_bridge.stderr.read() if proc_bridge.stderr else ""
            if bridge_stderr:
                print("zedra_ros_node stderr (run %d):" % (run + 1), bridge_stderr, file=sys.stderr)

    for run, (final_meta, last_meta) in enumerate(results):
        if last_meta is None:
            print("ERROR: run %d did not receive any SnapshotMeta" % (run + 1), file=sys.stderr)
            sys.exit(1)
        print("run %d: %s" % (run + 1, _meta_header(last_meta)))
        if final_meta is None:
            print(
                "FAIL: run %d did not process full log (need applied>=%d dropped=0)" % (run + 1, NUM_EVENTS),
                file=sys.stderr,
            )
            sys.exit(1)

    if results[0][0].hash != results[1][0].hash:
        print(
            "FAIL: hash mismatch run1=%s run2=%s (determinism broken; on ARM Mac with amd64 image this can be due to Rosetta emulation)"
            % (results[0][0].hash, results[1][0].hash),
            file=sys.stderr,
        )
        sys.exit(1)
    print("PASS: both replays produced identical hash: %s" % results[0][0].hash)
    try:
        import shutil
        shutil.rmtree(bag_dir, ignore_errors=True)
    except Exception:
        pass
    sys.exit(0)


if __name__ == "__main__":
    main()
