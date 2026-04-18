#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/session/session_state.h"
#include "data_model/subscription/retain_handling.h"
#include "data_model/subscription/subscription.h"
#include "data_model/subscription/subscription_options.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/variable_byte_integer.h"
#include "persistence/crash_safe_file.h"
#include "persistence/inflight_persistence.h"
#include "persistence/persistence_error.h"
#include "persistence/record_codec.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"

namespace {

// Helper: create a unique temp directory per test to avoid collisions.
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("mqtt_persist_test_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code err_code;
    std::filesystem::remove_all(path, err_code);
  }
};

mqtt::Utf8String utf8(std::string val) {
  return mqtt::Utf8String{std::move(val)};
}

mqtt::BinaryData binary(std::vector<uint8_t> val) {
  return mqtt::BinaryData{std::move(val)};
}

mqtt::Message make_message(std::string topic, std::vector<uint8_t> payload,
                           mqtt::QoS qos = mqtt::QoS::AtLeastOnce,
                           bool retain = true) {
  mqtt::Message msg;
  msg.topic = utf8(std::move(topic));
  msg.payload = binary(std::move(payload));
  msg.qos = qos;
  msg.retain = retain;
  return msg;
}

mqtt::SessionState make_session(std::string client_id,
                                uint32_t expiry = 3600U) {
  mqtt::SessionState ses;
  ses.client_id = utf8(std::move(client_id));
  ses.session_expiry_interval = expiry;
  return ses;
}

} // namespace

//  CRC-32
//

TEST_CASE("crc32_empty_input", "[persistence]") {
  std::vector<uint8_t> empty;
  CHECK(mqtt::CrashSafeFile::crc32(empty) == 0x00000000U);
}

TEST_CASE("crc32_known_value", "[persistence]") {
  // IEEE 802.3 / ISO-HDLC test vector: "123456789" → 0xCBF43926
  std::vector<uint8_t> data = {0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                               0x36U, 0x37U, 0x38U, 0x39U};
  CHECK(mqtt::CrashSafeFile::crc32(data) == 0xCBF43926U);
}

//  CrashSafeFile
//

TEST_CASE("crash_safe_write_then_read", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  std::vector<uint8_t> payload = {0x01U, 0x02U, 0x03U};
  csf.write(payload, 3U);

  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->first == 3U);
  CHECK(result->second == payload);
}

TEST_CASE("crash_safe_corrupted_dat_falls_back", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  std::vector<uint8_t> first_payload = {0xAAU, 0xBBU};
  std::vector<uint8_t> second_payload = {0xCCU, 0xDDU};

  csf.write(first_payload, 1U);  // → dat
  csf.write(second_payload, 1U); // → bak=first, dat=second

  // Corrupt .dat by flipping a byte in the middle.
  auto dat_path = tmp.path / "test.dat";
  {
    std::fstream fout(dat_path,
                      std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(fout);
    fout.seekp(5, std::ios::beg); // skip magic+version+count header
    char byte_val = '\0';
    fout.read(&byte_val, 1);
    fout.seekp(5, std::ios::beg);
    byte_val = static_cast<char>(~static_cast<uint8_t>(byte_val));
    fout.write(&byte_val, 1);
  }

  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->first == 1U);
  CHECK(result->second == first_payload);
}

TEST_CASE("crash_safe_no_files_returns_nullopt", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "nofile");
  CHECK(!csf.read_latest().has_value());
}

TEST_CASE("crash_safe_tmp_used_when_only_file", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  std::vector<uint8_t> payload = {0x42U};
  csf.write(payload, 1U);

  // Rename .dat → .tmp to simulate a crash after writing tmp but before rename.
  std::filesystem::rename(tmp.path / "test.dat", tmp.path / "test.tmp");

  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->second == payload);
}

TEST_CASE("crash_safe_remove_all_deletes_files", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  csf.write({0x01U}, 1U);
  csf.write({0x02U}, 1U); // creates .bak too
  csf.remove_all();

  CHECK(!std::filesystem::exists(tmp.path / "test.dat"));
  CHECK(!std::filesystem::exists(tmp.path / "test.bak"));
  CHECK(!std::filesystem::exists(tmp.path / "test.tmp"));
}

TEST_CASE("crash_safe_overwrite_sequence", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "seq");

  for (uint8_t idx = 1U; idx <= 3U; ++idx) {
    csf.write({idx}, 1U);
    auto result = csf.read_latest();
    REQUIRE(result.has_value());
    CHECK(result->second == std::vector<uint8_t>{idx});
  }
}

TEST_CASE("crash_safe_directory_dat_falls_back_to_bak", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  std::vector<uint8_t> payload = {0x10U, 0x20U, 0x30U};
  csf.write(payload, 1U);

  std::filesystem::rename(tmp.path / "test.dat", tmp.path / "test.bak");
  std::filesystem::create_directory(tmp.path / "test.dat");

  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->first == 1U);
  CHECK(result->second == payload);
}

TEST_CASE("crash_safe_remove_all_throws_on_nonempty_directory", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "test");

  auto dat_dir = tmp.path / "test.dat";
  std::filesystem::create_directory(dat_dir);
  {
    std::ofstream marker(dat_dir / "keep.txt", std::ios::binary);
    REQUIRE(marker);
    marker << "x";
  }

  CHECK_THROWS_AS(csf.remove_all(), mqtt::PersistenceException);
}

//  SessionPersistence
//

TEST_CASE("session_persistence_empty_on_no_file", "[persistence]") {
  TempDir tmp;
  mqtt::SessionPersistence pers(tmp.path);
  CHECK(pers.load_all().empty());
}

TEST_CASE("session_persistence_round_trip", "[persistence]") {
  TempDir tmp;
  mqtt::SessionPersistence pers(tmp.path);

  mqtt::SessionState ses = make_session("client-1", 120U);
  mqtt::Subscription sub;
  sub.topic_filter = utf8("sensors/+/temp");
  sub.qos = mqtt::QoS::AtLeastOnce;
  sub.options.no_local = true;
  sub.options.retain_as_published = false;
  sub.options.retain_handling = mqtt::RetainHandling::SendIfNew;
  ses.subscriptions.push_back(sub);

  pers.save_all({ses});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0] == ses);
}

TEST_CASE("session_persistence_multiple_sessions", "[persistence]") {
  TempDir tmp;
  mqtt::SessionPersistence pers(tmp.path);

  std::vector<mqtt::SessionState> sessions = {
      make_session("alpha", 0U),
      make_session("beta", 60U),
      make_session("gamma", 0xFFFFFFFFU),
  };

  pers.save_all(sessions);
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == sessions.size());
  for (std::size_t idx = 0U; idx < sessions.size(); ++idx) {
    CHECK(loaded[idx] == sessions[idx]);
  }
}

TEST_CASE("session_subscription_with_identifier", "[persistence]") {
  TempDir tmp;
  mqtt::SessionPersistence pers(tmp.path);

  mqtt::SessionState ses = make_session("sub-id-client");
  mqtt::Subscription sub;
  sub.topic_filter = utf8("home/#");
  sub.identifier = 42U;
  ses.subscriptions.push_back(sub);

  pers.save_all({ses});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  REQUIRE(loaded[0].subscriptions.size() == 1U);
  CHECK(loaded[0].subscriptions[0].identifier == 42U);
}

//  RetainedMessagePersistence
//

TEST_CASE("retained_persistence_empty_on_no_file", "[persistence]") {
  TempDir tmp;
  mqtt::RetainedMessagePersistence pers(tmp.path);
  CHECK(pers.load_all().empty());
}

TEST_CASE("retained_persistence_round_trip", "[persistence]") {
  TempDir tmp;
  mqtt::RetainedMessagePersistence pers(tmp.path);

  mqtt::Message msg = make_message("sensors/temp", {0x12U, 0x34U});
  mqtt::Property prop;
  prop.id = mqtt::PropertyId::ContentType;
  prop.value = mqtt::PropertyValue{mqtt::Utf8String{"application/json"}};
  msg.properties.push_back(prop);

  pers.save_all({msg});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0] == msg);
}

TEST_CASE("retained_persistence_empty_payload", "[persistence]") {
  TempDir tmp;
  mqtt::RetainedMessagePersistence pers(tmp.path);

  mqtt::Message msg = make_message("control/reset", {});
  pers.save_all({msg});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].payload.data.empty());
}

TEST_CASE("retained_persistence_all_property_types", "[persistence]") {
  TempDir tmp;
  mqtt::RetainedMessagePersistence pers(tmp.path);

  mqtt::Message msg = make_message("all/props", {0xFFU});

  // variant index 0: uint8_t
  msg.properties.push_back({mqtt::PropertyId::PayloadFormatIndicator,
                            mqtt::PropertyValue{uint8_t{1U}}});
  // variant index 1: TwoByteInteger
  msg.properties.push_back({mqtt::PropertyId::ReceiveMaximum,
                            mqtt::PropertyValue{mqtt::TwoByteInteger{100U}}});
  // variant index 2: FourByteInteger
  msg.properties.push_back({mqtt::PropertyId::MessageExpiryInterval,
                            mqtt::PropertyValue{mqtt::FourByteInteger{3600U}}});
  // variant index 3: VariableByteInteger
  msg.properties.push_back(
      {mqtt::PropertyId::SubscriptionIdentifier,
       mqtt::PropertyValue{mqtt::VariableByteInteger{42U}}});
  // variant index 4: Utf8String
  msg.properties.push_back(
      {mqtt::PropertyId::ContentType,
       mqtt::PropertyValue{mqtt::Utf8String{"text/plain"}}});
  // variant index 5: Utf8StringPair
  msg.properties.push_back(
      {mqtt::PropertyId::UserProperty,
       mqtt::PropertyValue{mqtt::Utf8StringPair{utf8("key"), utf8("val")}}});
  // variant index 6: BinaryData
  msg.properties.push_back({mqtt::PropertyId::CorrelationData,
                            mqtt::PropertyValue{binary({0xABU, 0xCDU})}});

  pers.save_all({msg});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0] == msg);
}

//  InflightPersistence
//

TEST_CASE("inflight_persistence_empty_on_no_file", "[persistence]") {
  TempDir tmp;
  mqtt::InflightPersistence pers(tmp.path);
  CHECK(pers.load_all().empty());
}

TEST_CASE("inflight_persistence_round_trip", "[persistence]") {
  TempDir tmp;
  mqtt::InflightPersistence pers(tmp.path);

  mqtt::InflightPersistence::ClientEntry rec;
  rec.client_id = "client-qos2";
  rec.entry.packet_id = 7U;
  rec.entry.message = make_message("data/value", {0x01U, 0x02U});
  rec.entry.qos = mqtt::QoS::ExactlyOnce;
  rec.entry.state = mqtt::InflightState::WaitingForPubcomp;
  rec.entry.direction = mqtt::InflightDirection::Outbound;
  rec.entry.timestamp = std::chrono::steady_clock::now();

  pers.save_all({rec});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].client_id == rec.client_id);
  CHECK(loaded[0].entry.packet_id == rec.entry.packet_id);
  CHECK(loaded[0].entry.message == rec.entry.message);
}

TEST_CASE("inflight_persistence_round_trip_fields", "[persistence]") {
  TempDir tmp;
  mqtt::InflightPersistence pers(tmp.path);

  mqtt::InflightPersistence::ClientEntry rec;
  rec.client_id = "client-qos2-b";
  rec.entry.packet_id = 9U;
  rec.entry.message = make_message("data/b", {0x05U});
  rec.entry.qos = mqtt::QoS::ExactlyOnce;
  rec.entry.state = mqtt::InflightState::WaitingForPubcomp;
  rec.entry.direction = mqtt::InflightDirection::Outbound;

  pers.save_all({rec});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].entry.qos == rec.entry.qos);
  CHECK(loaded[0].entry.state == rec.entry.state);
  CHECK(loaded[0].entry.direction == rec.entry.direction);
}

TEST_CASE("inflight_persistence_timestamp_reset", "[persistence]") {
  TempDir tmp;
  mqtt::InflightPersistence pers(tmp.path);

  mqtt::InflightPersistence::ClientEntry rec;
  rec.client_id = "ts-client";
  rec.entry.packet_id = 1U;
  rec.entry.message = make_message("t", {});
  rec.entry.timestamp = std::chrono::steady_clock::time_point{}; // epoch

  auto before = std::chrono::steady_clock::now();
  pers.save_all({rec});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].entry.timestamp >= before);
}

TEST_CASE("inflight_persistence_all_property_types", "[persistence]") {
  // Ensures every branch of encode/decode_property_value_inf is covered.
  TempDir tmp;
  mqtt::InflightPersistence pers(tmp.path);

  mqtt::Message msg = make_message("inflight/props", {0x99U});
  msg.properties.push_back({mqtt::PropertyId::PayloadFormatIndicator,
                            mqtt::PropertyValue{uint8_t{1U}}});
  msg.properties.push_back({mqtt::PropertyId::ReceiveMaximum,
                            mqtt::PropertyValue{mqtt::TwoByteInteger{200U}}});
  msg.properties.push_back({mqtt::PropertyId::MessageExpiryInterval,
                            mqtt::PropertyValue{mqtt::FourByteInteger{7200U}}});
  msg.properties.push_back(
      {mqtt::PropertyId::SubscriptionIdentifier,
       mqtt::PropertyValue{mqtt::VariableByteInteger{99U}}});
  msg.properties.push_back(
      {mqtt::PropertyId::ContentType,
       mqtt::PropertyValue{mqtt::Utf8String{"application/cbor"}}});
  msg.properties.push_back(
      {mqtt::PropertyId::UserProperty,
       mqtt::PropertyValue{mqtt::Utf8StringPair{utf8("k"), utf8("v")}}});
  msg.properties.push_back({mqtt::PropertyId::CorrelationData,
                            mqtt::PropertyValue{binary({0x11U, 0x22U})}});

  mqtt::InflightPersistence::ClientEntry rec;
  rec.client_id = "props-client";
  rec.entry.packet_id = 3U;
  rec.entry.message = msg;
  rec.entry.qos = mqtt::QoS::AtLeastOnce;
  rec.entry.state = mqtt::InflightState::WaitingForPuback;
  rec.entry.direction = mqtt::InflightDirection::Inbound;

  pers.save_all({rec});
  auto loaded = pers.load_all();

  REQUIRE(loaded.size() == 1U);
  CHECK(loaded[0].entry.message == msg);
}

//  CrashSafeFile edge cases
//

namespace {

// Build a minimal 0-record persistence file payload (header + CRC).
std::vector<uint8_t> make_raw_file(uint8_t magic0, uint8_t version) {
  std::vector<uint8_t> buf;
  buf.push_back(magic0);
  buf.push_back('Q');
  buf.push_back('T');
  buf.push_back('T');
  buf.push_back(version);
  // count = 0 (4 bytes LE)
  buf.push_back(0U);
  buf.push_back(0U);
  buf.push_back(0U);
  buf.push_back(0U);
  // Append CRC-32 of these 9 bytes.
  uint32_t crc_val = mqtt::CrashSafeFile::crc32(buf);
  buf.push_back(static_cast<uint8_t>(crc_val & 0xFFU));
  buf.push_back(static_cast<uint8_t>((crc_val >> 8U) & 0xFFU));
  buf.push_back(static_cast<uint8_t>((crc_val >> 16U) & 0xFFU));
  buf.push_back(static_cast<uint8_t>((crc_val >> 24U) & 0xFFU));
  return buf;
}

void write_raw(const std::filesystem::path &path,
               const std::vector<uint8_t> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("crash_safe_too_small_file_ignored", "[persistence]") {
  TempDir tmp;
  // Write a 5-byte file — below the minimum 13-byte threshold.
  write_raw(tmp.path / "skip.dat", {0x01U, 0x02U, 0x03U, 0x04U, 0x05U});
  mqtt::CrashSafeFile csf(tmp.path, "skip");
  CHECK(!csf.read_latest().has_value());
}

TEST_CASE("crash_safe_wrong_magic_ignored", "[persistence]") {
  TempDir tmp;
  // 0xFF in first magic byte — CRC is valid but magic check must reject it.
  auto raw = make_raw_file(0xFFU, mqtt::CrashSafeFile::k_format_version);
  write_raw(tmp.path / "bad.dat", raw);
  mqtt::CrashSafeFile csf(tmp.path, "bad");
  CHECK(!csf.read_latest().has_value());
}

TEST_CASE("crash_safe_wrong_version_ignored", "[persistence]") {
  TempDir tmp;
  // Correct magic, unsupported version 0x02 — must be rejected.
  auto raw = make_raw_file('M', 0x02U);
  write_raw(tmp.path / "ver.dat", raw);
  mqtt::CrashSafeFile csf(tmp.path, "ver");
  CHECK(!csf.read_latest().has_value());
}

TEST_CASE("crash_safe_directory_as_dat_falls_back_to_bak", "[persistence]") {
  // When .dat exists as a directory, ifstream fails to open it;
  // read_latest should skip it and use .bak instead (if valid).
  TempDir tmp;
  mqtt::CrashSafeFile csf_bak(tmp.path, "dirtest");
  std::vector<uint8_t> payload = {0xEEU};
  csf_bak.write(payload, 1U); // creates .dat
  // Rename .dat → .bak, then create a directory as .dat.
  std::filesystem::rename(tmp.path / "dirtest.dat", tmp.path / "dirtest.bak");
  std::filesystem::create_directory(tmp.path / "dirtest.dat");

  mqtt::CrashSafeFile csf(tmp.path, "dirtest");
  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->second == payload);
}

//  PersistenceException
//

TEST_CASE("persistence_exception_carries_error_code", "[persistence]") {
  try {
    throw mqtt::PersistenceException(mqtt::PersistenceError::ChecksumMismatch,
                                     "checksum failed");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::ChecksumMismatch);
    CHECK(std::string(exc.what()) == "checksum failed");
  }
}

//  record_codec throw paths
//

TEST_CASE("record_codec_read_u8_throws_on_empty", "[persistence]") {
  std::span<const uint8_t> empty;
  try {
    (void)mqtt::record_codec::read_u8(empty);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("record_codec_read_u16_throws_on_short", "[persistence]") {
  std::vector<uint8_t> one_byte = {0x01U};
  std::span<const uint8_t> span{one_byte};
  try {
    (void)mqtt::record_codec::read_u16(span);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("record_codec_read_u32_throws_on_short", "[persistence]") {
  std::vector<uint8_t> three_bytes = {0x01U, 0x02U, 0x03U};
  std::span<const uint8_t> span{three_bytes};
  try {
    (void)mqtt::record_codec::read_u32(span);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("record_codec_read_utf8_throws_on_short_payload", "[persistence]") {
  // length field says 10 bytes but only 2 bytes follow.
  std::vector<uint8_t> buf = {0x0AU, 0x00U, 0xAAU, 0xBBU};
  std::span<const uint8_t> span{buf};
  try {
    (void)mqtt::record_codec::read_utf8(span);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("record_codec_read_binary_throws_on_short_payload", "[persistence]") {
  // length field says 10 bytes but nothing follows.
  std::vector<uint8_t> buf = {0x0AU, 0x00U, 0x00U, 0x00U};
  std::span<const uint8_t> span{buf};
  try {
    (void)mqtt::record_codec::read_binary(span);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("record_codec_write_string_throws_on_too_long", "[persistence]") {
  std::vector<uint8_t> buf;
  std::string long_str(70000U, 'x'); // exceeds 65535 limit
  try {
    mqtt::record_codec::write_string(buf, long_str);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::CorruptRecord);
  }
}

TEST_CASE("crash_safe_write_fails_on_nonexistent_dir", "[persistence]") {
  // Writing to a non-existent directory triggers WriteFailure (lines 86-89).
  std::filesystem::path bad_path =
      std::filesystem::temp_directory_path() / "mqtt_noexist_9x8y7z" / "sub";
  mqtt::CrashSafeFile csf(bad_path, "xfile");
  try {
    csf.write({0x01U}, 1U);
    FAIL("Expected PersistenceException");
  } catch (const mqtt::PersistenceException &exc) {
    CHECK(exc.error() == mqtt::PersistenceError::WriteFailure);
  }
}

TEST_CASE("crash_safe_write_two_then_read_latest", "[persistence]") {
  // Second write must rename .dat → .bak before promoting .tmp → .dat.
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "multi");

  csf.write({0xAAU}, 1U);
  csf.write({0xBBU}, 1U);

  auto result = csf.read_latest();
  REQUIRE(result.has_value());
  CHECK(result->second == std::vector<uint8_t>{0xBBU});
  CHECK(std::filesystem::exists(tmp.path / "multi.bak"));
}

#ifdef _WIN32
// On Windows, std::ifstream opens without FILE_SHARE_DELETE so DeleteFile
// fails with ERROR_SHARING_VIOLATION while the handle is held open.
TEST_CASE("crash_safe_remove_all_throws_when_file_locked", "[persistence]") {
  TempDir tmp;
  mqtt::CrashSafeFile csf(tmp.path, "lock_del");
  csf.write({0x42U}, 1U);

  // Keep .dat open – on Windows this prevents deletion.
  auto dat_path = tmp.path / "lock_del.dat";
  std::ifstream lock(dat_path, std::ios::binary);
  REQUIRE(lock.is_open());

  CHECK_THROWS_AS(csf.remove_all(), mqtt::PersistenceException);

  lock.close(); // release the lock so TempDir can clean up
}
#endif
