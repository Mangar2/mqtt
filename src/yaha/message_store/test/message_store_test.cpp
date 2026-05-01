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

struct FakeClock {
    std::int64_t nowMs{1000};
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
        return 19090U;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 19090U;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(sock);
        return 19090U;
    }

    const std::uint16_t port = ntohs(bound.sin_port);
    ::close(sock);
    return port;
}

bool waitForHttpReady(std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    client.set_connection_timeout(0, 100000);
    client.set_read_timeout(0, 100000);
    for (int i = 0; i < 50; ++i) {
        if (const auto res = client.Get("/store")) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
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
    std::filesystem::path path{};
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
    clock.nowMs += (2 * 86400000);

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
    clock.nowMs += (2 * 86400000);
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
        persistConfig.keepFiles = 5U;

        yaha::MessageTreePersistence persistence{persistConfig};
        REQUIRE(persistence.persistNow(sourceTree));
    }

    std::uint32_t startCount = 0U;
    std::uint32_t stopCount = 0U;

    yaha::MessageStoreConfig config{};
    config.serverPort = 0U;
    config.persistenceConfig.directory = tempDir;
    config.persistenceConfig.filename = "state";
    config.persistenceConfig.intervalMs = 20U;
    config.httpStartCallback = [&startCount]() {
        startCount += 1U;
    };
    config.httpStopCallback = [&stopCount]() {
        stopCount += 1U;
    };

    yaha::MessageStore store{config};
    store.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{70});
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
    store.handleMessage(yaha::Message{"home/temp", 21.5});
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
    REQUIRE(response->body.find("invalid_percent_encoding") != std::string::npos);
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
    store.handleMessage(yaha::Message{"home/temp", 20.0});
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

}
