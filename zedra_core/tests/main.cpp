#include <iostream>

extern void run_unit_event_tests();
extern void run_unit_state_tests();
extern void run_unit_lock_free_queue_tests();
extern void run_replay_tests();
extern void run_behavioral_guarantees_tests();
extern void run_example_smoke_tests();
extern void run_concurrent_tests();

int main() {
  std::cerr << "zedra_core_tests\n";
  std::cerr << "unit event tests\n";
  run_unit_event_tests();
  std::cerr << "unit state tests\n";
  run_unit_state_tests();
  std::cerr << "unit lock_free_queue tests\n";
  run_unit_lock_free_queue_tests();
  std::cerr << "replay tests\n";
  run_replay_tests();
  std::cerr << "behavioral guarantees tests\n";
  run_behavioral_guarantees_tests();
  std::cerr << "example smoke tests\n";
  run_example_smoke_tests();
  std::cerr << "concurrent tests\n";
  run_concurrent_tests();
  std::cerr << "all tests passed\n";
  return 0;
}
