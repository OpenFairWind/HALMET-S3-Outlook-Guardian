#ifndef GUARDIAN_PROTOCOL_PROTOCOL_H_
#define GUARDIAN_PROTOCOL_PROTOCOL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace guardian::protocol {

constexpr uint8_t kMagic0 = 0x47;  // 'G'
constexpr uint8_t kMagic1 = 0x53;  // 'S'
constexpr uint8_t kVersion = 1;
constexpr size_t kMaxPayload = 512;
constexpr size_t kHeaderSize = 8;
constexpr size_t kCrcSize = 2;
constexpr size_t kMaxFrameSize = kHeaderSize + kMaxPayload + kCrcSize;

enum class Type : uint8_t {
  kHello = 0x01,
  kCaps = 0x02,
  kPing = 0x03,
  kPong = 0x04,
  kSpeak = 0x10,
  kStop = 0x11,
  kStatus = 0x12,
  kAck = 0x13,
  kStarted = 0x14,
  kDone = 0x15,
  kError = 0x16,
  kSetConfig = 0x17,
  kSetRgb = 0x18,
  kTestAll = 0x19,
  kSetVolume = 0x1a,
  kUpdateBegin = 0x20,
  kUpdateChunk = 0x21,
  kUpdateEnd = 0x22,
  kUpdateAbort = 0x23,
  kBtGetStatus = 0x30,
  kBtSetEnabled = 0x31,
  kBtScan = 0x32,
  kBtScanStop = 0x33,
  kBtConnect = 0x34,
  kBtDisconnect = 0x35,
  kBtForget = 0x36,
  kBtState = 0x37,
  kBtDevice = 0x38,
  kBtHidEvent = 0x39,
  kBtTest = 0x3a,
};

enum class ParseError {
  kNone,
  kUnsupportedVersion,
  kOversizedPayload,
  kBadCrc,
};

struct Frame {
  Type type = Type::kError;
  uint16_t sequence = 0;
  std::vector<uint8_t> payload;

  bool operator==(const Frame& other) const {
    return type == other.type && sequence == other.sequence &&
           payload == other.payload;
  }
};

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, no reflection, xorout 0.
uint16_t crc16_ccitt(const uint8_t* data, size_t length);

bool encode(const Frame& frame, std::vector<uint8_t>* output);

class Parser {
 public:
  using FrameHandler = void (*)(const Frame&, void*);
  using ErrorHandler = void (*)(ParseError, void*);

  Parser(FrameHandler frame_handler, ErrorHandler error_handler, void* context);
  void feed(const uint8_t* data, size_t length);
  void reset();

 private:
  void consume(uint8_t byte);
  void report(ParseError error);

  enum class State { kMagic0, kMagic1, kHeader, kPayload, kCrc0, kCrc1 };
  State state_ = State::kMagic0;
  std::array<uint8_t, kHeaderSize> header_{};
  std::array<uint8_t, kMaxPayload> payload_{};
  size_t header_pos_ = 0;
  size_t payload_pos_ = 0;
  uint16_t payload_length_ = 0;
  uint8_t crc0_ = 0;
  FrameHandler frame_handler_;
  ErrorHandler error_handler_;
  void* context_;
};

bool valid_utf8(const uint8_t* data, size_t length);

}  // namespace guardian::protocol

#endif
