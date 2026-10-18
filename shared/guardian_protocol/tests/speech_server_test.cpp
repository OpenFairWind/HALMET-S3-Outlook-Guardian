#include "guardian/speech_server.h"

#include <cassert>
#include <vector>

using namespace guardian;

struct Engine : SpeechEngine {
  std::vector<protocol::SpeakRequest> started;
  bool active = false;
  bool start(const protocol::SpeakRequest& r) override { started.push_back(r); return active = true; }
  void stop() override { active = false; }
  bool speaking() const override { return active; }
};
void sent(const protocol::Frame& f, void* p) {
  static_cast<std::vector<protocol::Frame>*>(p)->push_back(f);
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
  SpeechServer server(&engine, sent, &replies);
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
}
