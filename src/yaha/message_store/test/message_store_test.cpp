#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "yaha/message_store/message_store.h"

namespace {

constexpr std::int64_t k_initial_now_ms{1000};
constexpr std::uint16_t k_fallback_http_port{19090U};
constexpr int k_http_timeout_microseconds{100000};
constexpr int k_http_ready_max_attempts{50};
constexpr int k_http_ready_sleep_ms{10};
constexpr std::int64_t k_millis_per_day{86400000};
constexpr std::uint32_t k_periodic_interval_ms{20U};
constexpr int k_periodic_wait_ms{70};
constexpr double k_room_temperature_celsius{21.5};
constexpr double k_snapshot_temperature_celsius{20.0};
constexpr std::int64_t k_reason_time_fallback_now_ms{999999};

struct FakeClock {
    std::int64_t nowMs{k_initial_now_ms};
};

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_msgstore_component_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code err{};
    std::filesystem::remove_all(path, err);
}

std::size_t countSnapshotFiles(const std::filesystem::path& path) {
    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{path}) {
        if (entry.path().extension().string() == ".mtree") {
            count += 1U;
        }
    }
    return count;
}

std::uint16_t reserveFreeLocalPort() {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return k_fallback_http_port;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return k_fallback_http_port;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(sock);
        return k_fallback_http_port;
    }

    const std::uint16_t port = ntohs(bound.sin_port);
    ::close(sock);
    return port;
}

bool waitForHttpReady(std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
    for (int attempt = 0; attempt < k_http_ready_max_attempts; ++attempt) {
        if (const auto res = client.Get("/store")) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_http_ready_sleep_ms});
    }
    return false;
}

struct StoreCloseGuard {
    yaha::MessageStore* store{nullptr};
    ~StoreCloseGuard() {
        if (store != nullptr) {
            store->close();
        }
    }
};

struct DirectoryCleanupGuard {
    std::filesystem::path path;
    ~DirectoryCleanupGuard() {
        if (!path.empty()) {
            removeDirectoryQuiet(path);
        }
    }
};

} // namespace

TEST_CASE("get_subscriptions_returns_configured_map", "[message_store]") {
    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.subscriptions = {
        {"#", yaha::Qos::AtLeastOnce},
        {"sensor/+/state", yaha::Qos::AtMostOnce}
    };

    yaha::MessageStore store{config};
    const auto subscriptions = store.getSubscriptions();

    REQUIRE(subscriptions.size() == 2U);
    REQUIRE(subscriptions.at("#") == yaha::Qos::AtLeastOnce);
    REQUIRE(subscriptions.at("sensor/+/state") == yaha::Qos::AtMostOnce);
}

TEST_CASE("handle_message_adds_regular_topic_to_tree", "[message_store]") {
    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    yaha::MessageStore store{config};

    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});

    const auto nodes = store.querySection("home/lamp", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "home/lamp");
    REQUIRE(std::get<std::string>(nodes.front().value) == "on");
}

TEST_CASE("handle_message_cleanup_topic_uses_numeric_payload", "[message_store]") {
    FakeClock clock{};

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.treeConfig.nowMillisecondsProvider = [&clock]() {
        return clock.nowMs;
    };

    yaha::MessageStore store{config};

    store.handleMessage(yaha::Message{"old/topic", std::string{"x"}});
    clock.nowMs += (2 * k_millis_per_day);

    store.handleMessage(yaha::Message{"$MONITORING/messages/cleanup", 1.0});

    const auto nodes = store.querySection("", 5U, false, true);
    REQUIRE(nodes.empty());
}

TEST_CASE("handle_message_cleanup_topic_ignores_invalid_payload", "[message_store]") {
    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    yaha::MessageStore store{config};

    store.handleMessage(yaha::Message{"keep/topic", std::string{"x"}});
    store.handleMessage(yaha::Message{"$MONITORING/messages/cleanup", std::string{"abc"}});

    const auto nodes = store.querySection("keep/topic", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
}

TEST_CASE("handle_message_cleanup_topic_accepts_numeric_string_payload", "[message_store]") {
    FakeClock clock{};

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.treeConfig.nowMillisecondsProvider = [&clock]() {
        return clock.nowMs;
    };

    yaha::MessageStore store{config};

    store.handleMessage(yaha::Message{"old/topic", std::string{"x"}});
    clock.nowMs += (2 * k_millis_per_day);
    store.handleMessage(yaha::Message{"$MONITORING/messages/cleanup", std::string{"1"}});

    const auto nodes = store.querySection("", 5U, false, true);
    REQUIRE(nodes.empty());
}

TEST_CASE("run_restore_starts_callbacks_and_periodic_persist", "[message_store]") {
    const auto tempDir = makeTempDirectory();

    {
        yaha::MessageTree sourceTree{};
        sourceTree.addData(yaha::Message{"restore/topic", std::string{"value"}});

        yaha::MessageTreePersistence::Config persistConfig{};
        persistConfig.directory = tempDir;
        persistConfig.filename = "state";
        persistConfig.keepFiles = yaha::MessageTreePersistence::Config::k_default_keep_files;

        yaha::MessageTreePersistence persistence{persistConfig};
        REQUIRE(persistence.persistNow(sourceTree));
    }

    std::uint32_t startCount = 0U;
    std::uint32_t stopCount = 0U;

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";
    config.persistenceConfig.intervalMs = k_periodic_interval_ms;
    config.httpStartCallback = [&startCount]() {
        startCount += 1U;
    };
    config.httpStopCallback = [&stopCount]() {
        stopCount += 1U;
    };

    yaha::MessageStore store{config};
    store.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{k_periodic_wait_ms});
    store.close();

    const auto restored = store.querySection("restore/topic", 0U, false, true);
    REQUIRE(restored.size() == 1U);
    REQUIRE(startCount == 1U);
    REQUIRE(stopCount == 1U);
    REQUIRE(countSnapshotFiles(tempDir) >= 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("close_performs_final_persist_when_periodic_disabled", "[message_store]") {
    const auto tempDir = makeTempDirectory();

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";
    config.persistenceConfig.intervalMs = 0U;

    yaha::MessageStore store{config};
    store.handleMessage(yaha::Message{"persist/topic", std::string{"value"}});

    store.run();
    store.close();

    REQUIRE(countSnapshotFiles(tempDir) >= 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("run_and_close_are_idempotent", "[message_store]") {
    const auto tempDir = makeTempDirectory();

    std::uint32_t startCount = 0U;
    std::uint32_t stopCount = 0U;

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";
    config.persistenceConfig.intervalMs = 0U;
    config.httpStartCallback = [&startCount]() {
        startCount += 1U;
    };
    config.httpStopCallback = [&stopCount]() {
        stopCount += 1U;
    };

    yaha::MessageStore store{config};

    store.run();
    store.run();
    REQUIRE(store.isRunning());

    store.close();
    store.close();
    REQUIRE_FALSE(store.isRunning());

    REQUIRE(startCount == 1U);
    REQUIRE(stopCount == 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("http_get_store_returns_json_for_topic_prefix", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.handleMessage(yaha::Message{"home/temp", k_room_temperature_celsius});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/store/home");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home/lamp\"") != std::string::npos);
    REQUIRE(response->body.find("\"topic\":\"home/temp\"") != std::string::npos);

}

TEST_CASE("http_get_store_decodes_percent_encoded_topic_prefix", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home room/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/store/home%20room");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home room/lamp\"") != std::string::npos);
}

TEST_CASE("http_get_store_rejects_invalid_percent_encoded_topic_prefix", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/store/home%2");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 400);
    REQUIRE(response->body.find("code=YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING")
            != std::string::npos);
}

TEST_CASE("http_get_store_decodes_percent_encoded_hex_bytes", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"homeAroom/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/store/home%41room");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"homeAroom/lamp\"") != std::string::npos);
}

TEST_CASE("http_get_store_applies_levelamount_history_reason_headers", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"off"}});
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"on"}});
    store.handleMessage(yaha::Message{"home/zone1/light/state", std::string{"stable"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    httplib::Headers headers{
        {"levelamount", "2"},
        {"history", "true"},
        {"reason", "false"}
    };
    const auto response = client.Get("/store/home", headers);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home/zone1/light\"") != std::string::npos);
    REQUIRE(response->body.find("\"history\":[") != std::string::npos);
    REQUIRE(response->body.find("\"reason\":[]") != std::string::npos);

}

TEST_CASE("http_get_store_header_defaults_apply_for_blank_or_invalid_values", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/zone/light", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    httplib::Headers headers{
        {"levelamount", "   "},
        {"history", "   "},
        {"reason", "not-a-bool"}
    };
    const auto response = client.Get("/store/home", headers);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
}

TEST_CASE("http_get_store_snapshot_body_uses_diff_mode", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.handleMessage(yaha::Message{"home/temp", k_snapshot_temperature_celsius});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    const std::string snapshotBody =
        R"([{"topic":"home/lamp","value":"off"},{"topic":"home/temp","value":20.0}])";
    httplib::Request request{};
    request.method = "GET";
    request.path = "/store/home";
    request.body = snapshotBody;
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home/lamp\"") != std::string::npos);
    REQUIRE(response->body.find("\"topic\":\"home/temp\"") == std::string::npos);

}

TEST_CASE("http_get_store_snapshot_body_skips_unknown_fields_and_json_variants", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    const std::string snapshotBody =
        R"([{"topic":"home/lamp","value":"off","meta":{"a":1},"arr":[1,2],"flag":true,"nil":null}])";
    httplib::Request request{};
    request.method = "GET";
    request.path = "/store/home";
    request.body = snapshotBody;
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home/lamp\"") != std::string::npos);
}

TEST_CASE("http_get_store_snapshot_body_supports_escaped_strings", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    const std::string snapshotBody =
        R"([{"topic":"home\/lamp","value":"line1\nline2\t\"q\"\\x"}])";
    httplib::Request request{};
    request.method = "GET";
    request.path = "/store/home";
    request.body = snapshotBody;
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
}

TEST_CASE("http_get_store_json_output_escapes_special_characters", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/escaped", std::string{"line1\nline2\r\t\"q\"\\x"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/store/home/escaped");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("line1\\nline2\\r\\t\\\"q\\\"\\\\x") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_get_store_outputs_iso_time_and_reason_timestamps", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    FakeClock clock{};
    clock.nowMs = k_reason_time_fallback_now_ms;

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";
    config.treeConfig.nowMillisecondsProvider = [&clock]() {
        return clock.nowMs;
    };

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};

    yaha::Message first{"device/state", std::string{"off"}};
    first.addReason("initial", "1970-01-01T00:00:00.500Z");
    store.handleMessage(first);

    yaha::Message second{"device/state", std::string{"on"}};
    second.addReason("updated", "1970-01-01T00:00:01.250Z");
    store.handleMessage(second);

    yaha::Message third{"device/state", std::string{"auto"}};
    third.addReason("updated-again", "1970-01-01T00:00:02.000Z");
    store.handleMessage(third);

    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    httplib::Headers headers{{"history", "true"}};
    const auto response = client.Get("/store/device/state", headers);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"time\":\"1970-01-01T00:00:02.000Z\"") != std::string::npos);
    REQUIRE(response->body.find("\"time\":\"1970-01-01T00:00:01.250Z\"") != std::string::npos);
    REQUIRE(response->body.find("\"time\":\"1970-01-01T00:00:00.500Z\"") != std::string::npos);
    REQUIRE(response->body.find("\"timestamp\":\"1970-01-01T00:00:02.000Z\"") != std::string::npos);

    const std::string newestHistoryPrefix = "\"history\":[{\"time\":\"1970-01-01T00:00:01.250Z\"";
    const std::size_t newestPosition = response->body.find(newestHistoryPrefix);
    const std::size_t oldestPosition = response->body.find("\"time\":\"1970-01-01T00:00:00.500Z\"");
    REQUIRE(newestPosition != std::string::npos);
    REQUIRE(oldestPosition != std::string::npos);
    REQUIRE(newestPosition < oldestPosition);

    REQUIRE(response->body.find("\"timeMs\":") == std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_post_store_sensor_payload_uses_topic_and_query_flags", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"off"}});
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"on"}});
    store.handleMessage(yaha::Message{"home/zone1/light/state", std::string{"stable"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    httplib::Request request{};
    request.method = "POST";
    request.path = "/store";
    request.body = R"({"topic":"home","history":"true","reason":"false","levelAmount":2})";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("{\"payload\":[") == 0);
    REQUIRE(response->body.find("\"topic\":\"home/zone1/light\"") != std::string::npos);
    REQUIRE(response->body.find("\"history\":[") != std::string::npos);
    REQUIRE(response->body.find("\"reason\":[]") != std::string::npos);
}

TEST_CASE("http_post_store_sensor_payload_nodes_activates_diff_mode", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.handleMessage(yaha::Message{"home/temp", k_snapshot_temperature_celsius});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    httplib::Request request{};
    request.method = "POST";
    request.path = "/store";
    request.body =
        R"({"topic":"home","nodes":[{"topic":"home/lamp","value":"off"},{"topic":"home/temp","value":20.0}]})";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("{\"payload\":[") == 0);
    REQUIRE(response->body.find("\"topic\":\"home/lamp\"") != std::string::npos);
    REQUIRE(response->body.find("\"topic\":\"home/temp\"") == std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_post_store_sensor_payload_empty_nodes_uses_section_query", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"off"}});
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"on"}});
    store.handleMessage(yaha::Message{"office/zone2/light", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    httplib::Request request{};
    request.method = "POST";
    request.path = "/store";
    request.body =
        R"({"topic":"home","history":"true","reason":"false","levelAmount":2,"nodes":[]})";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("{\"payload\":[") == 0);
    REQUIRE(response->body.find("\"topic\":\"home/zone1/light\"") != std::string::npos);
    REQUIRE(response->body.find("\"topic\":\"office/zone2/light\"") == std::string::npos);
    REQUIRE(response->body.find("\"history\":[") != std::string::npos);
    REQUIRE(response->body.find("\"reason\":[]") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_post_store_sensor_payload_accepts_json_boolean_flags", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"off"}});
    store.handleMessage(yaha::Message{"home/zone1/light", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    httplib::Request request{};
    request.method = "POST";
    request.path = "/store";
    request.body =
        R"({"topic":"home","history":true,"reason":false,"levelamount":2,"nodes":[]})";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("{\"payload\":[") == 0);
    REQUIRE(response->body.find("\"topic\":\"home/zone1/light\"") != std::string::npos);
    REQUIRE(response->body.find("\"history\":[") != std::string::npos);
    REQUIRE(response->body.find("\"reason\":[]") != std::string::npos);
}

TEST_CASE("http_post_store_invalid_json_falls_back_to_section_query", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};

    httplib::Request request{};
    request.method = "POST";
    request.path = "/store/home";
    request.body = "not-json";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body.find("\"topic\":\"home/lamp\"") != std::string::npos);
}

TEST_CASE("http_get_store_malformed_snapshot_returns_empty_array", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.handleMessage(yaha::Message{"home/lamp", std::string{"on"}});
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    httplib::Request request{};
    request.method = "GET";
    request.path = "/store/home";
    request.body = "not-json";
    request.set_header("Content-Type", "application/json");
    const auto response = client.send(request);

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    REQUIRE(response->body == "[]");

}

TEST_CASE("http_unknown_path_returns_404", "[message_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::MessageStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";

    yaha::MessageStore store{config};
    StoreCloseGuard guard{&store};
    store.run();

    REQUIRE(waitForHttpReady(config.serverPort));
    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto response = client.Get("/unknown/path");

    REQUIRE(response != nullptr);
    REQUIRE(response->status == 404);
    REQUIRE(response->body.find("code=YAHA_MESSAGE_STORE_HTTP_NOT_FOUND") != std::string::npos);

}
