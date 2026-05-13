#pragma once

/**
 * @file file_store.h
 * @brief YAHA FileStore component with HTTP key/value API and MQTT monitoring publishes.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace httplib {
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace yaha {

/**
 * @brief Default polling interval for filesystem watcher in milliseconds.
 */
inline constexpr std::uint32_t k_file_store_default_watch_interval_ms{1000U};

/**
 * @brief Default HTTP listen port for FileStore.
 */
inline constexpr std::uint16_t k_file_store_default_server_port{8210U};

/**
 * @brief Default maximum accepted key length for FileStore.
 */
inline constexpr std::uint32_t k_file_store_default_max_key_length{100U};

/**
 * @brief Monitoring publish configuration for FileStore.
 */
struct FileStoreMonitoringConfig {
    bool enabled{true};
    std::string topicPrefix{"$MONITOR/FileStore"};
    Qos qos{Qos::AtLeastOnce};
    bool retain{false};
    std::uint32_t watchIntervalMs{k_file_store_default_watch_interval_ms};
};

/**
 * @brief Runtime configuration for FileStore component.
 */
struct FileStoreConfig {
    std::string serverHost{"127.0.0.1"};
    std::uint16_t serverPort{k_file_store_default_server_port};
    std::filesystem::path directory{"data"};
    std::uint32_t keepFiles{2U};
    std::uint32_t maxKeyLength{k_file_store_default_max_key_length};
    FileStoreMonitoringConfig monitoring{};
    std::function<void()> httpStartCallback;
    std::function<void()> httpStopCallback;
};

/**
 * @brief FileStore component implementation.
 */
class FileStore final : public IMqttComponent {
public:
    /**
     * @brief Result for file snapshot scan operation.
     */
    struct SnapshotBuildResult {
        bool success{false};               ///< True when snapshot scan succeeded.
        std::string errorText;             ///< Human-readable error description on failure.
        std::unordered_map<std::string, std::filesystem::file_time_type> snapshot; ///< Filename to mtime map.
    };

    /**
     * @brief Result for one write operation.
     */
    struct WritePayloadResult {
        bool success{false};      ///< True when write succeeded.
        std::string filename;     ///< Encoded filename for key path.
        std::string errorText;    ///< Human-readable error description on failure.
    };

    /**
     * @brief Result for one read operation.
     */
    struct ReadPayloadResult {
        bool success{false};        ///< True when read succeeded.
        std::string responseJson;   ///< JSON response body content.
        std::string errorText;      ///< Human-readable error description on failure.
    };

    /**
     * @brief Constructs component from runtime configuration.
     * @param config Runtime configuration.
     */
    explicit FileStore(FileStoreConfig config);

    /**
     * @brief Stops component resources on destruction.
     */
    ~FileStore() override;

    /**
     * @brief Copy constructor is disabled.
     * @param other Source object.
     */
    FileStore(const FileStore& other) = delete;

    /**
     * @brief Copy assignment is disabled.
     * @param other Source object.
     * @return Reference to this object.
     */
    FileStore& operator=(const FileStore& other) = delete;

    /**
     * @brief Maps one key path to deterministic filename.
     * @param keyPath Logical key path.
     * @return Filename token used inside storage directory.
     */
    [[nodiscard]] static std::string encodeKeyPathToFilename(const std::string& keyPath);

    /**
     * @brief Returns topic subscriptions requested by component.
     * @return Empty subscription mapping.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one inbound MQTT message.
     * @param message Inbound MQTT message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Sets callback used for outgoing monitoring publishes.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Starts HTTP listener and filesystem watcher loop.
     */
    void run() override;

    /**
     * @brief Stops watcher and HTTP listener.
     */
    void close() override;

    /**
     * @brief Returns whether component lifecycle is active.
     * @return True when running.
     */
    [[nodiscard]] bool isRunning() const;

private:
    /**
     * @brief Encoded payload kept in store file format.
     */
    struct StoredPayload {
        bool isJson{false};
        std::string body;
    };

    /**
     * @brief Snapshot map type alias.
     */
    using FileSnapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;

    /**
     * @brief Starts HTTP server thread when configured.
     */
    void startHttpServer();

    /**
     * @brief Stops HTTP server thread.
     */
    void stopHttpServer();

    /**
     * @brief Polling watcher loop for filesystem changes.
     */
    void watcherLoop();

    /**
     * @brief Builds one snapshot of current files in data directory.
     * @return Snapshot scan result object.
     */
    [[nodiscard]] SnapshotBuildResult buildSnapshot() const;

    /**
     * @brief Writes one key payload to filesystem.
     * @param keyPath Logical key path.
     * @param payload Storage payload.
     * @return Write result object.
     */
    [[nodiscard]] WritePayloadResult writeKeyPayload(const std::string& keyPath,
                                                     const StoredPayload& payload) const;

    /**
     * @brief Reads one key payload from filesystem.
     * @param keyPath Logical key path.
     * @return Read result object.
     */
    [[nodiscard]] ReadPayloadResult readKeyPayload(const std::string& keyPath) const;

    /**
     * @brief Performs lightweight JSON payload validation.
     * @param jsonText Candidate JSON text.
     * @return True when text resembles valid JSON payload start.
     */
    [[nodiscard]] static bool validateJsonPayload(const std::string& jsonText);

    /**
     * @brief Publishes one monitoring event message.
     * @param eventType Event type suffix.
     * @param keyPath Optional logical key path.
     * @param filename Encoded filename.
     * @param source Event source marker.
     * @param details Optional details string.
     */
    void publishMonitoring(const std::string& eventType,
                           const std::string* keyPath,
                           const std::string& filename,
                           const std::string& source,
                           const std::string* details) const;

    /**
     * @brief Escapes one text for embedding in JSON string value.
     * @param text Input text.
     * @return Escaped text.
     */
    [[nodiscard]] static std::string jsonEscape(const std::string& text);

    /**
     * @brief Converts one ASCII string to lowercase.
     * @param text Input text.
     * @return Lower-cased text.
     */
    [[nodiscard]] static std::string toLower(std::string text);

    /**
     * @brief Normalizes monitoring topic prefix.
     * @param prefix Input prefix.
     * @return Normalized prefix without trailing slash.
     */
    [[nodiscard]] static std::string trimTopicPrefix(std::string prefix);

    /**
     * @brief Publishes one watcher-triggered monitoring event with known key mapping when available.
     * @param eventType Event type suffix.
     * @param filename Changed filename in data directory.
     */
    void publishWatcherMonitoring(const std::string& eventType,
                                  const std::string& filename) const;

    /**
     * @brief Stores known filename to key path mapping.
     * @param filename Encoded filename.
     * @param keyPath Logical key path.
     */
    void rememberKnownKeyPath(const std::string& filename,
                              const std::string& keyPath) const;

    /**
     * @brief Looks up known key path for one filename.
     * @param filename Encoded filename.
     * @return Known key path when available.
     */
    [[nodiscard]] std::optional<std::string> lookupKnownKeyPath(const std::string& filename) const;

    /**
     * @brief Removes known filename to key path mapping.
     * @param filename Encoded filename.
     */
    void forgetKnownKeyPath(const std::string& filename) const;

    /**
     * @brief Handles HTTP OPTIONS request.
     * @param response HTTP response object.
     */
    static void handleHttpOptions(const httplib::Request& request,
                                  httplib::Response& response);

    /**
     * @brief Handles HTTP POST request.
     * @param store FileStore instance.
     * @param request HTTP request object.
     * @param response HTTP response object.
     */
    static void handleHttpPost(FileStore& store,
                               const httplib::Request& request,
                               httplib::Response& response);

    /**
     * @brief Handles HTTP GET request.
     * @param store FileStore instance.
     * @param request HTTP request object.
     * @param response HTTP response object.
     */
    static void handleHttpGet(FileStore& store,
                              const httplib::Request& request,
                              httplib::Response& response);

    FileStoreConfig config_{}; ///< Runtime configuration.

    std::unique_ptr<httplib::Server> httpServer_;   ///< HTTP server instance.
    std::thread httpThread_;                        ///< HTTP listener thread.
    std::thread watcherThread_;                     ///< Filesystem polling thread.

    mutable std::mutex stateMutex_;                 ///< Guards lifecycle state flags.
    mutable std::mutex publishMutex_;               ///< Guards publish callback access.
    mutable std::mutex knownFilesMutex_;            ///< Guards known filename to key path map.
    std::condition_variable stopCondition_;         ///< Wakes watcher on shutdown.
    bool running_{false};                           ///< True when lifecycle is active.
    bool stopRequested_{false};                     ///< True when shutdown requested.
    PublishCallback publishCallback_;               ///< Outgoing MQTT publish callback.
    mutable std::unordered_map<std::string, std::string> knownFilenameToKeyPath_{};
};

} // namespace yaha
