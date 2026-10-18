#include "guardian_protocol/protocol.h"

namespace guardian::protocol {

namespace {
void put16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8));
}
uint16_t get16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}
}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool encode(const Frame& frame, std::vector<uint8_t>* output) {
  if (output == nullptr || frame.payload.size() > kMaxPayload) return false;
  output->clear();
  output->reserve(kHeaderSize + frame.payload.size() + kCrcSize);
  output->push_back(kMagic0);
  output->push_back(kMagic1);
  output->push_back(kVersion);
  output->push_back(static_cast<uint8_t>(frame.type));
  put16(output, frame.sequence);
  put16(output, static_cast<uint16_t>(frame.payload.size()));
  output->insert(output->end(), frame.payload.begin(), frame.payload.end());
  const uint16_t crc = crc16_ccitt(output->data() + 2, output->size() - 2);
  put16(output, crc);
  return true;
}

Parser::Parser(FrameHandler frame_handler, ErrorHandler error_handler,
               void* context)
    : frame_handler_(frame_handler),
      error_handler_(error_handler),
      context_(context) {}

void Parser::reset() {
  state_ = State::kMagic0;
  header_pos_ = payload_pos_ = payload_length_ = 0;
}

void Parser::report(ParseError error) {
  if (error_handler_ != nullptr) error_handler_(error, context_);
}

void Parser::feed(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) consume(data[i]);
}

void Parser::consume(uint8_t byte) {
  switch (state_) {
    case State::kMagic0:
      if (byte == kMagic0) state_ = State::kMagic1;
      break;
    case State::kMagic1:
      if (byte == kMagic1) {
        header_[0] = kMagic0;
        header_[1] = kMagic1;
        header_pos_ = 2;
        state_ = State::kHeader;
      } else {
        state_ = byte == kMagic0 ? State::kMagic1 : State::kMagic0;
      }
      break;
    case State::kHeader:
      header_[header_pos_++] = byte;
      if (header_pos_ == kHeaderSize) {
        if (header_[2] != kVersion) {
          report(ParseError::kUnsupportedVersion);
          reset();
        } else if ((payload_length_ = get16(&header_[6])) > kMaxPayload) {
          report(ParseError::kOversizedPayload);
          reset();
        } else {
          payload_pos_ = 0;
          state_ = payload_length_ == 0 ? State::kCrc0 : State::kPayload;
        }
      }
      break;
    case State::kPayload:
      payload_[payload_pos_++] = byte;
      if (payload_pos_ == payload_length_) state_ = State::kCrc0;
      break;
    case State::kCrc0:
      crc0_ = byte;
      state_ = State::kCrc1;
      break;
    case State::kCrc1: {
      std::array<uint8_t, kHeaderSize - 2 + kMaxPayload> covered{};
      for (size_t i = 2; i < kHeaderSize; ++i) covered[i - 2] = header_[i];
      for (size_t i = 0; i < payload_length_; ++i)
        covered[kHeaderSize - 2 + i] = payload_[i];
      const uint16_t received = static_cast<uint16_t>(crc0_) |
                                static_cast<uint16_t>(byte) << 8;
      const uint16_t expected =
          crc16_ccitt(covered.data(), kHeaderSize - 2 + payload_length_);
      if (received != expected) {
        report(ParseError::kBadCrc);
      } else if (frame_handler_ != nullptr) {
        Frame frame;
        frame.type = static_cast<Type>(header_[3]);
        frame.sequence = get16(&header_[4]);
        frame.payload.assign(payload_.begin(), payload_.begin() + payload_length_);
        frame_handler_(frame, context_);
      }
      reset();
      break;
    }
  }
}

bool valid_utf8(const uint8_t* data, size_t length) {
  size_t i = 0;
  while (i < length) {
    const uint8_t c = data[i++];
    if (c <= 0x7f) continue;
    size_t continuation = 0;
    uint32_t code = 0;
    if ((c & 0xe0) == 0xc0) { continuation = 1; code = c & 0x1f; }
    else if ((c & 0xf0) == 0xe0) { continuation = 2; code = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0) { continuation = 3; code = c & 0x07; }
    else return false;
    if (i + continuation > length) return false;
    for (size_t n = 0; n < continuation; ++n) {
      const uint8_t d = data[i++];
      if ((d & 0xc0) != 0x80) return false;
      code = (code << 6) | (d & 0x3f);
    }
    if ((continuation == 1 && code < 0x80) ||
        (continuation == 2 && code < 0x800) ||
        (continuation == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}

}  // namespace guardian::protocol
