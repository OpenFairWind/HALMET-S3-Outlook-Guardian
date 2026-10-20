#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <N2kMsg.h>
#include <NMEA2000.h>
#include <NMEA2000_esp32.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp32-hal-bt.h>
#include <esp_system.h>
#include <map>
#include <memory>
#include <vector>

#include "guardian/uart_speech_backend.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp/signalk/signalk_listener.h"
#include "sensesp/signalk/signalk_put_request.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp/system/system_status_led.h"
#include "sensesp/transforms/debounce.h"
#include "sensesp/ui/config_item.h"
#include "sensesp/ui/status_page_item.h"
#include "sensesp/ui/ui_button.h"
#include "sensesp_app.h"
#include "sensesp_app_builder.h"

using namespace sensesp;

// HALMET processor-side pins. Connector positions remain hardware-dependent.
#ifndef N2K_CAN_TX_PIN
#define N2K_CAN_TX_PIN 19
#endif
#ifndef N2K_CAN_RX_PIN
#define N2K_CAN_RX_PIN 18
#endif
// Push buttons, wired between the pin and ground.
#ifndef ACK_BUTTON_PIN
#define ACK_BUTTON_PIN 25
#endif
#ifndef SNOOZE_BUTTON_PIN
#define SNOOZE_BUTTON_PIN 27
#endif
#ifndef PIN_RGB_LED
#define PIN_RGB_LED LED_BUILTIN
#endif
// SSD1306 128x64 OLED display on I2C
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 21
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 22
#endif
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3C
#endif
// Contrast of the display while no alarm is raised, 1 to 255. Not 0: some
// panels go completely dark there.
#ifndef OLED_DIM_CONTRAST
#define OLED_DIM_CONTRAST 0x20
#endif
#ifndef GUARDIAN_BUSY_PIN
#define GUARDIAN_BUSY_PIN 33
#endif

// Signal K Outlook Guardian: speaks vessel alarms through a MAX98357A I2S
// amplifier using PicoTTS text-to-speech and shows them on the on-board RGB
// LED.
//
// - Signal K: subscribes to `notifications.*` and speaks the `message` of
//   every raised notification. The paths to subscribe to are set in the web
//   UI; the default is all of them.
// - NMEA 2000: listens to the Alert (PGN 126983) and Alert Text (PGN 126985)
//   messages through a CAN transceiver, composes a sentence out of the alert
//   type, category, description and location, and speaks it. This is a
//   fallback: NMEA 2000 is ignored while the Signal K server is connected.
//   The alert types and categories to announce are set in the web UI.
// - An alarm is repeated until it clears or is acknowledged or snoozed with
//   the two push buttons. For Signal K notifications the button press is
//   sent to the server as a PUT on the notification's `method`.
// - The LED color follows the most severe alarm: blue for alert/caution,
//   yellow for warning, red for alarm and emergency. It blinks until the
//   alarm is acknowledged or snoozed, and is off when there is no alarm.
//   It flashes red twice a second if the audio output failed to start; the
//   alarms are then shown on the LED and the display, but not spoken.
// - An optional SSD1306 OLED display shows the IP address, the WiFi mode,
//   the state of the Signal K and NMEA 2000 streams and the latest
//   notification received. The firmware works the same without it.
// - The language, voice, volume, repeat interval, snooze time and welcome
//   message are set in the web UI.

enum class Severity { kNone, kAlert, kWarn, kAlarm, kEmergency };

///////////////////////////////////////////////////////////////////////////////
// Configuration

// PicoTTS has a single speaker per language, so the voices are pitch
// variations of it.
struct Voice {
  const char* name;
  int pitch;
};
const Voice kVoices[] = {{"Standard", 100}, {"Deep", 80}, {"High", 120}};
// All the languages PicoTTS supports.
const char* const kLanguages[] = {"en-US", "en-GB", "it-IT",
                                  "de-DE", "es-ES", "fr-FR"};

class GuardianConfig : public FileSystemSaveable {
 public:
  GuardianConfig(const String& config_path) : FileSystemSaveable(config_path) {
    // The S3 advertises these six installed languages in protocol v1.
    for (const char* language : kLanguages) {
      languages_.push_back(language);
    }
    if (!languages_.empty()) {
      language_ = languages_.front();
    }
    load();
  }

  bool to_json(JsonObject& root) override {
    root["language"] = language_;
    root["voice"] = voice_;
    root["speed"] = speed_;
    root["volume"] = volume_;
    root["repeat_interval"] = repeat_interval_s_;
    root["snooze_time"] = snooze_time_min_;
    root["welcome_enabled"] = welcome_enabled_;
    root["welcome_message"] = welcome_message_;
    root["transport"] = transport_;
    return true;
  }

  bool from_json(const JsonObject& config) override {
    const String language = config["language"] | language_;
    for (const String& available : languages_) {
      if (available == language) {
        language_ = language;
      }
    }
    voice_ = config["voice"] | voice_;
    speed_ = constrain(config["speed"] | speed_, 50, 200);
    volume_ = constrain(config["volume"] | volume_, 0, 100);
    repeat_interval_s_ = max(config["repeat_interval"] | repeat_interval_s_, 0);
    snooze_time_min_ = max(config["snooze_time"] | snooze_time_min_, 1);
    welcome_enabled_ = config["welcome_enabled"] | welcome_enabled_;
    welcome_message_ = config["welcome_message"] | welcome_message_;
    welcome_message_.trim();
    const String transport = config["transport"] | transport_;
    if (transport == "Auto" || transport == "GPIO" || transport == "USB") {
      transport_ = transport;
    }
    if (on_change_) {
      on_change_();
    }
    return true;
  }

  const String& language() const { return language_; }
  const std::vector<String>& languages() const { return languages_; }
  float volume() const { return volume_ / 100.0f; }
  uint8_t volume_percent() const { return volume_; }
  uint8_t speed() const { return speed_; }
  uint8_t pitch() const {
    for (const Voice& voice : kVoices) if (voice_ == voice.name) return voice.pitch;
    return 100;
  }
  uint32_t repeat_interval_ms() const { return repeat_interval_s_ * 1000UL; }
  uint32_t snooze_time_ms() const { return snooze_time_min_ * 60000UL; }
  bool welcome_enabled() const { return welcome_enabled_; }
  // Empty for the standard announcement in the configured language.
  const String& welcome_message() const { return welcome_message_; }
  const String& transport() const { return transport_; }

  // Wrap the text in the PicoTTS markup that selects the voice.
  String markup(String text) const {
    // Anything that looks like a tag would be taken as markup.
    text.replace('<', ' ');
    text.replace('>', ' ');
    int pitch = 100;
    for (const Voice& voice : kVoices) {
      if (voice_ == voice.name) {
        pitch = voice.pitch;
      }
    }
    if (pitch != 100) {
      text = "<pitch level=\"" + String(pitch) + "\">" + text + "</pitch>";
    }
    if (speed_ != 100) {
      text = "<speed level=\"" + String(speed_) + "\">" + text + "</speed>";
    }
    return text;
  }

  void on_change(std::function<void()> callback) { on_change_ = callback; }

 protected:
  std::vector<String> languages_;
  String language_ = "en-US";
  String voice_ = kVoices[0].name;
  int speed_ = 100;
  int volume_ = 50;
  int repeat_interval_s_ = 30;
  int snooze_time_min_ = 5;
  bool welcome_enabled_ = true;
  String welcome_message_;
  String transport_ = "Auto";
  std::function<void()> on_change_;
};

const String ConfigSchema(const GuardianConfig& obj) {
  const auto select = [](const String& title, const String& options) {
    return String(R"###({"title":")###") + title +
           R"###(","type":"array","format":"select","uniqueItems":true,)###"
           R"###("items":{"type":"string","enum":[)###" +
           options + "]}}";
  };
  String languages;
  for (const String& language : obj.languages()) {
    languages += (languages.length() > 0 ? ",\"" : "\"") + language + "\"";
  }
  String voices;
  for (const Voice& voice : kVoices) {
    voices += String(voices.length() > 0 ? ",\"" : "\"") + voice.name + "\"";
  }
  return String(R"###({"type":"object","properties":{"language":)###") +
         select("Language", languages) + R"###(,"voice":)###" +
         select("Voice", voices) +
         R"###(,"speed":{"title":"Speech speed (%, 50-200)","type":"integer"})###"
         R"###(,"volume":{"title":"Volume (%)","type":"integer"})###"
         R"###(,"repeat_interval":{"title":"Repeat an unacknowledged alarm every (s, 0 = speak once)","type":"integer"})###"
         R"###(,"snooze_time":{"title":"Snooze time (min)","type":"integer"})###"
         R"###(,"welcome_enabled":{"title":"Speak a welcome message at startup","type":"boolean"})###"
         R"###(,"welcome_message":{"title":"Welcome message (empty = the standard announcement)","type":"string"})###"
         R"###(,"transport":{"title":"S3 protocol transport (restart required)","type":"string","enum":["Auto","GPIO","USB"]}}})###";
}

// Which notifications are announced, by source.
constexpr char kNotificationsPrefix[] = "notifications.";
constexpr size_t kMaxSignalKPaths = 16;

class SourcesConfig : public FileSystemSaveable {
 public:
  SourcesConfig(const String& config_path) : FileSystemSaveable(config_path) {
    load();
  }

  bool to_json(JsonObject& root) override {
    root["sk_paths"] = sk_paths_;
    root["n2k_emergency"] = n2k_emergency_;
    root["n2k_alarm"] = n2k_alarm_;
    root["n2k_warning"] = n2k_warning_;
    root["n2k_caution"] = n2k_caution_;
    root["n2k_navigational"] = n2k_navigational_;
    root["n2k_technical"] = n2k_technical_;
    return true;
  }

  bool from_json(const JsonObject& config) override {
    sk_paths_ = config["sk_paths"] | sk_paths_;
    sk_paths_.trim();
    n2k_emergency_ = config["n2k_emergency"] | n2k_emergency_;
    n2k_alarm_ = config["n2k_alarm"] | n2k_alarm_;
    n2k_warning_ = config["n2k_warning"] | n2k_warning_;
    n2k_caution_ = config["n2k_caution"] | n2k_caution_;
    n2k_navigational_ = config["n2k_navigational"] | n2k_navigational_;
    n2k_technical_ = config["n2k_technical"] | n2k_technical_;
    return true;
  }

  // The Signal K paths to subscribe to, which may contain `*` wildcards.
  // Paths are separated by commas or spaces, and "notifications." is added
  // to those that lack it. All the notifications if none is given.
  std::vector<String> signalk_paths() const {
    std::vector<String> paths;
    String list = sk_paths_;
    list.replace(',', ' ');
    list.replace(';', ' ');
    list.replace('\n', ' ');
    int start = 0;
    while (start < static_cast<int>(list.length()) &&
           paths.size() < kMaxSignalKPaths) {
      int end = list.indexOf(' ', start);
      if (end < 0) {
        end = list.length();
      }
      String path = list.substring(start, end);
      start = end + 1;
      if (path.length() == 0) {
        continue;
      }
      if (!path.startsWith(kNotificationsPrefix)) {
        path = kNotificationsPrefix + path;
      }
      bool known = false;
      for (const String& other : paths) {
        known = known || other == path;
      }
      if (!known) {
        paths.push_back(path);
      }
    }
    if (paths.empty()) {
      paths.push_back(String(kNotificationsPrefix) + "*");
    }
    return paths;
  }

  // Whether an NMEA 2000 alert of this type and category (PGN 126983) is to
  // be announced.
  bool n2k_alert_enabled(uint8_t type, uint8_t category) const {
    if ((category == 0 && !n2k_navigational_) ||
        (category == 1 && !n2k_technical_)) {
      return false;
    }
    switch (type) {
      case 1:
        return n2k_emergency_;
      case 2:
        return n2k_alarm_;
      case 5:
        return n2k_warning_;
      default:
        return n2k_caution_;
    }
  }

 protected:
  String sk_paths_ = "notifications.*";
  bool n2k_emergency_ = true;
  bool n2k_alarm_ = true;
  bool n2k_warning_ = true;
  bool n2k_caution_ = true;
  bool n2k_navigational_ = true;
  bool n2k_technical_ = true;
};

const String ConfigSchema(const SourcesConfig& obj) {
  return R"###({"type":"object","properties":{)###"
         R"###("sk_paths":{"title":"Signal K notification paths (comma separated, * = any)","type":"string"})###"
         R"###(,"n2k_emergency":{"title":"NMEA 2000: emergency alarms","type":"boolean"})###"
         R"###(,"n2k_alarm":{"title":"NMEA 2000: alarms","type":"boolean"})###"
         R"###(,"n2k_warning":{"title":"NMEA 2000: warnings","type":"boolean"})###"
         R"###(,"n2k_caution":{"title":"NMEA 2000: cautions and other alerts","type":"boolean"})###"
         R"###(,"n2k_navigational":{"title":"NMEA 2000: navigational alerts","type":"boolean"})###"
         R"###(,"n2k_technical":{"title":"NMEA 2000: technical alerts","type":"boolean"}}})###";
}

class BluetoothConfig : public FileSystemSaveable {
 public:
  struct Profile {
    String address;
    int address_type = 0;
    String name = "Bluetooth remote";
    String mappings;
    String last_hid = "";
  };

  explicit BluetoothConfig(const String& path) : FileSystemSaveable(path) {
    load();
  }

  bool to_json(JsonObject& root) override {
    root["scan_seconds"] = scan_seconds_;
    root["selected"] = selected_address_;
    root["connected"] = connected_address_;
    root["scanning"] = scanning_;
    JsonArray profiles = root["profiles"].to<JsonArray>();
    for (const Profile& profile : profiles_) {
      JsonObject item = profiles.add<JsonObject>();
      item["address"] = profile.address;
      item["address_type"] = profile.address_type;
      item["name"] = profile.name;
      item["last_hid"] = profile.last_hid;
      JsonArray mappings = item["mappings"].to<JsonArray>();
      append_lines(mappings, profile.mappings);
    }
    JsonArray nearby = root["nearby"].to<JsonArray>();
    for (const Profile& device : nearby_) {
      JsonObject item = nearby.add<JsonObject>();
      item["address"] = device.address;
      item["address_type"] = device.address_type;
      item["name"] = device.name;
      item["rssi"] = device.last_hid.toInt();
    }
    return true;
  }

  bool from_json(const JsonObject& root) override {
    scan_seconds_ = constrain(root["scan_seconds"] | scan_seconds_, 1, 30);
    selected_address_ = root["selected"] | selected_address_;
    if (root["profiles"].is<JsonArrayConst>()) {
      std::vector<Profile> parsed;
      for (JsonObjectConst item : root["profiles"].as<JsonArrayConst>()) {
        Profile profile;
        profile.address = item["address"] | "";
        if (profile.address.length() == 0 || parsed.size() >= 8) continue;
        profile.address_type = item["address_type"] | 0;
        profile.name = item["name"] | "Bluetooth remote";
        profile.last_hid = item["last_hid"] | "";
        if (item["mappings"].is<JsonArrayConst>()) {
          for (JsonVariantConst mapping : item["mappings"].as<JsonArrayConst>()) {
            String line = mapping.as<String>();
            line.trim();
            if (line.length() > 0)
              profile.mappings +=
                  (profile.mappings.length() > 0 ? "\n" : "") + line;
          }
        }
        parsed.push_back(profile);
      }
      profiles_ = parsed;
    }
    // Migrate the first Bluetooth UI, which stored one selected device and a
    // global mapping list.
    String legacy_device;
    if (root["device"].is<JsonArrayConst>() && root["device"].size() > 0)
      legacy_device = root["device"][0].as<String>();
    // Migrate the first version, which stored address and type separately.
    if (legacy_device.length() == 0 && root["address"].is<String>())
      legacy_device = root["address"].as<String>() + "|" +
                      String(root["address_type"] | 0);
    if (profiles_.empty() && legacy_device.length() > 0) {
      Profile profile;
      const int first = legacy_device.indexOf('|');
      const int second = legacy_device.indexOf('|', first + 1);
      const int third = legacy_device.indexOf('|', second + 1);
      profile.address = first < 0 ? legacy_device
                                  : legacy_device.substring(0, first);
      profile.address_type = first < 0
                                 ? 0
                                 : legacy_device.substring(first + 1, second)
                                       .toInt();
      if (third >= 0) profile.name = legacy_device.substring(third + 1);
      for (JsonVariantConst item : root["mappings"].as<JsonArrayConst>()) {
        String line = item.as<String>();
        line.trim();
        if (line.length() > 0)
          profile.mappings +=
              (profile.mappings.length() > 0 ? "\n" : "") + line;
      }
      if (root["mappings"].is<String>())
        profile.mappings = root["mappings"].as<String>();
      profiles_.push_back(profile);
      selected_address_ = profile.address;
    }
    return true;
  }

  int scan_seconds() const { return scan_seconds_; }
  const String& address() const { return selected_address_; }
  int address_type() const {
    const Profile* profile = selected_profile();
    if (profile != nullptr) return profile->address_type;
    for (const Profile& device : nearby_)
      if (device.address == selected_address_) return device.address_type;
    return 0;
  }
  const String& mappings() const {
    static const String empty;
    const Profile* profile = selected_profile();
    return profile == nullptr ? empty : profile->mappings;
  }
  void set_nearby(const std::vector<Profile>& devices, bool scanning) {
    nearby_ = devices;
    scanning_ = scanning;
  }
  void select_device(const String& address, int address_type = 0,
                     const String& name = "Bluetooth remote") {
    selected_address_ = address;
    if (selected_profile() != nullptr) return;
    Profile profile;
    profile.address = address;
    profile.address_type = address_type;
    profile.name = name;
    profiles_.push_back(profile);
  }
  void set_connected(bool connected) {
    if (!connected) {
      connected_address_ = "";
    } else if (connected_address_.length() == 0) {
      connected_address_ = selected_address_;
    }
  }
  void set_last_hid(const String& value) {
    Profile* profile = selected_profile();
    if (profile != nullptr) profile->last_hid = value;
  }
  String last_hid() const {
    const Profile* profile = selected_profile();
    return profile == nullptr ? "" : profile->last_hid;
  }

 protected:
  static void append_lines(JsonArray& output, const String& text) {
    int start = 0;
    while (start < static_cast<int>(text.length())) {
      int end = text.indexOf('\n', start);
      if (end < 0) end = text.length();
      String line = text.substring(start, end);
      line.trim();
      if (line.length() > 0 && line[0] != '#') output.add(line);
      start = end + 1;
    }
  }
  Profile* selected_profile() {
    for (Profile& profile : profiles_)
      if (profile.address == selected_address_) return &profile;
    return nullptr;
  }
  const Profile* selected_profile() const {
    for (const Profile& profile : profiles_)
      if (profile.address == selected_address_) return &profile;
    return nullptr;
  }
  friend const String ConfigSchema(const BluetoothConfig&);
  int scan_seconds_ = 5;
  String selected_address_;
  String connected_address_;
  bool scanning_ = false;
  std::vector<Profile> profiles_;
  std::vector<Profile> nearby_;
};

const String ConfigSchema(const BluetoothConfig&) {
  return R"###({"type":"object","properties":{
"scan_seconds":{"title":"Scan time","description":"Five seconds is normally enough. Increase it if the remote is difficult to discover.","type":"integer","minimum":1,"maximum":30},
"selected":{"type":"string"},"connected":{"type":"string","readOnly":true},
"scanning":{"type":"boolean","readOnly":true},"profiles":{"type":"array"},
"nearby":{"type":"array","readOnly":true}}})###";
}

///////////////////////////////////////////////////////////////////////////////
// RGB LED

// Shows the alarm state instead of the SensESP system status.
class AlarmLed : public SystemStatusLed {
 public:
  AlarmLed(uint8_t pin, uint8_t brightness = 40)
      : SystemStatusLed(pin, brightness), pin_{pin}, brightness_{brightness} {}

  void set_alarm(Severity severity, bool blinking) {
    severity_ = severity;
    blinking_ = blinking;
  }

  // The audio output does not work: flash red whatever the alarm state.
  void set_fault(bool fault) { fault_ = fault; }

 protected:
  // Called every few milliseconds by the base class.
  void show() override {
    uint32_t color = 0x000000;
    switch (severity_) {
      case Severity::kAlert:
        color = 0x0000ff;
        break;
      case Severity::kWarn:
        color = 0xffa000;
        break;
      case Severity::kAlarm:
      case Severity::kEmergency:
        color = 0xff0000;
        break;
      default:
        break;
    }
    const uint32_t half_period_ms =
        severity_ == Severity::kEmergency ? 125 : 500;
    if (blinking_ && (millis() / half_period_ms) % 2 == 1) {
      color = 0x000000;
    }
    if (fault_) {
      // Two short flashes a second, unlike any alarm.
      const uint32_t phase = millis() % 1000;
      color = phase < 100 || (phase >= 200 && phase < 300) ? 0xff0000 : 0;
    }
    if (color == shown_color_) {
      return;
    }
    shown_color_ = color;
    rgbLedWrite(pin_, apply_brightness(color >> 16, brightness_),
                apply_brightness((color >> 8) & 0xff, brightness_),
                apply_brightness(color & 0xff, brightness_));
  }

  void set_brightness(uint8_t brightness) override { brightness_ = brightness; }

  uint8_t pin_;
  uint8_t brightness_;
  Severity severity_ = Severity::kNone;
  bool blinking_ = false;
  bool fault_ = false;
  // Not a color, so that the LED is switched off on the first call.
  uint32_t shown_color_ = 0xffffffff;
};

///////////////////////////////////////////////////////////////////////////////

std::shared_ptr<GuardianConfig> config;
std::shared_ptr<SourcesConfig> sources;
std::shared_ptr<BluetoothConfig> bluetooth_config;
std::shared_ptr<AlarmLed> alarm_led;
std::shared_ptr<StatusPageItem<String>> s3_link_status;
std::shared_ptr<StatusPageItem<String>> bluetooth_action_status;
std::shared_ptr<StatusPageItem<String>> bluetooth_devices_status;
bool s3_link_up = false;
uint32_t s3_last_frame_ms = 0;
uint32_t s3_last_frame_count = 0;

enum class BluetoothAction { kNone, kRefresh, kScan, kScanPair, kPair, kDisconnect,
                             kTest, kForget };
BluetoothAction bluetooth_action = BluetoothAction::kNone;
uint32_t bluetooth_action_started_ms = 0;
uint16_t bluetooth_action_sequence = 0;
bool bluetooth_auto_pairing = false;
class ArduinoSerialTransport final : public guardian::ByteTransport {
 public:
  enum class Mode { kAuto, kGpio, kUsb };

  ArduinoSerialTransport(HardwareSerial* gpio, HardwareSerial* usb, Mode mode)
      : gpio_(gpio), usb_(usb), mode_(mode) {}
  size_t available() const override {
    if (mode_ == Mode::kGpio) return gpio_->available();
    if (mode_ == Mode::kUsb) return usb_->available();
    return gpio_->available() + usb_->available();
  }
  int read() override {
    if (mode_ == Mode::kGpio) return gpio_->read();
    if (mode_ == Mode::kUsb) return usb_->read();
    // Prefer the dedicated link. USB replies remain available when GPIO is
    // unconnected, while duplicate replies are harmless protocol replays.
    return gpio_->available() ? gpio_->read() : usb_->read();
  }
  size_t write(const uint8_t* bytes, size_t length) override {
    if (mode_ == Mode::kGpio) return gpio_->write(bytes, length);
    if (mode_ == Mode::kUsb) return usb_->write(bytes, length);
    const size_t gpio_written = gpio_->write(bytes, length);
    const size_t usb_written = usb_->write(bytes, length);
    return gpio_written == length || usb_written == length ? length : 0;
  }
 private:
  HardwareSerial* gpio_;
  HardwareSerial* usb_;
  Mode mode_;
};
std::unique_ptr<ArduinoSerialTransport> speech_transport;
std::unique_ptr<guardian::UartSpeechBackend> speech;
tNMEA2000* nmea2000;
// When the last NMEA 2000 message of any kind was received
bool n2k_message_received = false;
uint32_t n2k_message_at_ms = 0;

void set_bluetooth_action_status(const String& value) {
  if (bluetooth_action_status != nullptr) bluetooth_action_status->set(value);
}

void run_bluetooth_action(BluetoothAction action) {
  bool sent = false;
  String progress;
  switch (action) {
    case BluetoothAction::kRefresh:
      progress = "Refreshing Bluetooth status…";
      sent = speech->bluetooth_get_status();
      break;
    case BluetoothAction::kScan:
    case BluetoothAction::kScanPair:
      progress = action == BluetoothAction::kScanPair
                     ? "Scanning for the remote… keep it close and in pairing mode"
                     : "Scanning for Bluetooth devices…";
      sent = speech->bluetooth_scan(bluetooth_config->scan_seconds());
      break;
    case BluetoothAction::kPair:
      progress = "Pairing selected Bluetooth device…";
      sent = speech->bluetooth_connect(bluetooth_config->address().c_str(),
                                       bluetooth_config->address_type());
      break;
    case BluetoothAction::kDisconnect:
      progress = "Disconnecting Bluetooth device…";
      sent = speech->bluetooth_disconnect();
      break;
    case BluetoothAction::kTest:
      progress = "Testing the last Bluetooth button…";
      sent = speech->bluetooth_test();
      break;
    case BluetoothAction::kForget:
      progress = "Forgetting all Bluetooth devices…";
      sent = speech->bluetooth_forget();
      break;
    default:
      return;
  }
  if (!sent) {
    bluetooth_action = BluetoothAction::kNone;
    set_bluetooth_action_status("Command could not be sent to the S3");
    return;
  }
  bluetooth_action = action;
  bluetooth_action_started_ms = millis();
  bluetooth_action_sequence = speech->last_sent_sequence();
  set_bluetooth_action_status(progress);
}

void say(const String& text) {
  if (speech == nullptr) return;
  guardian::protocol::SpeakRequest request;
  request.priority = guardian::protocol::Priority::kNormal;
  // Per-request volume stays at full scale; HALMET owns the S3 master volume
  // through SET_VOLUME and restores it whenever the S3 sends HELLO.
  request.volume = 100;
  request.pitch = config->pitch();
  request.speed = config->speed();
  request.language = config->language().c_str();
  request.text = text.c_str();
  if (!speech->speak(request)) alarm_led->set_fault(true);
}

bool signalk_connected() {
  return sensesp_app->get_ws_client()->is_connected();
}

///////////////////////////////////////////////////////////////////////////////
// Alarms, from either Signal K or NMEA 2000

constexpr size_t kMaxAlarms = 32;

struct Alarm {
  bool from_signalk = false;
  Severity severity = Severity::kNone;
  String message;
  // Whether the source wants the alarm to sound.
  bool audible = true;
  bool remote_acknowledged = false;
  // Acknowledged with the button
  bool acknowledged = false;
  bool snoozed = false;
  uint32_t snoozed_at_ms = 0;
  // Signal K `method` of the notification, as JSON, and its value before
  // snoozing, which is restored when the snooze time is over.
  String method;
  String method_before_snooze;
  bool spoken = false;
  uint32_t spoken_at_ms = 0;

  bool is_acknowledged() const { return acknowledged || remote_acknowledged; }
  bool is_pending() const { return !is_acknowledged() && !snoozed; }
};

// Keyed by the Signal K path, or by "n2k:" and the NMEA 2000 alert identity.
std::map<String, Alarm> alarms;

// The latest notification received, kept for the display after it clears.
struct LatestNotification {
  bool received = false;
  String key;
  bool from_signalk = false;
  Severity severity = Severity::kNone;
  String message;
};
LatestNotification latest_notification;

void raise_alarm(const String& key, bool from_signalk, Severity severity,
                 const String& message, bool audible, bool remote_acknowledged,
                 const String& method = "") {
  auto it = alarms.find(key);
  if (it == alarms.end()) {
    if (alarms.size() >= kMaxAlarms) {
      return;
    }
    it = alarms.emplace(key, Alarm{}).first;
  }
  Alarm& alarm = it->second;
  if (alarm.severity != severity || alarm.message != message) {
    // New or changed: announce it again, whatever was done about it before.
    ESP_LOGI(__FILENAME__, "Alarm %s: %s", key.c_str(), message.c_str());
    alarm.spoken = false;
    alarm.acknowledged = false;
    alarm.snoozed = false;
    latest_notification = {true, key, from_signalk, severity, message};
  }
  alarm.from_signalk = from_signalk;
  alarm.severity = severity;
  alarm.message = message;
  alarm.audible = audible;
  alarm.remote_acknowledged = remote_acknowledged;
  alarm.method = method;
}

void clear_alarms(bool from_signalk) {
  for (auto it = alarms.begin(); it != alarms.end();) {
    it = it->second.from_signalk == from_signalk ? alarms.erase(it) : ++it;
  }
}

// Tell the Signal K server how a notification should be indicated from now
// on. `method` is a JSON array, e.g. `["visual"]`.
void put_notification_method(const String& path, const String& method) {
  if (!signalk_connected()) {
    return;
  }
  JsonDocument request;
  request["context"] = "vessels.self";
  request["put"]["path"] = path + ".method";
  request["put"]["value"] = serialized(method);
  SKRequest::send_request(request, [path](JsonDocument& response) {
    const int status = response["statusCode"] | 0;
    if (status != 0 && status != 200 && status != 202) {
      ESP_LOGW(__FILENAME__, "Server refused the update of %s: %d",
               path.c_str(), status);
    }
  });
}

void send_signalk_put(const String& path, const String& json_value) {
  if (!signalk_connected() || path.length() == 0) return;
  JsonDocument value;
  if (deserializeJson(value, json_value) != DeserializationError::Ok) {
    ESP_LOGW(__FILENAME__, "Invalid HID mapping JSON value: %s",
             json_value.c_str());
    return;
  }
  JsonDocument request;
  request["context"] = "vessels.self";
  request["put"]["path"] = path;
  request["put"]["value"].set(value.as<JsonVariantConst>());
  SKRequest::send_request(request, [path](JsonDocument& response) {
    const int status = response["statusCode"] | 0;
    if (status != 0 && status != 200 && status != 202)
      ESP_LOGW(__FILENAME__, "HID Signal K PUT rejected for %s: %d",
               path.c_str(), status);
  });
}

String hid_code(const std::vector<uint8_t>& report) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  String code;
  // The first two bytes identify the remote GATT characteristic. Mapping the
  // HID report bytes only keeps codes stable across reconnections.
  for (size_t index = 2; index < report.size(); ++index) {
    code += kHex[report[index] >> 4];
    code += kHex[report[index] & 0x0f];
  }
  return code;
}

void handle_hid_report(const std::vector<uint8_t>& report) {
  if (bluetooth_config == nullptr) return;
  const String code = hid_code(report);
  bluetooth_config->set_last_hid(code);
  ESP_LOGI(__FILENAME__, "S3 Bluetooth HID report: %s", code.c_str());

  String mappings = bluetooth_config->mappings();
  int start = 0;
  while (start <= static_cast<int>(mappings.length())) {
    int end = mappings.indexOf('\n', start);
    if (end < 0) end = mappings.length();
    String line = mappings.substring(start, end);
    start = end + 1;
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    const int equals = line.indexOf('=');
    const int first_pipe = line.indexOf('|', equals + 1);
    const int second_pipe = line.indexOf('|', first_pipe + 1);
    if (equals <= 0 || first_pipe < 0) continue;
    String mapped_code = line.substring(0, equals);
    mapped_code.trim();
    mapped_code.toUpperCase();
    if (mapped_code != code) continue;
    String path = line.substring(equals + 1, first_pipe);
    String value = second_pipe < 0 ? line.substring(first_pipe + 1)
                                   : line.substring(first_pipe + 1, second_pipe);
    String spoken = second_pipe < 0 ? "" : line.substring(second_pipe + 1);
    path.trim();
    value.trim();
    spoken.trim();
    send_signalk_put(path, value);
    if (spoken.length() > 0) say(spoken);
  }
}

// Acknowledge button: the pending alarms stay on the LED but are no longer
// spoken.
void acknowledge_alarms() {
  for (auto& [key, alarm] : alarms) {
    if (alarm.is_acknowledged()) {
      continue;
    }
    ESP_LOGI(__FILENAME__, "Acknowledged %s", key.c_str());
    alarm.acknowledged = true;
    alarm.snoozed = false;
    if (alarm.from_signalk) {
      put_notification_method(key, "[]");
    }
  }
}

// Snooze button: the pending alarms keep quiet for the snooze time.
void snooze_alarms() {
  for (auto& [key, alarm] : alarms) {
    if (!alarm.is_pending()) {
      continue;
    }
    ESP_LOGI(__FILENAME__, "Snoozed %s", key.c_str());
    alarm.snoozed = true;
    alarm.snoozed_at_ms = millis();
    if (alarm.from_signalk) {
      alarm.method_before_snooze = alarm.method;
      put_notification_method(key, "[\"visual\"]");
    }
  }
}

// Wake up the snoozed alarms, speak the next alarm that is due and update
// the LED.
void process_alarms() {
  const uint32_t now = millis();
  const uint32_t repeat_interval_ms = config->repeat_interval_ms();
  Alarm* next = nullptr;
  Severity led_severity = Severity::kNone;
  bool pending = false;

  for (auto& [key, alarm] : alarms) {
    if (alarm.snoozed && now - alarm.snoozed_at_ms > config->snooze_time_ms()) {
      alarm.snoozed = false;
      alarm.spoken = false;
      if (alarm.from_signalk) {
        put_notification_method(key, alarm.method_before_snooze.length() > 0
                                         ? alarm.method_before_snooze
                                         : "[\"visual\",\"sound\"]");
      }
    }
    led_severity = max(led_severity, alarm.severity);
    pending = pending || alarm.is_pending();

    const bool due =
        !alarm.spoken ||
        (repeat_interval_ms > 0 && now - alarm.spoken_at_ms > repeat_interval_ms);
    if (!alarm.is_pending() || !alarm.audible || !due ||
        alarm.message.length() == 0) {
      continue;
    }
    // The most severe first, then the one that has waited the longest.
    if (next == nullptr || alarm.severity > next->severity ||
        (alarm.severity == next->severity && next->spoken &&
         (!alarm.spoken || now - alarm.spoken_at_ms > now - next->spoken_at_ms))) {
      next = &alarm;
    }
  }

  alarm_led->set_alarm(led_severity, pending);
  if (next != nullptr && speech != nullptr && !speech->speaking()) {
    next->spoken = true;
    next->spoken_at_ms = now;
    say(next->message);
  }
}

///////////////////////////////////////////////////////////////////////////////
// OLED display

constexpr int kDisplayWidth = 128;
constexpr int kDisplayHeight = 64;
// Characters per line and lines of the notification message, in the 6x8 font
constexpr size_t kDisplayColumns = kDisplayWidth / 6;
constexpr int kDisplayMessageLines = 4;
// The NMEA 2000 stream is taken as lost after this long without a message.
constexpr uint32_t kN2kStreamTimeoutMs = 5000;

Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, -1);
// The display is optional: false while it does not answer on the I2C bus.
bool display_found = false;
// How often to look for a display that is not there.
constexpr uint32_t kDisplayProbeIntervalMs = 10000;

// Convert UTF-8 to the code page 437 of the display font. Accented letters
// the font lacks lose their accent.
String to_display_text(const String& text) {
  // Latin-1 code points from U+00A0 on, in pairs with their replacement.
  static const uint8_t kLatin1[][2] = {
      {0xa1, 0xad}, {0xb0, 0xf8}, {0xbf, 0xa8}, {0xc0, 'A'},  {0xc1, 'A'},
      {0xc2, 'A'},  {0xc4, 0x8e}, {0xc7, 0x80}, {0xc8, 'E'},  {0xc9, 0x90},
      {0xca, 'E'},  {0xcb, 'E'},  {0xcc, 'I'},  {0xcd, 'I'},  {0xce, 'I'},
      {0xcf, 'I'},  {0xd1, 0xa5}, {0xd2, 'O'},  {0xd3, 'O'},  {0xd4, 'O'},
      {0xd6, 0x99}, {0xd9, 'U'},  {0xda, 'U'},  {0xdb, 'U'},  {0xdc, 0x9a},
      {0xdf, 0xe1}, {0xe0, 0x85}, {0xe1, 0xa0}, {0xe2, 0x83}, {0xe4, 0x84},
      {0xe7, 0x87}, {0xe8, 0x8a}, {0xe9, 0x82}, {0xea, 0x88}, {0xeb, 0x89},
      {0xec, 0x8d}, {0xed, 0xa1}, {0xee, 0x8c}, {0xef, 0x8b}, {0xf1, 0xa4},
      {0xf2, 0x95}, {0xf3, 0xa2}, {0xf4, 0x93}, {0xf6, 0x94}, {0xf9, 0x97},
      {0xfa, 0xa3}, {0xfb, 0x96}, {0xfc, 0x81},
  };
  String converted;
  converted.reserve(text.length());
  for (size_t i = 0; i < text.length(); i++) {
    const uint8_t byte = text[i];
    if (byte < 0x80) {
      converted += static_cast<char>(byte < ' ' ? ' ' : byte);
      continue;
    }
    if ((byte & 0xc0) == 0x80) {
      continue;  // Continuation byte of a character already handled
    }
    char replacement = '?';
    if ((byte == 0xc2 || byte == 0xc3) && i + 1 < text.length()) {
      const uint8_t code = ((byte & 0x03) << 6) | (text[i + 1] & 0x3f);
      for (const auto& pair : kLatin1) {
        if (pair[0] == code) {
          replacement = static_cast<char>(pair[1]);
        }
      }
    }
    converted += replacement;
  }
  return converted;
}

// Print the text word-wrapped from the cursor position, on at most
// `max_lines` lines.
void print_wrapped(const String& text, int max_lines) {
  const int x = display.getCursorX();
  int y = display.getCursorY();
  size_t start = 0;
  for (int line = 0; line < max_lines && start < text.length(); line++) {
    while (start < text.length() && text[start] == ' ') {
      start++;
    }
    size_t end = start + kDisplayColumns;
    if (end > text.length()) {
      end = text.length();
    }
    if (end < text.length() && text[end] != ' ') {
      // Break at the last space, unless the word fills the whole line.
      const int space = text.lastIndexOf(' ', end);
      if (space > static_cast<int>(start)) {
        end = space;
      }
    }
    display.setCursor(x, y);
    display.print(text.substring(start, end));
    start = end;
    y += 8;
  }
}

void print_right_aligned(const String& text, int y) {
  display.setCursor(kDisplayWidth - 6 * text.length(), y);
  display.print(text);
}

const char* severity_label(Severity severity) {
  switch (severity) {
    case Severity::kEmergency:
      return "EMERGENCY";
    case Severity::kAlarm:
      return "ALARM";
    case Severity::kWarn:
      return "WARNING";
    default:
      return "ALERT";
  }
}

// First line: the IP address and whether the device is its own access
// point or a client of an infrastructure network.
void draw_network() {
  display.setCursor(0, 0);
  if (WiFi.isConnected()) {
    display.print(WiFi.localIP().toString());
    print_right_aligned("INFRA", 0);
  } else if (WiFi.getMode() & WIFI_MODE_AP) {
    display.print(WiFi.softAPIP().toString());
    print_right_aligned("AP", 0);
  } else {
    display.print("No WiFi");
  }
}

// Second line: whether the Signal K server is connected and whether there
// is traffic on the NMEA 2000 bus.
void draw_streams() {
  const bool n2k_active = n2k_message_received &&
                          millis() - n2k_message_at_ms < kN2kStreamTimeoutMs;
  display.setCursor(0, 8);
  display.print(signalk_connected() ? "SK:ON" : "SK:--");
  print_right_aligned(n2k_active ? "N2K:ON" : "N2K:--", 8);
  if (speech == nullptr || !s3_link_up) {
    display.setCursor((kDisplayWidth - 6 * 8) / 2, 8);
    display.print("NO AUDIO");
  }
}

// The rest of the screen: source, severity, state and message of the latest
// notification.
void draw_latest_notification() {
  const LatestNotification& latest = latest_notification;
  display.setCursor(0, 21);
  if (!latest.received) {
    display.print("No notifications");
    return;
  }
  display.print(latest.from_signalk ? "SK " : "N2K ");
  display.print(severity_label(latest.severity));
  const auto it = alarms.find(latest.key);
  const char* state = "CLEARED";
  if (it != alarms.end()) {
    const Alarm& alarm = it->second;
    state = alarm.is_acknowledged() ? "ACK"
            : alarm.snoozed         ? "SNOOZED"
            : !alarm.audible        ? "SILENCED"
                                    : "ACTIVE";
  }
  print_right_aligned(state, 21);
  display.setCursor(0, 30);
  print_wrapped(to_display_text(latest.message), kDisplayMessageLines);
}

bool display_answers() {
  Wire.beginTransmission(OLED_I2C_ADDRESS);
  return Wire.endTransmission() == 0;
}

bool setup_display() {
  if (!display_answers()) {
    return false;
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS, true, false)) {
    ESP_LOGE(__FILENAME__, "Failed to start the display");
    return false;
  }
  display.cp437(true);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Outlook Guardian");
  display.display();
  return true;
}

void update_display() {
  static uint32_t shown_hash = 0;
  static bool dimmed = false;
  if (!display_found) {
    // Not connected, or not ready yet at startup: look for it now and then.
    static uint32_t probed_at_ms = 0;
    if (millis() - probed_at_ms < kDisplayProbeIntervalMs) {
      return;
    }
    probed_at_ms = millis();
    display_found = setup_display();
    if (!display_found) {
      return;
    }
    ESP_LOGI(__FILENAME__, "Display found");
    shown_hash = 0;
    dimmed = false;
  }
  display.clearDisplay();
  draw_network();
  draw_streams();
  display.drawFastHLine(0, 18, kDisplayWidth, SSD1306_WHITE);
  draw_latest_notification();

  // Sending a frame takes a few tens of milliseconds: do it only when the
  // picture has changed.
  uint32_t hash = 2166136261UL;
  const uint8_t* buffer = display.getBuffer();
  for (size_t i = 0; i < kDisplayWidth * kDisplayHeight / 8; i++) {
    hash = (hash ^ buffer[i]) * 16777619UL;
  }
  if (hash != shown_hash) {
    if (!display_answers()) {
      ESP_LOGW(__FILENAME__, "Display lost");
      display_found = false;
      return;
    }
    shown_hash = hash;
    display.display();
  }
  // Spare the OLED while no alarm is raised.
  if (dimmed != alarms.empty()) {
    dimmed = alarms.empty();
    if (dimmed) {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(OLED_DIM_CONTRAST);
    } else {
      display.dim(false);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Signal K notifications

bool json_array_contains(JsonVariantConst array, const char* text) {
  for (JsonVariantConst item : array.as<JsonArrayConst>()) {
    if (item.as<String>() == text) {
      return true;
    }
  }
  return false;
}

void handle_sk_notification(const String& path, JsonVariantConst value) {
  const String state = value["state"] | "";
  Severity severity = Severity::kNone;
  if (state == "alert") {
    severity = Severity::kAlert;
  } else if (state == "warn") {
    severity = Severity::kWarn;
  } else if (state == "alarm") {
    severity = Severity::kAlarm;
  } else if (state == "emergency") {
    severity = Severity::kEmergency;
  }
  if (severity == Severity::kNone) {
    // Removed (null value), or back to "normal" or "nominal".
    alarms.erase(path);
    return;
  }
  // `method` says how to indicate the alarm. Other devices acknowledge it
  // by emptying the list, and silence it by removing "sound" from it. Newer
  // servers also report these in `status`.
  const JsonVariantConst method = value["method"];
  const bool has_method = method.is<JsonArrayConst>();
  const bool acknowledged = (has_method && method.size() == 0) ||
                            (value["status"]["acknowledged"] | false);
  const bool audible = (!has_method || json_array_contains(method, "sound")) &&
                       !(value["status"]["silenced"] | false);
  String method_json;
  if (has_method) {
    serializeJson(method, method_json);
  }
  raise_alarm(path, true, severity, value["message"] | "",
              audible, acknowledged, method_json);
}

// Whether the text matches the pattern, in which `*` stands for any run of
// characters.
bool glob_match(const char* pattern, const char* text) {
  const char* star = nullptr;
  const char* retry = nullptr;
  while (*text != '\0') {
    if (*pattern == '*') {
      star = pattern++;
      retry = text;
    } else if (*pattern == *text) {
      pattern++;
      text++;
    } else if (star != nullptr) {
      pattern = star + 1;
      text = ++retry;
    } else {
      return false;
    }
  }
  while (*pattern == '*') {
    pattern++;
  }
  return *pattern == '\0';
}

// Subscribes to one of the configured notification paths, which may contain
// wildcards, and takes the notifications received on it.
class NotificationListener : public SKListener {
 public:
  NotificationListener(const String& path) : SKListener(path, 1000) {}

  bool matches(const String& path) const override {
    return glob_match(sk_path.c_str(), path.c_str());
  }

  void parse_value(const JsonObject& json) override {
    handle_sk_notification(json["path"].as<String>(), json["value"]);
  }
};

///////////////////////////////////////////////////////////////////////////////
// NMEA 2000 alerts

constexpr unsigned long kN2kAlertPgn = 126983L;
constexpr unsigned long kN2kAlertTextPgn = 126985L;
// Alert State values of PGN 126983
constexpr uint8_t kN2kAlertStateDisabled = 0;
constexpr uint8_t kN2kAlertStateNormal = 1;
constexpr uint8_t kN2kAlertStateSilenced = 3;
constexpr uint8_t kN2kAlertStateAcknowledged = 4;
// How long to wait for the Alert Text of a newly raised alert before
// announcing it without a description.
constexpr uint32_t kN2kAlertTextWaitMs = 2000;
constexpr size_t kN2kMaxAlerts = 32;

struct N2kAlert {
  uint8_t type = 0;
  uint8_t category = 0;
  uint16_t id = 0;
  String text;
  String location;
  // Raised according to an Alert message; the text alone does not raise it.
  bool raised = false;
  uint32_t raised_at_ms = 0;
  bool silenced = false;
  bool acknowledged = false;
};

std::map<String, N2kAlert> n2k_alerts;

// Both alert PGNs start with the same fields identifying the alert.
String parse_n2k_alert_header(const tN2kMsg& msg, int& index, uint8_t& type,
                              uint8_t& category, uint16_t& id) {
  const uint8_t type_and_category = msg.GetByte(index);
  type = type_and_category & 0x0f;
  category = type_and_category >> 4;
  const uint8_t system = msg.GetByte(index);
  const uint8_t subsystem = msg.GetByte(index);
  id = msg.Get2ByteUInt(index);
  const uint64_t source_name = msg.GetUInt64(index);
  const uint8_t instance = msg.GetByte(index);
  const uint8_t source_index = msg.GetByte(index);
  const uint8_t occurrence = msg.GetByte(index);
  char key[64];
  snprintf(key, sizeof(key), "n2k:%u/%u/%u/%u/%016llx/%u/%u/%u", type, system,
           subsystem, id, static_cast<unsigned long long>(source_name),
           instance, source_index, occurrence);
  return key;
}

N2kAlert* find_n2k_alert(const String& key) {
  auto it = n2k_alerts.find(key);
  if (it != n2k_alerts.end()) {
    return &it->second;
  }
  if (n2k_alerts.size() >= kN2kMaxAlerts) {
    return nullptr;
  }
  return &n2k_alerts[key];
}

String read_n2k_string(const tN2kMsg& msg, int& index) {
  char buffer[128] = "";
  size_t size = sizeof(buffer);
  msg.GetVarStr(size, buffer, index);
  String text{buffer};
  text.trim();
  return text;
}

Severity n2k_alert_severity(const N2kAlert& alert) {
  switch (alert.type) {
    case 1:
      return Severity::kEmergency;
    case 2:
      return Severity::kAlarm;
    case 5:
      return Severity::kWarn;
    default:
      return Severity::kAlert;
  }
}

// The words used to compose the NMEA 2000 alert sentences and the startup
// announcement, by language.
struct Phrases {
  const char* language;  // Two-letter prefix of the language code
  const char* emergency_alarm;
  const char* alarm;
  const char* warning;
  const char* caution;
  const char* alert;
  // Category, with the separator that joins it to the alert type.
  const char* navigational;
  const char* technical;
  // The category goes before the type, as in "Technical warning".
  bool category_first;
  const char* number;
  const char* active;
};

const Phrases kPhrases[] = {
    {"en", "emergency alarm", "alarm", "warning", "caution", "alert",
     "Navigational ", "Technical ", true, "number", "active"},
    {"it", "Allarme di emergenza", "Allarme", "Avviso", "Avviso di cautela",
     "Avviso", " di navigazione", " tecnico", false, "numero", "attivo"},
    {"de", "Notfallalarm", "Alarm", "Warnung", "Vorsicht", "Meldung",
     "Navigation, ", "Technik, ", true, "Nummer", "aktiv"},
    {"es", "Alarma de emergencia", "Alarma", "Advertencia", "Precaución",
     "Alerta", " de navegación", " técnica", false, "número", "activo"},
    {"fr", "Alarme d'urgence", "Alarme", "Avertissement", "Attention",
     "Alerte", " de navigation", " technique", false, "numéro", "actif"},
};

// The phrases of the configured language, or the English ones.
const Phrases& phrases() {
  for (const Phrases& candidate : kPhrases) {
    if (config->language().startsWith(candidate.language)) {
      return candidate;
    }
  }
  return kPhrases[0];
}

// Compose the sentence to speak in the configured language, e.g.
// "Technical warning. Low oil pressure. Engine room."
String compose_n2k_alert_message(const N2kAlert& alert) {
  const Phrases& words = phrases();
  const char* type;
  switch (alert.type) {
    case 1:
      type = words.emergency_alarm;
      break;
    case 2:
      type = words.alarm;
      break;
    case 5:
      type = words.warning;
      break;
    case 8:
      type = words.caution;
      break;
    default:
      type = words.alert;
      break;
  }
  const char* category = alert.category == 0   ? words.navigational
                         : alert.category == 1 ? words.technical
                                               : "";
  String message = words.category_first ? String(category) + type
                                        : String(type) + category;
  if (alert.text.length() == 0) {
    return message + " " + words.number + " " + String(alert.id) + ".";
  }
  message += ". " + alert.text + ".";
  if (alert.location.length() > 0) {
    message += " " + alert.location + ".";
  }
  return message;
}

void handle_n2k_alert(const tN2kMsg& msg) {
  int index = 0;
  uint8_t type;
  uint8_t category;
  uint16_t id;
  const String key = parse_n2k_alert_header(msg, index, type, category, id);
  // Skip the status flags, the acknowledge source NAME, the trigger
  // condition, the threshold status and the priority.
  index += 11;
  const uint8_t state = msg.GetByte(index);

  if (state == kN2kAlertStateDisabled || state == kN2kAlertStateNormal) {
    // Cleared: forget it so that it is announced again the next time.
    n2k_alerts.erase(key);
    alarms.erase(key);
    return;
  }
  N2kAlert* alert = find_n2k_alert(key);
  if (alert == nullptr) {
    return;
  }
  alert->type = type;
  alert->category = category;
  alert->id = id;
  alert->silenced = state == kN2kAlertStateSilenced;
  alert->acknowledged = state == kN2kAlertStateAcknowledged;
  if (!alert->raised) {
    alert->raised = true;
    alert->raised_at_ms = millis();
  }
}

void handle_n2k_alert_text(const tN2kMsg& msg) {
  int index = 0;
  uint8_t type;
  uint8_t category;
  uint16_t id;
  const String key = parse_n2k_alert_header(msg, index, type, category, id);
  msg.GetByte(index);  // Language ID
  N2kAlert* alert = find_n2k_alert(key);
  if (alert == nullptr) {
    return;
  }
  alert->type = type;
  alert->category = category;
  alert->id = id;
  alert->text = read_n2k_string(msg, index);
  alert->location = read_n2k_string(msg, index);
}

void handle_n2k_message(const tN2kMsg& msg) {
  n2k_message_received = true;
  n2k_message_at_ms = millis();
  // NMEA 2000 is the fallback: the Signal K server reports the alarms while
  // it is connected.
  if (signalk_connected()) {
    return;
  }
  if (msg.PGN == kN2kAlertPgn) {
    handle_n2k_alert(msg);
  } else if (msg.PGN == kN2kAlertTextPgn) {
    handle_n2k_alert_text(msg);
  }
}

// The alert and its text come in separate messages: raise the alarm once the
// text is known, or without it if the text does not show up in time. A text
// arriving after that changes the message, which is then spoken again.
void raise_n2k_alarms() {
  for (auto& [key, alert] : n2k_alerts) {
    if (!sources->n2k_alert_enabled(alert.type, alert.category)) {
      alarms.erase(key);
      continue;
    }
    if (alert.raised &&
        (alert.text.length() > 0 ||
         millis() - alert.raised_at_ms > kN2kAlertTextWaitMs)) {
      raise_alarm(key, false, n2k_alert_severity(alert),
                  compose_n2k_alert_message(alert), !alert.silenced,
                  alert.acknowledged);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

// Switch between the two alarm sources as the Signal K server comes and goes.
// What the other source reported is dropped: it is no longer kept up to date.
void check_alarm_source() {
  static bool was_connected = false;
  const bool connected = signalk_connected();
  if (connected == was_connected) {
    return;
  }
  was_connected = connected;
  ESP_LOGI(__FILENAME__, "Alarms now come from %s",
           connected ? "Signal K" : "NMEA 2000");
  if (connected) {
    n2k_alerts.clear();
  }
  clear_alarms(!connected);
}

// How long the device may fail to join the WiFi network before it opens its
// own access point, so that the web interface stays within reach.
constexpr uint32_t kAccessPointFallbackMs = 60000;
constexpr char kAccessPointFallbackPassword[] = "thisisfine";

// With the access point disabled in the WiFi settings, a wrong password or a
// network that is gone would lock the user out of the device.
void check_access_point_fallback() {
  static uint32_t last_ok_ms = 0;
  if (WiFi.isConnected() || (WiFi.getMode() & WIFI_MODE_AP)) {
    last_ok_ms = millis();
    return;
  }
  if (millis() - last_ok_ms < kAccessPointFallbackMs) {
    return;
  }
  const String ssid = SensESPBaseApp::get_hostname();
  ESP_LOGW(__FILENAME__, "No WiFi connection: opening the access point %s",
           ssid.c_str());
  // The client keeps trying to join the configured network.
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(ssid.c_str(), kAccessPointFallbackPassword)) {
    ESP_LOGE(__FILENAME__, "Failed to start the access point");
  }
}

// Call `on_press` when the push button wired between `pin` and ground is
// pressed.
void setup_button(uint8_t pin, void (*on_press)()) {
  auto* input = new DigitalInputChange(pin, INPUT_PULLUP, CHANGE);
  input->connect_to(new Debounce<bool>(20))
      ->connect_to(new LambdaConsumer<bool>([on_press](bool level) {
        if (level == LOW) {
          on_press();
        }
      }));
}

void setup() {
  SetupLogging();

  // HALMET owns Wi-Fi/Signal K only. All Bluetooth controller work belongs to
  // the S3 and is reached through the Guardian UART protocol.
  btStop();

  // Create the global SensESPApp() object. The on-board RGB LED shows the
  // alarms instead of the system status.
  SensESPAppBuilder builder;
  alarm_led = std::make_shared<AlarmLed>(PIN_RGB_LED);
  sensesp_app = builder.set_hostname("outlook-guardian")
                    ->set_system_status_led(alarm_led)
                    ->get_app();

  // The display is optional: everything else works without it.
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, 400000);
  display_found = setup_display();
  if (!display_found) {
    ESP_LOGW(__FILENAME__, "No display at I2C address 0x%02x",
             OLED_I2C_ADDRESS);
  }

  config = std::make_shared<GuardianConfig>("/Outlook Guardian/Speech");
  ConfigItem(config)
      ->set_title("Speech and alarms")
      ->set_description(
          "Language and voice of the spoken alarms, how they are repeated "
          "and snoozed, and the welcome message.");

  sources = std::make_shared<SourcesConfig>("/Outlook Guardian/Sources");
  ConfigItem(sources)
      ->set_title("Alarm sources")
      ->set_description(
          "The Signal K notification paths to subscribe to, and the NMEA "
          "2000 alerts to announce while the Signal K server is not "
          "connected.")
      ->set_requires_restart(true);

  bluetooth_config =
      std::make_shared<BluetoothConfig>("/Outlook Guardian/Bluetooth Remote");
  ConfigItem(bluetooth_config)
      ->set_title("BLE remote setup (ESP32-S3)")
      ->set_description(
          "Persistent setup only: scan duration, selected BLE device and HID "
          "mappings. Run actions on Control and monitor results on Status. "
          "Bluetooth Classic remotes cannot be discovered by ESP32-S3.");

  // One image supports the production GPIO UART and the developer USB route.
  // In Auto mode frames go to both; replies may arrive from either. The host
  // router extracts CRC-valid frames from the USB stream and ignores logs.
  Serial.begin(115200);
  // Arduino orders these arguments as RX, TX. Match J201 GPIO17 (RX from the
  // S3) and GPIO16 (TX to the S3) documented in docs/hardware/schematic.md.
  Serial2.begin(115200, SERIAL_8N1, 17, 16);
  ArduinoSerialTransport::Mode transport_mode =
      ArduinoSerialTransport::Mode::kAuto;
  if (config->transport() == "GPIO") {
    transport_mode = ArduinoSerialTransport::Mode::kGpio;
  } else if (config->transport() == "USB") {
    transport_mode = ArduinoSerialTransport::Mode::kUsb;
  }
  speech_transport = std::make_unique<ArduinoSerialTransport>(
      &Serial2, &Serial, transport_mode);
  pinMode(GUARDIAN_BUSY_PIN, INPUT_PULLDOWN);
  // HALMET and S3 can reset independently. A fresh boot seed prevents the S3's
  // recent-sequence replay cache from mistaking a new request for one sent by
  // an earlier HALMET boot.
  uint16_t initial_sequence = static_cast<uint16_t>(esp_random());
  if (initial_sequence == 0) initial_sequence = 1;
  speech = std::make_unique<guardian::UartSpeechBackend>(
      speech_transport.get(), initial_sequence);
  speech->set_volume(config->volume_percent());
  speech->on_bluetooth_hid(handle_hid_report);
  s3_link_status = std::make_shared<StatusPageItem<String>>(
      "HALMET ↔ ESP32-S3", "Checking…", "Outlook Guardian", 100);
  bluetooth_action_status = std::make_shared<StatusPageItem<String>>(
      "Bluetooth action", "Idle", "Outlook Guardian", 110);
  bluetooth_devices_status = std::make_shared<StatusPageItem<String>>(
      "Bluetooth scan results", "No scan results yet", "Outlook Guardian", 120);
  UIButton::add("guardian-bt-refresh", "Bluetooth: Refresh status", false)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kRefresh); });
  UIButton::add("guardian-bt-scan", "Bluetooth: Scan for nearby devices", false)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kScan); });
  UIButton::add("guardian-bt-pair", "Bluetooth: Pair selected device", false)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kPair); });
  UIButton::add("guardian-bt-disconnect", "Bluetooth: Disconnect", false)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kDisconnect); });
  UIButton::add("guardian-bt-test", "Bluetooth: Test last button", false)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kTest); });
  UIButton::add("guardian-bt-forget", "Bluetooth: Forget all devices", true)
      ->attach([]() { run_bluetooth_action(BluetoothAction::kForget); });
  // UART is nonblocking and advisory BUSY is deliberately not used as truth.
  event_loop()->onRepeat(5, []() { speech->poll(); });
  event_loop()->onRepeat(2000, []() { speech->ping(); });
  event_loop()->onRepeat(500, []() {
    const uint32_t now = millis();
    const uint32_t received = speech->received_frames();
    if (received != s3_last_frame_count) {
      s3_last_frame_count = received;
      s3_last_frame_ms = now;
    }
    s3_link_up = received > 0 && now - s3_last_frame_ms < 5000;
    const String link = s3_link_up
                            ? "Connected · last valid response " +
                                  String((now - s3_last_frame_ms) / 1000) +
                                  " s ago · " + config->transport()
                            : "Disconnected · no valid response for 5 s · " +
                                  config->transport();
    s3_link_status->set(link);

    const auto& state = speech->bluetooth_state();
    if (bluetooth_action != BluetoothAction::kPair)
      bluetooth_config->set_connected(state.connected);
    if (bluetooth_action != BluetoothAction::kNone) {
      const uint32_t elapsed = now - bluetooth_action_started_ms;
      bool finished = false;
      String result;
      if (speech->last_terminal_sequence() == bluetooth_action_sequence) {
        finished = true;
        if (speech->last_terminal_was_error()) {
          result = "Action failed · S3 returned an error";
        } else if (bluetooth_action == BluetoothAction::kScan ||
                   bluetooth_action == BluetoothAction::kScanPair) {
          const auto& found = speech->bluetooth_devices();
          result = "Scan complete · " + String(static_cast<int>(found.size())) +
                   " device(s) found";
          if (bluetooth_action == BluetoothAction::kScanPair) {
            int candidate = -1;
            int named_matches = 0;
            for (size_t index = 0; index < found.size(); ++index) {
              String name(found[index].name.c_str());
              name.toLowerCase();
              if (name == "qxs-001" || name == "smartremote") {
                candidate = static_cast<int>(index);
                ++named_matches;
              }
            }
            // A nameless HID may expose its name only after connecting. Pair
            // automatically only when exactly one device was discovered.
            if (named_matches != 1 && found.size() == 1) candidate = 0;
            if ((named_matches == 1 || found.size() == 1) && candidate >= 0) {
              const auto& device = found[candidate];
              const String name = device.name.empty() ? "Unnamed device"
                                                       : device.name.c_str();
              bluetooth_config->select_device(device.address.c_str(),
                                              device.address_type, name);
              bluetooth_config->save();
              if (speech->bluetooth_connect(device.address,
                                             device.address_type)) {
                bluetooth_action = BluetoothAction::kPair;
                bluetooth_auto_pairing = true;
                bluetooth_action_started_ms = now;
                bluetooth_action_sequence = speech->last_sent_sequence();
                set_bluetooth_action_status(
                    "Remote found · pairing automatically…");
                return;
              }
              result = "Remote found, but pairing command could not be sent";
            } else if (named_matches > 1) {
              result = "Several QXS-001 remotes found · select one in Configuration";
            } else {
              result = "Scan complete · no named QXS-001 · select the newly "
                       "appearing device in Configuration";
            }
          }
        } else if (bluetooth_action == BluetoothAction::kPair) {
          result = bluetooth_auto_pairing ? "Remote found and paired"
                                          : "Pairing complete";
          bluetooth_config->set_connected(false);
          bluetooth_config->set_connected(true);
          bluetooth_config->save();
          bluetooth_auto_pairing = false;
        } else if (bluetooth_action == BluetoothAction::kDisconnect) {
          result = "Disconnect complete";
          bluetooth_config->set_connected(false);
        } else if (bluetooth_action == BluetoothAction::kForget) {
          bluetooth_config->set_connected(false);
        } else {
          result = "Action completed";
        }
      }
      if (finished) {
        if (bluetooth_action == BluetoothAction::kPair ||
            bluetooth_action == BluetoothAction::kDisconnect ||
            bluetooth_action == BluetoothAction::kForget) {
          speech->bluetooth_get_status();
        }
        bluetooth_action = BluetoothAction::kNone;
        set_bluetooth_action_status(result);
      } else if (elapsed >
                 (bluetooth_action == BluetoothAction::kScan ||
                          bluetooth_action == BluetoothAction::kScanPair
                      ? static_cast<uint32_t>(bluetooth_config->scan_seconds() + 5) *
                            1000
                      : 15000)) {
        bluetooth_action = BluetoothAction::kNone;
        set_bluetooth_action_status("Action timed out · check the S3 link");
      }
    }
    String devices;
    std::vector<BluetoothConfig::Profile> nearby;
    for (const auto& device : speech->bluetooth_devices()) {
      if (devices.length() > 0) devices += "\n";
      const String name = device.name.empty() ? "Unnamed device"
                                               : device.name.c_str();
      BluetoothConfig::Profile item;
      item.address = device.address.c_str();
      item.address_type = device.address_type;
      item.name = name;
      // Reuse this runtime-only field to avoid another almost-identical
      // struct; to_json exposes it as the numeric RSSI value.
      item.last_hid = String(static_cast<int>(device.rssi));
      nearby.push_back(item);
      devices += name + " — " + String(static_cast<int>(device.rssi)) +
                 " dBm — " + device.address.c_str();
    }
    if (devices.length() == 0) {
      devices = state.scanning
                    ? "Scanning… keep the remote awake and in pairing mode"
                    : "No BLE devices found. Keep the remote close, awake and "
                      "in pairing mode, then scan again.";
    }
    bluetooth_config->set_nearby(nearby, state.scanning);
    if (bluetooth_devices_status != nullptr)
      bluetooth_devices_status->set(devices);
  });
  speech->bluetooth_get_status();
  if (config->welcome_enabled()) {
    say(config->welcome_message().length() > 0
            ? config->welcome_message()
            : String("Signal K Outlook Guardian ") + phrases().active);
  }

  // The listeners live as long as the firmware runs.
  for (const String& path : sources->signalk_paths()) {
    ESP_LOGI(__FILENAME__, "Subscribing to %s", path.c_str());
    new NotificationListener(path);
  }

  nmea2000 = new tNMEA2000_esp32(static_cast<gpio_num_t>(N2K_CAN_TX_PIN),
                                 static_cast<gpio_num_t>(N2K_CAN_RX_PIN));
  nmea2000->SetMode(tNMEA2000::N2km_ListenOnly);
  nmea2000->EnableForward(false);
  nmea2000->SetMsgHandler(handle_n2k_message);
  nmea2000->Open();
  event_loop()->onRepeat(1, []() { nmea2000->ParseMessages(); });

  setup_button(ACK_BUTTON_PIN, acknowledge_alarms);
  setup_button(SNOOZE_BUTTON_PIN, snooze_alarms);

  event_loop()->onRepeat(100, []() {
    check_alarm_source();
    raise_n2k_alarms();
    process_alarms();
    check_access_point_fallback();
    update_display();
    alarm_led->set_fault(speech == nullptr || !s3_link_up);
  });
}

void loop() { event_loop()->tick(); }
