#ifndef GUARDIAN_UART_SPEECH_BACKEND_H_
#define GUARDIAN_UART_SPEECH_BACKEND_H_

#include "guardian/speech_backend.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace guardian {

class UartSpeechBackend final : public SpeechBackend {
 public:
  struct BluetoothState {
    bool enabled = false;
    bool scanning = false;
    bool connected = false;
    bool wifi_off = false;
    uint8_t bonds = 0;
  };
  struct BluetoothDevice {
    std::string address;
    uint8_t address_type = 0;
    int8_t rssi = 0;
    std::string name;
  };
  explicit UartSpeechBackend(ByteTransport* transport,
                             uint16_t initial_sequence = 1);
  bool speak(const protocol::SpeakRequest& request) override;
  bool stop() override;
  bool set_rgb(uint8_t red, uint8_t green, uint8_t blue);
  bool test_all();
  bool set_volume(uint8_t volume);
  bool bluetooth_get_status();
  bool bluetooth_set_enabled(bool enabled);
  bool bluetooth_scan(uint8_t seconds);
  bool bluetooth_scan_stop();
  bool bluetooth_connect(const std::string& address, uint8_t address_type);
  bool bluetooth_disconnect();
  bool bluetooth_forget();
  bool bluetooth_test();
  void on_bluetooth_hid(
      std::function<void(const std::vector<uint8_t>&)> callback) {
    bluetooth_hid_callback_ = std::move(callback);
  }
  const BluetoothState& bluetooth_state() const { return bluetooth_state_; }
  const std::vector<BluetoothDevice>& bluetooth_devices() const {
    return bluetooth_devices_;
  }
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
  uint8_t master_volume_ = 100;
  bool volume_configured_ = false;
  BluetoothState bluetooth_state_;
  std::vector<BluetoothDevice> bluetooth_devices_;
  std::function<void(const std::vector<uint8_t>&)> bluetooth_hid_callback_;
};

}  // namespace guardian
#endif
