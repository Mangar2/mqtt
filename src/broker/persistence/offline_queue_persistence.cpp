#include "persistence/offline_queue_persistence.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/variable_byte_integer.h"
#include "persistence/crash_safe_file.h"
#include "persistence/persistence_error.h"
#include "persistence/record_codec.h"

namespace mqtt {

namespace {

// ---------------------------------------------------------------------------
// Property serialization — identical to inflight_persistence internal helpers.
// ---------------------------------------------------------------------------

void encode_property_value_oq(std::vector<uint8_t> &buf,
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

PropertyValue decode_property_value_oq(std::span<const uint8_t> &cursor,
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
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Unknown property value type in offline queue record");
  }
}

void encode_properties_oq(std::vector<uint8_t> &buf,
                           const std::vector<Property> &props) {
  record_codec::write_u32(buf, static_cast<uint32_t>(props.size()));
  for (const auto &prop : props) {
    record_codec::write_u8(buf, static_cast<uint8_t>(prop.id));
    encode_property_value_oq(buf, prop.value);
  }
}

std::vector<Property> decode_properties_oq(std::span<const uint8_t> &cursor) {
  const uint32_t count = record_codec::read_u32(cursor);
  std::vector<Property> props;
  props.reserve(count);
  for (uint32_t idx = 0U; idx < count; ++idx) {
    Property prop;
    prop.id = static_cast<PropertyId>(record_codec::read_u8(cursor));
    const uint8_t type_tag = record_codec::read_u8(cursor);
    prop.value = decode_property_value_oq(cursor, type_tag);
    props.push_back(std::move(prop));
  }
  return props;
}

// ---------------------------------------------------------------------------
// Message serialization
// ---------------------------------------------------------------------------

void encode_message_oq(std::vector<uint8_t> &buf, const Message &msg) {
  record_codec::write_utf8(buf, msg.topic);
  record_codec::write_binary(buf, msg.payload);
  record_codec::write_u8(buf, static_cast<uint8_t>(msg.qos));
  record_codec::write_bool(buf, msg.retain);
  encode_properties_oq(buf, msg.properties);
}

Message decode_message_oq(std::span<const uint8_t> &cursor) {
  Message msg;
  msg.topic = record_codec::read_utf8(cursor);
  msg.payload = record_codec::read_binary(cursor);
  msg.qos = static_cast<QoS>(record_codec::read_u8(cursor));
  msg.retain = record_codec::read_bool(cursor);
  msg.properties = decode_properties_oq(cursor);
  return msg;
}

// ---------------------------------------------------------------------------
// Flat record: (client_id, Message)
// ---------------------------------------------------------------------------

struct ClientMessageRecord {
  std::string client_id;
  Message message;
};

void encode_record(std::vector<uint8_t> &buf,
                   const std::string &client_id,
                   const Message &msg) {
  record_codec::write_string(buf, client_id);
  encode_message_oq(buf, msg);
}

ClientMessageRecord decode_record(std::span<const uint8_t> &cursor) {
  ClientMessageRecord rec;
  rec.client_id = record_codec::read_utf8(cursor).value;
  rec.message = decode_message_oq(cursor);
  return rec;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OfflineQueuePersistence::OfflineQueuePersistence(std::filesystem::path dir)
    : dir_(std::move(dir)) {}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void OfflineQueuePersistence::save_all(const std::vector<ClientMessages> &entries) {
  std::filesystem::create_directories(dir_);

  std::vector<uint8_t> records;
  uint32_t total_count = 0U;
  for (const auto &client : entries) {
    for (const auto &msg : client.messages) {
      encode_record(records, client.client_id, msg);
      ++total_count;
    }
  }

  CrashSafeFile file(dir_, "offline_queue");
  file.write(records, total_count);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

std::vector<OfflineQueuePersistence::ClientMessages>
OfflineQueuePersistence::load_all() const {
  CrashSafeFile file(dir_, "offline_queue");
  auto result = file.read_latest();
  if (!result.has_value()) {
    return {};
  }

  auto [count, record_bytes] = std::move(*result);
  std::span<const uint8_t> cursor(record_bytes);

  // Accumulate messages per client while preserving FIFO order.
  std::vector<std::string> client_order;
  std::unordered_map<std::string, std::vector<Message>> by_client;

  for (uint32_t idx = 0U; idx < count; ++idx) {
    ClientMessageRecord rec = decode_record(cursor);
    if (by_client.find(rec.client_id) == by_client.end()) {
      client_order.push_back(rec.client_id);
    }
    by_client[rec.client_id].push_back(std::move(rec.message));
  }

  std::vector<ClientMessages> result_vec;
  result_vec.reserve(client_order.size());
  for (const auto &cid : client_order) {
    result_vec.push_back({.client_id = cid,
                          .messages = std::move(by_client.at(cid))});
  }
  return result_vec;
}

} // namespace mqtt
