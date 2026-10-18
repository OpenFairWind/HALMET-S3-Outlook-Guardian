#include <Arduino.h>
#include <ESP_I2S.h>
#include <picotts.h>
#include <picotts_lang.h>

#include <memory>
#include <string>
#include <vector>

#include "guardian/speech_server.h"

#ifndef GUARDIAN_S3_RX_PIN
#define GUARDIAN_S3_RX_PIN 44
#endif
#ifndef GUARDIAN_S3_TX_PIN
#define GUARDIAN_S3_TX_PIN 43
#endif
#ifndef GUARDIAN_S3_BUSY_PIN
#define GUARDIAN_S3_BUSY_PIN 7
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
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
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

 private:
  static void on_samples(int16_t* samples, unsigned count) {
    if (instance_ == nullptr) return;
    const int32_t gain = instance_->volume_ * 256 / 100;
    for (unsigned i = 0; i < count; ++i)
      samples[i] = static_cast<int16_t>((samples[i] * gain) >> 8);
    instance_->i2s_.write(reinterpret_cast<uint8_t*>(samples),
                          count * sizeof(int16_t));
    instance_->drain_after_ms_ = millis() + 250;
  }
  static void on_idle() { if (instance_ != nullptr) instance_->idle_ = true; }
  static void on_error() { if (instance_ != nullptr) instance_->failed_ = true; }

  static PicoI2sEngine* instance_;
  mutable I2SClass i2s_;
  std::string language_;
  volatile uint8_t volume_ = 50;
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

void send_frame(const guardian::protocol::Frame& frame, void*) {
  std::vector<uint8_t> wire;
  if (guardian::protocol::encode(frame, &wire)) Serial0.write(wire.data(), wire.size());
}
void receive_frame(const guardian::protocol::Frame& frame, void*) {
  if (server != nullptr) server->handle(frame);
}
void receive_error(guardian::protocol::ParseError, void*) {}

}  // namespace

void setup() {
  // UART0 is intentionally the only communications interface. Wi-Fi and the
  // network stack are not started on the speech coprocessor.
  Serial0.begin(115200, SERIAL_8N1, GUARDIAN_S3_RX_PIN, GUARDIAN_S3_TX_PIN);
  pinMode(GUARDIAN_S3_BUSY_PIN, OUTPUT);
  digitalWrite(GUARDIAN_S3_BUSY_PIN, LOW);
  if (!engine.begin()) return;
  server = std::make_unique<guardian::SpeechServer>(&engine, send_frame, nullptr);
  parser = new guardian::protocol::Parser(receive_frame, receive_error, nullptr);
  send_frame({guardian::protocol::Type::kHello, 0, {1, 8}}, nullptr);
}

void loop() {
  while (parser != nullptr && Serial0.available() > 0) {
    const int value = Serial0.read();
    if (value < 0) break;
    const uint8_t byte = static_cast<uint8_t>(value);
    parser->feed(&byte, 1);
  }
  if (server != nullptr) {
    server->poll();
    digitalWrite(GUARDIAN_S3_BUSY_PIN,
                 engine.speaking() || server->queued() > 0 ? HIGH : LOW);
  }
  delay(1);
}
