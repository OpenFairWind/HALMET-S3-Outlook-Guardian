#include "guardian/uart_speech_backend.h"

#include <cassert>
#include <cstdint>
#include <vector>

struct Transport : guardian::ByteTransport {
  std::vector<uint8_t> input;
  std::vector<uint8_t> output;
  size_t at = 0;
  size_t available() const override { return input.size() - at; }
  int read() override { return at < input.size() ? input[at++] : -1; }
  size_t write(const uint8_t* p, size_t n) override {
    output.insert(output.end(), p, p + n); return n;
  }
  void receive(const guardian::protocol::Frame& frame) {
    std::vector<uint8_t> wire;
    assert(guardian::protocol::encode(frame, &wire));
    input.insert(input.end(), wire.begin(), wire.end());
  }
};

int main() {
  Transport transport;
  guardian::UartSpeechBackend backend(&transport, 0x4000);
  guardian::protocol::SpeakRequest request{
      guardian::protocol::Priority::kHigh, 60, 80, 100, "it-IT", "Allarme"};
  assert(backend.speak(request));
  assert(backend.set_rgb(10, 20, 30));
  assert(backend.test_all());
  assert(backend.set_volume(75));
  assert(!backend.set_volume(101));
  assert(backend.bluetooth_get_status());
  assert(backend.bluetooth_scan(3));
  assert(!backend.bluetooth_scan(0));
  assert(backend.bluetooth_connect("aa:bb:cc:dd:ee:ff", 1));
  assert(!backend.bluetooth_connect("bad", 0));
  assert(backend.bluetooth_test());
  assert(backend.ping());

  std::vector<guardian::protocol::Frame> decoded;
  auto capture = [](const guardian::protocol::Frame& f, void* p) {
    static_cast<std::vector<guardian::protocol::Frame>*>(p)->push_back(f);
  };
  guardian::protocol::Parser parser(capture, nullptr, &decoded);
  parser.feed(transport.output.data(), transport.output.size());
  assert(decoded.size() == 9 && decoded[0].type == guardian::protocol::Type::kSpeak);
  assert(decoded[0].sequence == 0x4000);
  assert(decoded[1].type == guardian::protocol::Type::kSetRgb);
  assert(decoded[1].payload == std::vector<uint8_t>({10, 20, 30}));
  assert(decoded[2].type == guardian::protocol::Type::kTestAll);
  assert(decoded[2].payload.empty());
  assert(decoded[3].type == guardian::protocol::Type::kSetVolume);
  assert(decoded[3].payload == std::vector<uint8_t>({75}));
  assert(decoded[4].type == guardian::protocol::Type::kBtGetStatus);
  assert(decoded[5].type == guardian::protocol::Type::kBtScan);
  assert(decoded[5].payload == std::vector<uint8_t>({3}));
  assert(decoded[6].type == guardian::protocol::Type::kBtConnect);
  assert(decoded[7].type == guardian::protocol::Type::kBtTest);
  assert(decoded[8].type == guardian::protocol::Type::kPing);
  assert(backend.last_sent_sequence() == decoded[8].sequence);

  std::vector<uint8_t> hid;
  backend.on_bluetooth_hid(
      [&hid](const std::vector<uint8_t>& report) { hid = report; });
  transport.receive({guardian::protocol::Type::kBtState, 5,
                     {1, 0x0d, 2}});
  transport.receive({guardian::protocol::Type::kBtDevice, 6,
                     {'a','a',':','b','b',':','c','c',':','d','d',':','e','e',':','f','f',
                      1, static_cast<uint8_t>(-42), 'R','e','m','o','t','e'}});
  transport.receive({guardian::protocol::Type::kBtHidEvent, 0,
                     {0x34, 0x12, 0x01, 0x02}});
  backend.poll();
  assert(backend.received_frames() == 3);
  assert(backend.bluetooth_state().enabled);
  assert(backend.bluetooth_state().connected);
  assert(backend.bluetooth_state().wifi_off);
  assert(backend.bluetooth_state().bonds == 2);
  assert(backend.bluetooth_devices().size() == 1);
  assert(backend.bluetooth_devices()[0].name == "Remote");
  assert(hid == std::vector<uint8_t>({0x34, 0x12, 0x01, 0x02}));

  const size_t before_restore = transport.output.size();
  transport.receive({guardian::protocol::Type::kHello, 0, {}});
  transport.receive({guardian::protocol::Type::kStarted, decoded[0].sequence, {}});
  backend.poll();
  assert(backend.received_frames() == 5);
  assert(backend.connected() && backend.speaking());
  std::vector<guardian::protocol::Frame> restored;
  guardian::protocol::Parser restore_parser(capture, nullptr, &restored);
  restore_parser.feed(transport.output.data() + before_restore,
                      transport.output.size() - before_restore);
  assert(restored.size() == 1);
  assert(restored[0].type == guardian::protocol::Type::kSetVolume);
  assert(restored[0].payload == std::vector<uint8_t>({75}));
  transport.receive({guardian::protocol::Type::kDone, decoded[0].sequence, {}});
  backend.poll();
  assert(backend.received_frames() == 6);
  assert(backend.last_terminal_sequence() == decoded[0].sequence);
  assert(!backend.last_terminal_was_error());
  assert(!backend.speaking());
}
