#include <zedra/event.hpp>
#include <zedra/reducer.hpp>
#include <zedra/state.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

int main() {
  zedra::Reducer reducer(1024);
  reducer.start();

  // Producer: push a few events (type 0 = upsert: key + value in payload)
  auto upsert = [](std::uint64_t tick, std::uint64_t key, const char* value) {
    std::size_t len = std::strlen(value);
    std::vector<std::byte> payload(sizeof(key) + len);
    std::memcpy(payload.data(), &key, sizeof(key));
    std::memcpy(payload.data() + sizeof(key), value, len);
    return zedra::Event(tick, 0, 0, std::move(payload));
  };

  reducer.submit(upsert(0, 1, "hello"));
  reducer.submit(upsert(1, 2, "world"));
  reducer.submit(upsert(2, 1, "zedra"));

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto snapshot = reducer.get_snapshot();
  reducer.stop();

  if (snapshot) {
    std::cout << "version=" << snapshot->version()
              << " hash=0x" << std::hex << snapshot->hash() << std::dec << "\n";
    for (const auto& [k, v] : snapshot->data()) {
      std::string s(v.size(), '\0');
      for (size_t i = 0; i < v.size(); ++i)
        s[i] = static_cast<char>(static_cast<unsigned char>(v[i]));
      std::cout << "  key=" << k << " value=\"" << s << "\"\n";
    }
  }
  return 0;
}
