#ifndef GUARDIAN_UART_SPEECH_BACKEND_H_
#define GUARDIAN_UART_SPEECH_BACKEND_H_

#include "guardian/speech_backend.h"

#include <array>

namespace guardian {

class UartSpeechBackend final : public SpeechBackend {
 public:
  explicit UartSpeechBackend(ByteTransport* transport);
  bool speak(const protocol::SpeakRequest& request) override;
  bool stop() override;
  void poll() override;
  bool connected() const override { return connected_; }
  bool speaking() const override { return speaking_; }

 private:
  static void on_frame(const protocol::Frame& frame, void* context);
  static void on_error(protocol::ParseError, void*) {}
  bool send(protocol::Type type, uint16_t sequence,
            const std::vector<uint8_t>& payload = {});
  ByteTransport* transport_;
  protocol::Parser parser_;
  uint16_t next_sequence_ = 1;
  bool connected_ = false;
  bool speaking_ = false;
};

}  // namespace guardian
#endif
