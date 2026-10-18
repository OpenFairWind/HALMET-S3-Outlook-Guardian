#include "guardian/uart_speech_backend.h"

namespace guardian {

UartSpeechBackend::UartSpeechBackend(ByteTransport* transport,
                                     uint16_t initial_sequence)
    : transport_(transport), parser_(on_frame, on_error, this),
      next_sequence_(initial_sequence == 0 ? 1 : initial_sequence) {}

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

bool UartSpeechBackend::set_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return send(protocol::Type::kSetRgb, next_sequence_++, {red, green, blue});
}

bool UartSpeechBackend::test_all() {
  return send(protocol::Type::kTestAll, next_sequence_++);
}

bool UartSpeechBackend::set_volume(uint8_t volume) {
  if (volume > 100) return false;
  master_volume_ = volume;
  volume_configured_ = true;
  return send(protocol::Type::kSetVolume, next_sequence_++, {volume});
}

bool UartSpeechBackend::bluetooth_get_status() {
  return send(protocol::Type::kBtGetStatus, next_sequence_++);
}

bool UartSpeechBackend::bluetooth_set_enabled(bool enabled) {
  return send(protocol::Type::kBtSetEnabled, next_sequence_++,
              {static_cast<uint8_t>(enabled)});
}

bool UartSpeechBackend::bluetooth_scan(uint8_t seconds) {
  if (seconds < 1 || seconds > 30) return false;
  bluetooth_devices_.clear();
  return send(protocol::Type::kBtScan, next_sequence_++, {seconds});
}

bool UartSpeechBackend::bluetooth_scan_stop() {
  return send(protocol::Type::kBtScanStop, next_sequence_++);
}

bool UartSpeechBackend::bluetooth_connect(const std::string& address,
                                           uint8_t address_type) {
  if (address.size() != 17 || address_type > 3) return false;
  std::vector<uint8_t> payload(address.begin(), address.end());
  payload.push_back(address_type);
  return send(protocol::Type::kBtConnect, next_sequence_++, payload);
}

bool UartSpeechBackend::bluetooth_disconnect() {
  return send(protocol::Type::kBtDisconnect, next_sequence_++);
}

bool UartSpeechBackend::bluetooth_forget() {
  return send(protocol::Type::kBtForget, next_sequence_++);
}

bool UartSpeechBackend::bluetooth_test() {
  return send(protocol::Type::kBtTest, next_sequence_++);
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
      self->connected_ = true;
      if (self->volume_configured_)
        self->send(protocol::Type::kSetVolume, self->next_sequence_++,
                   {self->master_volume_});
      break;
    case protocol::Type::kCaps:
    case protocol::Type::kPong: self->connected_ = true; break;
    case protocol::Type::kStarted: self->speaking_ = true; break;
    case protocol::Type::kDone:
    case protocol::Type::kError: self->speaking_ = false; break;
    case protocol::Type::kBtState:
      if (frame.payload.size() == 3 && frame.payload[0] == 1) {
        const uint8_t flags = frame.payload[1];
        self->bluetooth_state_ = {static_cast<bool>(flags & 0x01),
                                  static_cast<bool>(flags & 0x02),
                                  static_cast<bool>(flags & 0x04),
                                  static_cast<bool>(flags & 0x08),
                                  frame.payload[2]};
      }
      break;
    case protocol::Type::kBtDevice:
      if (frame.payload.size() >= 19) {
        self->bluetooth_devices_.push_back({
            std::string(frame.payload.begin(), frame.payload.begin() + 17),
            frame.payload[17], static_cast<int8_t>(frame.payload[18]),
            std::string(frame.payload.begin() + 19, frame.payload.end())});
      }
      break;
    case protocol::Type::kBtHidEvent:
      if (frame.payload.size() >= 3 && self->bluetooth_hid_callback_)
        self->bluetooth_hid_callback_(frame.payload);
      break;
    default: break;
  }
}

}  // namespace guardian
