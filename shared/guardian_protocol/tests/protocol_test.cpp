#include "guardian_protocol/protocol.h"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace guardian::protocol;

struct Capture { std::vector<Frame> frames; std::vector<ParseError> errors; };
void got_frame(const Frame& f, void* p) { static_cast<Capture*>(p)->frames.push_back(f); }
void got_error(ParseError e, void* p) { static_cast<Capture*>(p)->errors.push_back(e); }

int main() {
  // CRC catalogue check value for CRC-16/CCITT-FALSE.
  const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
  assert(crc16_ccitt(check, sizeof(check)) == 0x29b1);

  Frame speak{Type::kSpeak, 0x1234, {'H','i'}};
  std::vector<uint8_t> wire;
  assert(encode(speak, &wire));
  const std::vector<uint8_t> golden =
      {0x47,0x53,0x01,0x10,0x34,0x12,0x02,0x00,0x48,0x69,0xe0,0xbc};
  assert(wire == golden);

  Capture capture;
  Parser parser(got_frame, got_error, &capture);
  for (uint8_t b : wire) parser.feed(&b, 1);  // every fragmentation boundary
  assert(capture.frames.size() == 1 && capture.frames[0] == speak);

  auto corrupt = wire;
  corrupt[8] ^= 1;
  parser.feed(corrupt.data(), corrupt.size());
  assert(capture.errors.back() == ParseError::kBadCrc);
  // Noise and a partial magic prefix must resynchronize.
  const uint8_t noise[] = {0, 0x47, 0, 0xff, 0x47};
  parser.feed(noise, sizeof(noise));
  parser.feed(wire.data() + 1, wire.size() - 1);
  assert(capture.frames.size() == 2);

  const uint8_t valid[] = {0x49,0x74,0x61,0x6c,0x69,0x61,0x6e,0x6f,0x20,0xc3,0xa8};
  const uint8_t overlong[] = {0xc0,0x80};
  const uint8_t surrogate[] = {0xed,0xa0,0x80};
  assert(valid_utf8(valid, sizeof(valid)));
  assert(!valid_utf8(overlong, sizeof(overlong)));
  assert(!valid_utf8(surrogate, sizeof(surrogate)));

  Frame too_big{Type::kSpeak, 1, std::vector<uint8_t>(kMaxPayload + 1)};
  assert(!encode(too_big, &wire));
}
