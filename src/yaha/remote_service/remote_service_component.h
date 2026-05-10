#pragma once

/**
 * @file remote_service_component.h
 * @brief RemoteService mapping parser helpers and IMqttComponent implementation.
 */

#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/remote_service/remote_service_config.h"

#include <map>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace yaha {

/**
 * @brief One configured RemoteService path mapping entry.
 */
struct RemoteServiceServiceMapping {
    std::map<std::string, std::string> devices{}; ///< Device id to outbound topic map.
    Qos qos{Qos::AtLeastOnce};                    ///< Optional per-service publish QoS.
    std::string reason{"remote command"};        ///< Optional per-service publish reason text.
};

/**
 * @brief Service lookup map indexed by exact HTTP path.
 */
using RemoteServiceMap = std::map<std::string, RemoteServiceServiceMapping>;

/**
 * @brief Domain request DTO for one RemoteService command invocation.
 */
struct RemoteServiceCommandRequest {
    std::string path{};     ///< Exact service path key.
    std::string deviceId{}; ///< Device id key in selected service mapping.
    Value state{};          ///< Command state payload forwarded to MQTT publish.
    std::string token{};    ///< Token field passed through HTTP adapter contract.
};

/**
 * @brief Domain command execution status.
 */
enum class RemoteServiceCommandStatus : std::uint8_t {
    Success = 0U,       ///< Command resolved and publish succeeded.
    ServiceNotFound = 1U, ///< Service path or device id was not found.
    PublishFailed = 2U, ///< Publish callback missing or publish callback threw.
};

/**
 * @brief Domain command result with optional resolved MQTT message.
 */
struct RemoteServiceCommandResult {
    RemoteServiceCommandStatus status{RemoteServiceCommandStatus::Success}; ///< Resolution/publish status.
    std::optional<Message> resolvedMessage{}; ///< Resolved MQTT message when available.

    /**
     * @brief Returns true when status equals success.
     * @return True when command completed successfully.
     */
    [[nodiscard]] bool isSuccess() const;
};

/**
 * @brief Parses FileStore mapping payload for RemoteService.
 *
 * The parser expects a root object with `services` array where each entry contains
 * required fields `path` and `devices`, and optional `qos` and `reason`.
 *
 * Duplicate `path` entries keep the first occurrence and ignore following entries,
 * while writing one error line per ignored duplicate to `std::cerr`.
 *
 * @param payloadText JSON payload from FileStore.
 * @param output Parsed service map on success.
 * @param errorMessage Human-readable parser error text on failure.
 * @return True when payload is valid and fully parsed.
 */
[[nodiscard]] bool tryParseRemoteServiceMappingPayload(
    const std::string& payloadText,
    RemoteServiceMap& output,
    std::string& errorMessage);

/**
 * @brief Parses monitor event payload and extracts `keyPath` when available.
 * @param payloadText Monitor JSON payload text.
 * @return Extracted key path when payload contains string field `keyPath`.
 */
[[nodiscard]] std::optional<std::string> tryExtractFileStoreMonitorKeyPath(
    const std::string& payloadText);

/**
 * @brief YAHA RemoteService component with FileStore-backed mapping lifecycle.
 */
class RemoteServiceComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component from runtime configuration.
     * @param config Runtime configuration.
     */
    explicit RemoteServiceComponent(RemoteServiceConfig config);

    /**
     * @brief Returns monitor subscription map.
     * @return Topic filter map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one incoming MQTT message.
     * @param message Incoming message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts lifecycle and triggers startup mapping load from FileStore.
     */
    void run() override;

    /**
     * @brief Stops lifecycle.
     */
    void close() override;

    /**
     * @brief Stores publish callback for phase-3 handoff compatibility.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Returns whether lifecycle is currently running.
     * @return True when running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns number of currently loaded service path entries.
     * @return Number of service path mappings.
     */
    [[nodiscard]] std::size_t serviceCount() const;

    /**
     * @brief Returns whether one exact service path exists in map.
     * @param servicePath Exact path key.
     * @return True when path exists.
     */
    [[nodiscard]] bool hasServicePath(const std::string& servicePath) const;

    /**
     * @brief Resolves mapped topic for one service path and device id.
     * @param servicePath Exact path key.
     * @param deviceId Device id key.
     * @return Topic string when mapping exists.
     */
    [[nodiscard]] std::optional<std::string> mappedTopicFor(
        const std::string& servicePath,
        const std::string& deviceId) const;

    /**
     * @brief Resolves one domain command into outbound MQTT publish message.
     * @param request Command request DTO.
     * @return Resolution result including resolved message on success.
     */
    [[nodiscard]] RemoteServiceCommandResult resolveCommand(
        const RemoteServiceCommandRequest& request) const;

    /**
     * @brief Resolves and publishes one domain command through callback.
     * @param request Command request DTO.
     * @return Publish result translated to domain command status.
     */
    [[nodiscard]] RemoteServiceCommandResult publishCommand(
        const RemoteServiceCommandRequest& request);

private:
    [[nodiscard]] bool reloadMappingFromFileStore();
    [[nodiscard]] bool isMonitoringTopic(const std::string& topicName) const;
    [[nodiscard]] bool isMatchingMappingReloadEvent(const Message& message) const;

    RemoteServiceConfig config_{};

    mutable std::mutex stateMutex_;
    RemoteServiceMap servicesByPath_{};
    bool running_{false};

    mutable std::mutex publishCallbackMutex_;
    PublishCallback publishCallback_{};
};

} // namespace yaha