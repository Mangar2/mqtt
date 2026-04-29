#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
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

} // namespace

TEST_CASE("get_subscriptions_returns_configured_map", "[message_store]") {
    yaha::MessageStoreConfig config{};
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
    yaha::MessageStore store{config};

    store.handleMessage(yaha::Message{"keep/topic", std::string{"x"}});
    store.handleMessage(yaha::Message{"$MONITORING/messages/cleanup", std::string{"abc"}});

    const auto nodes = store.querySection("keep/topic", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
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
