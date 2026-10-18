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
};

class SpeechServer {
 public:
  using SendHandler = void (*)(const protocol::Frame&, void*);
  static constexpr size_t kQueueCapacity = 8;
  static constexpr size_t kHistoryCapacity = 16;

  SpeechServer(SpeechEngine* engine, SendHandler send, void* context);
  void handle(const protocol::Frame& frame);
  void poll();
  size_t queued() const { return queue_size_; }

 private:
  struct Entry { uint16_t sequence = 0; protocol::SpeakRequest request; };
  bool seen(uint16_t sequence) const;
  void remember(uint16_t sequence);
  void reply(protocol::Type type, uint16_t sequence,
             std::vector<uint8_t> payload = {});
  void error(uint16_t sequence, protocol::ErrorCode code);
  bool enqueue(Entry entry);

  SpeechEngine* engine_;
  SendHandler send_;
  void* context_;
  std::array<Entry, kQueueCapacity> queue_{};
  size_t queue_size_ = 0;
  std::array<uint16_t, kHistoryCapacity> history_{};
  size_t history_size_ = 0;
  size_t history_next_ = 0;
  uint16_t active_sequence_ = 0;
  bool active_ = false;
};

}  // namespace guardian
#endif
