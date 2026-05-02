#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include "yaha/file_store/file_store.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_filestore_component_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code err{};
    std::filesystem::remove_all(path, err);
}

std::uint16_t reserveFreeLocalPort() {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 19190U;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 19190U;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(sock);
        return 19190U;
    }

    const std::uint16_t port = ntohs(bound.sin_port);
    ::close(sock);
    return port;
}

bool waitForHttpReady(const std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    client.set_connection_timeout(0, 100000);
    client.set_read_timeout(0, 100000);
    for (int attemptIndex = 0; attemptIndex < 50; ++attemptIndex) {
        if (const auto res = client.Options("/")) {
            if (res->status == 200) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

struct StoreCloseGuard {
    yaha::FileStore* store{nullptr};
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

TEST_CASE("encode_key_path_to_filename_is_deterministic", "[file_store]") {
    REQUIRE(yaha::FileStore::encodeKeyPathToFilename("/a") == "4797");
}

TEST_CASE("get_subscriptions_returns_empty_map", "[file_store]") {
    yaha::FileStoreConfig config{};
    config.serverPort = 0U;

    yaha::FileStore store{config};
    REQUIRE(store.getSubscriptions().empty());
}

TEST_CASE("http_post_and_get_roundtrip_text_payload", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;

    yaha::FileStore store{config};
    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto postResponse = client.Post("/file/test", "Hello World", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);

    const auto getResponse = client.Get("/file/test");
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 200);
    REQUIRE(getResponse->body == "\"Hello World\"");
}

TEST_CASE("http_post_and_get_roundtrip_json_payload", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;

    yaha::FileStore store{config};
    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto postResponse = client.Post("/file/json", "{\"name\":\"volker\"}", "application/json");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);

    const auto getResponse = client.Get("/file/json");
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 200);
    REQUIRE(getResponse->body == "{\"name\":\"volker\"}");
}

TEST_CASE("http_post_rejects_key_longer_than_limit", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;

    yaha::FileStore store{config};
    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const std::string longKey(101U, 'x');
    const auto postResponse = client.Post(("/" + longKey).c_str(), "Hello", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 400);
}

TEST_CASE("http_post_emits_monitoring_changed_event", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;

    yaha::FileStore store{config};

    std::mutex eventsMutex{};
    std::vector<yaha::Message> events{};
    store.setPublishCallback([&eventsMutex, &events](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{eventsMutex};
        events.push_back(message.clone());
    });

    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto postResponse = client.Post("/automation/rules", "{\"ok\":true}", "application/json");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);

    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    std::lock_guard<std::mutex> lock{eventsMutex};
    REQUIRE_FALSE(events.empty());
    REQUIRE(events.front().topic() == "$MONITOR/FileStore/changed");
    REQUIRE(std::holds_alternative<std::string>(events.front().value()));
}

TEST_CASE("run_and_close_are_idempotent", "[file_store]") {
    yaha::FileStoreConfig config{};
    config.serverPort = 0U;

    yaha::FileStore store{config};

    store.run();
    store.run();
    REQUIRE(store.isRunning());

    store.close();
    store.close();
    REQUIRE_FALSE(store.isRunning());
}
