#pragma once

/**
 * @file message_store.h
 * @brief MessageStore component logic implementing IMqttComponent.
 */

#include "yaha/message_store/message_tree.h"
#include "yaha/message_store/message_tree_persistence.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <functional>
#include <filesystem>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace httplib {
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace yaha {

/**
 * @brief Runtime configuration for MessageStore component logic.
 */
struct MessageStoreConfig {
    static constexpr std::uint16_t k_default_server_port{8090U};

    SubscriptionMap subscriptions;                    ///< Topic subscriptions for MQTT transport.
    std::string cleanupTopic{"$MONITOR/messages/cleanup"}; ///< Cleanup command topic.
    std::string serverHost{"127.0.0.1"};             ///< HTTP server bind host/IP.
    std::string serverPath{"/store"};                 ///< HTTP GET base path.
    std::uint16_t serverPort{k_default_server_port};   ///< HTTP server listen port.
    std::filesystem::path replayLoadedStateFile{};      ///< Optional JSONL replay dump after restore.
    std::filesystem::path replayIncomingMessagesFile{}; ///< Optional JSONL append log for messages after restore.
    MessageTreeConfig treeConfig{};                    ///< MessageTree behavior config.
    MessageTreePersistence::Config persistenceConfig{};///< Persistence behavior config.
    std::function<void()> httpStartCallback;           ///< Optional HTTP start callback.
    std::function<void()> httpStopCallback;            ///< Optional HTTP stop callback.
};

/**
 * @brief MessageStore component implementation for MQTT-facing runtime logic.
 */
class MessageStore final : public IMqttComponent {
public:
    /**
     * @brief Constructs MessageStore from runtime configuration.
     * @param config MessageStore configuration.
     */
    explicit MessageStore(MessageStoreConfig config);

    /**
     * @brief Virtual destructor.
     */
    ~MessageStore() override;

    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;

    /**
     * @brief Returns configured MQTT subscriptions.
     * @return Topic filter to QoS map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one inbound message from MQTT client.
     * @param message Inbound message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Stores one message directly in the internal tree.
     * @param message Inbound message.
     */
    void storeMessageDirect(const Message& message);

    /**
     * @brief Starts component lifecycle: restore, HTTP start, periodic persistence.
     */
    void run() override;

    /**
     * @brief Stops component lifecycle: HTTP stop, periodic stop, final persist.
     */
    void close() override;

    /**
     * @brief Returns whether component lifecycle is currently running.
     * @return True when running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns tree section for future HTTP queries.
     * @param topicPrefix Prefix path; empty means root.
     * @param levelAmount Relative depth from prefix.
     * @param includeHistory Include history entries in result.
     * @param includeReason Include reason lists in result.
     * @return Flat node list under prefix.
     */
    [[nodiscard]] std::vector<MessageTreeNode>
    querySection(const std::string& topicPrefix,
                 std::uint32_t levelAmount,
                 bool includeHistory,
                 bool includeReason) const;

    /**
     * @brief Returns changed/new nodes relative to a snapshot list.
     * @param snapshot Snapshot baseline used for diff mode.
        * @param includeHistory Include history entries in result.
        * @param includeReason Include reason lists in result.
     * @return Flat node list that changed since snapshot.
     */
    [[nodiscard]] std::vector<MessageTreeNode>
        queryNodes(const std::vector<MessageTreeSnapshotNode>& snapshot,
                bool includeHistory,
                bool includeReason) const;

    /**
     * @brief Returns compression statistics of the internal tree.
     * @return Internal compression counters.
     */
    [[nodiscard]] MessageTree::CompressionStats queryCompressionStats() const;

    /**
     * @brief Persists one snapshot immediately and returns written file path.
     * @return Written snapshot path on success; empty optional on failure.
     */
    [[nodiscard]] std::optional<std::filesystem::path> persistSnapshotNow();

private:
    struct ReplayRow {
        Value value{};
        ReasonList reason{};
        std::int64_t timeMs{0};
    };

    void startCompressionStatsLogging();
    void stopCompressionStatsLogging();
    void logCompressionStatsLineLocked(std::string_view phaseText) const;
    void writeReplayLoadedStateLocked();
    void appendReplayIncomingMessage(const Message& message);
    [[nodiscard]] static Message buildReplayMessage(const std::string& topicPath,
                                                    const ReplayRow& replayRow);
    [[nodiscard]] static std::vector<ReplayRow>
    buildReplayRowsForNode(const MessageTreeNode& node);

    void startHttpServer();
    void stopHttpServer();

    [[nodiscard]] static std::optional<std::uint32_t> parseCleanupDays(const Value& value);
    [[nodiscard]] static bool parseBoolHeaderValue(const std::string& value, bool defaultValue);

    static void handleHttpRequest(MessageStore& store,
                                  const std::string& basePath,
                                  const httplib::Request& request,
                                  httplib::Response& response);

    static void handleHttpOptionsRequest(const std::string& basePath,
                                         const httplib::Request& request,
                                         httplib::Response& response);

    MessageStoreConfig config_{};
    MessageTree tree_;
    MessageTreePersistence persistence_;
    std::unique_ptr<httplib::Server> httpServer_;
    std::thread httpThread_;
    std::atomic<bool> compressionStatsStopRequested_{false};
    std::thread compressionStatsThread_;

    mutable std::mutex lifecycleStateMutex_;
    mutable std::mutex treeStateMutex_;
    mutable std::mutex replayFileMutex_;
    bool running_{false};
    std::atomic<bool> replayIncomingCaptureEnabled_{false};
};

} // namespace yaha
