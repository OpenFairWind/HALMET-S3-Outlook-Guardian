#include "guardian_protocol/messages.h"

namespace guardian::protocol {

bool encode_speak(const SpeakRequest& r, std::vector<uint8_t>* out) {
  if (out == nullptr || r.language.empty() || r.language.size() > 15 ||
      r.text.empty() || r.volume > 100 || r.pitch < 50 || r.pitch > 200 ||
      r.speed < 50 || r.speed > 200 ||
      !valid_utf8(reinterpret_cast<const uint8_t*>(r.text.data()), r.text.size()) ||
      5 + r.language.size() + r.text.size() > kMaxPayload) return false;
  out->clear();
  out->push_back(static_cast<uint8_t>(r.priority));
  out->push_back(r.volume);
  out->push_back(r.pitch);
  out->push_back(r.speed);
  out->push_back(static_cast<uint8_t>(r.language.size()));
  out->insert(out->end(), r.language.begin(), r.language.end());
  out->insert(out->end(), r.text.begin(), r.text.end());
  return true;
}

bool decode_speak(const std::vector<uint8_t>& p, SpeakRequest* r) {
  if (r == nullptr || p.size() < 7) return false;
  const size_t language_length = p[4];
  if (language_length == 0 || language_length > 15 ||
      5 + language_length >= p.size() || p[0] > 3 || p[1] > 100 ||
      p[2] < 50 || p[2] > 200 || p[3] < 50 || p[3] > 200) return false;
  const size_t text_at = 5 + language_length;
  if (!valid_utf8(p.data() + text_at, p.size() - text_at)) return false;
  r->priority = static_cast<Priority>(p[0]);
  r->volume = p[1]; r->pitch = p[2]; r->speed = p[3];
  r->language.assign(reinterpret_cast<const char*>(p.data() + 5), language_length);
  r->text.assign(reinterpret_cast<const char*>(p.data() + text_at), p.size() - text_at);
  return true;
}

}  // namespace guardian::protocol
