#include "guardian/speech_server.h"

#include <utility>

namespace guardian {

SpeechServer::SpeechServer(SpeechEngine* engine, SendHandler send, void* context,
                           RgbHandler set_rgb, void* rgb_context)
    : engine_(engine),
      send_(send),
      context_(context),
      set_rgb_(set_rgb),
      rgb_context_(rgb_context) {}

void SpeechServer::reply(protocol::Type type, uint16_t sequence,
                         std::vector<uint8_t> payload) {
  if (send_ != nullptr) send_({type, sequence, std::move(payload)}, context_);
}

void SpeechServer::error(uint16_t sequence, protocol::ErrorCode code) {
  reply(protocol::Type::kError, sequence, {static_cast<uint8_t>(code)});
}

bool SpeechServer::seen(uint16_t sequence) const {
  for (size_t i = 0; i < history_size_; ++i) if (history_[i] == sequence) return true;
  return false;
}

void SpeechServer::remember(uint16_t sequence) {
  history_[history_next_] = sequence;
  history_next_ = (history_next_ + 1) % kHistoryCapacity;
  if (history_size_ < kHistoryCapacity) ++history_size_;
}

bool SpeechServer::enqueue(Entry entry) {
  if (queue_size_ == kQueueCapacity) return false;
  size_t at = queue_size_;
  while (at > 0 && static_cast<uint8_t>(queue_[at - 1].request.priority) <
                       static_cast<uint8_t>(entry.request.priority)) {
    queue_[at] = std::move(queue_[at - 1]);
    --at;
  }
  queue_[at] = std::move(entry);
  ++queue_size_;
  return true;
}

void SpeechServer::remove_queued(uint16_t sequence) {
  size_t write = 0;
  for (size_t read = 0; read < queue_size_; ++read) {
    if (queue_[read].sequence != sequence)
      queue_[write++] = std::move(queue_[read]);
  }
  queue_size_ = write;
}

void SpeechServer::handle(const protocol::Frame& frame) {
  if (frame.type == protocol::Type::kPing) {
    reply(protocol::Type::kPong, frame.sequence);
    return;
  }
  if (frame.type == protocol::Type::kStop) {
    if (engine_ != nullptr) engine_->stop();
    queue_size_ = 0;
    active_ = false;
    reply(protocol::Type::kAck, frame.sequence);
    reply(protocol::Type::kDone, frame.sequence);
    return;
  }
  if (frame.type == protocol::Type::kSetRgb) {
    if (frame.payload.size() != 3) {
      error(frame.sequence, protocol::ErrorCode::kMalformed);
      return;
    }
    if (set_rgb_ == nullptr) {
      error(frame.sequence, protocol::ErrorCode::kUnsupported);
      return;
    }
    set_rgb_(frame.payload[0], frame.payload[1], frame.payload[2], rgb_context_);
    reply(protocol::Type::kAck, frame.sequence);
    reply(protocol::Type::kDone, frame.sequence);
    return;
  }
  if (frame.type == protocol::Type::kSetVolume) {
    if (frame.payload.size() != 1 || frame.payload[0] > 100) {
      error(frame.sequence, protocol::ErrorCode::kMalformed);
      return;
    }
    if (engine_ == nullptr) {
      error(frame.sequence, protocol::ErrorCode::kUnsupported);
      return;
    }
    engine_->set_volume(frame.payload[0]);
    reply(protocol::Type::kAck, frame.sequence);
    reply(protocol::Type::kDone, frame.sequence);
    return;
  }
  if (frame.type == protocol::Type::kTestAll) {
    if (!frame.payload.empty()) {
      error(frame.sequence, protocol::ErrorCode::kMalformed);
      return;
    }
    if (seen(frame.sequence)) {
      reply(protocol::Type::kAck, frame.sequence);
      return;
    }
    struct TestSentence { const char* language; const char* text; };
    static constexpr TestSentence tests[] = {
        {"en-US", "This is the American English voice."},
        {"en-GB", "This is the British English voice."},
        {"it-IT", "Questa è la voce italiana."},
        {"de-DE", "Dies ist die deutsche Stimme."},
        {"es-ES", "Esta es la voz española."},
        {"fr-FR", "Ceci est la voix française."},
    };
    constexpr size_t test_count = sizeof(tests) / sizeof(tests[0]);
    if (kQueueCapacity - queue_size_ < test_count) {
      error(frame.sequence, protocol::ErrorCode::kQueueFull);
      return;
    }
    for (size_t i = 0; i < test_count; ++i) {
      protocol::SpeakRequest request;
      request.priority = protocol::Priority::kNormal;
      request.volume = 80;
      request.pitch = 100;
      request.speed = 100;
      request.language = tests[i].language;
      request.text = tests[i].text;
      enqueue({frame.sequence, std::move(request), i == 0,
               i + 1 == test_count});
    }
    remember(frame.sequence);
    reply(protocol::Type::kAck, frame.sequence);
    return;
  }
  if (frame.type != protocol::Type::kSpeak) {
    error(frame.sequence, protocol::ErrorCode::kUnsupported);
    return;
  }
  if (seen(frame.sequence)) {
    // Idempotent retry: confirm acceptance without adding another playback.
    reply(protocol::Type::kAck, frame.sequence);
    return;
  }
  protocol::SpeakRequest request;
  if (!protocol::decode_speak(frame.payload, &request)) {
    error(frame.sequence, protocol::ErrorCode::kMalformed);
    return;
  }
  // A critical request cancels lower-priority active audio. Other requests do
  // not interrupt and are ordered stably by priority in the bounded queue.
  if (active_ && request.priority == protocol::Priority::kCritical &&
      engine_ != nullptr) {
    engine_->stop();
    error(active_sequence_, protocol::ErrorCode::kPlayback);
    remove_queued(active_sequence_);
    active_ = false;
  }
  if (!enqueue({frame.sequence, std::move(request)})) {
    error(frame.sequence, protocol::ErrorCode::kQueueFull);
    return;
  }
  remember(frame.sequence);
  reply(protocol::Type::kAck, frame.sequence);
}

void SpeechServer::poll() {
  if (active_ && engine_ != nullptr && !engine_->speaking()) {
    if (active_report_done_) reply(protocol::Type::kDone, active_sequence_);
    active_ = false;
  }
  if (active_ || queue_size_ == 0 || engine_ == nullptr) return;
  Entry entry = std::move(queue_[0]);
  for (size_t i = 1; i < queue_size_; ++i) queue_[i - 1] = std::move(queue_[i]);
  --queue_size_;
  if (!engine_->start(entry.request)) {
    error(entry.sequence, protocol::ErrorCode::kPlayback);
    remove_queued(entry.sequence);
    return;
  }
  active_sequence_ = entry.sequence;
  active_report_done_ = entry.report_done;
  active_ = true;
  if (entry.report_started)
    reply(protocol::Type::kStarted, entry.sequence);
}

}  // namespace guardian
