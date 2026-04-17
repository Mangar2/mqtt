#include "persistence/inflight_persistence.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/variable_byte_integer.h"
#include "persistence/crash_safe_file.h"
#include "persistence/persistence_error.h"
#include "persistence/record_codec.h"

namespace mqtt {

namespace {

// ── Property serialization (shared with retained_message_persistence)
// ─────────

void encode_property_value_inf(std::vector<uint8_t> &buf,
                               const PropertyValue &val) {
  record_codec::write_u8(buf, static_cast<uint8_t>(val.index()));
  std::visit(
      [&buf](const auto &inner) {
        using T = std::decay_t<decltype(inner)>;
        if constexpr (std::is_same_v<T, uint8_t>) {
          record_codec::write_u8(buf, inner);
        } else if constexpr (std::is_same_v<T, TwoByteInteger>) {
          record_codec::write_u16(buf, inner);
        } else if constexpr (std::is_same_v<T, FourByteInteger>) {
          record_codec::write_u32(buf, inner);
        } else if constexpr (std::is_same_v<T, VariableByteInteger>) {
          record_codec::write_u32(buf, inner.value);
        } else if constexpr (std::is_same_v<T, Utf8String>) {
          record_codec::write_utf8(buf, inner);
        } else if constexpr (std::is_same_v<T, Utf8StringPair>) {
          record_codec::write_utf8(buf, inner.name);
          record_codec::write_utf8(buf, inner.value);
        } else if constexpr (std::is_same_v<T, BinaryData>) {
          record_codec::write_binary(buf, inner);
        }
      },
      val);
}

PropertyValue decode_property_value_inf(std::span<const uint8_t> &cursor,
                                        uint8_t type_tag) {
  switch (type_tag) {
  case 0U:
    return PropertyValue{record_codec::read_u8(cursor)};
  case 1U:
    return PropertyValue{TwoByteInteger{record_codec::read_u16(cursor)}};
  case 2U:
    return PropertyValue{FourByteInteger{record_codec::read_u32(cursor)}};
  case 3U: {
    VariableByteInteger vbi;
    vbi.value = record_codec::read_u32(cursor);
    return PropertyValue{vbi};
  }
  case 4U:
    return PropertyValue{record_codec::read_utf8(cursor)};
  case 5U: {
    Utf8StringPair pair;
    pair.name = record_codec::read_utf8(cursor);
    pair.value = record_codec::read_utf8(cursor);
    return PropertyValue{pair};
  }
  case 6U:
    return PropertyValue{record_codec::read_binary(cursor)};
  default:
    throw PersistenceException(
        PersistenceError::CorruptRecord,
        "Unknown property value type tag in inflight record");
  }
}

void encode_properties_inf(std::vector<uint8_t> &buf,
                           const std::vector<Property> &props) {
  record_codec::write_u32(buf, static_cast<uint32_t>(props.size()));
  for (const auto &prop : props) {
    record_codec::write_u8(buf, static_cast<uint8_t>(prop.id));
    encode_property_value_inf(buf, prop.value);
  }
}

std::vector<Property> decode_properties_inf(std::span<const uint8_t> &cursor) {
  uint32_t count = record_codec::read_u32(cursor);
  std::vector<Property> props;
  props.reserve(count);
  for (uint32_t idx = 0U; idx < count; ++idx) {
    Property prop;
    prop.id = static_cast<PropertyId>(record_codec::read_u8(cursor));
    uint8_t type_tag = record_codec::read_u8(cursor);
    prop.value = decode_property_value_inf(cursor, type_tag);
    props.push_back(std::move(prop));
  }
  return props;
}

// ── Message serialization
// ─────────────────────────────────────────────────────

void encode_message_inf(std::vector<uint8_t> &buf, const Message &msg) {
  record_codec::write_utf8(buf, msg.topic);
  record_codec::write_binary(buf, msg.payload);
  record_codec::write_u8(buf, static_cast<uint8_t>(msg.qos));
  record_codec::write_bool(buf, msg.retain);
  encode_properties_inf(buf, msg.properties);
}

Message decode_message_inf(std::span<const uint8_t> &cursor) {
  Message msg;
  msg.topic = record_codec::read_utf8(cursor);
  msg.payload = record_codec::read_binary(cursor);
  msg.qos = static_cast<QoS>(record_codec::read_u8(cursor));
  msg.retain = record_codec::read_bool(cursor);
  msg.properties = decode_properties_inf(cursor);
  return msg;
}

// ── ClientEntry serialization
// ─────────────────────────────────────────────────

void encode_client_entry(std::vector<uint8_t> &buf,
                         const InflightPersistence::ClientEntry &rec) {
  // client_id stored as a plain UTF-8 string.
  record_codec::write_string(buf, rec.client_id);
  record_codec::write_u16(buf, rec.entry.packet_id);
  encode_message_inf(buf, rec.entry.message);
  record_codec::write_u8(buf, static_cast<uint8_t>(rec.entry.qos));
  record_codec::write_u8(buf, static_cast<uint8_t>(rec.entry.state));
  record_codec::write_u8(buf, static_cast<uint8_t>(rec.entry.direction));
  // Timestamps are not portable across reboots — write 0 as a placeholder.
  record_codec::write_u32(buf, 0U);
  record_codec::write_u32(buf, 0U);
}

InflightPersistence::ClientEntry
decode_client_entry(std::span<const uint8_t> &cursor) {
  InflightPersistence::ClientEntry rec;
  // client_id: read as Utf8String then take the value.
  rec.client_id = record_codec::read_utf8(cursor).value;
  rec.entry.packet_id = record_codec::read_u16(cursor);
  rec.entry.message = decode_message_inf(cursor);
  rec.entry.qos = static_cast<QoS>(record_codec::read_u8(cursor));
  rec.entry.state = static_cast<InflightState>(record_codec::read_u8(cursor));
  rec.entry.direction =
      static_cast<InflightDirection>(record_codec::read_u8(cursor));
  // Discard stored placeholder; reset to now so QoS engine retransmits.
  (void)record_codec::read_u32(cursor);
  (void)record_codec::read_u32(cursor);
  rec.entry.timestamp = std::chrono::steady_clock::now();
  return rec;
}

} // namespace

// ── Construction
// ──────────────────────────────────────────────────────────────

InflightPersistence::InflightPersistence(std::filesystem::path dir)
    : dir_(std::move(dir)) {}

// ── Save
// ──────────────────────────────────────────────────────────────────────

void InflightPersistence::save_all(
    const std::vector<InflightPersistence::ClientEntry> &entries) {
  std::filesystem::create_directories(dir_);

  std::vector<uint8_t> records;
  for (const auto &rec : entries) {
    encode_client_entry(records, rec);
  }

  CrashSafeFile file(dir_, "inflight");
  file.write(records, static_cast<uint32_t>(entries.size()));
}

// ── Load
// ──────────────────────────────────────────────────────────────────────

std::vector<InflightPersistence::ClientEntry>
InflightPersistence::load_all() const {
  CrashSafeFile file(dir_, "inflight");
  auto result = file.read_latest();
  if (!result.has_value()) {
    return {};
  }

  auto [count, record_bytes] = std::move(*result);
  std::span<const uint8_t> cursor(record_bytes);

  std::vector<ClientEntry> entries;
  entries.reserve(count);
  for (uint32_t idx = 0U; idx < count; ++idx) {
    entries.push_back(decode_client_entry(cursor));
  }
  return entries;
}

} // namespace mqtt
