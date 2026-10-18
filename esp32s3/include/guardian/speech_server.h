#ifndef GUARDIAN_SPEECH_SERVER_H_
#define GUARDIAN_SPEECH_SERVER_H_

#include "guardian_protocol/messages.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace guardian {

class SpeechEngine {
 public:
  virtual ~SpeechEngine() = default;
  virtual bool start(const protocol::SpeakRequest& request) = 0;
  virtual void stop() = 0;
  virtual bool speaking() const = 0;
  virtual void set_volume(uint8_t volume) = 0;
};

class SpeechServer {
 public:
  using SendHandler = void (*)(const protocol::Frame&, void*);
  using RgbHandler = void (*)(uint8_t red, uint8_t green, uint8_t blue,
                              void* context);
  static constexpr size_t kQueueCapacity = 8;
  static constexpr size_t kHistoryCapacity = 16;

  SpeechServer(SpeechEngine* engine, SendHandler send, void* context,
               RgbHandler set_rgb = nullptr, void* rgb_context = nullptr);
  void handle(const protocol::Frame& frame);
  void poll();
  size_t queued() const { return queue_size_; }

 private:
  struct Entry {
    uint16_t sequence = 0;
    protocol::SpeakRequest request;
    bool report_started = true;
    bool report_done = true;
  };
  bool seen(uint16_t sequence) const;
  void remember(uint16_t sequence);
  void reply(protocol::Type type, uint16_t sequence,
             std::vector<uint8_t> payload = {});
  void error(uint16_t sequence, protocol::ErrorCode code);
  bool enqueue(Entry entry);
  void remove_queued(uint16_t sequence);

  SpeechEngine* engine_;
  SendHandler send_;
  void* context_;
  RgbHandler set_rgb_;
  void* rgb_context_;
  std::array<Entry, kQueueCapacity> queue_{};
  size_t queue_size_ = 0;
  std::array<uint16_t, kHistoryCapacity> history_{};
  size_t history_size_ = 0;
  size_t history_next_ = 0;
  uint16_t active_sequence_ = 0;
  bool active_report_done_ = true;
  bool active_ = false;
};

}  // namespace guardian
#endif
