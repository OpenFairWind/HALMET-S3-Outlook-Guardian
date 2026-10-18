#include "guardian/speech_server.h"

#include <cassert>
#include <vector>

using namespace guardian;

struct Engine : SpeechEngine {
  std::vector<protocol::SpeakRequest> started;
  bool active = false;
  uint8_t volume = 100;
  bool start(const protocol::SpeakRequest& r) override { started.push_back(r); return active = true; }
  void stop() override { active = false; }
  bool speaking() const override { return active; }
  void set_volume(uint8_t value) override { volume = value; }
};
void sent(const protocol::Frame& f, void* p) {
  static_cast<std::vector<protocol::Frame>*>(p)->push_back(f);
}
struct Rgb { uint8_t red = 0; uint8_t green = 0; uint8_t blue = 0; };
void set_rgb(uint8_t red, uint8_t green, uint8_t blue, void* p) {
  *static_cast<Rgb*>(p) = {red, green, blue};
}
protocol::Frame speak(uint16_t sequence, protocol::Priority priority, const char* text) {
  protocol::SpeakRequest request{priority, 50, 100, 100, "it-IT", text};
  std::vector<uint8_t> payload;
  assert(protocol::encode_speak(request, &payload));
  return {protocol::Type::kSpeak, sequence, payload};
}

int main() {
  Engine engine;
  std::vector<protocol::Frame> replies;
  Rgb rgb;
  SpeechServer server(&engine, sent, &replies, set_rgb, &rgb);
  server.handle({protocol::Type::kSetRgb, 50, {10, 20, 30}});
  assert(rgb.red == 10 && rgb.green == 20 && rgb.blue == 30);
  assert(replies[replies.size() - 2].type == protocol::Type::kAck);
  assert(replies.back().type == protocol::Type::kDone);
  server.handle({protocol::Type::kSetRgb, 51, {10, 20}});
  assert(replies.back().type == protocol::Type::kError);
  server.handle({protocol::Type::kSetVolume, 52, {75}});
  assert(engine.volume == 75);
  assert(replies[replies.size() - 2].type == protocol::Type::kAck);
  assert(replies.back().type == protocol::Type::kDone);
  server.handle({protocol::Type::kSetVolume, 53, {101}});
  assert(replies.back().type == protocol::Type::kError);
  server.handle(speak(1, protocol::Priority::kLow, "uno"));
  server.handle(speak(2, protocol::Priority::kHigh, "due"));
  server.handle(speak(2, protocol::Priority::kHigh, "duplicate"));
  assert(server.queued() == 2);  // duplicate was ACKed, never queued twice
  server.poll();
  assert(engine.started[0].text == "due");
  engine.active = false;
  server.poll();
  assert(engine.started[1].text == "uno");

  engine.active = true;
  server.handle(speak(3, protocol::Priority::kCritical, "emergenza"));
  assert(!engine.active);  // critical cancels lower priority playback
  server.poll();
  assert(engine.started.back().text == "emergenza");

  std::vector<uint8_t> bad = {1, 50, 100, 100, 5, 'i'};
  server.handle({protocol::Type::kSpeak, 99, bad});
  assert(replies.back().type == protocol::Type::kError);

  Engine test_engine;
  std::vector<protocol::Frame> test_replies;
  SpeechServer test_server(&test_engine, sent, &test_replies);
  test_server.handle({protocol::Type::kTestAll, 100, {}});
  assert(test_replies.size() == 1 &&
         test_replies[0].type == protocol::Type::kAck);
  test_server.poll();
  for (int i = 0; i < 6; ++i) {
    test_engine.active = false;
    test_server.poll();
  }
  assert(test_engine.started.size() == 6);
  assert(test_engine.started[0].language == "en-US");
  assert(test_engine.started[1].language == "en-GB");
  assert(test_engine.started[2].language == "it-IT");
  assert(test_engine.started[3].language == "de-DE");
  assert(test_engine.started[4].language == "es-ES");
  assert(test_engine.started[5].language == "fr-FR");
  assert(test_replies.size() == 3);
  assert(test_replies[1].type == protocol::Type::kStarted);
  assert(test_replies[2].type == protocol::Type::kDone);
  test_server.handle({protocol::Type::kTestAll, 101, {1}});
  assert(test_replies.back().type == protocol::Type::kError);
}
