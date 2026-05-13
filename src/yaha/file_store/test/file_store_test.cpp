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
#include <unordered_map>
#include <vector>

// NOLINTBEGIN

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

bool waitForTopicAtLeast(std::mutex& eventsMutex,
                         const std::vector<yaha::Message>& events,
                         const std::string& topicName,
                         const std::size_t minimumCount) {
    for (int attemptIndex = 0; attemptIndex < 200; ++attemptIndex) {
        std::size_t count = 0U;
        {
            std::lock_guard<std::mutex> lock{eventsMutex};
            for (const auto& eventValue : events) {
                if (eventValue.topic() == topicName) {
                    count += 1U;
                }
            }
        }
        if (count >= minimumCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

bool waitForAnyTopicAtLeast(std::mutex& eventsMutex,
                            const std::vector<yaha::Message>& events,
                            const std::vector<std::string>& topicNames,
                            const std::size_t minimumCount) {
    for (int attemptIndex = 0; attemptIndex < 200; ++attemptIndex) {
        std::size_t count = 0U;
        {
            std::lock_guard<std::mutex> lock{eventsMutex};
            for (const auto& eventValue : events) {
                for (const auto& topicName : topicNames) {
                    if (eventValue.topic() == topicName) {
                        count += 1U;
                        break;
                    }
                }
            }
        }
        if (count >= minimumCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

bool waitForMonitoringEventWithSourceAndKeyPath(std::mutex& eventsMutex,
                                                const std::vector<yaha::Message>& events,
                                                const std::string& eventTopic,
                                                const std::string& sourceText,
                                                const std::string& keyPathText) {
    const std::string sourceToken = std::string{"\"source\":\""} + sourceText + "\"";
    const std::string keyPathToken = std::string{"\"keyPath\":\""} + keyPathText + "\"";

    for (int attemptIndex = 0; attemptIndex < 200; ++attemptIndex) {
        {
            std::lock_guard<std::mutex> lock{eventsMutex};
            for (const auto& eventValue : events) {
                if (eventValue.topic() != eventTopic) {
                    continue;
                }
                if (!std::holds_alternative<std::string>(eventValue.value())) {
                    continue;
                }
                const std::string& payloadText = std::get<std::string>(eventValue.value());
                if (payloadText.find(sourceToken) != std::string::npos
                    && payloadText.find(keyPathToken) != std::string::npos) {
                    return true;
                }
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
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
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

TEST_CASE("http_get_rejects_key_longer_than_limit", "[file_store]") {
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
    const auto getResponse = client.Get(("/" + longKey).c_str());
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 400);
}

TEST_CASE("http_get_missing_key_returns_error", "[file_store]") {
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
    const auto getResponse = client.Get("/file/does-not-exist");
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 404);
    REQUIRE(getResponse->body.find("code=YAHA_FILE_STORE_KEY_NOT_FOUND") != std::string::npos);
}

TEST_CASE("http_post_invalid_json_returns_error", "[file_store]") {
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
    const auto postResponse = client.Post("/file/json", "   ", "application/json");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 400);
    REQUIRE(postResponse->body.find("code=YAHA_FILE_STORE_INVALID_JSON_PAYLOAD") != std::string::npos);
}

TEST_CASE("http_post_invalid_json_token_returns_error", "[file_store]") {
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
    const auto postResponse = client.Post("/file/json", ":bad-json", "application/json");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 400);
    REQUIRE(postResponse->body.find("code=YAHA_FILE_STORE_INVALID_JSON_PAYLOAD") != std::string::npos);
}

TEST_CASE("http_options_returns_cors_headers", "[file_store]") {
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
    const auto optionsResponse = client.Options("/file/path");
    REQUIRE(optionsResponse != nullptr);
    REQUIRE(optionsResponse->status == 200);
    REQUIRE(optionsResponse->get_header_value("Access-Control-Allow-Origin") == "*");
    REQUIRE(optionsResponse->get_header_value("Access-Control-Allow-Methods") == "POST, GET, OPTIONS");
    REQUIRE(optionsResponse->get_header_value("Access-Control-Allow-Headers") == "content-type");
}

TEST_CASE("http_options_echoes_requested_headers_for_preflight", "[file_store]") {
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
    httplib::Headers headers{};
    headers.emplace("Access-Control-Request-Headers", "content-type, x-requested-with");
    const auto optionsResponse = client.Options("/file/path", headers);
    REQUIRE(optionsResponse != nullptr);
    REQUIRE(optionsResponse->status == 200);
    REQUIRE(optionsResponse->get_header_value("Access-Control-Allow-Origin") == "*");
    REQUIRE(optionsResponse->get_header_value("Access-Control-Allow-Headers")
            == "content-type, x-requested-with");
}

TEST_CASE("http_roundtrip_text_payload_escapes_json_control_chars", "[file_store]") {
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
    const std::string inputText = "line1\\\"\n\r\tline2";
    const auto postResponse = client.Post("/file/escape", inputText, "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);

    const auto getResponse = client.Get("/file/escape");
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 200);
    REQUIRE(getResponse->body == "\"line1\\\\\\\"\\n\\r\\tline2\"");
}

TEST_CASE("http_get_reads_legacy_untyped_payload_file", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    const std::string keyPath = "/legacy/raw";
    const auto filename = yaha::FileStore::encodeKeyPathToFilename(keyPath);
    {
        std::ofstream output{tempDir / filename, std::ios::binary | std::ios::trunc};
        output << "legacy-text";
    }

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;

    yaha::FileStore store{config};
    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto getResponse = client.Get(keyPath.c_str());
    REQUIRE(getResponse != nullptr);
    REQUIRE(getResponse->status == 200);
    REQUIRE(getResponse->body == "\"legacy-text\"");
}

TEST_CASE("monitoring_disabled_suppresses_post_event", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;
    config.monitoring.enabled = false;

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
    REQUIRE(events.empty());
}

TEST_CASE("monitoring_error_event_is_emitted_for_invalid_directory", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};
    const auto invalidDirectoryPath = tempDir / "not-a-directory";
    {
        std::ofstream output{invalidDirectoryPath, std::ios::binary | std::ios::trunc};
        output << "content";
    }

    yaha::FileStoreConfig config{};
    config.serverPort = 0U;
    config.directory = invalidDirectoryPath;
    config.monitoring.watchIntervalMs = 10U;

    yaha::FileStore store{config};

    std::mutex eventsMutex{};
    std::vector<yaha::Message> events{};
    store.setPublishCallback([&eventsMutex, &events](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{eventsMutex};
        events.push_back(message.clone());
    });

    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForTopicAtLeast(eventsMutex, events, "$MONITOR/FileStore/error", 1U));

    std::lock_guard<std::mutex> lock{eventsMutex};
    REQUIRE_FALSE(events.empty());
    REQUIRE(std::holds_alternative<std::string>(events.front().value()));
    const auto& payload = std::get<std::string>(events.front().value());
    REQUIRE(payload.find("\"details\":") != std::string::npos);
}

TEST_CASE("monitoring_topic_prefix_all_slashes_uses_suffix_topic", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;
    config.monitoring.topicPrefix = "////";

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
    const auto postResponse = client.Post("/file/topic", "txt", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);
    REQUIRE(waitForTopicAtLeast(eventsMutex, events, "changed", 1U));
}

TEST_CASE("monitoring_topic_prefix_empty_uses_default_prefix", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;
    config.monitoring.topicPrefix = "";

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
    const auto postResponse = client.Post("/file/topic-default", "txt", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);
    REQUIRE(waitForTopicAtLeast(eventsMutex, events, "$MONITOR/FileStore/changed", 1U));
}

TEST_CASE("http_post_returns_error_when_directory_is_not_writable_directory", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};
    const auto invalidDirectoryPath = tempDir / "directory-target-file";
    {
        std::ofstream output{invalidDirectoryPath, std::ios::binary | std::ios::trunc};
        output << "file";
    }

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = invalidDirectoryPath;

    yaha::FileStore store{config};
    StoreCloseGuard storeGuard{&store};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(config.serverPort)};
    const auto postResponse = client.Post("/file/fail-write", "payload", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 500);
    REQUIRE(postResponse->get_header_value("Access-Control-Allow-Origin") == "*");
    REQUIRE(postResponse->body.find("code=YAHA_FILE_STORE_PERSIST_FAILED") != std::string::npos);
    REQUIRE(postResponse->body.find("failed to create directory") != std::string::npos);
}

TEST_CASE("run_invokes_http_start_and_stop_callbacks", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    std::mutex callbackMutex{};
    std::uint32_t startCount = 0U;
    std::uint32_t stopCount = 0U;

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;
    config.httpStartCallback = [&callbackMutex, &startCount]() {
        std::lock_guard<std::mutex> lock{callbackMutex};
        startCount += 1U;
    };
    config.httpStopCallback = [&callbackMutex, &stopCount]() {
        std::lock_guard<std::mutex> lock{callbackMutex};
        stopCount += 1U;
    };

    yaha::FileStore store{config};
    store.run();
    REQUIRE(waitForHttpReady(config.serverPort));
    store.close();

    std::lock_guard<std::mutex> lock{callbackMutex};
    REQUIRE(startCount == 1U);
    REQUIRE(stopCount == 1U);
}

TEST_CASE("handle_message_is_noop", "[file_store]") {
    yaha::FileStoreConfig config{};
    config.serverPort = 0U;
    yaha::FileStore store{config};

    store.handleMessage(yaha::Message{"topic/a", "payload"});
    REQUIRE(store.getSubscriptions().empty());
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

TEST_CASE("watcher_emits_created_changed_deleted_events", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = 0U;
    config.directory = tempDir;
    config.monitoring.watchIntervalMs = 10U;

    yaha::FileStore store{config};

    std::mutex eventsMutex{};
    std::vector<yaha::Message> events{};
    store.setPublishCallback([&eventsMutex, &events](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{eventsMutex};
        events.push_back(message.clone());
    });

    StoreCloseGuard storeGuard{&store};
    store.run();

    const auto filename = yaha::FileStore::encodeKeyPathToFilename("/watch/key");
    const auto watchPath = tempDir / filename;

    bool sawCreateOrChange = false;
    for (int attemptIndex = 0; attemptIndex < 40; ++attemptIndex) {
        {
            std::ofstream output{watchPath, std::ios::binary | std::ios::trunc};
            output << "T\npulse-" << attemptIndex;
        }
        if (waitForAnyTopicAtLeast(
                eventsMutex,
                events,
                {"$MONITOR/FileStore/created", "$MONITOR/FileStore/changed"},
                1U)) {
            sawCreateOrChange = true;
            break;
        }
    }
    REQUIRE(sawCreateOrChange);

    {
        std::ofstream output{watchPath, std::ios::binary | std::ios::trunc};
        output << "T\nchanged";
    }
    REQUIRE(waitForTopicAtLeast(eventsMutex, events, "$MONITOR/FileStore/changed", 1U));

    std::filesystem::remove(watchPath);
    REQUIRE(waitForTopicAtLeast(eventsMutex, events, "$MONITOR/FileStore/deleted", 1U));

    std::unordered_map<std::string, std::size_t> topicCounts{};
    {
        std::lock_guard<std::mutex> lock{eventsMutex};
        for (const auto& eventValue : events) {
            topicCounts[eventValue.topic()] += 1U;
        }
    }

    REQUIRE(topicCounts["$MONITOR/FileStore/changed"] >= 1U);
    REQUIRE(topicCounts["$MONITOR/FileStore/deleted"] >= 1U);
}

TEST_CASE("watcher_changed_event_includes_key_path_for_known_file", "[file_store]") {
    const auto tempDir = makeTempDirectory();
    DirectoryCleanupGuard dirGuard{tempDir};

    yaha::FileStoreConfig config{};
    config.serverPort = reserveFreeLocalPort();
    config.directory = tempDir;
    config.monitoring.watchIntervalMs = 10U;

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
    const auto postResponse = client.Post("/watch/key", "value", "text/plain");
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 200);

    REQUIRE(waitForMonitoringEventWithSourceAndKeyPath(
        eventsMutex,
        events,
        "$MONITOR/FileStore/changed",
        "filesystem-watch",
        "/watch/key"));
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

// NOLINTEND
