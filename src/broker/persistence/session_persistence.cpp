#include "persistence/session_persistence.h"

#include <span>
#include <vector>

#include "data_model/subscription/retain_handling.h"
#include "data_model/subscription/subscription.h"
#include "data_model/subscription/subscription_options.h"
#include "data_model/types/qos.h"
#include "persistence/crash_safe_file.h"
#include "persistence/record_codec.h"

namespace mqtt {

namespace {

void encode_subscription(std::vector<uint8_t> &buf, const Subscription &sub) {
  record_codec::write_utf8(buf, sub.topic_filter);
  record_codec::write_u8(buf, static_cast<uint8_t>(sub.qos));
  record_codec::write_bool(buf, sub.options.no_local);
  record_codec::write_bool(buf, sub.options.retain_as_published);
  record_codec::write_u8(buf,
                         static_cast<uint8_t>(sub.options.retain_handling));
  record_codec::write_bool(buf, sub.identifier.has_value());
  if (sub.identifier.has_value()) {
    record_codec::write_u32(buf, sub.identifier.value());
  }
}

Subscription decode_subscription(std::span<const uint8_t> &cursor) {
  Subscription sub;
  sub.topic_filter = record_codec::read_utf8(cursor);
  sub.qos = static_cast<QoS>(record_codec::read_u8(cursor));
  sub.options.no_local = record_codec::read_bool(cursor);
  sub.options.retain_as_published = record_codec::read_bool(cursor);
  sub.options.retain_handling =
      static_cast<RetainHandling>(record_codec::read_u8(cursor));
  bool has_id = record_codec::read_bool(cursor);
  if (has_id) {
    sub.identifier = record_codec::read_u32(cursor);
  }
  return sub;
}

void encode_session(std::vector<uint8_t> &buf, const SessionState &ses) {
  record_codec::write_utf8(buf, ses.client_id);
  record_codec::write_u32(buf, ses.session_expiry_interval);
  record_codec::write_u32(buf, static_cast<uint32_t>(ses.subscriptions.size()));
  for (const auto &sub : ses.subscriptions) {
    encode_subscription(buf, sub);
  }
}

SessionState decode_session(std::span<const uint8_t> &cursor) {
  SessionState ses;
  ses.client_id = record_codec::read_utf8(cursor);
  ses.session_expiry_interval = record_codec::read_u32(cursor);
  uint32_t sub_count = record_codec::read_u32(cursor);
  ses.subscriptions.reserve(sub_count);
  for (uint32_t idx = 0U; idx < sub_count; ++idx) {
    ses.subscriptions.push_back(decode_subscription(cursor));
  }
  return ses;
}

} // namespace

//  Construction
//

SessionPersistence::SessionPersistence(std::filesystem::path dir)
    : dir_(std::move(dir)) {}

//  Save
//

void SessionPersistence::save_all(const std::vector<SessionState> &sessions) {
  std::filesystem::create_directories(dir_);

  std::vector<uint8_t> records;
  for (const auto &ses : sessions) {
    encode_session(records, ses);
  }

  CrashSafeFile file(dir_, "sessions");
  file.write(records, static_cast<uint32_t>(sessions.size()));
}

//  Load
//

std::vector<SessionState> SessionPersistence::load_all() const {
  CrashSafeFile file(dir_, "sessions");
  auto result = file.read_latest();
  if (!result.has_value()) {
    return {};
  }

  auto [count, record_bytes] = std::move(*result);
  std::span<const uint8_t> cursor(record_bytes);

  std::vector<SessionState> sessions;
  sessions.reserve(count);
  for (uint32_t idx = 0U; idx < count; ++idx) {
    sessions.push_back(decode_session(cursor));
  }
  return sessions;
}

} // namespace mqtt
