#ifndef GUARDIAN_PROTOCOL_MESSAGES_H_
#define GUARDIAN_PROTOCOL_MESSAGES_H_

#include "guardian_protocol/protocol.h"

#include <cstdint>
#include <string>

namespace guardian::protocol {

enum class Priority : uint8_t { kLow = 0, kNormal = 1, kHigh = 2, kCritical = 3 };
enum class ErrorCode : uint8_t {
  kMalformed = 1, kInvalidUtf8 = 2, kQueueFull = 3, kUnsupported = 4,
  kPlayback = 5, kUpdateRejected = 6
};

struct SpeakRequest {
  Priority priority = Priority::kNormal;
  uint8_t volume = 50;
  uint8_t pitch = 100;
  uint8_t speed = 100;
  std::string language;
  std::string text;
};

// SPEAK payload: priority, volume, pitch, speed, language byte length,
// language bytes, UTF-8 text bytes. Text is limited by the frame maximum.
bool encode_speak(const SpeakRequest& request, std::vector<uint8_t>* payload);
bool decode_speak(const std::vector<uint8_t>& payload, SpeakRequest* request);

}  // namespace guardian::protocol

#endif
