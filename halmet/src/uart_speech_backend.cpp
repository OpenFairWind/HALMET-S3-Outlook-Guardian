#include "guardian/uart_speech_backend.h"

namespace guardian {

UartSpeechBackend::UartSpeechBackend(ByteTransport* transport)
    : transport_(transport), parser_(on_frame, on_error, this) {}

bool UartSpeechBackend::send(protocol::Type type, uint16_t sequence,
                             const std::vector<uint8_t>& payload) {
  if (transport_ == nullptr) return false;
  std::vector<uint8_t> wire;
  if (!protocol::encode({type, sequence, payload}, &wire)) return false;
  return transport_->write(wire.data(), wire.size()) == wire.size();
}

bool UartSpeechBackend::speak(const protocol::SpeakRequest& request) {
  std::vector<uint8_t> payload;
  if (!protocol::encode_speak(request, &payload)) return false;
  return send(protocol::Type::kSpeak, next_sequence_++, payload);
}

bool UartSpeechBackend::stop() {
  return send(protocol::Type::kStop, next_sequence_++);
}

void UartSpeechBackend::poll() {
  if (transport_ == nullptr) return;
  while (transport_->available() > 0) {
    const int byte = transport_->read();
    if (byte < 0) break;
    const uint8_t value = static_cast<uint8_t>(byte);
    parser_.feed(&value, 1);
  }
}

void UartSpeechBackend::on_frame(const protocol::Frame& frame, void* context) {
  auto* self = static_cast<UartSpeechBackend*>(context);
  switch (frame.type) {
    case protocol::Type::kHello:
    case protocol::Type::kCaps:
    case protocol::Type::kPong: self->connected_ = true; break;
    case protocol::Type::kStarted: self->speaking_ = true; break;
    case protocol::Type::kDone:
    case protocol::Type::kError: self->speaking_ = false; break;
    default: break;
  }
}

}  // namespace guardian
