#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "yaha/message_store/message_tree.h"
#include "yaha/message_store/message_tree_persistence.h"

namespace {

struct FakeClock {
    std::int64_t nowMs{1000};
};

yaha::MessageTree makeTree(FakeClock& clock) {
    yaha::MessageTreeConfig config{};
    config.nowMillisecondsProvider = [&clock]() {
        return clock.nowMs;
    };
    return yaha::MessageTree{config};
}

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_msgstore_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code err{};
    std::filesystem::remove_all(path, err);
}

const yaha::MessageTreeNode* findNode(const std::vector<yaha::MessageTreeNode>& nodes,
                                      const std::string& topic) {
    for (const auto& node : nodes) {
        if (node.topic == topic) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("persist_now_writes_snapshot_and_restore_latest_rebuilds_tree", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);

    yaha::Message first{"home/light", std::string{"on"}};
    first.addReason("created", "2026-01-01T00:00:00Z");
    source.addData(first);
    clock.nowMs += 1000;
    yaha::Message second{"home/light", std::string{"off"}};
    second.addReason("updated", "2026-01-01T00:00:01Z");
    source.addData(second);
    source.addData(yaha::Message{"home/temp", 21.5});

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";
    persistenceConfig.keepFiles = 3U;

    yaha::MessageTreePersistence persistence{persistenceConfig};
    REQUIRE(persistence.persistNow(source));

    FakeClock restoreClock{};
    yaha::MessageTree restored = makeTree(restoreClock);

    REQUIRE(persistence.restoreLatest(restored));

    const auto restoredNodes = restored.getSection("", 10U, true, true);
    const auto* light = findNode(restoredNodes, "home/light");
    const auto* temp = findNode(restoredNodes, "home/temp");

    REQUIRE(light != nullptr);
    REQUIRE(temp != nullptr);
    REQUIRE(std::get<std::string>(light->value) == "off");
    REQUIRE(light->reason.size() == 1U);
    REQUIRE(light->history.size() == 1U);
    REQUIRE(std::get<std::string>(light->history.front().value) == "on");
    REQUIRE(light->history.front().reason.size() == 1U);
    REQUIRE(std::get<double>(temp->value) == 21.5);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("restore_latest_skips_corrupt_newest_file_and_uses_previous_valid", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);
    source.addData(yaha::Message{"house/kitchen", std::string{"ok"}});

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";
    persistenceConfig.keepFiles = 5U;

    yaha::MessageTreePersistence persistence{persistenceConfig};
    REQUIRE(persistence.persistNow(source));

    const auto corruptPath = tempDir / "state_9999999999999.mtree";
    std::ofstream corrupt{corruptPath, std::ios::out | std::ios::trunc};
    corrupt << "BROKEN\n";
    corrupt.close();

    FakeClock restoreClock{};
    yaha::MessageTree restored = makeTree(restoreClock);

    REQUIRE(persistence.restoreLatest(restored));
    const auto nodes = restored.getSection("", 5U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "house/kitchen");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("retention_deletes_old_files_beyond_keep_files", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";
    persistenceConfig.keepFiles = 2U;

    yaha::MessageTreePersistence persistence{persistenceConfig};

    for (int idx = 0; idx < 3; ++idx) {
        source.addData(yaha::Message{"retain/topic", static_cast<double>(idx)});
        REQUIRE(persistence.persistNow(source));
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        clock.nowMs += 1000;
    }

    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{tempDir}) {
        if (entry.path().extension().string() == ".mtree") {
            count += 1U;
        }
    }

    REQUIRE(count == 2U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("start_periodic_persists_until_stopped", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);
    source.addData(yaha::Message{"periodic/topic", std::string{"v"}});

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";
    persistenceConfig.intervalMs = 20U;
    persistenceConfig.keepFiles = 5U;

    yaha::MessageTreePersistence persistence{persistenceConfig};
    persistence.startPeriodic(source);
    std::this_thread::sleep_for(std::chrono::milliseconds{70});
    persistence.stopPeriodic();

    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{tempDir}) {
        if (entry.path().extension().string() == ".mtree") {
            count += 1U;
        }
    }

    REQUIRE(count >= 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("restore_latest_returns_false_when_no_files_exist", "[message_store]") {
    const auto tempDir = makeTempDirectory();

    std::ofstream invalidName{tempDir / "state_abc.mtree", std::ios::out | std::ios::trunc};
    invalidName << "MTREE1\n0\n";
    invalidName.close();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";

    yaha::MessageTreePersistence persistence{persistenceConfig};

    FakeClock restoreClock{};
    yaha::MessageTree restored = makeTree(restoreClock);
    REQUIRE_FALSE(persistence.restoreLatest(restored));

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("restore_latest_skips_malformed_node_payload", "[message_store]") {
    const auto tempDir = makeTempDirectory();

    const auto malformed = tempDir / "state_999.mtree";
    std::ofstream file{malformed, std::ios::out | std::ios::trunc};
    file << "MTREE1\n";
    file << "1\n";
    file << "\"bad/topic\"\n";
    file << "1000\n";
    file << "X\n";
    file.close();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";

    yaha::MessageTreePersistence persistence{persistenceConfig};

    FakeClock restoreClock{};
    yaha::MessageTree restored = makeTree(restoreClock);
    REQUIRE_FALSE(persistence.restoreLatest(restored));

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("retention_keep_files_zero_disables_deletion", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = tempDir;
    persistenceConfig.filename = "state";
    persistenceConfig.keepFiles = 0U;

    yaha::MessageTreePersistence persistence{persistenceConfig};

    for (int idx = 0; idx < 3; ++idx) {
        source.addData(yaha::Message{"retain0/topic", static_cast<double>(idx)});
        REQUIRE(persistence.persistNow(source));
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        clock.nowMs += 1000;
    }

    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{tempDir}) {
        if (entry.path().extension().string() == ".mtree") {
            count += 1U;
        }
    }
    REQUIRE(count == 3U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("persist_now_returns_false_when_directory_is_regular_file", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);
    source.addData(yaha::Message{"home/a", std::string{"x"}});

    const auto tempDir = makeTempDirectory();
    const auto regularFilePath = tempDir / "not_a_directory";
    std::ofstream marker{regularFilePath, std::ios::out | std::ios::trunc};
    marker << "marker";
    marker.close();

    yaha::MessageTreePersistence::Config persistenceConfig{};
    persistenceConfig.directory = regularFilePath;
    persistenceConfig.filename = "state";

    yaha::MessageTreePersistence persistence{persistenceConfig};
    REQUIRE_FALSE(persistence.persistNow(source));

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("start_periodic_noop_when_interval_zero_or_already_running", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);
    source.addData(yaha::Message{"periodic/guard", std::string{"x"}});

    const auto tempDir = makeTempDirectory();

    yaha::MessageTreePersistence::Config zeroIntervalConfig{};
    zeroIntervalConfig.directory = tempDir;
    zeroIntervalConfig.filename = "state";
    zeroIntervalConfig.intervalMs = 0U;

    yaha::MessageTreePersistence zeroInterval{zeroIntervalConfig};
    zeroInterval.startPeriodic(source);
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    zeroInterval.stopPeriodic();

    std::size_t zeroCount = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{tempDir}) {
        if (entry.path().extension().string() == ".mtree") {
            zeroCount += 1U;
        }
    }
    REQUIRE(zeroCount == 0U);

    yaha::MessageTreePersistence::Config normalConfig{};
    normalConfig.directory = tempDir;
    normalConfig.filename = "state";
    normalConfig.intervalMs = 15U;

    yaha::MessageTreePersistence normal{normalConfig};
    normal.startPeriodic(source);
    normal.startPeriodic(source);
    std::this_thread::sleep_for(std::chrono::milliseconds{45});
    normal.stopPeriodic();

    std::size_t normalCount = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{tempDir}) {
        if (entry.path().extension().string() == ".mtree") {
            normalCount += 1U;
        }
    }
    REQUIRE(normalCount >= 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("default_constructor_can_persist_and_restore_reason_history", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree source = makeTree(clock);

    yaha::Message first{"home/default", std::string{"one"}};
    first.addReason("r1", "2026-01-01T00:00:00Z");
    source.addData(first);

    clock.nowMs += 1000;
    yaha::Message second{"home/default", std::string{"two"}};
    second.addReason("r2", "2026-01-01T00:00:01Z");
    source.addData(second);

    const auto tempDir = makeTempDirectory();
    const auto oldCwd = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    yaha::MessageTreePersistence persistence{};
    REQUIRE(persistence.persistNow(source));

    FakeClock restoreClock{};
    yaha::MessageTree restored = makeTree(restoreClock);
    REQUIRE(persistence.restoreLatest(restored));

    const auto nodes = restored.getSection("home/default", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().reason.size() == 1U);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(nodes.front().history.front().reason.size() == 1U);

    std::filesystem::current_path(oldCwd);
    removeDirectoryQuiet(tempDir / "data");
    removeDirectoryQuiet(tempDir);
}
