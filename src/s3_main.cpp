#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLESecurity.h>
#include <ESP_I2S.h>
#include <esp_wifi.h>
#include <host/ble_store.h>
#include <picotts.h>
#include <picotts_lang.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cctype>

#include "guardian/speech_server.h"

#ifndef GUARDIAN_S3_RX_PIN
#define GUARDIAN_S3_RX_PIN 2
#endif
#ifndef GUARDIAN_S3_TX_PIN
#define GUARDIAN_S3_TX_PIN 1
#endif
#ifndef GUARDIAN_S3_BUSY_PIN
#define GUARDIAN_S3_BUSY_PIN 7
#endif
#ifndef GUARDIAN_S3_RGB_PIN
#define GUARDIAN_S3_RGB_PIN 48
#endif
#ifndef MAX98357A_LRC_PIN
#define MAX98357A_LRC_PIN 4
#endif
#ifndef MAX98357A_BCLK_PIN
#define MAX98357A_BCLK_PIN 5
#endif
#ifndef MAX98357A_DIN_PIN
#define MAX98357A_DIN_PIN 6
#endif

namespace {

class PicoI2sEngine final : public guardian::SpeechEngine {
 public:
  bool begin() {
    instance_ = this;
    i2s_.setPins(MAX98357A_BCLK_PIN, MAX98357A_LRC_PIN, MAX98357A_DIN_PIN);
    if (!i2s_.begin(I2S_MODE_STD, PICOTTS_SAMPLE_FREQ_HZ,
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                    I2S_STD_SLOT_BOTH)) return false;
    picotts_set_idle_notify(on_idle);
    picotts_set_error_notify(on_error);
    return true;
  }

  bool start(const guardian::protocol::SpeakRequest& request) override {
    if (speaking_) return false;
    if (!running_ || language_ != request.language) {
      if (running_) picotts_shutdown();
      running_ = false;
      if (!picotts_set_language(request.language.c_str()) ||
          !picotts_init(2, on_samples, 0)) return false;
      language_ = request.language;
      running_ = true;
    }
    volume_ = request.volume;
    std::string text = request.text;
    for (char& c : text) if (c == '<' || c == '>') c = ' ';
    if (request.pitch != 100)
      text = "<pitch level=\"" + std::to_string(request.pitch) + "\">" +
             text + "</pitch>";
    if (request.speed != 100)
      text = "<speed level=\"" + std::to_string(request.speed) + "\">" +
             text + "</speed>";
    idle_ = false;
    failed_ = false;
    speaking_ = true;
    picotts_add(text.c_str(), text.size() + 1);
    return true;
  }

  void stop() override {
    if (running_) picotts_shutdown();
    running_ = false;
    speaking_ = false;
    idle_ = false;
  }

  bool speaking() const override {
    if (!speaking_) return false;
    if (failed_) {
      const_cast<PicoI2sEngine*>(this)->speaking_ = false;
      return false;
    }
    // Arduino's I2S wrapper exposes no nonblocking drain query. Each write
    // blocks for DMA space, so retaining BUSY for 250 ms after the last block
    // conservatively covers the remaining DMA buffers at 16 kHz.
    if (idle_ && static_cast<int32_t>(millis() - drain_after_ms_) >= 0)
      const_cast<PicoI2sEngine*>(this)->speaking_ = false;
    return speaking_;
  }

  void set_volume(uint8_t volume) override { master_volume_ = volume; }

 private:
  static void on_samples(int16_t* samples, unsigned count) {
    if (instance_ == nullptr) return;
    const int32_t gain =
        instance_->volume_ * instance_->master_volume_ * 256 / 10000;
    int16_t stereo[256];
    if (count > 128) count = 128;
    for (unsigned i = 0; i < count; ++i) {
      const int16_t sample =
          static_cast<int16_t>((samples[i] * gain) >> 8);
      stereo[2 * i] = sample;
      stereo[2 * i + 1] = sample;
    }
    instance_->i2s_.write(reinterpret_cast<uint8_t*>(stereo),
                          count * 2 * sizeof(int16_t));
    instance_->drain_after_ms_ = millis() + 250;
  }
  static void on_idle() { if (instance_ != nullptr) instance_->idle_ = true; }
  static void on_error() { if (instance_ != nullptr) instance_->failed_ = true; }

  static PicoI2sEngine* instance_;
  mutable I2SClass i2s_;
  std::string language_;
  volatile uint8_t volume_ = 50;
  volatile uint8_t master_volume_ = 100;
  volatile bool idle_ = false;
  volatile bool failed_ = false;
  mutable bool speaking_ = false;
  bool running_ = false;
  volatile uint32_t drain_after_ms_ = 0;
};
PicoI2sEngine* PicoI2sEngine::instance_ = nullptr;

PicoI2sEngine engine;
std::unique_ptr<guardian::SpeechServer> server;
guardian::protocol::Parser* parser = nullptr;
// Keep the board-to-board protocol off UART0. On recent Arduino-ESP32 cores,
// UART0 is also owned by the ESP32-S3 USB Serial/JTAG console and traffic can
// disappear when that USB port is connected. UART1 can still be routed to the
// GPIO2/GPIO1 board-to-board harness.
HardwareSerial guardian_serial(1);
void send_frame(const guardian::protocol::Frame& frame, void*);

class BluetoothManager final {
 public:
  struct ObservedDevice {
    String address;
    uint8_t address_type = 0;
    String name;
    int rssi = -127;
    bool connectable = false;
    bool advertises_hid = false;
  };

  class ScanCallbacks final : public BLEAdvertisedDeviceCallbacks {
   public:
    explicit ScanCallbacks(BluetoothManager* owner) : owner_(owner) {}
    void onResult(BLEAdvertisedDevice device) override {
      owner_->observe(device);
    }

   private:
    BluetoothManager* owner_;
  };

  bool begin() {
    BLEDevice::init("Guardian-S3");
    scan_ = BLEDevice::getScan();
    scan_->setActiveScan(true);
    // Arduino-ESP32's default BLEScan result collector keeps only the first
    // packet received from each address. For active scans that packet is often
    // the nameless advertisement; the following scan response containing
    // "QXS-001" is then discarded. Receive duplicates and merge them here.
    scan_->setAdvertisedDeviceCallbacks(&scan_callbacks_, true, true);
    scan_->setInterval(100);
    scan_->setWindow(80);
    enabled_ = true;
    BLESecurity::setCapability(ESP_IO_CAP_NONE);
    BLESecurity::setAuthenticationMode(true, false, false);
    BLESecurity::setForceAuthentication(true);
    instance_ = this;
    return true;
  }

  bool handles(guardian::protocol::Type type) const {
    using guardian::protocol::Type;
    return (type >= Type::kBtGetStatus && type <= Type::kBtForget) ||
           type == Type::kBtTest;
  }

  void handle(const guardian::protocol::Frame& frame) {
    using guardian::protocol::Type;
    switch (frame.type) {
      case Type::kBtGetStatus:
        if (!frame.payload.empty()) return error(frame.sequence, 1);
        ack(frame.sequence);
        state(frame.sequence);
        done(frame.sequence);
        return;
      case Type::kBtSetEnabled:
        if (frame.payload.size() != 1 || frame.payload[0] > 1)
          return error(frame.sequence, 1);
        ack(frame.sequence);
        set_enabled(frame.payload[0] != 0);
        state(frame.sequence);
        done(frame.sequence);
        return;
      case Type::kBtScan:
        if (frame.payload.size() != 1 || frame.payload[0] < 1 ||
            frame.payload[0] > 30 || !enabled_ || scanning_)
          return error(frame.sequence, 1);
        ack(frame.sequence);
        send({Type::kStarted, frame.sequence, {}});
        scan_sequence_ = frame.sequence;
        scan_complete_ = false;
        scan_seconds_ = frame.payload[0];
        observed_devices_.clear();
        scan_->clearResults();
        scanning_ = xTaskCreatePinnedToCore(scan_task, "guardian-bt-scan",
                                            6144, this, 1, &scan_task_handle_,
                                            0) == pdPASS;
        if (!scanning_) {
          scan_sequence_ = 0;
          error(frame.sequence, 5);
        }
        return;
      case Type::kBtScanStop:
        if (!frame.payload.empty()) return error(frame.sequence, 1);
        ack(frame.sequence);
        if (enabled_ && scanning_) scan_->stop();
        scanning_ = false;
        scan_sequence_ = 0;
        done(frame.sequence);
        return;
      case Type::kBtConnect:
        if (frame.payload.size() != 18 || !enabled_ || scanning_)
          return error(frame.sequence, 1);
        ack(frame.sequence);
        send({Type::kStarted, frame.sequence, {}});
        connect(frame);
        return;
      case Type::kBtDisconnect:
        if (!frame.payload.empty()) return error(frame.sequence, 1);
        ack(frame.sequence);
        if (client_ != nullptr && client_->isConnected()) client_->disconnect();
        done(frame.sequence);
        return;
      case Type::kBtForget:
        if (!frame.payload.empty()) return error(frame.sequence, 1);
        ack(frame.sequence);
        if (client_ != nullptr && client_->isConnected()) client_->disconnect();
        ble_store_clear();
        done(frame.sequence);
        return;
      case Type::kBtTest:
        if (!frame.payload.empty()) return error(frame.sequence, 1);
        if (last_hid_event_.empty()) return error(frame.sequence, 5);
        ack(frame.sequence);
        send({Type::kBtHidEvent, frame.sequence, last_hid_event_});
        done(frame.sequence);
        return;
      default:
        return error(frame.sequence, 4);
    }
  }

  void poll() {
    if (hid_pending_) {
      std::vector<uint8_t> event;
      portENTER_CRITICAL(&hid_mux_);
      event.assign(pending_hid_event_.begin(),
                   pending_hid_event_.begin() + pending_hid_length_);
      hid_pending_ = false;
      portEXIT_CRITICAL(&hid_mux_);
      last_hid_event_ = event;
      send({guardian::protocol::Type::kBtHidEvent, 0, event});
    }
    if (connect_complete_) {
      connect_complete_ = false;
      const uint16_t sequence = connect_sequence_;
      connect_sequence_ = 0;
      connect_succeeded_ ? done(sequence) : error(sequence, 5);
    }
    if (!scan_complete_) return;
    scan_complete_ = false;
    scanning_ = false;
    {
      for (const auto& device : observed_devices_) {
        // The UI is for pairing controllers, not observing beacon traffic.
        // Excluding non-connectable advertisements prevents users from being
        // offered devices which can only time out when Connect is pressed.
        if (!device.connectable) continue;
        const String& address = device.address;
        String name = device.name;
        // Advertisement data is untrusted binary input. Some nearby devices
        // publish control bytes or invalid UTF-8 in the local-name field;
        // forwarding those bytes would make the HALMET status JSON invalid.
        String safe_name;
        safe_name.reserve(name.length());
        for (size_t byte_index = 0; byte_index < name.length(); ++byte_index) {
          const uint8_t value = static_cast<uint8_t>(name[byte_index]);
          if (value >= 0x20 && value <= 0x7e) safe_name += static_cast<char>(value);
        }
        name = safe_name;
        // Many low-power remotes omit their local name while in pairing mode,
        // but still advertise the standard Human Interface Device service.
        // Surface that fact so the HALMET UI can distinguish a remote from
        // unrelated nameless beacons without attempting blind connections.
        if (name.length() == 0 && device.advertises_hid) {
          name = "BLE HID device";
        }
        std::vector<uint8_t> payload;
        payload.insert(payload.end(), address.c_str(),
                       address.c_str() + address.length());
        payload.push_back(device.address_type);
        payload.push_back(static_cast<uint8_t>(device.rssi));
        const size_t name_length = min(static_cast<size_t>(name.length()),
                                       static_cast<size_t>(48));
        payload.insert(payload.end(), name.c_str(), name.c_str() + name_length);
        send({guardian::protocol::Type::kBtDevice, scan_sequence_, payload});
      }
    }
    done(scan_sequence_);
    scan_sequence_ = 0;
    scan_->clearResults();
  }

 private:
  void observe(BLEAdvertisedDevice& incoming) {
    const String address = incoming.getAddress().toString();
    auto found = std::find_if(observed_devices_.begin(), observed_devices_.end(),
                              [&](const ObservedDevice& item) {
                                return item.address == address;
                              });
    if (found == observed_devices_.end()) {
      observed_devices_.push_back({});
      found = observed_devices_.end() - 1;
      found->address = address;
      found->address_type = static_cast<uint8_t>(incoming.getAddressType());
    }
    found->rssi = incoming.getRSSI();
    found->connectable = found->connectable || incoming.isConnectable();
    found->advertises_hid =
        found->advertises_hid ||
        incoming.isAdvertisingService(BLEUUID((uint16_t)0x1812));
    if (incoming.haveName() && incoming.getName().length() > 0) {
      found->name = incoming.getName();
    }
  }

  static bool valid_address(const uint8_t* value) {
    for (size_t index = 0; index < 17; ++index) {
      if (index % 3 == 2) {
        if (value[index] != ':') return false;
      } else if (!isxdigit(value[index])) {
        return false;
      }
    }
    return true;
  }

  static void scan_task(void* argument) {
    auto* self = static_cast<BluetoothManager*>(argument);
    self->scan_->start(self->scan_seconds_, false);
    self->scan_task_handle_ = nullptr;
    self->scan_complete_ = true;
    vTaskDelete(nullptr);
  }

  void set_enabled(bool enabled) {
    if (enabled == enabled_) return;
    if (!enabled) {
      if (scanning_) scan_->stop();
      if (client_ != nullptr && client_->isConnected()) client_->disconnect();
      BLEDevice::deinit(false);
      scan_ = nullptr;
      scanning_ = false;
      enabled_ = false;
      return;
    }
    begin();
  }

  void connect(const guardian::protocol::Frame& frame) {
    if (!valid_address(frame.payload.data()) || frame.payload[17] > 3) {
      error(frame.sequence, 1);
      return;
    }
    memcpy(connect_address_, frame.payload.data(), 17);
    connect_address_[17] = '\0';
    connect_address_type_ = frame.payload[17];
    connect_sequence_ = frame.sequence;
    connect_complete_ = false;
    if (xTaskCreatePinnedToCore(connect_task, "guardian-bt-connect", 8192,
                                this, 1, &connect_task_handle_, 0) != pdPASS) {
      connect_sequence_ = 0;
      error(frame.sequence, 5);
    }
  }

  static void connect_task(void* argument) {
    auto* self = static_cast<BluetoothManager*>(argument);
    self->connect_succeeded_ = self->connect_selected();
    self->connect_task_handle_ = nullptr;
    self->connect_complete_ = true;
    vTaskDelete(nullptr);
  }

  bool connect_selected() {
    if (client_ == nullptr) client_ = BLEDevice::createClient();
    if (client_->isConnected()) client_->disconnect();
    const bool connected = client_->connect(
        BLEAddress(String(connect_address_)), connect_address_type_, 5000);
    if (connected) {
      BLESecurity::startSecurity(client_->getConnId());
      BLESecurity::waitForAuthenticationComplete(5000);
      subscribe_input_reports();
    }
    return connected;
  }

  void subscribe_input_reports() {
    // Standard HID remotes notify through 0x1812/0x2A4D, while several small
    // media/steering remotes use vendor services. Treat any notifying or
    // indicating characteristic as an input source; the characteristic handle
    // is included in the event so mappings remain unambiguous.
    auto* services = client_->getServices();
    if (services == nullptr) return;
    for (auto& service_entry : *services) {
      BLERemoteService* service = service_entry.second;
      if (service == nullptr) continue;
      auto* characteristics = service->getCharacteristics();
      if (characteristics == nullptr) continue;
      for (auto& entry : *characteristics) {
        BLERemoteCharacteristic* characteristic = entry.second;
        if (characteristic == nullptr) continue;
        if (characteristic->canNotify()) {
          characteristic->registerForNotify(hid_notify, true);
        } else if (characteristic->canIndicate()) {
          characteristic->registerForNotify(hid_notify, false);
        }
      }
    }
  }

  static void hid_notify(BLERemoteCharacteristic* characteristic,
                         uint8_t* data, size_t length, bool) {
    if (instance_ == nullptr || characteristic == nullptr) return;
    if (length > 60) length = 60;
    const uint16_t handle = characteristic->getHandle();
    portENTER_CRITICAL(&instance_->hid_mux_);
    instance_->pending_hid_event_[0] = static_cast<uint8_t>(handle);
    instance_->pending_hid_event_[1] = static_cast<uint8_t>(handle >> 8);
    memcpy(instance_->pending_hid_event_.data() + 2, data, length);
    instance_->pending_hid_length_ = length + 2;
    instance_->hid_pending_ = true;
    portEXIT_CRITICAL(&instance_->hid_mux_);
  }

  void state(uint16_t sequence) {
    uint8_t flags = enabled_ ? 0x01 : 0;
    if (scanning_) flags |= 0x02;
    if (client_ != nullptr && client_->isConnected()) flags |= 0x04;
    flags |= 0x08;  // Wi-Fi is intentionally disabled by this firmware.
    int bonds = 0;
    if (enabled_) ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonds);
    send({guardian::protocol::Type::kBtState, sequence,
          {1, flags, static_cast<uint8_t>(max(0, min(bonds, 255)))}});
  }

  static void send(const guardian::protocol::Frame& frame) {
    send_frame(frame, nullptr);
  }
  static void ack(uint16_t sequence) {
    send({guardian::protocol::Type::kAck, sequence, {}});
  }
  static void done(uint16_t sequence) {
    send({guardian::protocol::Type::kDone, sequence, {}});
  }
  static void error(uint16_t sequence, uint8_t code) {
    send({guardian::protocol::Type::kError, sequence, {code}});
  }

  static BluetoothManager* instance_;
  BLEScan* scan_ = nullptr;
  ScanCallbacks scan_callbacks_{this};
  std::vector<ObservedDevice> observed_devices_;
  BLEClient* client_ = nullptr;
  volatile bool scan_complete_ = false;
  bool enabled_ = false;
  bool scanning_ = false;
  uint16_t scan_sequence_ = 0;
  uint8_t scan_seconds_ = 5;
  TaskHandle_t scan_task_handle_ = nullptr;
  char connect_address_[18]{};
  uint8_t connect_address_type_ = 0;
  uint16_t connect_sequence_ = 0;
  TaskHandle_t connect_task_handle_ = nullptr;
  volatile bool connect_complete_ = false;
  bool connect_succeeded_ = false;
  portMUX_TYPE hid_mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile bool hid_pending_ = false;
  std::array<uint8_t, 62> pending_hid_event_{};
  size_t pending_hid_length_ = 0;
  std::vector<uint8_t> last_hid_event_;
};
BluetoothManager* BluetoothManager::instance_ = nullptr;
BluetoothManager bluetooth;

void send_frame(const guardian::protocol::Frame& frame, void*) {
  std::vector<uint8_t> wire;
  if (guardian::protocol::encode(frame, &wire)) {
    guardian_serial.write(wire.data(), wire.size());
    // Mirror the framed protocol to USB for guardian_cli.py and the optional
    // host router. Production traffic remains on UART1 GPIO2/GPIO1.
    Serial.write(wire.data(), wire.size());
  }
}
void receive_frame(const guardian::protocol::Frame& frame, void*) {
  if (bluetooth.handles(frame.type)) {
    bluetooth.handle(frame);
    return;
  }
  if (server != nullptr) server->handle(frame);
}
void receive_error(guardian::protocol::ParseError, void*) {}
void set_rgb(uint8_t red, uint8_t green, uint8_t blue, void*) {
  rgbLedWrite(GUARDIAN_S3_RGB_PIN, red, green, blue);
}

}  // namespace

void setup() {
  // The S3 is an offline speech/BLE coprocessor. Keep Wi-Fi stopped so it cannot
  // associate or start an IP stack; BLE remains available as a central.
  // esp_wifi_stop is idempotent; no Wi-Fi initialization follows this call.
  esp_wifi_stop();
  Serial.begin(115200);
  guardian_serial.begin(115200, SERIAL_8N1, GUARDIAN_S3_RX_PIN,
                        GUARDIAN_S3_TX_PIN);
  pinMode(GUARDIAN_S3_BUSY_PIN, OUTPUT);
  digitalWrite(GUARDIAN_S3_BUSY_PIN, LOW);
  // Bluetooth and the host protocol must remain available even when the I2S
  // amplifier is absent or fails to initialize. Speech requests will report
  // the engine failure through the normal SpeechServer error path.
  engine.begin();
  rgbLedWrite(GUARDIAN_S3_RGB_PIN, 0, 0, 0);
  bluetooth.begin();
  server = std::make_unique<guardian::SpeechServer>(
      &engine, send_frame, nullptr, set_rgb, nullptr);
  parser = new guardian::protocol::Parser(receive_frame, receive_error, nullptr);
  send_frame({guardian::protocol::Type::kHello, 0, {1, 8}}, nullptr);
}

void loop() {
  while (parser != nullptr && guardian_serial.available() > 0) {
    const int value = guardian_serial.read();
    if (value < 0) break;
    const uint8_t byte = static_cast<uint8_t>(value);
    parser->feed(&byte, 1);
  }
  while (parser != nullptr && Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    const uint8_t byte = static_cast<uint8_t>(value);
    parser->feed(&byte, 1);
  }
  if (server != nullptr) {
    server->poll();
    digitalWrite(GUARDIAN_S3_BUSY_PIN,
                 engine.speaking() || server->queued() > 0 ? HIGH : LOW);
  }
  bluetooth.poll();
  delay(1);
}
