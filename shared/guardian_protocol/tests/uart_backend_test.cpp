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
  guardian::UartSpeechBackend backend(&transport);
  guardian::protocol::SpeakRequest request{
      guardian::protocol::Priority::kHigh, 60, 80, 100, "it-IT", "Allarme"};
  assert(backend.speak(request));

  std::vector<guardian::protocol::Frame> decoded;
  auto capture = [](const guardian::protocol::Frame& f, void* p) {
    static_cast<std::vector<guardian::protocol::Frame>*>(p)->push_back(f);
  };
  guardian::protocol::Parser parser(capture, nullptr, &decoded);
  parser.feed(transport.output.data(), transport.output.size());
  assert(decoded.size() == 1 && decoded[0].type == guardian::protocol::Type::kSpeak);

  transport.receive({guardian::protocol::Type::kHello, 0, {}});
  transport.receive({guardian::protocol::Type::kStarted, decoded[0].sequence, {}});
  backend.poll();
  assert(backend.connected() && backend.speaking());
  transport.receive({guardian::protocol::Type::kDone, decoded[0].sequence, {}});
  backend.poll();
  assert(!backend.speaking());
}
