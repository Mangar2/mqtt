#pragma once

/**
 * @file message_tree_persistence.h
 * @brief Persistence service for MessageTree snapshots.
 */

#include "yaha/message_store/message_tree.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace yaha {

/**
 * @brief Persists and restores MessageTree snapshots on local storage.
 */
class MessageTreePersistence {
public:
    /**
     * @brief Runtime persistence configuration.
     */
    struct Config {
        std::filesystem::path directory{"data"};    ///< Snapshot directory.
        std::string filename{"messagestore"};       ///< Snapshot filename prefix.
        std::uint32_t intervalMs{0U};                ///< Periodic persist interval; 0 disables periodic loop.
        std::uint32_t keepFiles{5U};                 ///< Retain newest N snapshot files.
    };

    /**
     * @brief Constructs persistence service with default configuration.
     */
    MessageTreePersistence();

    /**
     * @brief Constructs persistence service with configuration.
     * @param config Persistence settings.
     */
    explicit MessageTreePersistence(Config config);

    /**
     * @brief Stops periodic loop if running.
     */
    ~MessageTreePersistence();

    MessageTreePersistence(const MessageTreePersistence&) = delete;
    MessageTreePersistence& operator=(const MessageTreePersistence&) = delete;

    /**
     * @brief Writes one snapshot file for current tree state.
     * @param tree Source tree.
     * @return True on successful write.
     */
    [[nodiscard]] bool persistNow(const MessageTree& tree);

    /**
     * @brief Restores from the newest valid snapshot file.
     * @param tree Target tree to replace.
     * @return True when a valid snapshot was restored.
     */
    [[nodiscard]] bool restoreLatest(MessageTree& tree);

    /**
     * @brief Starts periodic snapshot loop.
     * @param tree Source tree for snapshots.
     */
    void startPeriodic(const MessageTree& tree);

    /**
     * @brief Stops periodic snapshot loop.
     */
    void stopPeriodic();

private:
    [[nodiscard]] std::filesystem::path makeSnapshotPath(std::int64_t timestampMs) const;
    [[nodiscard]] std::vector<std::filesystem::path> listSnapshotFilesNewestFirst() const;
    void enforceRetention();
    void periodicLoop();

    Config config_{};
    std::atomic<bool> periodicRunning_{false};
    const MessageTree* periodicTree_{nullptr};
    std::thread periodicThread_{};
};

} // namespace yaha
