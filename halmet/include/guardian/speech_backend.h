#ifndef GUARDIAN_SPEECH_BACKEND_H_
#define GUARDIAN_SPEECH_BACKEND_H_

#include "guardian_protocol/messages.h"

#include <cstdint>

namespace guardian {

class SpeechBackend {
 public:
  virtual ~SpeechBackend() = default;
  // Accepted means queued by the local transport, not played by the S3.
  virtual bool speak(const protocol::SpeakRequest& request) = 0;
  virtual bool stop() = 0;
  virtual void poll() = 0;
  virtual bool connected() const = 0;
  virtual bool speaking() const = 0;
};

class ByteTransport {
 public:
  virtual ~ByteTransport() = default;
  virtual size_t available() const = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t* bytes, size_t length) = 0;
};

}  // namespace guardian

#endif
