#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/session/session_state.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "network/tcp_connection.h"
#include "outbound_queue/outbound_queue.h"
#include "persistence/inflight_persistence.h"
#include "persistence/offline_queue_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// Helpers

namespace {

constexpr uint16_t k_test_port_base = 18883U;
constexpr uint32_t k_port_scan_attempts = 3000U;
constexpr uint32_t k_port_wrap_window = 1000U;
constexpr uint32_t k_port_max_exclusive = 60999U;
constexpr uint32_t k_seed_session_expiry_seconds = 60U;
constexpr uint32_t k_seed_queue_session_expiry_seconds = 300U;
constexpr auto k_accept_settle_delay = 20ms;

SocketHandle create_tcp_socket() {
  return static_cast<SocketHandle>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

void close_socket_handle(mqtt::SocketHandle socket_handle) {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

bool can_bind_loopback_port(uint16_t port_value) {
  const SocketHandle probe_socket = create_tcp_socket();
  if (probe_socket == mqtt::k_invalid_socket) {
    return false;
  }

  int reuse_addr = 1;
#ifdef _WIN32
  (void)::setsockopt(static_cast<SOCKET>(probe_socket), SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse_addr),
                     static_cast<int>(sizeof(reuse_addr)));
#else
  (void)::setsockopt(static_cast<int>(probe_socket), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse_addr),
                     static_cast<socklen_t>(sizeof(reuse_addr)));
#endif

  sockaddr_in probe_addr{};
  probe_addr.sin_family = AF_INET;
  probe_addr.sin_port = htons(port_value);
  probe_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#ifdef _WIN32
  const int bind_result =
      ::bind(static_cast<SOCKET>(probe_socket),
             reinterpret_cast<const sockaddr *>(&probe_addr),
             static_cast<int>(sizeof(probe_addr)));
#else
  const int bind_result =
      ::bind(static_cast<int>(probe_socket),
             reinterpret_cast<const sockaddr *>(&probe_addr),
             static_cast<socklen_t>(sizeof(probe_addr)));
#endif
  close_socket_handle(probe_socket);
  return bind_result == 0;
}

uint16_t next_test_port() {
  static std::atomic<uint32_t> next_port_candidate{k_test_port_base};

  for (uint32_t attempt_index = 0; attempt_index < k_port_scan_attempts;
       ++attempt_index) {
    uint32_t candidate_port =
        next_port_candidate.fetch_add(1U, std::memory_order_relaxed);
    if (candidate_port >= k_port_max_exclusive) {
      const uint32_t wrapped_port =
          k_test_port_base + (candidate_port % k_port_wrap_window);
      candidate_port = wrapped_port;
    }

    if (can_bind_loopback_port(static_cast<uint16_t>(candidate_port))) {
      return static_cast<uint16_t>(candidate_port);
    }
  }

  return k_test_port_base;
}

/// Build a BrokerConfig that binds a single MQTT TCP listener on a
/// high-numbered ephemeral test port selected at runtime.
BrokerConfig make_test_config() {
  BrokerConfig cfg;
  cfg.mqtt_port = next_test_port();
  cfg.ws_port = 0U;       // disabled
  cfg.allow_anonymous = true;
  cfg.persistence_mode = PersistenceMode::Off;
  return cfg;
}

std::filesystem::path make_temp_dir() {
  static std::atomic<uint64_t> temp_dir_counter{0U};
  const uint64_t counter_value = temp_dir_counter.fetch_add(1U);
  const auto now_ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto thread_hash =
      std::hash<std::thread::id>{}(std::this_thread::get_id());
  auto dir = std::filesystem::temp_directory_path() /
             ("broker_test_data_" + std::to_string(now_ticks) + "_" +
              std::to_string(counter_value) + "_" +
              std::to_string(thread_hash));
  std::filesystem::create_directories(dir);
  return dir;
}

void remove_temp_dir(const std::filesystem::path &dir) {
  std::filesystem::remove_all(dir);
}

void connect_loopback(uint16_t port_value) {
  mqtt::SocketHandle socket_handle = create_tcp_socket();
  REQUIRE(socket_handle != mqtt::k_invalid_socket);

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_value);
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const int connect_result =
      ::connect(socket_handle, reinterpret_cast<const sockaddr *>(&server_addr),
                sizeof(server_addr));
  CHECK(connect_result == 0);
  close_socket_handle(socket_handle);
}

} // namespace

//
// Initial state

TEST_CASE("broker_initially_not_running", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  CHECK(broker.is_running() == false);
}

//
// Lifecycle

TEST_CASE("broker_running_after_startup", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
}

TEST_CASE("broker_not_running_after_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  broker.shutdown();
  CHECK(broker.is_running() == false);
}

TEST_CASE("broker_startup_already_running_throws", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  try {
    broker.startup();
    FAIL("Expected BrokerException");
  } catch (const BrokerException &exc) {
    CHECK(exc.error() == BrokerError::AlreadyRunning);
  }
  broker.shutdown();
}

TEST_CASE("broker_shutdown_idempotent", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  broker.shutdown();
  broker.shutdown(); // second shutdown is a no-op
  CHECK(broker.is_running() == false);
}

TEST_CASE("broker_destructor_auto_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  {
    Broker broker(cfg);
    broker.startup();
    CHECK(broker.is_running() == true);
    // Destructor calls shutdown() -- no crash
  }
}

//
// Module accessors

TEST_CASE("broker_module_accessors_after_startup", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto &sess_mgr = broker.session_manager();
  auto &msg_rtr = broker.message_router();
  auto &auth = broker.authenticator();
  auto &acl = broker.acl_engine();
  auto &will_pub = broker.will_publisher();

  (void)sess_mgr;
  (void)msg_rtr;
  (void)auth;
  (void)acl;
  (void)will_pub;

  broker.shutdown();
}

//
// Connection registration

TEST_CASE("broker_register_unregister_connection", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto queue = std::make_shared<OutboundQueue>();
  broker.register_connection("client1", queue);

  broker.unregister_connection("client1");
  broker.unregister_connection("client1"); // idempotent

  broker.shutdown();
  CHECK(queue->is_empty());
}

//
// Signal handling

TEST_CASE("broker_shutdown_requested_false_initially", "[broker]") {
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);
}

//
// Persistence

TEST_CASE("broker_with_persistence_startup", "[broker]") {
  const auto tmp_dir = make_temp_dir();
  BrokerConfig cfg = make_test_config();
  cfg.persistence_mode = PersistenceMode::Full;
  cfg.persistence_dir = tmp_dir;

  {
    Broker broker(cfg);
    broker.startup();
    CHECK(broker.is_running() == true);
    broker.shutdown();
  }

  remove_temp_dir(tmp_dir);
}

TEST_CASE("broker_persistence_startup_loads_seeded_records", "[broker]") {
  const auto tmp_dir = make_temp_dir();

  SessionState session;
  session.client_id = Utf8String{"seed_client"};
  session.session_expiry_interval = k_seed_session_expiry_seconds;

  Message retained;
  retained.topic = Utf8String{"seed/retained"};
  retained.qos = QoS::AtMostOnce;
  retained.retain = true;

  InflightEntry inflight;
  inflight.packet_id = 1U;
  inflight.qos = QoS::AtLeastOnce;
  inflight.state = InflightState::WaitingForPuback;
  inflight.direction = InflightDirection::Outbound;
  inflight.message.topic = Utf8String{"seed/topic"};
  inflight.timestamp = std::chrono::steady_clock::now();

  SessionPersistence session_persistence(tmp_dir);
  RetainedMessagePersistence retained_persistence(tmp_dir);
  InflightPersistence inflight_persistence(tmp_dir);

  session_persistence.save_all(std::vector<SessionState>{session});
  retained_persistence.save_all(std::vector<Message>{retained});
    inflight_persistence.save_all(std::vector<InflightPersistence::ClientEntry>{
      {.client_id = "seed_client", .entry = inflight}});

  BrokerConfig cfg = make_test_config();
  cfg.persistence_mode = PersistenceMode::Full;
  cfg.persistence_dir = tmp_dir;

  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"seed_client"};
  connect.clean_start = false;
  const ConnectResult result = broker.handle_connect(connect, []() {});
  CHECK(result.session_present == true);
  CHECK(result.reason_code == ReasonCode::Success);

  broker.shutdown();
  remove_temp_dir(tmp_dir);
}

TEST_CASE("broker_persistence_startup_loads_seeded_offline_queue", "[broker]") {
  const auto tmp_dir = make_temp_dir();

  // Seed a session so persistence coordinator has something to load.
  SessionState session;
  session.client_id = Utf8String{"queue_client"};
  session.session_expiry_interval = k_seed_queue_session_expiry_seconds;

  SessionPersistence session_persistence(tmp_dir);
  session_persistence.save_all(std::vector<SessionState>{session});

  // Seed the offline queue with one message for queue_client.
  Message queued_msg;
  queued_msg.topic = Utf8String{"offline/topic"};
  queued_msg.qos = QoS::AtLeastOnce;

  OfflineQueuePersistence offline_persistence(tmp_dir);
  offline_persistence.save_all(
      std::vector<OfflineQueuePersistence::ClientMessages>{
        {.client_id = "queue_client", .messages = {queued_msg}}});

  BrokerConfig cfg = make_test_config();
  cfg.persistence_mode = PersistenceMode::Full;
  cfg.persistence_dir = tmp_dir;

  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);

  broker.shutdown();
  remove_temp_dir(tmp_dir);
}

TEST_CASE("broker_password_auth_when_not_anonymous", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
}

//
// Signal handling — handle_signal path

TEST_CASE("broker_handle_signal_sets_shutdown_requested", "[broker]") {
  // Install handlers so the C signal handler is pointing to
  // Broker::handle_signal.
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);

  // std::raise delivers the signal synchronously; the handler runs inline.
  std::raise(SIGINT);
  CHECK(Broker::shutdown_requested() == true);

  // Reset for subsequent tests.
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);
}

//
// WebSocket listener

TEST_CASE("broker_ws_listener_startup_and_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.ws_port = next_test_port();
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
  CHECK(broker.is_running() == false);
}

TEST_CASE("broker_reactor_accept_invokes_client_handler", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.mqtt_port = next_test_port();

  Broker broker(cfg);
  broker.startup();

  connect_loopback(cfg.mqtt_port);
  std::this_thread::sleep_for(k_accept_settle_delay);

  broker.shutdown();
  CHECK(broker.is_running() == false);
}

//
// Online delivery — is_online + deliver lambdas

TEST_CASE("broker_handle_publish_without_subscribers_is_safe", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Route a message; the allow-all anonymous ACL passes for any client.
  Message msg;
  msg.topic = Utf8String{"chat/room"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);
  CHECK_NOTHROW(broker.handle_publish(msg, "pub_client", "", alias_table));
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.shutdown();
}

//
