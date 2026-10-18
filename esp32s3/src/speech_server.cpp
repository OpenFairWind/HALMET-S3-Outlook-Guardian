#include "guardian/speech_server.h"

#include <utility>

namespace guardian {

SpeechServer::SpeechServer(SpeechEngine* engine, SendHandler send, void* context)
    : engine_(engine), send_(send), context_(context) {}

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
    reply(protocol::Type::kDone, active_sequence_);
    active_ = false;
  }
  if (active_ || queue_size_ == 0 || engine_ == nullptr) return;
  Entry entry = std::move(queue_[0]);
  for (size_t i = 1; i < queue_size_; ++i) queue_[i - 1] = std::move(queue_[i]);
  --queue_size_;
  if (!engine_->start(entry.request)) {
    error(entry.sequence, protocol::ErrorCode::kPlayback);
    return;
  }
  active_sequence_ = entry.sequence;
  active_ = true;
  reply(protocol::Type::kStarted, entry.sequence);
}

}  // namespace guardian
