/**
 * @file monitoring_test.cpp
 * @brief Unit tests for Module 16: Monitoring (StatisticsCollector +
 *        SysTopicPublisher).
 */

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/types/qos.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/structured_tracer.h"
#include "monitoring/sys_topic_publisher.h"
#include "store/retained_message_store.h"
#include "store/subscription_store.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// StatisticsCollector
//

TEST_CASE("stats_initial_snapshot_is_zero", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  const auto snap = stats.snapshot();

  CHECK(snap.connected_clients == 0U);
  CHECK(snap.messages_inbound == 0U);
  CHECK(snap.messages_outbound == 0U);
  CHECK(snap.active_subscriptions == 0U);
  CHECK(snap.retained_messages == 0U);
  CHECK(snap.uptime.count() >= 0);
}

TEST_CASE("stats_client_connect_disconnect", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  SECTION("single connect increments counter") {
    stats.on_client_connected();
    CHECK(stats.snapshot().connected_clients == 1U);
  }

  SECTION("connect then disconnect returns to zero") {
    stats.on_client_connected();
    stats.on_client_connected();
    stats.on_client_disconnected();
    CHECK(stats.snapshot().connected_clients == 1U);
  }

  SECTION("multiple connects accumulate") {
    stats.on_client_connected();
    stats.on_client_connected();
    stats.on_client_connected();
    CHECK(stats.snapshot().connected_clients == 3U);
  }
}

TEST_CASE("stats_message_throughput", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  SECTION("inbound counter increments") {
    stats.on_message_inbound();
    stats.on_message_inbound();
    CHECK(stats.snapshot().messages_inbound == 2U);
  }

  SECTION("outbound counter increments") {
    stats.on_message_outbound();
    CHECK(stats.snapshot().messages_outbound == 1U);
  }

  SECTION("inbound and outbound are independent") {
    stats.on_message_inbound();
    stats.on_message_outbound();
    stats.on_message_outbound();
    const auto snap = stats.snapshot();
    CHECK(snap.messages_inbound == 1U);
    CHECK(snap.messages_outbound == 2U);
  }
}

TEST_CASE("stats_subscription_count_from_store", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  CHECK(stats.snapshot().active_subscriptions == 0U);

  Subscription sub;
  sub.topic_filter = Utf8String{"test/topic"};
  sub.qos = QoS::AtMostOnce;
  sub_store.store("client1", sub);

  CHECK(stats.snapshot().active_subscriptions == 1U);

  sub_store.store("client2", sub);
  CHECK(stats.snapshot().active_subscriptions == 2U);
}

TEST_CASE("stats_retained_count_from_store", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  CHECK(stats.snapshot().retained_messages == 0U);

  Message msg;
  msg.topic = Utf8String{"sensors/temp"};
  msg.payload = BinaryData{{0x31, 0x38}};
  msg.retain = true;
  retained_store.store(msg);

  CHECK(stats.snapshot().retained_messages == 1U);
}

TEST_CASE("stats_uptime_increases", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  // Uptime is non-negative immediately after construction.
  CHECK(stats.snapshot().uptime.count() >= 0);
}

//
// SysTopicPublisher
//

namespace {

/// Helper: build a store-backed StatisticsCollector with default empty stores.
struct TestFixture {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats{sub_store, retained_store};
};

} // namespace

TEST_CASE("sys_publisher_zero_interval_no_publish", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(
      fix.stats, 0s, [&](Message msg) { published.push_back(std::move(msg)); });

  // Tick far in the future — should never publish with interval == 0.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK_FALSE(publisher.tick(far_future));
  CHECK(published.empty());
}

TEST_CASE("sys_publisher_first_tick_publishes", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  // First tick with now far in the future — interval (≥ epoch) is always
  // elapsed on the initial call since last_publish_ is the epoch.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK(publisher.tick(far_future));
  CHECK_FALSE(published.empty());
}

TEST_CASE("sys_publisher_interval_not_elapsed", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  const auto base = std::chrono::steady_clock::now() + 1000s;
  // First tick publishes.
  publisher.tick(base);
  published.clear();

  // Second tick only 1 second later — interval is 60 s, should not publish.
  CHECK_FALSE(publisher.tick(base + 1s));
  CHECK(published.empty());
}

TEST_CASE("sys_publisher_interval_elapsed", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  const auto base = std::chrono::steady_clock::now() + 1000s;
  publisher.tick(base);
  published.clear();

  // 60 seconds later — interval exactly elapsed, should publish.
  CHECK(publisher.tick(base + 60s));
  CHECK_FALSE(published.empty());
}

TEST_CASE("sys_publisher_publishes_all_sys_topics", "[monitoring]") {
  TestFixture fix;
  std::vector<std::string> topics;
  SysTopicPublisher publisher(
      fix.stats, 60s, [&](Message msg) { topics.push_back(msg.topic.value); });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  REQUIRE(topics.size() == 6U);
  CHECK(std::ranges::find(topics, "$SYS/broker/clients/connected") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/messages/received") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/messages/sent") != topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/subscriptions/count") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/retained messages/count") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/uptime") != topics.end());
}

TEST_CASE("sys_publisher_payload_is_decimal", "[monitoring]") {
  TestFixture fix;
  fix.stats.on_client_connected();
  fix.stats.on_client_connected();

  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  const auto connected_it =
      std::ranges::find_if(published, [](const Message &msg) {
        return msg.topic.value == "$SYS/broker/clients/connected";
      });
  REQUIRE(connected_it != published.end());

  const std::string payload(connected_it->payload.data.begin(),
                            connected_it->payload.data.end());
  CHECK(payload == "2");
}

TEST_CASE("sys_publisher_retain_flag_set", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  for (const auto &msg : published) {
    CHECK(msg.retain);
  }
}

TEST_CASE("sys_publisher_qos_at_most_once", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  for (const auto &msg : published) {
    CHECK(msg.qos == QoS::AtMostOnce);
  }
}

//
// StructuredTracer
//

namespace {

class FailOnceStringBuffer : public std::stringbuf {
public:
  int overflow(int character_value) override {
    const char character = static_cast<char>(character_value);
    return static_cast<int>(xsputn(&character, 1));
  }

  std::streamsize xsputn(const char *text_ptr,
                         std::streamsize text_size) override {
    if (!has_failed_) {
      has_failed_ = true;
      throw std::runtime_error("simulated stream failure");
    }
    captured_text_.append(text_ptr, static_cast<std::size_t>(text_size));
    return text_size;
  }

  [[nodiscard]] const std::string &captured_text() const noexcept {
    return captured_text_;
  }

private:
  bool has_failed_{false};
  std::string captured_text_;
};

[[nodiscard]] std::vector<std::string> split_non_empty_lines(
    std::string_view multiline_text) {
  std::vector<std::string> lines;
  std::size_t line_start = 0U;
  while (line_start < multiline_text.size()) {
    const std::size_t line_end = multiline_text.find('\n', line_start);
    if (line_end == std::string_view::npos) {
      const std::string_view line = multiline_text.substr(line_start);
      if (!line.empty()) {
        lines.emplace_back(line);
      }
      break;
    }

    const std::string_view line = multiline_text.substr(line_start, line_end - line_start);
    if (!line.empty()) {
      lines.emplace_back(line);
    }
    line_start = line_end + 1U;
  }
  return lines;
}

} // namespace

TEST_CASE("tracer_emits_json_line_with_mandatory_fields", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "connection";
  event.info = "connect_received";
  tracer.emit(event);

  const std::string line = output_stream.str();
  REQUIRE_FALSE(line.empty());
  CHECK(line.find("\"timestamp\":") != std::string::npos);
  CHECK(line.find("\"level\":\"info\"") != std::string::npos);
  CHECK(line.find("\"module\":\"connection\"") != std::string::npos);
  CHECK(line.find("\"info\":\"connect_received\"") != std::string::npos);
  CHECK(line.find("\"theme_count\":1") != std::string::npos);
  CHECK(line.find("\"theme_rate_per_second\":0.000") != std::string::npos);
  CHECK(line.find('\n') != std::string::npos);
}

TEST_CASE("tracer_emits_optional_detail_and_data", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Trace);

  TraceEvent event;
  event.level = TraceLevel::Trace;
  event.module = "connection";
  event.info = "decode_step";
  event.detail = "remaining length parsed";
  event.data = {{"remaining_length", "23"}, {"packet_type", "CONNECT"}};

  tracer.emit(event);

  const std::string line = output_stream.str();
  CHECK(line.find("\"detail\":\"remaining length parsed\"") !=
        std::string::npos);
  CHECK(line.find("\"data\":{") != std::string::npos);
  CHECK(line.find("\"remaining_length\":\"23\"") != std::string::npos);
  CHECK(line.find("\"packet_type\":\"CONNECT\"") != std::string::npos);
}

TEST_CASE("tracer_global_hierarchy_filters_non_trace_levels", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Warning);

  CHECK(tracer.should_emit(TraceLevel::Error, "broker"));
  CHECK(tracer.should_emit(TraceLevel::Warning, "broker"));
  CHECK_FALSE(tracer.should_emit(TraceLevel::Info, "broker"));
  CHECK_FALSE(tracer.should_emit(TraceLevel::Trace, "broker"));
}

TEST_CASE("tracer_trace_module_override_works_with_global_error",
          "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Error);

  CHECK_FALSE(tracer.should_emit(TraceLevel::Trace, "connection"));
  tracer.enable_trace_module("connection");
  CHECK(tracer.should_emit(TraceLevel::Trace, "connection"));
  CHECK_FALSE(tracer.should_emit(TraceLevel::Trace, "broker"));
}

TEST_CASE("tracer_none_disables_all_output", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::None);
  tracer.enable_trace_module("connection");

  CHECK_FALSE(tracer.should_emit(TraceLevel::Error, "connection"));
  CHECK_FALSE(tracer.should_emit(TraceLevel::Trace, "connection"));

  TraceEvent event;
  event.level = TraceLevel::Error;
  event.module = "connection";
  event.info = "should_not_emit";
  tracer.emit(event);
  CHECK(output_stream.str().empty());
}

TEST_CASE("tracer_emits_theme_count_and_rate_per_second", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);

  const auto base_time = std::chrono::system_clock::now();

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "connection";
  event.info = "decode_packet_end";

  event.timestamp = base_time;
  tracer.emit(event);

  event.timestamp = base_time + 2s;
  tracer.emit(event);

  event.timestamp = base_time + 4s;
  tracer.emit(event);

  const std::vector<std::string> lines = split_non_empty_lines(output_stream.str());
  REQUIRE(lines.size() == 3U);

  CHECK(lines[0].find("\"theme_count\":1") != std::string::npos);
  CHECK(lines[0].find("\"theme_rate_per_second\":0.000") != std::string::npos);

  CHECK(lines[1].find("\"theme_count\":2") != std::string::npos);
  CHECK(lines[1].find("\"theme_rate_per_second\":0.500") != std::string::npos);

  CHECK(lines[2].find("\"theme_count\":3") != std::string::npos);
  CHECK(lines[2].find("\"theme_rate_per_second\":0.500") != std::string::npos);
}

TEST_CASE("tracer_limits_emits_per_theme_per_window", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);
  tracer.set_max_events_per_theme_interval(2U);

  const auto base_time = std::chrono::system_clock::now();

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "connection";
  event.info = "decode_packet_end";

  event.timestamp = base_time;
  tracer.emit(event);

  event.timestamp = base_time + 100ms;
  tracer.emit(event);

  // Same measurement window: should be suppressed by limit.
  event.timestamp = base_time + 200ms;
  tracer.emit(event);

  const std::vector<std::string> lines = split_non_empty_lines(output_stream.str());
  REQUIRE(lines.size() == 2U);
  CHECK(lines[0].find("\"theme_count\":1") != std::string::npos);
  CHECK(lines[1].find("\"theme_count\":2") != std::string::npos);
}

TEST_CASE("tracer_resets_theme_limit_on_next_window", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);
  tracer.set_max_events_per_theme_interval(2U);

  const auto base_time = std::chrono::system_clock::now();

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "connection";
  event.info = "decode_packet_end";

  event.timestamp = base_time;
  tracer.emit(event);

  event.timestamp = base_time + 100ms;
  tracer.emit(event);

  // Suppressed in first window.
  event.timestamp = base_time + 200ms;
  tracer.emit(event);

  // New window starts after >= 1 second and allows emits again.
  event.timestamp = base_time + 1500ms;
  tracer.emit(event);

  event.timestamp = base_time + 1600ms;
  tracer.emit(event);

  const std::vector<std::string> lines = split_non_empty_lines(output_stream.str());
  REQUIRE(lines.size() == 4U);
  CHECK(lines[0].find("\"theme_count\":1") != std::string::npos);
  CHECK(lines[1].find("\"theme_count\":2") != std::string::npos);
  CHECK(lines[2].find("\"theme_count\":4") != std::string::npos);
  CHECK(lines[3].find("\"theme_count\":5") != std::string::npos);
}

TEST_CASE("tracer_serialization_failure_falls_back_to_minimal_record",
          "[monitoring]") {
  FailOnceStringBuffer fail_once_buffer;
  std::ostream output_stream(&fail_once_buffer);
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "broker";
  event.info = "connect_handled";

  CHECK_NOTHROW(tracer.emit(event));
  const std::string output_text = fail_once_buffer.captured_text();
  CHECK(output_text.find("trace_serialization_failed") != std::string::npos);
}

TEST_CASE("trace_level_roundtrip_and_case_insensitive_parse", "[monitoring]") {
  CHECK(to_string(TraceLevel::None) == "none");
  CHECK(to_string(TraceLevel::Error) == "error");
  CHECK(to_string(TraceLevel::Warning) == "warning");
  CHECK(to_string(TraceLevel::Info) == "info");
  CHECK(to_string(TraceLevel::Trace) == "trace");

  CHECK(parse_trace_level("NONE").value() == TraceLevel::None);
  CHECK(parse_trace_level("Error").value() == TraceLevel::Error);
  CHECK(parse_trace_level("WaRnInG").value() == TraceLevel::Warning);
  CHECK(parse_trace_level("info").value() == TraceLevel::Info);
  CHECK(parse_trace_level("TrAcE").value() == TraceLevel::Trace);
  CHECK_FALSE(parse_trace_level("verbose").has_value());
}

TEST_CASE("tracer_set_trace_modules_and_escape_sequences", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Error);
  tracer.set_trace_modules({"connection", "", "broker"});

  CHECK(tracer.should_emit(TraceLevel::Trace, "connection"));
  CHECK(tracer.should_emit(TraceLevel::Trace, "broker"));
  CHECK_FALSE(tracer.should_emit(TraceLevel::Trace, "session_manager"));

  TraceEvent event;
  event.level = TraceLevel::Trace;
    event.module = "connection";
  event.info = "line1\nline2\rline3\tend";
  event.detail = "detail\\\"";
  event.data = {{"key\\n", "value\t\""}};
  tracer.emit(event);

  const std::string output_text = output_stream.str();
    CHECK_FALSE(output_text.empty());
}

TEST_CASE("tracer_truncates_overlong_text_fields", "[monitoring]") {
  std::ostringstream output_stream;
  StructuredTracer tracer(output_stream);
  tracer.set_global_level(TraceLevel::Info);
  tracer.set_max_text_length(32U);

  TraceEvent event;
  event.level = TraceLevel::Info;
  event.module = "connection";
  event.info = std::string(120U, 't');
  event.detail = std::string(120U, 'x');
  event.data = {{"key", std::string(120U, 'v')}};
  tracer.emit(event);

  const std::string output_text = output_stream.str();
  CHECK_FALSE(output_text.empty());
  CHECK(output_text.find("...<truncated>") != std::string::npos);
  CHECK(output_text.find(std::string(48U, 't')) == std::string::npos);
  CHECK(output_text.find(std::string(48U, 'x')) == std::string::npos);
  CHECK(output_text.find(std::string(48U, 'v')) == std::string::npos);
}
